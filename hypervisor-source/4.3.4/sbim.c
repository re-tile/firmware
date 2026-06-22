/*
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
 *
 *   The source code contained or described herein and all documents
 *   related to the source code ("Material") are owned by Tilera
 *   Corporation or its suppliers or licensors.  Title to the Material
 *   remains with Tilera Corporation or its suppliers and licensors. The
 *   software is licensed under the Tilera MDE License.
 *
 *   Unless otherwise agreed by Tilera in writing, you may not remove or
 *   alter this notice or any other notice embedded in Materials by Tilera
 *   or Tilera's suppliers or licensors in any way.
 */

/**
 * @file
 * SROM boot image management tool.
 */

#include <sys/stat.h>
#include <sys/types.h>

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "filesys.h"
#include "sha1.h"
#include "srom_format.h"

// ISSUE: Why not use "basename of argv[0]" instead of "SBIM_NAME"?

#ifdef __tile__

/** Program name. */
#define SBIM_NAME "sbim"

/** Chip. */
static const int tile_chip = __tile_chip__;

#else // __tile__

/** Program name. */
#define SBIM_NAME "tile-sbim"

/** Chip. */
static const int tile_chip = TILE_CHIP;

#endif // __tile__


/* Include files for instruction layout. */
#include "arch/abi.h"
#include "arch/opcode.h"

/* Include files for shim registers. */
#include "arch/boot_shm.h"
#include "arch/rsh.h"

//
// Address of the "hw_l0boot_next_pkt" label in the L0 booter.
// This really ought to be in an include file somewhere, but it isn't.
//
const unsigned long hw_l0boot_next_pkt = 0;

/**
 * Command options, first the long versions.
 */
static const struct option long_options[] =
{
  { .name = "info",              .has_arg = 0, .val = 'o' },
  { .name = "install",           .has_arg = 1, .val = 'i' },
  { .name = "extract",           .has_arg = 1, .val = 'e' },
  { .name = "install-booter",    .has_arg = 1, .val = 'I' },
  { .name = "install-prebooter", .has_arg = 1, .val = 'P' },
  { .name = "extract-booter",    .has_arg = 1, .val = 'E' },
  { .name = "invalidate",        .has_arg = 1, .val = 'n' },
  { .name = "invalidate-booter", .has_arg = 0, .val = 'N' },
  { .name = "promote",           .has_arg = 1, .val = 'p' },
  { .name = "lock",              .has_arg = 1, .val = 'L' },
  { .name = "unlock",            .has_arg = 1, .val = 'U' },
  { .name = "verify",            .has_arg = 0, .val = 'v' },
  { .name = "dump",              .has_arg = 0, .val = 'd' },
  { .name = "file",              .has_arg = 1, .val = 'f' },
  { .name = "location",          .has_arg = 1, .val = 'l' },
  { .name = "comment",           .has_arg = 1, .val = 'c' },
  { .name = "sector-size",       .has_arg = 1, .val = 's' },
  { .name = "page-size",         .has_arg = 1, .val = 'g' },
  { .name = "srom-size",         .has_arg = 1, .val = 'z' },
  { .name = "sectors",           .has_arg = 1, .val = 'Z' },
  { .name = "num-images",        .has_arg = 1, .val = 'm' },
  { .name = "yes",               .has_arg = 0, .val = 'y' },
  { .name = "verbose",           .has_arg = 0, .val = 'V' },
  { .name = "damage",            .has_arg = 1, .val = 'D' },
  { .name = "sanitize",          .has_arg = 0, .val = 'S' },
  { .name = "pattern",           .has_arg = 1, .val = 'a' },
  { .name = "oldest",            .has_arg = 0, .val = 'O' },
  { 0 },
};

/**
 * Now the short ones.  Note that some long options are intentionally
 * omitted from the short options list; we want them to be hard to execute
 * by mistake.
 */
static const char options[] = "oi:e:n:p:L:U:vdf:l:c:s:g:z:Z:m:V";

//
// Option values.  Some of these are set to defaults or computed values if
// not specified as actual options.
//

/** Filename from which we'll install a boot image or SROM booter. */
static char* opt_input_file;

/** Filename to which we'll extract a boot image or SROM booter. */
static char* opt_output_file;

/** ID of the boot image we'll act on. */
static char* opt_id;

/** Name of the SROM file. */
static char* opt_file;

/** Comment for the image or booter header. */
static char* opt_comment;

/** SROM sector size in bytes. */
static int opt_sector_size;

/** SROM page size in bytes. */
static int opt_page_size;

/** Total SROM size in bytes. */
static int opt_srom_size;

/** Number of sectors in SROM, rounded down. */
static int opt_sectors;

/** Nonzero if --yes was specified. */
static int opt_yes;

/** Nonzero if --old-format was specified. */
static int opt_old_format;

/** Number of images we support. */
static int opt_num_images = -1;

/** Nonzero if --verify was specified. */
static int opt_verify;

/** Number of times --verbose was specified. */
static int opt_verbose;

/** Nonzero if --oldest was specified. */
static int opt_oldest;

/** Code specifying which part of the image is to be damaged:
 *  hdr     - Image header (not technically part of the image)
 *  l0.5    - L0.5 booter
 *  l1      - L1 booter
 *  l1p     - L1 booter parameters
 *  hv      - Hypervisor
 *  hvfshdr - Hypervisor file system header
 *  hvfs    - Hypervisor file system body
 */
static char* opt_damage;

/** Pattern to use for a sanitize operation. */
static char* opt_pattern;

/** Option letter for the action to take on this run. */
static int opt_action;

//
// Attributes of the SROM layout which are fixed or which are computed based
// only on the SROM characteristics.
//

/** Maximum number of images we support. */
#define MAX_IMAGES 2
/** Default number of images we support. */
#define DEFAULT_IMAGES 2

/** Maximum length of the SROM booter.  This will be overridden if the
    user says they don't want to allow for any boot images. */
static int booter_max_length = 64 * 1024;

/** Byte offset within the SROM where the SROM booter starts. */
static const int booter_offset = 0;

/** Maximum length of any boot image (including its header). */
static int image_max_length;

/** Byte offset within the SROM where the header for each boot image starts. */
static int image_offsets[MAX_IMAGES];

//
// Things which describe the current content of the SROM.
//
/** File descriptor for the SROM. */
static int srom_fd;

/** Nonzero if this srom has a valid header. */
static int srom_valid;

/** Header for the srom. */
static struct srom_overall_header srom_hdr;

/** Nonzero if a particular image has a valid header. */
static int image_valid[MAX_IMAGES];

/** Headers for the images. */
static struct srom_image_header image_hdr[MAX_IMAGES];

//
// Other state.
//
/** File descriptor for the input file. */
static int input_fd;

/** File descriptor for the output file. */
static int output_fd;

//
// Options parsing/handling.
//
static void
usage()
{
  fprintf(stderr,
"Usage: " SBIM_NAME " [ -o | --info ]\n"
"       " SBIM_NAME " { -i | --install } <in-file>\n"
"       " SBIM_NAME " { -e | --extract } <out-file>\n"
"       " SBIM_NAME " { -n | --invalidate } <location>\n"
"       " SBIM_NAME " { -L | --lock } <location>\n"
"       " SBIM_NAME " { -U | --unlock } <location>\n"
"       " SBIM_NAME " { -p | --promote } <location>\n"
"       " SBIM_NAME " { -d | --dump }\n"
"       " SBIM_NAME " --install-booter <in-file>\n"
"       " SBIM_NAME " --install-prebooter <in-file>\n"
"       " SBIM_NAME " --extract-booter <out-file>\n"
"       " SBIM_NAME " --invalidate-booter\n"
"       " SBIM_NAME " --sanitize\n"
"Options:\n"
"       { -c | --comment } <comment>       (install, install-[pre]booter)\n"
"       { -f | --file } <srom-file>        (all actions)\n"
"       { -l | --location } <location>     (install, extract)\n"
"       { -m | --num-images } <nimages> ]  (install-booter)\n"
"       { -g | --page-size } <pgsz>        (install-booter)\n"
"       { -Z | --sectors } <numsec>        (install-booter)\n"
"       { -s | --sector-size } <secsz>     (install-booter)\n"
"       { -z | --srom-size } <sromsz>      (install-booter)\n"
"       { -V | --verbose }                 (all actions)\n"
"       { -v | --verify }                  (info, install[-booter])\n"
"       --yes                              (install-booter, "
                                           "invalidate[-booter])\n"
"       --pattern <pattern>                (sanitize)\n"
"       --oldest                           (install)\n"
"Locations:\n"
"       @<n>     Absolute slot number n\n"
"       0        Most recent image (default for --extract)\n"
"       -<n>     nth most recent image (-1 is default for --promote; oldest\n"
"                                       unlocked is default for --install;\n"
"                                       oldest is default for other actions)\n"
  );
  exit(1);
}

static void
ensure_nimages_nonzero(char* action)
{
  if (opt_num_images > 0)
    return;

  fprintf(stderr, SBIM_NAME ": cannot use --%s when SROM header configured"
                  " for 0 boot images\n", action);

  exit(1);
}

/** Convert a string to a long integer.  Supports a trailing "k" or "m", which
 *  multiplies the result by 2^10 or 2^20, respectively.
 * @param s String to convert.
 * @return Parsed value.  Exits on failure.
 */
static long
strtol_with_suffix(const char* s)
{
  char* endptr;
  long val = strtol(s, &endptr, 0);

  if (*endptr == 'k' || *endptr == 'K')
  {
    val <<= 10;
    endptr++;
  }
  else if (*endptr == 'm' || *endptr == 'M')
  {
    val <<= 20;
    endptr++;
  }

  if (*endptr)
  {
    fprintf(stderr, SBIM_NAME ": improperly formatted numerical "
            "argument %s\n", s);
    exit(1);
  }

  return val;
}


//
// Variable-length buffer routines.  These maintain a contiguous memory
// area which may be appended to; it's automatically resized as needed.
//

/** Variable-length buffer type. */
typedef struct _vbuf
{
  size_t tot_len;          /**< Total length of buf[]. */
  size_t cur_len;          /**< Bytes currently used in buf[]. */
  unsigned char buf[];     /**< Buffer contents. */
}
*vbuf_t;


/** Allocate a variable-length buffer.
 * @param init_size Initial buffer capacity, in bytes.  If this is zero,
 *  a reasonable default will be used.
 * @return The newly allocated buffer.
 */
vbuf_t
vbuf_alloc(size_t init_size)
{
  if (init_size == 0)
    init_size = 64;

  vbuf_t vb = calloc(1, sizeof (*vb) + init_size);
  if (!vb)
  {
    fprintf(stderr, SBIM_NAME ": failed to allocate %zd bytes of memory\n",
            init_size);
    exit(1);
  }

  vb->tot_len = init_size;
  return vb;
}


/** Free a variable-length buffer.
 * @param vb Buffer to free.
 */
void
vbuf_free(vbuf_t vb)
{
  free(vb);
}


/** Provide the current number of bytes in a variable-length buffer.
 * @param vb The buffer.
 * @return Number of bytes contained in the buffer.
 */
size_t
vbuf_len(vbuf_t vb)
{
  return vb->cur_len;
}


/** Provide a pointer to the current contents of a variable-length buffer.
 * @param vb The buffer.
 * @return Pointer to the buffer's data area.
 */
void*
vbuf_data(vbuf_t vb)
{
  return &vb->buf;
}


/** Append data to a variable-length buffer.
 * @param vbp Pointer to the buffer to be appended to.  Note that the
 *  value of *vbp may change as a result of this routine.
 * @param data Pointer to bytes to append.
 * @param len Number of bytes to append.
 */
void
vbuf_append(vbuf_t* vbp, const void* data, size_t len)
{
  vbuf_t nvb = *vbp;

  //
  // See if the appended data will fit in the current buffer.  If not,
  // double the size of the current buffer until the extra data fits.
  //
  if (nvb->tot_len - nvb->cur_len < len)
  {
    while (nvb->tot_len - nvb->cur_len < len)
      nvb->tot_len *= 2;

    nvb = realloc(nvb, sizeof (*nvb) + nvb->tot_len);
    if (!nvb)
    {
      fprintf(stderr, SBIM_NAME ": failed to allocate %zd bytes of memory\n",
              nvb->tot_len);
      exit(1);
    }

    *vbp = nvb;
  }

  memcpy(&nvb->buf[nvb->cur_len], data, len);
  nvb->cur_len += len;
}


/** Merge two variable-length buffers.
 * @param dst Pointer to the buffer to be appended to.  Note that the
 *  value of *dst may change as a result of this routine.
 * @param src Buffer whose data will be appended to *dst.  After the
 *  buffers are merged, this buffer will be freed.
 */
void
vbuf_merge(vbuf_t* dst, vbuf_t src)
{
  vbuf_append(dst, vbuf_data(src), vbuf_len(src));
  vbuf_free(src);
}


/** Return a vbuf with any extra SROM segment headers & data which might
 *  be required for a particular run, based on our flags.
 * @return A vbuf which contains zero or more bytes of SROM segment headers
 *  and data.
 */
static vbuf_t
get_extra_segments()
{
  vbuf_t vb = vbuf_alloc(0);

  return vb;
}

//
// Utility routines.
//

/** Compute a CRC-32 digest of a string.
 * @param ptr Pointer to bytes to digest.
 * @param len Number of bytes to digest.
 * @param old_crc Initial CRC value.
 * @return New CRC value.
 */
uint_reg_t
do_crc(const void* ptr, size_t len, uint_reg_t old_crc)
{
  uint32_t crc = ~old_crc;

  uint8_t* ptr8 = (uint8_t*) ptr;

  //
  // If running natively on Tile, we can use the CRC instructions.
  //
#ifdef __tile__

  //
  // Handle 0-3 bytes one at a time to get us to a word boundary.
  //
  while (len && ((intptr_t) ptr8 & 0x3))
  {
    crc = __insn_crc32_8(crc, *ptr8++);
    len--;
  }

  //
  // Handle as many full words as are left.
  //
  uint32_t* ptr32 = (uint32_t*) ptr8;

  while (len > 3)
  {
    crc = __insn_crc32_32(crc, *ptr32++);
    len -= 4;
  }

  //
  // Handle any remaining bytes.
  //
  ptr8 = (uint8_t*) ptr32;

  while (len)
  {
    crc = __insn_crc32_8(crc, *ptr8++);
    len--;
  }

#else /* __tile__ */

  //
  // Since we're doing this in a slow way anyway, we just do a byte at a time.
  //
  while (len--)
  {
    uint8_t input = *ptr8++;

    for (int i = 0; i < 8; i++)
    {
      crc = (crc >> 1) ^ (((input ^ crc) & 1) ? 0xEDB88320 : 0);
      input >>= 1;
    }
  }

#endif /* __tile__ */

  return ~crc;
}

/** Modify the current position of the SROM file; exit on failure.
 * @param offset New absolute byte position.
 */
void
lseek_srom_or_die(off_t offset)
{
  if (lseek(srom_fd, offset, SEEK_SET) == (off_t) -1)
  {
    fprintf(stderr, SBIM_NAME ": cannot seek SROM file %s: %s\n", opt_file,
            strerror(errno));
    exit(1);
  }
}

/** Return the size of the SROM file; exit on failure.
 * @return File size.
 */
off_t
get_srom_size_or_die()
{
  off_t size = lseek(srom_fd, 0, SEEK_END);
  if (size == (off_t) -1)
  {
    fprintf(stderr, SBIM_NAME ": cannot get size of SROM file %s: %s\n",
            opt_file, strerror(errno));
    exit(1);
  }
  return size;
}

/** Return the current position of the SROM file; exit on failure.
 * @return Current position.
 */
off_t
get_srom_pos_or_die()
{
  off_t pos = lseek(srom_fd, 0, SEEK_CUR);
  if (pos == (off_t) -1)
  {
    fprintf(stderr, SBIM_NAME ": cannot get position in SROM file %s: %s\n",
            opt_file, strerror(errno));
    exit(1);
  }
  return pos;
}

/** Read bytes from the SROM file; exit on failure.
 * @param buf Where to put the bytes.
 * @param len Number of bytes to read.
 */
void
read_srom_or_die(void* buf, size_t len)
{
  if (read(srom_fd, buf, len) != len)
  {
    fprintf(stderr, SBIM_NAME ": cannot read SROM file %s: %s\n", opt_file,
            (errno) ? strerror(errno) : "File too short");
    exit(1);
  }
}

/** Write bytes to the SROM file; exit on failure.
 * @param buf Where to get the bytes.
 * @param len Number of bytes to write.
 */
void
write_srom_or_die(const void* buf, size_t len)
{
  if (write(srom_fd, buf, len) != len)
  {
    fprintf(stderr, SBIM_NAME ": cannot write SROM file %s: %s\n", opt_file,
            strerror(errno));
    exit(1);
  }
}

/** Write bytes to the SROM file, and simultaneously update a running CRC
 *  and byte count; exit on failure.
 * @param buf Where to get the bytes.
 * @param len Number of bytes to write.
 * @param crc Pointer to CRC value to update.
 * @param count Pointer to count value to update.
 */
void
write_srom_or_die_do_crc(const void* buf, size_t len, uint32_t* crc,
                         int* count)
{
  if (write(srom_fd, buf, len) != len)
  {
    fprintf(stderr, SBIM_NAME ": cannot write SROM file %s: %s\n", opt_file,
            strerror(errno));
    exit(1);
  }

  *crc = do_crc(buf, len, *crc);
  *count += len;
}

/** Ensure that no data we've written to the SROM is cached, so that we
 *  can read it back to verify that it made it to the actual hardware
 *  properly.
 */
void
flush_srom()
{
  char dummy;

  lseek_srom_or_die(get_srom_size_or_die() - 1);
  read_srom_or_die(&dummy, sizeof (dummy));

  lseek_srom_or_die(0);
  read_srom_or_die(&dummy, sizeof (dummy));
}

/** Read in the main SROM header and check for validity. */
void
read_srom_header()
{
  lseek_srom_or_die(booter_offset);

  struct srom_overall_header* sh = &srom_hdr;

  read_srom_or_die(sh, sizeof (*sh));

  uint32_t crc = do_crc(sh, offsetof(typeof(*sh), header_crc), 0);

  if (sh->magic == SROM_HEADER_MAGIC && crc == sh->header_crc)
    srom_valid = 1;
}

/** Read in the SROM image headers and check for validity. */
void
read_image_headers()
{
  for (int i = 0; i < opt_num_images; i++)
  {
    lseek_srom_or_die(image_offsets[i]);

    struct srom_image_header* ih = &image_hdr[i];

    read_srom_or_die(ih, sizeof (*ih));

    uint32_t crc = do_crc(ih, offsetof(typeof(*ih), header_crc), 0);
    uint32_t crc_ext1 = do_crc(ih, offsetof(typeof(*ih), header_crc_ext1), 0);

    if (ih->magic == SROM_IMAGE_MAGIC && crc == ih->header_crc &&
        (!(ih->flags & SROM_IMAGE_FLG_EXT1) || crc_ext1 == ih->header_crc_ext1))
      image_valid[i] = 1;
  }
}

//
// Flags for open_srom_or_die()
//
/** Open SROM for reading. */
#define OPEN_SROM_READ             0x0
/** Open SROM for writing. */
#define OPEN_SROM_WRITE            0x1
/** Don't fail if the SROM has no header. */
#define OPEN_SROM_NO_HDR_OK        0x2
/** Don't fail if we don't know the sector or page sizes. */
#define OPEN_SROM_NO_PARAM_OK      0x4
/** We're going to invalidate the header; don't do any of the usual
 * consistency checks. */
#define OPEN_SROM_INVALIDATE_HDR   0x8
/** Ignore the number of images in the header (used when installing a
 *  new booter). */
#define OPEN_SROM_IGNORE_NUM_IMG   0x10

/** Open the SROM device file, make sure we have appropriate device
 *  characteristics available, and calculate the values which depend upon
 *  those characteristics (e.g., the positions of the boot image slots).
 * @param flags Operation modifiers (OPEN_SROM_xxx).
 */
void
open_srom_or_die(int flags)
{
  //
  // If they didn't specify an SROM (the default), we use the normal boot
  // image file, and we get its characteristics from the running system.
  // If they did specify a file, we'll try to get characteristics from the
  // header.  Otherwise we'll expect that they've set them with command line
  // arguments.
  //
  if (!opt_file)
  {
#ifdef __tile__
    long val;

    int sector_size = 0;
    int page_size = 0;

    opt_file = "/dev/srom/bootimage";

    FILE* info_file = fopen("/sys/devices/platform/srom/0/page_size", "r");
    if (info_file)
    {
      if (fscanf(info_file, "%ld", &val) == 1)
        page_size = val;
      fclose(info_file);
      info_file = fopen("/sys/devices/platform/srom/0/sector_size", "r");
      if (info_file)
      {
        if (fscanf(info_file, "%ld", &val) == 1)
          sector_size = val;
        fclose(info_file);
      }
    }
    else
    {
      /* Try deprecated old character device API. */
      info_file = fopen("/dev/srom/info", "r");
      if (info_file)
      {
        char keyword[80];
        while (fscanf(info_file, " %[^:]: %ld", keyword, &val) == 2)
        {
          if (!strcmp(keyword, "sector_size"))
            sector_size = val;
          if (!strcmp(keyword, "page_size"))
            page_size = val;
        }
        fclose(info_file);
      }
    }

    if (info_file)
    {
      //
      // Essentially we don't let the user specify the sector or page
      // sizes on a real ROM.  We allow the options through if we couldn't
      // get the real sizes from the system; this isn't expected to happen
      // but provides us with an out if it does.
      //
      if ((opt_sector_size && sector_size) || (opt_page_size && page_size))
      {
        fprintf(stderr, SBIM_NAME ": cannot specify sector/page sizes on"
                "real SROM\n");
        exit(1);
      }

      opt_sector_size = sector_size ? sector_size : opt_sector_size;
      opt_page_size = page_size ? page_size : opt_page_size;
    }
#else /* __tile__ */
    //
    // If we aren't running native, we can't probe the SROM, so we force them
    // to enter a file name.
    //
    fprintf(stderr, SBIM_NAME ": must use --file option if not in "
            "native mode\n");
    usage();
#endif /* __tile__ */
  }

  //
  // Open the SROM file, load the header.  If we're supposed to have a
  // header, and don't, die.
  //
  srom_fd = open(opt_file, (flags & OPEN_SROM_WRITE) ? O_RDWR : O_RDONLY);
  if (srom_fd < 0)
  {
    fprintf(stderr, SBIM_NAME ": can't open srom file %s: %s\n", opt_file,
            strerror(errno));
    exit(1);
  }

  read_srom_header();

  if (!srom_valid && !(flags & OPEN_SROM_NO_HDR_OK))
  {
    fprintf(stderr, SBIM_NAME ": srom file %s has an invalid SROM header.\n"
            "  Use --install-booter to add one.\n", opt_file);
    exit(1);
  }

  //
  // If we're going to invalidate the header, we don't need any of the
  // information we might retrieve from it below, and in fact we don't want to
  // do any of the consistency checks, since that might prevent us from
  // invalidating a really corrupt but supposedly-valid header.  So, we just
  // return now.
  //
  if (flags & OPEN_SROM_INVALIDATE_HDR)
    return;

  //
  // Make sure the sector and page sizes match the header.  (If they're unset,
  // take them from the header.)
  //
  if (srom_valid)
  {
    if (opt_sector_size == 0)
      opt_sector_size = srom_hdr.sector_size;
    if (opt_page_size == 0)
      opt_page_size = srom_hdr.page_size;

    if (opt_sector_size != srom_hdr.sector_size ||
        opt_page_size != srom_hdr.page_size)
    {
      fprintf(stderr, SBIM_NAME ": mismatch between SROM header (sector "
              "size %d, page size %d)\n"
              "  and option arguments or SROM characteristics.  Suggest "
              "correcting option\n"
              "  arguments, or invalidating SROM header with "
              "--invalidate-booter and then\n"
              "  reinstalling SROM booter with --install-boooter.\n",
              srom_hdr.sector_size, srom_hdr.page_size);
      exit(1);
    }
  }

  //
  // If we don't know the necessary values by now, die.
  //
  if (opt_sector_size == 0 || opt_page_size == 0)
  {
    if (flags & OPEN_SROM_NO_PARAM_OK)
    {
      //
      // In the info case, we might not have parameters, and we don't want
      // to force the user to enter them just so we can say "you don't
      // have an SROM header".  We set these to nonzero values so that
      // code below doesn't blow up.
      //
      opt_sector_size = opt_page_size = 1;
    }
    else
    {
      fprintf(stderr, SBIM_NAME ": can't determine SROM characteristics, "
              "please specify --sector-size and --page-size\n");
      exit(1);
    }
  }

  //
  // Get the size of the SROM.  If we have a valid header, we always use
  // that size; otherwise, we let the user specify one as long as it's not
  // bigger than the real size, and if they don't, we use the real size.
  //
  if (opt_sectors)
    opt_srom_size = opt_sectors * opt_sector_size;

  if (srom_valid)
  {
    if (opt_srom_size)
    {
      fprintf(stderr, SBIM_NAME ": can't specify SROM size if SROM already "
              "has valid header.\n");
      exit(1);
    }
    else
      opt_srom_size = srom_hdr.srom_size;
  }

  off_t real_srom_size = get_srom_size_or_die();

  if (opt_srom_size > real_srom_size)
  {
    fprintf(stderr, SBIM_NAME ": specified or header SROM size larger than "
            "actual size\n");
    exit(1);
  }

  if (opt_srom_size == 0)
    opt_srom_size = real_srom_size;

  opt_sectors = opt_srom_size / opt_sector_size;

  //
  // Get the number of images.
  //
  if (srom_valid && !(flags & OPEN_SROM_IGNORE_NUM_IMG))
  {
    if (opt_num_images >= 0)
    {
      fprintf(stderr, SBIM_NAME ": can't specify number of images if SROM "
              "already has valid header.\n");
      exit(1);
    }
    else
      opt_num_images = srom_hdr.num_images;
  }

  if (opt_num_images < 0)
    opt_num_images = DEFAULT_IMAGES;

  //
  // Now that we know the SROM characteristics, figure out where the
  // various image slots are.
  //
  if (opt_num_images)
  {
    int boot_sectors =
      (booter_offset + booter_max_length + opt_sector_size - 1) /
      opt_sector_size;
    int sec_per_image = (opt_sectors - boot_sectors) / opt_num_images;
    image_max_length = sec_per_image * opt_sector_size;
    for (int i = 0; i < opt_num_images; i++)
      image_offsets[i] = opt_sector_size * (boot_sectors + i * sec_per_image);
  }
  else
  {
    booter_max_length = opt_srom_size;
  }

  if (opt_verbose > 1)
  {
    fprintf(stderr, "open_srom: file %s sectors %d sector_sz %#x "
            "page_sz %#x\n",
            opt_file, opt_sectors, opt_sector_size, opt_page_size);
    fprintf(stderr, "           boot_max_len %#x image_len %#x image_off",
            booter_max_length, image_max_length);
    for (int i = 0; i < opt_num_images; i++)
      fprintf(stderr, " %#x", image_offsets[i]);
    fprintf(stderr, "\n");
  }
}

/** Choose a target image based on an input specification; it need not be
 *  currently valid.  If no specification is given, pick the oldest
 *  unlocked image if all are valid, else pick any invalid image.  (The
 *  default is chosen for use by the install operation.)  Also optionally
 *  returns the highest and lowest generation number found in a valid
 *  image.  Exits on failure.
 * @param target_str Input image specification; may be NULL.
 * @param hi_gen_p Pointer to highest valid generation number; may be NULL
 *        if return of the high generation is not desired.  Zero if no
 *        images are valid.
 * @param lo_gen_p Pointer to lowest valid generation number; may be NULL
 *        if return of the low generation is not desired.  INT_MAX if no
 *        images are valid.
 * @return Image number selected.
 */
int
pick_target(const char* target_str, int* hi_gen_p, int* lo_gen_p)
{
  int target = 0;
  int hi_gen = 0;              // Highest generation seen
  int lo_gen = INT_MAX;        // Lowest generation seen
  int lo_unl_gen = INT_MAX;    // Lowest unlocked generation seen
  int lo_unl_gen_target = -1;  // Image which had the low unlocked generation

  //
  // Go through the images; figure out the highest generation number and the
  // oldest image.
  //
  for (int i = 0; i < opt_num_images; i++)
  {
    if (image_valid[i])
    {
      struct srom_image_header* ih = &image_hdr[i];

      if (ih->generation > hi_gen)
        hi_gen = ih->generation;

      if (ih->generation < lo_gen)
        lo_gen = ih->generation;

      if (ih->generation <= lo_unl_gen && !(ih->flags & SROM_IMAGE_FLG_LOCK))
      {
        lo_unl_gen = ih->generation;
        lo_unl_gen_target = i;
      }
    }
    else
    {
      //
      // If we ever hit an invalid image, we call it the oldest image.
      // We set the low generation number so it won't be replaced by a
      // later valid image.
      //
      lo_unl_gen = 0;
      lo_unl_gen_target = i;
    }
  }

  if (target_str == NULL)
  {
    //
    // No specification, so pick the default, as long as we found an unlocked
    // or invalid image.
    //
    if (lo_unl_gen_target < 0)
    {
      fprintf(stderr, SBIM_NAME ": no unlocked or invalid images\n");
      exit(1);
    }
    else
      target = lo_unl_gen_target;
  }
  else if (*target_str == '@')
  {
    //
    // Absolute slot number.
    //
    char* endptr;
    target = strtol(target_str + 1, &endptr, 0);

    if (*endptr || !target_str[1] || target >= opt_num_images)
    {
      fprintf(stderr, SBIM_NAME ": direct location %s invalid or too large\n",
              target_str);
      exit(1);
    }
  }
  else
  {
    //
    // Relative slot number.  To handle this case we sort the available
    // images in increasing order of age, with the invalid ones at the end;
    // then we just pick the -Nth entry in the list.
    //
    char* endptr;
    int rel_target = strtol(target_str, &endptr, 0);
    if (*endptr || !*target_str || rel_target > 0 ||
        rel_target < 1 - opt_num_images)
    {
      fprintf(stderr, SBIM_NAME ": relative location %s invalid\n", target_str);
      exit(1);
    }

    //
    // We don't actually want to move the image headers so we set up an
    // array of indexes pointing to them, and swap that.
    //
    int im_index[opt_num_images];
    for (int i = 0; i < opt_num_images; i++)
      im_index[i] = i;

    for (int i = 0; i < opt_num_images - 1; i++)
      for (int j = i + 1; j < opt_num_images; j++)
        if (!image_valid[im_index[i]] || image_hdr[im_index[i]].generation <
                                         image_hdr[im_index[j]].generation)
        {
          int t = im_index[i];
          im_index[i] = im_index[j];
          im_index[j] = t;
        }

    target = im_index[-rel_target];
  }

  if (hi_gen_p)
    *hi_gen_p = hi_gen;

  if (lo_gen_p)
    *lo_gen_p = lo_gen;

  return target;
}

/** Open an input file for reading.  Exits on failure.
 * @param max_size Maximum allowable size of the input file in bytes.
 */
void
open_input_or_die(int max_size)
{
  //
  // Open input file.
  //
  input_fd = open(opt_input_file, O_RDONLY);
  if (input_fd < 0)
  {
    fprintf(stderr, SBIM_NAME ": cannot open input file %s: %s\n",
            opt_input_file, strerror(errno));
    exit(1);
  }

  //
  // Get input file size, validate against available length.
  //
  struct stat sb;
  if (fstat(input_fd, &sb) , 0)
  {
    perror(SBIM_NAME ": can't stat input file");
    exit(1);
  }

  if (sb.st_size > max_size)
  {
    fprintf(stderr, SBIM_NAME ": input file %s is too large; file is "
            "%lld bytes, maximum size is %d bytes\n",
            opt_input_file, (long long)sb.st_size, max_size);
    exit(1);
  }
}

/** Return the current position of the input file; exit on failure.
 * @return Current position.
 */
off_t
get_input_pos_or_die()
{
  off_t pos = lseek(input_fd, 0, SEEK_CUR);
  if (pos == (off_t) -1)
  {
    fprintf(stderr, SBIM_NAME ": cannot get position in input file %s: %s\n",
            opt_input_file, strerror(errno));
    exit(1);
  }
  return pos;
}

/** Read bytes from the input file; exit on failure.  Getting fewer bytes
 *  than requested is not an error condition.
 * @param buf Where to put the bytes.
 * @param len Number of bytes to read.
 * @return Number of bytes actually read.
 */
size_t
read_input_or_die(void* buf, size_t len)
{
  int rv = read(input_fd, buf, len);
  if (rv < 0)
  {
    fprintf(stderr, SBIM_NAME ": cannot read input file %s: %s\n",
            opt_input_file, strerror(errno));
    exit(1);
  }

  return rv;
}

/** Read bytes from the input file; exit on failure.  Getting fewer bytes
 *  than requested is an error condition, and results in an exit.
 * @param buf Where to put the bytes.
 * @param len Number of bytes to read.
 */
void
read_all_input_or_die(void* buf, size_t len)
{
  int rv = read(input_fd, buf, len);
  if (rv != len)
  {
    fprintf(stderr, SBIM_NAME ": cannot read input file %s: file too short\n",
            opt_input_file);
    exit(1);
  }
}

/** Modify the current position of the input file; exit on failure.
 * @param offset New absolute byte position.
 */
void
lseek_input_or_die(off_t offset)
{
  if (lseek(input_fd, offset, SEEK_SET) == (off_t) -1)
  {
    fprintf(stderr, SBIM_NAME ": cannot seek input file %s: %s\n",
            opt_input_file, strerror(errno));
    exit(1);
  }
}

/** Sets the input file pointer to the start of the input file.  Exits on
 *  failure.
 */
void
rewind_input_or_die()
{
  lseek_input_or_die(0);
}

/** CRC bytes from the input file; exit on failure.  Getting fewer bytes
 *  than requested is an error condition, and results in an exit.
 * @param len Number of bytes to CRC.
 * @param crc Pointer to the running CRC to be updated.
 */
void
crc_all_input_or_die(size_t len, uint_reg_t* crc)
{
  while (len)
  {
    size_t passlen = len;

    char buf[64 * 1024];

    if (passlen > sizeof (buf))
      passlen = sizeof (buf);

    read_all_input_or_die(buf, passlen);

    *crc = do_crc(buf, passlen, *crc);

    len -= passlen;
  }
}

/** Open an output file for writing.  Exits on failure.
 */
void
open_output_or_die()
{
  //
  // Open output file.
  //
  output_fd = open(opt_output_file, O_RDWR | O_CREAT | O_TRUNC, 0600);
  if (output_fd < 0)
  {
    fprintf(stderr, SBIM_NAME ": cannot open output file %s: %s\n",
            opt_output_file, strerror(errno));
    exit(1);
  }
}

/** Write bytes to the output file; exit on failure.
 * @param buf Where to get the bytes.
 * @param len Number of bytes to write.
 */
void
write_output_or_die(const void* buf, size_t len)
{
  if (write(output_fd, buf, len) != len)
  {
    fprintf(stderr, SBIM_NAME ": cannot write output file %s: %s\n",
            opt_output_file, strerror(errno));
    exit(1);
  }
}

/** Read a line of input from the terminal; if it's not "yes", print a
 *  message and exit.
 */
void
confirm()
{
  char resp[10];
  fgets(resp, sizeof (resp), stdin);

  if (!strcmp(resp, "yes\n"))
    return;

  printf("No change was made to the SROM.\n");
  exit(1);
}

/** Damage the SROM at or near its current location.
 * @param offset Offset from current byte position at which to do damage. */
static void
damage_srom(int offset)
{
  uint8_t byte;
  off_t pos = get_srom_pos_or_die();
  lseek_srom_or_die(pos + offset);
  read_srom_or_die(&byte, sizeof (byte));
  lseek_srom_or_die(pos + offset);
  byte++;
  write_srom_or_die(&byte, sizeof (byte));
}

/** Validate an L1 boot stream within the input file.   This is only meant
 *  to be called from validate_input(), below.
 * @param phase String printed to denote where we are in the file if we get
 *   a CRC miscompare.
 */
static void
validate_l1_stream(const char* phase)
{
  uint_reg_t running_crc = 0;
  uint_reg_t seg_len_addr[2];
  do
  {
    read_input_or_die(&seg_len_addr, sizeof (seg_len_addr));
    running_crc = do_crc(seg_len_addr, sizeof (seg_len_addr), running_crc);

    uint_reg_t crc;
    read_input_or_die(&crc, sizeof (crc));

    if (crc != running_crc)
    {
      fprintf(stderr, SBIM_NAME ": corrupt input file %s, bad CRC in %s\n",
              opt_input_file, phase);
      exit(1);
    }

    crc_all_input_or_die(seg_len_addr[0] * sizeof (uint_reg_t), &running_crc);

  } while (seg_len_addr[0] != 0);
}

/** Validate an input file before we install it in the SROM.  Our input
 *  stream must already be positioned at the start of the file; we rewind
 *  the file before returning.  If the file is corrupt, we print an error
 *  message and exit.
 * @param is_booter Nonzero if the input is to be used as an SROM booter
 *   image; zero if it will be a boot image.
 * @param is_prebooter Nonzero if the input is a prebooter image; zero if
 *   not.  If nonzero, the value of is_booter is ignored.
 */
static void
validate_input(int is_booter, int is_prebooter)
{
  //
  // Check the beginning of the file for the extra segment containing the
  // CRC for the L0.5 booter, unless we expect an old-format input file.
  //
  uint_reg_t crcseg[4] = { 0 };
  uint_reg_t seg_len_addr[2];

  if (opt_old_format)
    read_all_input_or_die(&seg_len_addr, sizeof (seg_len_addr));
  else
  {
    read_all_input_or_die(&crcseg, sizeof (crcseg));
    read_all_input_or_die(&seg_len_addr, sizeof (seg_len_addr));

    if (crcseg[0] != 1 || crcseg[3] != hw_l0boot_next_pkt ||
        crcseg[1] != seg_len_addr[1])
    {
      fprintf(stderr, SBIM_NAME ": corrupt input file %s, no CRC for L0.5 "
              "booter\n", opt_input_file);
      exit(1);
    }
  }

  //
  // CRC the L0.5 booter and check against the value we just got.
  //
  uint_reg_t exp_crc = crcseg[2];
  uint_reg_t act_crc = do_crc(seg_len_addr, sizeof (seg_len_addr), 0);

  //
  // We need to handle the first real segment specially, since we're going to
  // look at the first instruction to see what chip it's built for.
  //
  tile_bundle_bits first_bundle;
  read_all_input_or_die(&first_bundle, sizeof (first_bundle));
  act_crc = do_crc(&first_bundle, sizeof (first_bundle), act_crc);

  //
  // Insist on finding "moveli zero, <immediate>" in the X0 slot in the first
  // bundle.
  //
  if (get_Mode(first_bundle) != 0 ||
      get_Opcode_X0(first_bundle) != ADDLI_OPCODE_X0 ||
      get_Dest_X0(first_bundle) != TREG_ZERO ||
      get_SrcA_X0(first_bundle) != TREG_ZERO)
  {
    fprintf(stderr, SBIM_NAME ": corrupt input file %s, bad first bundle "
            "%#llx for L0.5 booter\n", opt_input_file, first_bundle);
    exit(1);
  }

  //
  // The <immediate> from the moveli instruction is the chip number.
  //
  int tile_chip_found = get_Imm16_X0(first_bundle);
  if (tile_chip_found != tile_chip)
  {
    fprintf(stderr, SBIM_NAME ": corrupt input file %s, built for chip %d, "
            "expected chip %d\n", opt_input_file, tile_chip_found, tile_chip);
    exit(1);
  }

  crc_all_input_or_die(seg_len_addr[0] * sizeof (uint_reg_t) -
                       sizeof (first_bundle), &act_crc);

  uint_reg_t jump_addr;
  read_all_input_or_die(&jump_addr, sizeof (jump_addr));
  act_crc = do_crc(&jump_addr, sizeof (jump_addr), act_crc);

  //
  // Handle any remaining segments.
  //
  while (jump_addr == hw_l0boot_next_pkt)
  {
    read_all_input_or_die(&seg_len_addr, sizeof (seg_len_addr));
    act_crc = do_crc(seg_len_addr, sizeof (seg_len_addr), act_crc);

    crc_all_input_or_die(seg_len_addr[0] * sizeof (uint_reg_t), &act_crc);

    read_all_input_or_die(&jump_addr, sizeof (jump_addr));
    act_crc = do_crc(&jump_addr, sizeof (jump_addr), act_crc);
  }

  //
  // For an old-format file, we never got an expected CRC, so we skip this
  // check.
  //
  if (!opt_old_format && act_crc != exp_crc)
  {
    fprintf(stderr, SBIM_NAME ": corrupt input file %s, bad CRC for L0.5 "
            "booter\n", opt_input_file);
    exit(1);
  }

  //
  // Validate the L1 stream containing the L1 booter.
  //
  validate_l1_stream("L1 booter");

  if (is_booter || is_prebooter)
  {
    //
    // If this is an SROM booter, or a prebooter on Gx, we ought to be at
    // the end of the file at this point.  We try to read one byte; if we
    // succeed, the file is corrupt.
    //
    char dummy;
    if (read_input_or_die(&dummy, 1) != 0)
    {
      fprintf(stderr, SBIM_NAME ": corrupt input file %s, bytes after L1 "
              "booter\n", opt_input_file);
      exit(1);
    }

    rewind_input_or_die();
    return;
  }

  //
  // Validate the arguments passed to the L1 booter.
  //
  uint_reg_t feedaddr;
  read_all_input_or_die(&feedaddr, sizeof (feedaddr));
  act_crc = do_crc(&feedaddr, sizeof (feedaddr), 0);
  crc_all_input_or_die((3 + ((feedaddr >> 1) & 0x3F)) * sizeof (uint_reg_t),
                       &act_crc);
  read_all_input_or_die(&exp_crc, sizeof (exp_crc));

  if (act_crc != exp_crc)
  {
    fprintf(stderr, SBIM_NAME ": corrupt input file %s, bad CRC in L1 "
            "boot arguments\n", opt_input_file);
    exit(1);
  }

  if (is_prebooter)
  {
    //
    // If this is a prebooter, we ought to be at the end of the file at
    // this point.  We try to read one byte; if we succeed, the file is
    // corrupt.
    //
    char dummy;
    if (read_input_or_die(&dummy, 1) != 0)
    {
      fprintf(stderr, SBIM_NAME ": corrupt input file %s, bytes after L1 "
              "boot arguments\n", opt_input_file);
      exit(1);
    }

    rewind_input_or_die();
    return;
  }

  //
  // Validate the L1 stream containing the hypervisor.
  //
  validate_l1_stream("hypervisor");

  //
  // Validate the hypervisor filesystem.
  //
  fs_hdr_t hvfs_header;
  read_all_input_or_die(&hvfs_header, sizeof (hvfs_header));
  if (hvfs_header.magic != HV_FS_MAGIC)
  {
    fprintf(stderr, SBIM_NAME ": corrupt input file %s, bad magic number in "
            "HVFS header\n", opt_input_file);
    exit(1);
  }

  act_crc = do_crc(&hvfs_header, sizeof (hvfs_header), 0);

  fs_crc_t hvfs_crc;
  read_all_input_or_die(&hvfs_crc, sizeof (hvfs_crc));
  if (act_crc != hvfs_crc.crc)
  {
    fprintf(stderr, SBIM_NAME ": corrupt input file %s, bad CRC in "
            "HVFS header\n", opt_input_file);
    exit(1);
  }

  act_crc = 0;
  crc_all_input_or_die(hvfs_header.fs_len - sizeof (hvfs_header), &act_crc);
  read_all_input_or_die(&hvfs_crc, sizeof (hvfs_crc));
  if (act_crc != hvfs_crc.crc)
  {
    fprintf(stderr, SBIM_NAME ": corrupt input file %s, bad CRC in HVFS\n",
            opt_input_file);
    exit(1);
  }

  //
  // We ought to be at the end of the file at this point.  We try to read one 
  // byte; if we succeed, the file is corrupt.
  //
  char dummy;
  if (read_input_or_die(&dummy, 1) != 0)
  {
    fprintf(stderr, SBIM_NAME ": corrupt input file %s, bytes after HVFS\n",
            opt_input_file);
    exit(1);
  }

  //
  // We're done; rewind the input file.
  //
  rewind_input_or_die();
}


//
// Action routines
//

/** Install a new image in the SROM.
 */
static void
action_install()
{
  //
  // Open the SROM; read the image headers; pick the slot where we'll write
  // the new image; open the input file and make sure it's not too long to fit.
  //
  open_srom_or_die(OPEN_SROM_WRITE);

  ensure_nimages_nonzero("install");

  read_image_headers();

  int hi_gen, lo_gen;
  int target = pick_target(opt_id, &hi_gen, &lo_gen);

  if (image_valid[target] && (image_hdr[target].flags & SROM_IMAGE_FLG_LOCK))
  {
    fprintf(stderr, SBIM_NAME ": cannot install image in slot %s since it is "
            "valid and locked\n", opt_id);
    exit(1);
  }

  int new_gen;

  if (opt_oldest)
  {
    //
    // We're adding this image as oldest.  If we're replacing the oldest
    // image, we can reuse its generation number; if no images are valid,
    // we use generation 1, as we do without --oldest; otherwise we pick
    // one lower than any valid image.
    //
    if (image_valid[target] && image_hdr[target].generation == lo_gen)
      new_gen = lo_gen;
    else if (lo_gen == INT_MAX)
      new_gen = 1;
    else
      new_gen = lo_gen - 1;

    if (new_gen < 0)
    {
      fprintf(stderr, SBIM_NAME ": cannot install image as oldest; "
              "image generation 0 is valid\n");
      exit(1);
    }
  }
  else
  {
    //
    // We're adding this image as newest; we just use the next available
    // generation number.
    //
    new_gen = hi_gen + 1;
  }

  open_input_or_die(image_max_length - sizeof (struct srom_image_header));

  validate_input(0, 0);

  //
  // Invalidate the current image header; if something bad happens while we're
  // writing the new image we want it to be obvious that the old one is busted.
  // Note that we use all 1's for the "invalid" value so that we won't have to
  // erase the target sector a second time when we write the real header.
  //
  struct srom_image_header inv_hdr;
  memset(&inv_hdr, ~0, sizeof (inv_hdr));

  lseek_srom_or_die(image_offsets[target]);
  write_srom_or_die(&inv_hdr, sizeof (inv_hdr));

  //
  // Copy the level 0.5 booter to the SROM; while copying, calculate its
  // length and CRC.
  //
  int l0_boot_bytes = 0;
  uint32_t l0_boot_crc = 0;
  SHA1Context sha1_ctx;
  SHA1Reset(&sha1_ctx);

  uint_reg_t jump_addr;

  do
  {
    uint_reg_t seg_len_addr[2];

    read_all_input_or_die(&seg_len_addr, sizeof (seg_len_addr));
    write_srom_or_die_do_crc(&seg_len_addr, sizeof (seg_len_addr),
                             &l0_boot_crc, &l0_boot_bytes);
    SHA1Input(&sha1_ctx, (const uint8_t*) &seg_len_addr, sizeof (seg_len_addr));

    uint_reg_t buf[seg_len_addr[0]];

    read_all_input_or_die(buf, sizeof (buf));
    write_srom_or_die_do_crc(buf, sizeof (buf),
                             &l0_boot_crc, &l0_boot_bytes);
    SHA1Input(&sha1_ctx, (const uint8_t*) buf, sizeof (buf));

    read_all_input_or_die(&jump_addr, sizeof (jump_addr));
    write_srom_or_die_do_crc(&jump_addr, sizeof (jump_addr),
                             &l0_boot_crc, &l0_boot_bytes);
    SHA1Input(&sha1_ctx, (const uint8_t*) &jump_addr, sizeof (jump_addr));
  } while (jump_addr == hw_l0boot_next_pkt);

  //
  // Copy the rest of the input file, recording the total length and CRC.
  //
  int total_bytes = l0_boot_bytes;
  uint32_t total_crc = l0_boot_crc;

  while (1)
  {
    char buf[64 * 1024];

    size_t len = read_input_or_die(buf, sizeof (buf));

    if (len == 0)
      break;

    write_srom_or_die_do_crc(buf, len, &total_crc, &total_bytes);
    SHA1Input(&sha1_ctx, (const uint8_t*) buf, len);
  }

  //
  // Pad out to a full number of words.  This shouldn't ever happen.
  //
  if (total_bytes & (sizeof (uint_reg_t) - 1))
  {
    char dummy[] = "\0\0\0\0\0\0\0";
    int padding_bytes = sizeof (uint_reg_t) -
                        (total_bytes & (sizeof (uint_reg_t) - 1));
    write_srom_or_die_do_crc(&dummy, padding_bytes, &total_crc, &total_bytes);
    SHA1Input(&sha1_ctx, (const uint8_t*) &dummy, padding_bytes);
  }

  //
  // Write the final image header to the SROM.
  //
  struct srom_image_header new_hdr =
  {
    .magic = SROM_IMAGE_MAGIC,
    .offset = sizeof (struct srom_image_header),
    .generation = new_gen,
    .l0_boot_words = l0_boot_bytes / sizeof (uint_reg_t),
    .l0_boot_crc = l0_boot_crc,
    .total_words = total_bytes / sizeof (uint_reg_t),
    .timestamp = (uint32_t) time(NULL),
    .total_crc = total_crc,
    .flags = SROM_IMAGE_FLG_EXT1,
  };
  SHA1Result(&sha1_ctx, new_hdr.sha1_digest);

  if (opt_comment)
    strncpy(new_hdr.comment, opt_comment, sizeof (new_hdr.comment));
  new_hdr.header_crc =
    do_crc(&new_hdr, offsetof(typeof(new_hdr), header_crc), 0);
  new_hdr.header_crc_ext1 =
    do_crc(&new_hdr, offsetof(typeof(new_hdr), header_crc_ext1), 0);

  lseek_srom_or_die(image_offsets[target]);
  write_srom_or_die(&new_hdr, sizeof (new_hdr));

  //
  // If requested, read back what we just wrote and verify it.
  //
  if (opt_verify)
  {
    int bad_verify = 0;

    flush_srom();

    read_image_headers();

    //
    // First verify the image header.
    //
    if (memcmp(&new_hdr, &image_hdr[target], sizeof (new_hdr)))
      bad_verify = 1;
    else
    {
      //
      // Now verify the image itself.
      //
      lseek_srom_or_die(image_offsets[target] + image_hdr[target].offset);
      rewind_input_or_die();

      int tot_bytes = image_hdr[target].total_words * sizeof (uint_reg_t);

      while (tot_bytes)
      {
        const int bufsiz = 64 * 1024;
        char in_buf[bufsiz];
        char srom_buf[bufsiz];

        int bytesthispass = tot_bytes;
        if (bytesthispass > bufsiz)
          bytesthispass = bufsiz;

        read_all_input_or_die(in_buf, bytesthispass);
        read_srom_or_die(srom_buf, bytesthispass);

        if (memcmp(in_buf, srom_buf, bytesthispass))
        {
          bad_verify = 1;
          break;
        }
        tot_bytes -= bytesthispass;
      }
    }

    if (bad_verify)
    {
      printf("Verification failed!\n");
      exit(1);
    }
    else if (opt_verbose)
      printf("Verification successful.\n");
  }
}

/** Extract an image from the SROM.
 */
static void
action_extract()
{
  //
  // Open the SROM; read the image headers; pick the slot where we'll get
  // the image from; make sure the image is valid; open the output file.
  //
  open_srom_or_die(OPEN_SROM_READ);

  ensure_nimages_nonzero("extract");

  read_image_headers();

  int target = pick_target((opt_id) ? opt_id : "0", NULL, NULL);

  if (!image_valid[target])
  {
    fprintf(stderr, SBIM_NAME ": image at location %s not valid\n", opt_id);
    exit(1);
  }

  open_output_or_die();

  //
  // Copy the image to the output file.
  //
  lseek_srom_or_die(image_offsets[target] + image_hdr[target].offset);

  int tot_bytes = image_hdr[target].total_words * sizeof (uint_reg_t);

  while (tot_bytes)
  {
    char buf[64 * 1024];

    int bytesthispass = tot_bytes;
    if (bytesthispass > sizeof(buf))
      bytesthispass = sizeof(buf);

    read_srom_or_die(buf, bytesthispass);

    write_output_or_die(buf, bytesthispass);

    tot_bytes -= bytesthispass;
  }
}

/** Install a boot image.
 * @param is_prebooter Nonzero if this is a preboot image.  A preboot image
 *   isn't an SROM booter, so we force the number of images to 0 and
 *   validate the image format slightly differently (we expect a L0.5
 *   booter and L1 booter with trailer, but nothing else).
 */
static void
action_install_booter(int is_prebooter)
{
  //
  // We write a separate segment header for each booter chunk of this
  // many bytes.
  //
  const int booter_chunk_size = 128 * 1024 - 8;

  //
  // Open the SROM, ignoring the current number of images since we're
  // installing a new booter; open the input file, and make sure it isn't
  // too big; make sure they really want to overwrite the booter.
  //
  open_srom_or_die(OPEN_SROM_WRITE | OPEN_SROM_NO_HDR_OK |
                   OPEN_SROM_IGNORE_NUM_IMG);

  int avail_space = booter_max_length - SROM_TRAILER_SIZE(opt_num_images) -
                    sizeof (BOOT_ROM_HEADER_t);

  avail_space -= (1 + ((avail_space + booter_chunk_size - 1) /
                       booter_chunk_size)) *
                 sizeof (BOOT_ROM_DESC_t);


















  open_input_or_die(avail_space);

  //
  // If the SROM is configured for 0 images, we want to validate the installed
  // file as a complete boot image, not as a booter; if this is a prebooter,
  // validate accordingly.
  //
  if (is_prebooter)
  {
    opt_num_images = 0;
    validate_input(0, 1);
  }
  else
    validate_input(opt_num_images > 0, 0);

  if (srom_valid && !opt_yes)
  {
    printf("You are about to overwrite the SROM booter.  If a power failure\n"
           "or system fault should occur during this process it might render\n"
           "this system no longer bootable from the SROM.  Are you sure you\n"
           "want to do this (yes/no)? ");

    confirm();
  }

  //
  // Get any extra SROM segments we might want to add.  These go between
  // the SROM header and the booter itself.
  //
  vbuf_t ex_seg = get_extra_segments();
  if (vbuf_len(ex_seg))
  {
    // If we have any extra segments, add one more at the end which
    // contains a CRC for the extra data.
    BOOT_ROM_DESC_t crc_seg =
    {{
      .words = 2,
      .addr = RSH_SCRATCHPAD / 8,
      .chn = 0,
      .e = 0,
    }};

    vbuf_append(&ex_seg, &crc_seg, sizeof (crc_seg));
    uint32_t crc = do_crc(vbuf_data(ex_seg), vbuf_len(ex_seg), 0);
    vbuf_append(&ex_seg, &crc, sizeof (crc));
  }

  //
  // Invalidate the current SROM header; if something bad happens while we're
  // writing the new booter we want it to be obvious that the old one is busted.
  // Note that we use all 1's for the "invalid" value so that we won't have to
  // erase the target sector a second time when we write the real header.
  //
  struct srom_overall_header inv_hdr;
  memset(&inv_hdr, ~0, sizeof (inv_hdr));

  lseek_srom_or_die(booter_offset);
  write_srom_or_die(&inv_hdr, sizeof (inv_hdr));
  lseek_srom_or_die(booter_offset + sizeof (inv_hdr) + vbuf_len(ex_seg));

  //
  // Copy the booter to the SROM, adding appropriate ROM segment headers, and
  // calcuating a CRC as we go.
  //
  char buf[booter_chunk_size];
  int booter_bytes = 0;
  uint32_t booter_crc = 0;
  size_t len;





































































































  while ((len = read_input_or_die(buf, booter_chunk_size)) != 0)
  {
    BOOT_ROM_DESC_t rsh =
    {{
      // words includes the actual segment header itself.
      .words = 1 + (len + 7) / 8,
      // This is bootable, so it gets written to the UDN.
      .addr = RSH_PG_DATA / 8,
      .chn = 0,
      .e = 0,
    }};

    write_srom_or_die_do_crc(&rsh, sizeof (rsh), &booter_crc, &booter_bytes);
    write_srom_or_die_do_crc(buf, len, &booter_crc, &booter_bytes);

    if (len & (sizeof (uint_reg_t) - 1))
    {
      char dummy[] = "\0\0\0\0\0\0\0";
      write_srom_or_die_do_crc(&dummy, sizeof (uint_reg_t) -
                                       (len & (sizeof (uint_reg_t) - 1)),
                               &booter_crc, &booter_bytes);
    }
  }

  //
  // Write the trailer to the SROM.
  //
  char sbt[SROM_TRAILER_SIZE(opt_num_images)];

  if (is_prebooter || opt_num_images == 0)
  {
    //
    // For a prebooter, or if the SROM booter is the bootrom itself, we
    // don't want to include the normal trailer.  Ideally we'd already be
    // done, but it's a pain to mark the last booter segment as such with
    // the code above, so we write one last segment which just writes a
    // dummy word to the scratchpad.
    //
    BOOT_ROM_DESC_t rsh4sbt =
    {{
      .words = 1,
      .addr = RSH_SCRATCHPAD / 8,
      .chn = 0,
      .e = 1,
    }};
    write_srom_or_die_do_crc(&rsh4sbt, sizeof (rsh4sbt),
                             &booter_crc, &booter_bytes);
    uint_reg_t dummy = 0;
    write_srom_or_die_do_crc(&dummy, sizeof (dummy),
                             &booter_crc, &booter_bytes);
  }
  else
  {
    //
    // If we're not a prebooter, we need the real trailer.  First do
    // the segment header.
    //
    BOOT_ROM_DESC_t rsh4sbt =
    {{
      .words = 1 + (SROM_TRAILER_SIZE(opt_num_images) + 7) / 8,
      .addr = RSH_PG_DATA / 8,
      .chn = 0,
      .e = 1,
    }};
    write_srom_or_die_do_crc(&rsh4sbt, sizeof (rsh4sbt),
                             &booter_crc, &booter_bytes);

    //
    // The trailer is a structure, followed by one or more image slot offsets,
    // finished off with a CRC.  We construct the whole thing in a buffer and
    // write it out all at once to make verify easier.
    //

    struct srom_boot_trailer sbt_template =
    {
      .magic = SROM_TRAILER_MAGIC,
      .sector_size = opt_sector_size,
      .page_size = opt_page_size,
      .srom_size = opt_srom_size,
      .num_images = opt_num_images,
    };

    memcpy(sbt, &sbt_template, sizeof (sbt_template));
    memcpy(sbt + sizeof (sbt_template), &image_offsets,
           opt_num_images * sizeof (*image_offsets));

    uint32_t crc = do_crc(sbt, sizeof (sbt) - sizeof (crc), 0);
    memcpy(sbt + sizeof (sbt) - sizeof (crc), &crc, sizeof (crc));

    write_srom_or_die_do_crc(sbt, sizeof (sbt), &booter_crc, &booter_bytes);
  }

  //
  // Now write the real header at the start of the SROM.
  //
  struct srom_overall_header new_hdr =
  {
    .rom_header =
    {{
      .rev_id = 0,
    }},
    .rom_seg_header =
    {{
      // Note that we count this RomSegHeader but not the RomHeader above.
      .words = (sizeof (struct srom_overall_header) + 7) / 8 - 1,
      // We don't want this to go to a tile; it's just for sbim's use.  We
      // write it to the scratchpad register to throw it away.
      .addr = RSH_SCRATCHPAD / 8,
      .chn = 0,
    }},
    .magic = SROM_HEADER_MAGIC,
    .sector_size = opt_sector_size,
    .page_size = opt_page_size,
    .srom_size = opt_srom_size,
    .num_images = opt_num_images,
    .offset = sizeof (struct srom_overall_header) + vbuf_len(ex_seg),
    .booter_words = booter_bytes / 4,
    .booter_crc = booter_crc,
    .timestamp = (uint32_t) time(NULL),
    .flags = (vbuf_len(ex_seg)) ? SROM_HDR_FLG_XSEG : 0,
  };

  if (opt_comment)
    strncpy(new_hdr.comment, opt_comment, sizeof (new_hdr.comment));
  new_hdr.header_crc =
    do_crc(&new_hdr, offsetof(typeof(new_hdr), header_crc), 0);

  lseek_srom_or_die(booter_offset);
  write_srom_or_die(&new_hdr, sizeof (new_hdr));

  //
  // Finally, write any extra segments.
  //
  if (vbuf_len(ex_seg))
    write_srom_or_die(vbuf_data(ex_seg), vbuf_len(ex_seg));

  //
  // If requested, read back what we just wrote and verify it.
  //
  if (opt_verify)
  {
    int bad_verify = 0;

    flush_srom();

    read_srom_header();

    //
    // First verify the SROM header.
    //
    if (memcmp(&new_hdr, &srom_hdr, sizeof (new_hdr)))
      bad_verify = 1;

    if (!bad_verify && vbuf_len(ex_seg))
    {
      char srom_extra_segs[vbuf_len(ex_seg)];

      lseek_srom_or_die(booter_offset + sizeof (new_hdr));
      read_srom_or_die(srom_extra_segs, vbuf_len(ex_seg));
      if (memcmp(srom_extra_segs, vbuf_data(ex_seg), vbuf_len(ex_seg)))
        bad_verify = 1;
    }


















    if (!bad_verify)
    {
      //
      // Now the booter itself.  This is a bit more complex than the
      // image case since we need to skip the segment headers.
      //






      lseek_srom_or_die(booter_offset + srom_hdr.offset);
      rewind_input_or_die();

      int tot_bytes = srom_hdr.booter_words * 4;


      while (tot_bytes)
      {
        BOOT_ROM_DESC_t rsh;

        read_srom_or_die(&rsh, sizeof (rsh));
        tot_bytes -= sizeof (rsh);

        int bytesthispass = (rsh.words - 1) * 8;

        if (rsh.e)
          break;

        if (bytesthispass > tot_bytes ||
            rsh.addr != RSH_PG_DATA / 8 ||
            rsh.chn != 0)
        {
          bad_verify = 1;
          break;
        }

        char in_buf[bytesthispass];
        char srom_buf[bytesthispass];

        read_all_input_or_die(in_buf, bytesthispass);
        read_srom_or_die(srom_buf, bytesthispass);

        if (memcmp(in_buf, srom_buf, bytesthispass))
        {
          bad_verify = 1;
          break;
        }
        tot_bytes -= bytesthispass;
      }

      //
      // Now the booter's trailer.
      //
      if (is_prebooter || opt_num_images == 0)
      {
        uint_reg_t dummy;
        read_srom_or_die(&dummy, sizeof (dummy));
        tot_bytes -= sizeof (dummy);

        if (dummy != 0 || tot_bytes != 0)
          bad_verify = 1;
      }
      else
      {
        char sbt_srom[sizeof (sbt)];
        read_srom_or_die(sbt_srom, sizeof (sbt));
        tot_bytes -= sizeof (sbt);

        if (memcmp(sbt_srom, sbt, sizeof (sbt)) || tot_bytes != 0)
          bad_verify = 1;
      }
    }

    if (bad_verify)
    {
      printf("Verification failed!\n");
      exit(1);
    }
    else if (opt_verbose)
      printf("Verification successful.\n");
  }
}

/** Extract the booter from the SROM.
 */
static void
action_extract_booter()
{
  //
  // Open the SROM; make sure the booter is valid; open the output file.
  //
  open_srom_or_die(OPEN_SROM_READ);

  if (!srom_valid)
  {
    fprintf(stderr, SBIM_NAME ": SROM booter not valid\n");
    exit(1);
  }

  open_output_or_die();

  //
  // Copy the file to the output.  We need to obey the segment headers;
  // also, we don't want to extract the trailer, so we have to pay attention
  // to the last-segment flag.
  //
  lseek_srom_or_die(booter_offset + srom_hdr.offset);

  int tot_bytes = srom_hdr.booter_words * sizeof (uint_reg_t);

  while (tot_bytes)
  {
    BOOT_ROM_DESC_t rsh;

    read_srom_or_die(&rsh, sizeof (rsh));
    tot_bytes -= sizeof (rsh);

    int bytesthispass = (rsh.words - 1) * 8;

    //
    // We normally expect to exit here via the last_seg check; we only check
    // bytesthispass to defend against a corrupt ROM.
    //
    if (bytesthispass > tot_bytes || rsh.e)
      break;

    char buf[bytesthispass];

    read_srom_or_die(buf, bytesthispass);
    write_output_or_die(buf, bytesthispass);

    tot_bytes -= bytesthispass;
  }
}

/** Invalidate an image.
 */
static void
action_invalidate()
{
  //
  // Open the SROM; read the image headers; pick the slot which we'll
  // invalidate.
  //
  open_srom_or_die(OPEN_SROM_WRITE);

  ensure_nimages_nonzero("invalidate");

  read_image_headers();

  int target = pick_target(opt_id, NULL, NULL);

  //
  // If this is the last valid image, require confirmation.
  //
  int valid_images = 0;
  for (int i = 0; i < opt_num_images; i++)
    if (image_valid[i])
      valid_images++;

  if (image_valid[target] && (image_hdr[target].flags & SROM_IMAGE_FLG_LOCK))
  {
    fprintf(stderr, SBIM_NAME ": cannot invalidate image %s since it is "
            "locked\n", opt_id);
    exit(1);
  }

  if (valid_images == 1 && image_valid[target] && !opt_yes)
  {
    printf("You are about to overwrite the last valid image, which will\n"
           "render this system no longer bootable from the SROM.  Are you\n"
           "sure you want to do this (yes/no)? ");

    confirm();
  }

  //
  // Do the invalidation.  We use zeroes here to avoid a sector erase.
  //
  struct srom_image_header inv_hdr;
  memset(&inv_hdr, 0, sizeof (inv_hdr));

  lseek_srom_or_die(image_offsets[target]);
  write_srom_or_die(&inv_hdr, sizeof (inv_hdr));
}

/** Invalidate the booter.
 */
static void
action_invalidate_booter()
{
  //
  // Open the SROM; make sure the booter isn't already invalid; get
  // confirmation.
  //
  open_srom_or_die(OPEN_SROM_WRITE | OPEN_SROM_NO_HDR_OK |
                   OPEN_SROM_INVALIDATE_HDR);

  if (!srom_valid)
    return;

  if (!opt_yes)
  {
    printf("You are about to invalidate the SROM booter, which will cause\n"
           "this system to be no longer bootable from the SROM.  Are you\n"
           "sure you want to do this (yes/no)? ");

    confirm();
  }

  //
  // Do the invalidation.  We use zeroes here to avoid a sector erase.
  //
  struct srom_overall_header inv_hdr;
  memset(&inv_hdr, 0, sizeof (inv_hdr));

  lseek_srom_or_die(booter_offset);
  write_srom_or_die(&inv_hdr, sizeof (inv_hdr));
}

/** Promote an image (make it the most recent).
 */
static void
action_promote()
{
  //
  // Open the SROM; read the image headers; pick the slot which we'll promote;
  // make sure it's valid.
  //
  open_srom_or_die(OPEN_SROM_WRITE);

  ensure_nimages_nonzero("promote");

  read_image_headers();

  int hi_gen;
  int target = pick_target(opt_id, &hi_gen, NULL);

  if (!image_valid[target])
  {
    fprintf(stderr, SBIM_NAME ": cannot promote image %s since it is "
            "not valid\n", opt_id);
    exit(1);
  }

  //
  // Update the generation in the header and write it out.
  //
  image_hdr[target].generation = hi_gen + 1;

  image_hdr[target].header_crc =
    do_crc(&image_hdr[target],
           offsetof(typeof(image_hdr[target]), header_crc), 0);
  image_hdr[target].header_crc_ext1 =
    do_crc(&image_hdr[target],
           offsetof(typeof(image_hdr[target]), header_crc_ext1), 0);

  //
  // The old image header might have been smaller than the current size of
  // an image header, so make sure we don't overwrite the start of the
  // image.
  //
  size_t hdr_size = sizeof (image_hdr[target]);
  if (image_hdr[target].offset > 0 && hdr_size > image_hdr[target].offset)
    hdr_size = image_hdr[target].offset;

  lseek_srom_or_die(image_offsets[target]);
  write_srom_or_die(&image_hdr[target], hdr_size);
}

/** Lock or unlock an image (set/clear its lock bit).
 * @param lock If nonzero, lock; otherwise, unlock.
 */
static void
action_lock_unlock(int lock)
{
  //
  // Open the SROM; read the image headers; pick the slot which we'll lock or
  // unlock; make sure it's valid.
  //
  open_srom_or_die(OPEN_SROM_WRITE);

  char* cmd = lock ? "lock" : "unlock";

  ensure_nimages_nonzero(cmd);

  read_image_headers();

  int target = pick_target(opt_id, NULL, NULL);

  if (!image_valid[target])
  {
    fprintf(stderr, SBIM_NAME ": cannot %s image %s since it is "
            "not valid\n", cmd, opt_id);
    exit(1);
  }

  //
  // Update the lock flag in the header and write it out.
  //
  if (lock)
    image_hdr[target].flags |= SROM_IMAGE_FLG_LOCK;
  else
    image_hdr[target].flags &= ~SROM_IMAGE_FLG_LOCK;

  image_hdr[target].header_crc =
    do_crc(&image_hdr[target],
           offsetof(typeof(image_hdr[target]), header_crc), 0);
  image_hdr[target].header_crc_ext1 =
    do_crc(&image_hdr[target],
           offsetof(typeof(image_hdr[target]), header_crc_ext1), 0);

  //
  // The old image header might have been smaller than the current size of
  // an image header, so make sure we don't overwrite the start of the
  // image.
  //
  size_t hdr_size = sizeof (image_hdr[target]);
  if (image_hdr[target].offset > 0 && hdr_size > image_hdr[target].offset)
    hdr_size = image_hdr[target].offset;

  lseek_srom_or_die(image_offsets[target]);
  write_srom_or_die(&image_hdr[target], hdr_size);
}

/** Print information about the current contents of the srom; optionally
 *  verify the checksums of the booter and valid images.  If we're verifying,
 *  we exit with a nonzero status if the booter or any valid images have a
 *  bad CRC, if we don't have a booter, or if we don't have at least one
 *  valid image.
 */
static void
action_info()
{
  int bad_verify = 0;
  int valid_images = 0;

  //
  // Open the SROM.  It's OK if there's no header, or if we don't know the
  // chip parameters, since we won't be writing anything.
  //
  open_srom_or_die(OPEN_SROM_READ | OPEN_SROM_NO_HDR_OK |
                   OPEN_SROM_NO_PARAM_OK);
  //
  // Dump out information from the SROM header.
  //
  if (srom_valid)
  {
    printf("SROM booter: Present, %d bytes.\n", srom_hdr.booter_words * 4);
    time_t timestamp = srom_hdr.timestamp;
    printf("   Time installed: %s", ctime(&timestamp));
    if (srom_hdr.comment[0])
      printf("   Comment: %.80s\n", srom_hdr.comment);

    //
    // If requested, verify the CRC for the SROM booter.
    //
    if (opt_verify)
    {
      lseek_srom_or_die(booter_offset + srom_hdr.offset);

      int tot_bytes = srom_hdr.booter_words * 4;
      uint32_t crc = 0;

      while (tot_bytes)
      {
        char buf[64 * 1024];

        int bytesthispass = tot_bytes;
        if (bytesthispass > sizeof(buf))
          bytesthispass = sizeof(buf);

        read_srom_or_die(buf, bytesthispass);

        crc = do_crc(buf, bytesthispass, crc);

        tot_bytes -= bytesthispass;
      }

      if (crc == srom_hdr.booter_crc)
        printf("   Booter CRC is good.\n");
      else
      {
        printf("   Booter CRC is BAD!\n");
        bad_verify = 1;
      }
    }

    //
    // If requested, verify the CRC for any extra SROM segments.
    //
    if (opt_verify && (srom_hdr.flags & SROM_HDR_FLG_XSEG))
    {
      lseek_srom_or_die(booter_offset + sizeof (srom_hdr));

      int tot_bytes = srom_hdr.offset - sizeof (srom_hdr);

      char buf[tot_bytes];

      read_srom_or_die(buf, tot_bytes);

      uint32_t crc = do_crc(buf, tot_bytes - sizeof (crc), 0);

      if (!memcmp(&crc, &buf[tot_bytes - sizeof (crc)], sizeof (crc)))
        printf("   Extra configuration CRC is good.\n");
      else
      {
        printf("   Extra configuration CRC is BAD!\n");
        bad_verify = 1;
      }
    }
  }
  else
  {
    printf("SROM booter: Not present.\n");
    if (opt_verify)
    {
      printf("Verification failed.\n");
      exit(1);
    }
    return;
  }

  //
  // Read the image headers.
  //
  read_image_headers();

  //
  // Dump out information from the image headers.
  //
  for (int i = 0; i < opt_num_images; i++)
  {
    struct srom_image_header* ih = &image_hdr[i];

    printf("Image in slot %d:", i);

    if (image_valid[i])
    {
      valid_images++;

      printf(" Present, ");
      if (ih->flags & SROM_IMAGE_FLG_LOCK)
        printf("locked, ");
      printf("generation %d, %zd bytes.\n", ih->generation,
             ih->total_words * sizeof (uint_reg_t));
      time_t timestamp = ih->timestamp;
      printf("   Time installed: %s", ctime(&timestamp));
      if (ih->comment[0])
        printf("   Comment: %.80s\n", ih->comment);

      //
      // If requested, verify the CRC and the SHA-1 digest on the image.
      //
      if (opt_verify)
      {
        lseek_srom_or_die(image_offsets[i] + ih->offset);

        int tot_bytes = ih->total_words * sizeof (uint_reg_t);
        uint32_t crc = 0;
        SHA1Context sha1_ctx;
        SHA1Reset(&sha1_ctx);
        int do_sha1 = (ih->flags & SROM_IMAGE_FLG_EXT1) != 0;

        while (tot_bytes)
        {
          char buf[64 * 1024];

          int bytesthispass = tot_bytes;
          if (bytesthispass > sizeof(buf))
            bytesthispass = sizeof(buf);

          read_srom_or_die(buf, bytesthispass);

          crc = do_crc(buf, bytesthispass, crc);
          if (do_sha1)
            SHA1Input(&sha1_ctx, (const uint8_t*) buf, bytesthispass);

          tot_bytes -= bytesthispass;
        }

        if (crc == ih->total_crc)
          printf("   Image CRC is good.\n");
        else
        {
          printf("   Image CRC is BAD!\n");
          bad_verify = 1;
        }

        if (do_sha1)
        {
          uint8_t sha1_digest[SHA1HashSize];
          SHA1Result(&sha1_ctx, sha1_digest);

          if (memcmp(sha1_digest, ih->sha1_digest, sizeof (sha1_digest)) == 0)
            printf("   Image SHA-1 digest is good.\n");
          else
          {
            printf("   Image SHA-1 digest is BAD!\n");
            bad_verify = 1;
          }
        }
      }
    }
    else
      printf(" Not present.\n");
  }

  //
  // If we're verifying, we want to see at least one bootable image, unless
  // the header says there are no image slots.
  //
  if (opt_verify && valid_images == 0 && srom_hdr.num_images > 0)
  {
    printf("No valid images!\n");
    bad_verify = 1;
  }

  if (bad_verify)
  {
    printf("Verification failed.\n");
    exit(1);
  }
}

/** Dump out data about the SROM.  The output format is intended to be both
 *  complete and easily parseable by scripts; changing it is inadvisable.
 */
static void
action_dump()
{
  //
  // Open the SROM, dump out header data.
  //
  open_srom_or_die(OPEN_SROM_READ | OPEN_SROM_NO_HDR_OK);

  printf("srom_valid: %d\n", srom_valid);
  if (srom_valid || opt_verbose)
  {
    printf("srom_RomHeader: %#llx\n",  srom_hdr.rom_header.word);
    printf("srom_RomSegmentHeader: %#llx\n",  srom_hdr.rom_seg_header.word);
    if (opt_verbose)
      printf("srom_magic: %#x\n",  srom_hdr.magic);
    printf("srom_sector_size: %d\n", srom_hdr.sector_size);
    printf("srom_page_size: %d\n", srom_hdr.page_size);
    printf("srom_srom_size: %d\n", srom_hdr.srom_size);
    printf("srom_num_images: %d\n", srom_hdr.num_images);
    printf("srom_offset: %d\n", srom_hdr.offset);
    printf("srom_booter_words: %d\n", srom_hdr.booter_words);
    printf("srom_booter_crc: %#x\n", srom_hdr.booter_crc);
    if (srom_hdr.comment[0])
      printf("srom_comment: %.80s\n", srom_hdr.comment);
    if (srom_hdr.flags)
      printf("srom_flags: %#x\n", srom_hdr.flags);
    time_t timestamp = srom_hdr.timestamp;
    printf("srom_timestamp: %s", ctime(&timestamp));
    printf("srom_header_crc: %#x\n", srom_hdr.header_crc);
    printf("max_image_size: %zd\n", image_max_length -
           sizeof (struct srom_image_header));
  }
  else
  {
    return;
  }

  //
  // Read the image headers and dump out image data.
  //
  read_image_headers();

  for (int i = 0; i < opt_num_images; i++)
  {
    printf("image%d_valid: %d\n", i, image_valid[i]);
    if (!image_valid[i] && !opt_verbose)
      continue;

    struct srom_image_header* ih = &image_hdr[i];

    if (opt_verbose)
      printf("image%d_magic: %#x\n", i, ih->magic);
    printf("image%d_generation: %d\n", i, ih->generation);
    printf("image%d_offset: %d\n", i, ih->offset);
    printf("image%d_l0_boot_words: %d\n", i, ih->l0_boot_words);
    printf("image%d_l0_boot_crc: %#x\n", i, ih->l0_boot_crc);
    printf("image%d_total_words: %d\n", i, ih->total_words);
    printf("image%d_total_crc: %#x\n", i, ih->total_crc);
    if (ih->comment[0])
      printf("image%d_comment: %.80s\n", i, ih->comment);
    if (ih->flags)
      printf("image%d_flags: %#x\n", i, ih->flags);

    time_t timestamp = ih->timestamp;
    printf("image%d_timestamp: %s", i, ctime(&timestamp));
    printf("image%d_header_crc: %#x\n", i, ih->header_crc);

    if (ih->flags & SROM_IMAGE_FLG_EXT1)
    {
      printf("image%d_sha1_digest: ", i);
      for (int j = 0; j < sizeof (ih->sha1_digest); j++)
        printf("%02x", ih->sha1_digest[j]);
      printf("\n");
      printf("image%d_header_crc_ext1: %#x\n", i, ih->header_crc_ext1);
    }
  }
}

/** Damage an image within the SROM.  This function is intended for internal
 *  testing use and is intentionally undocumented.
 */
static void
action_damage()
{
  //
  // Open the SROM; read the image headers; pick the slot which we'll damage.
  //
  open_srom_or_die(OPEN_SROM_WRITE);

  ensure_nimages_nonzero("damage");

  read_image_headers();

  int target = pick_target((opt_id) ? opt_id : "0", NULL, NULL);

  if (!image_valid[target])
  {
    fprintf(stderr, SBIM_NAME ": image at location %s not valid\n", opt_id);
    exit(1);
  }

  if (!opt_yes)
  {
    printf("Are you sure you want to irreversibly damage this boot image "
           "(yes/no)? ");
    confirm();
  }

  //
  // Damage the image header.
  //
  if (!strcmp(opt_damage, "hdr"))
  {
    lseek_srom_or_die(image_offsets[target]);
    damage_srom(offsetof(typeof(image_hdr[target]), header_crc));
    return;
  }

  //
  // Damage the L0.5 booter.
  //
  if (!strcmp(opt_damage, "l0.5"))
  {
    lseek_srom_or_die(image_offsets[target]);
    damage_srom(image_hdr[target].offset);
    return;
  }

  //
  // Damage the L1 booter.
  //
  if (!strcmp(opt_damage, "l1"))
  {
    lseek_srom_or_die(image_offsets[target] + image_hdr[target].offset);
    damage_srom(image_hdr[target].l0_boot_words * sizeof (uint_reg_t) + 8);
    damage_srom(0);
    return;
  }

  //
  // At this point we've reached the end of the things we can easily damage
  // based on data in the header; for the rest of the items we need to
  // interpret the image to find the right spot.
  //

  //
  // Seek to the start of the L1 booter.
  //
  lseek_srom_or_die(image_offsets[target] + image_hdr[target].offset +
                    image_hdr[target].l0_boot_words * sizeof (uint_reg_t));

  //
  // Read and discard the L1 booter.
  //
  uint_reg_t seg_len_addr_crc[3];
  while (1)
  {
    read_srom_or_die(&seg_len_addr_crc, sizeof (seg_len_addr_crc));
    if (seg_len_addr_crc[0] == 0)
      break;
    lseek_srom_or_die(get_srom_pos_or_die() + seg_len_addr_crc[0] * sizeof
                      (uint_reg_t));
  }

  //
  // We're now positioned at the start of the booter parameters.
  //
  if (!strcmp(opt_damage, "l1p"))
  {
    damage_srom(0);
    return;
  }

  //
  // Seek past the booter parameters.
  //
  uint_reg_t feedaddr;
  read_srom_or_die(&feedaddr, sizeof (feedaddr));
  lseek_srom_or_die(get_srom_pos_or_die() +
                    (4 + ((feedaddr >> 1) & 0x3F)) * sizeof (uint_reg_t));

  //
  // We're now positioned at the start of the hypervisor.
  //
  if (!strcmp(opt_damage, "hv"))
  {
    damage_srom(0);
    return;
  }

  //
  // Read and discard the hypervisor.
  //
  while (1)
  {
    read_srom_or_die(&seg_len_addr_crc, sizeof (seg_len_addr_crc));
    if (seg_len_addr_crc[0] == 0)
      break;
    lseek_srom_or_die(get_srom_pos_or_die() + seg_len_addr_crc[0] * sizeof
                      (uint_reg_t));
  }

  //
  // We're now positioned at the start of the hypervisor file system.
  //
  if (!strcmp(opt_damage, "hvfshdr"))
  {
    damage_srom(4);
    return;
  }
  else if (!strcmp(opt_damage, "hvfs"))
  {
    damage_srom(20);
    return;
  }

  fprintf(stderr, SBIM_NAME ": damage type %s not recognized; valid types "
          "are\n  hdr, l0.5, l1, l1p, hv, hvfshdr, hvfs\n", opt_damage);
  exit(1);
}

/** Erase the SROM and verify that it has been erased. */
static void
action_sanitize()
{
  if (!opt_yes)
  {
    printf("You are about to erase all data from the SROM.  This will "
             "prevent the\n"
           "system from being booted in standalone mode, although you "
             "should still\n"
           "be able to boot the system under control of a host system "
             "(i.e., over USB\n"
           "or PCIe).\n"
           "\n"
           "Note that this operation does NOT erase any data on disk drives, "
             "SSD\n"
           "drives, CFast cards and the like connected to or resident within "
             "this\n"
           "system.\n"
           "\n"
           "Are you sure you want to do this (yes/no)? ");

    confirm();
  }

  //
  // Create a data buffer filled with the sanitization pattern.
  //
  uint64_t pattern = ~0ULL;
  if (opt_pattern)
    pattern = strtoull(opt_pattern, NULL, 16);

  uint64_t dbuf[1024];
  for (int i = 0; i < sizeof (dbuf) / sizeof (*dbuf); i++)
    dbuf[i] = pattern;

  const char* const srom_files[] =
  {
    "/dev/srom/bootimage",
    "/dev/srom/bootparam",
    "/dev/srom/userparam",
  };

  off_t sizes[sizeof (srom_files) / sizeof (*srom_files)];

  //
  // Erase all of the SROM files.
  //
  for (int i = 0; i < sizeof (srom_files) / sizeof (*srom_files); i++)
  {
    //
    // Open.
    //
    int fd = open(srom_files[i], O_RDWR);
    if (fd < 0)
    {
      fprintf(stderr, SBIM_NAME ": can't open srom file %s: %s\n",
              srom_files[i], strerror(errno));
      exit(1);
    }

    //
    // Get size.
    //
    sizes[i] = lseek(fd, 0, SEEK_END);
    if (sizes[i] == (off_t) -1)
    {
      fprintf(stderr, SBIM_NAME ": cannot get size of SROM file %s: %s\n",
              srom_files[i], strerror(errno));
      exit(1);
    }

    //
    // Rewind.
    //
    if (lseek(fd, 0, SEEK_SET) == (off_t) -1)
    {
      fprintf(stderr, SBIM_NAME ": cannot rewind SROM file %s: %s\n",
              srom_files[i], strerror(errno));
      exit(1);
    }

    printf("Erasing %s, %lld bytes.\n", srom_files[i], (long long)sizes[i]);

    //
    // Write.
    //
    off_t bytes_remaining = sizes[i];
    int bufnum = 0;
    int totbufs = sizes[i] / sizeof (dbuf);

    while (bytes_remaining)
    {
      off_t len = bytes_remaining;
      if (len > sizeof (dbuf))
        len = sizeof (dbuf);

      if (write(fd, dbuf, len) != len)
      {
        fprintf(stderr, SBIM_NAME ": cannot write SROM file %s: %s\n",
                srom_files[i], strerror(errno));
        exit(1);
      }

      bytes_remaining -= len;
      bufnum++;

      printf("\r[buffer %d of %d]", bufnum, totbufs);
      fflush(stdout);
    }

    printf("\n");

    //
    // Close file to ensure data flushed to SROM.
    //
    if (close(fd))
    {
      fprintf(stderr, SBIM_NAME ": cannot close SROM file %s: %s\n",
              srom_files[i], strerror(errno));
      exit(1);
    }
  }

  //
  // Verify all of the SROM files.
  //
  for (int i = 0; i < sizeof (srom_files) / sizeof (*srom_files); i++)
  {
    //
    // Open.
    //
    int fd = open(srom_files[i], O_RDONLY);
    if (fd < 0)
    {
      fprintf(stderr, SBIM_NAME ": can't open srom file %s: %s\n",
              srom_files[i], strerror(errno));
      exit(1);
    }

    printf("Verifying %s, %lld bytes.\n", srom_files[i], (long long)sizes[i]);

    //
    // Read.
    //
    off_t bytes_remaining = sizes[i];
    int bufnum = 0;
    int totbufs = sizes[i] / sizeof (dbuf);

    uint64_t rbuf[sizeof (dbuf) / sizeof (uint64_t)];

    while (bytes_remaining)
    {
      off_t len = bytes_remaining;
      if (len > sizeof (rbuf))
        len = sizeof (rbuf);

      if (read(fd, rbuf, len) != len)
      {
        fprintf(stderr, SBIM_NAME ": cannot read SROM file %s: %s\n",
                srom_files[i], (errno) ? strerror(errno) : "File too short");
        exit(1);
      }

      if (memcmp(dbuf, rbuf, len))
      {
        fprintf(stderr, SBIM_NAME ": verification error on SROM file %s: %s\n",
                srom_files[i], strerror(errno));
        exit(1);
      }

      bytes_remaining -= len;
      bufnum++;

      printf("\r[buffer %d of %d]", bufnum, totbufs);
      fflush(stdout);
    }

    printf("\n");

    //
    // Close file.
    //
    if (close(fd))
    {
      fprintf(stderr, SBIM_NAME ": cannot close SROM file %s: %s\n",
              srom_files[i], strerror(errno));
      exit(1);
    }
  }

  printf("SROM sanitized successfully.\n");
}

/** Main program.
 */
int
main(int argc, char** argv)
{
  int opt;

  while ((opt = getopt_long(argc, argv, options, long_options, NULL)) > 0)
  {
    //
    // Ensure we only allow one of the exclusive options (the action we'll
    // perform).
    //
    if (strchr("ineINEdpLUDPS", opt))
    {
      if (opt_action)
      {
        fprintf(stderr, SBIM_NAME ": more than one action specified\n");
        usage();
      }
      else
        opt_action = opt;
    }

    //
    // Process individual options and their arguments.
    //
    switch (opt)
    {
    case 'o':   // --info
      break;

    case 'i':   // --install
      opt_input_file = optarg;
      break;

    case 'e':   // --extract
      opt_output_file = optarg;
      break;

    case 'I':   // --install-booter
      opt_input_file = optarg;
      break;

    case 'P':   // --install-prebooter
      opt_input_file = optarg;
      break;

    case 'E':   // --extract-booter
      opt_output_file = optarg;
      break;

    case 'n':   // --invalidate
      opt_id = optarg;
      break;

    case 'N':   // --invalidate-booter
      break;

    case 'p':   // --promote
      opt_id = optarg;
      break;

    case 'L':   // --lock
      opt_id = optarg;
      break;

    case 'U':   // --unlock
      opt_id = optarg;
      break;

    case 'v':   // --verify
      opt_verify = 1;
      break;

    case 'd':   // --dump
      break;

    case 'f':   // --file
      opt_file = optarg;
      break;

    case 'l':   // --location
      opt_id = optarg;
      break;

    case 'c':   // --comment
      opt_comment = optarg;
      break;

    case 's':   // --sector-size
      opt_sector_size = strtol_with_suffix(optarg);
      break;

    case 'g':   // --page-size
      opt_page_size = strtol_with_suffix(optarg);
      break;

    case 'z':   // --srom-size
      opt_srom_size = strtol_with_suffix(optarg);
      break;

    case 'Z':   // --sectors
      opt_sectors = strtol_with_suffix(optarg);
      break;

    case 'm':   // --num-images
      opt_num_images = strtol_with_suffix(optarg);
      if (opt_num_images < 0 || opt_num_images > MAX_IMAGES)
      {
        fprintf(stderr, SBIM_NAME ": --num-images must be between 0 and %d\n",
                MAX_IMAGES);
        exit(1);
      }
      break;

    case 'y':   // --yes
      opt_yes = 1;
      break;

    case 'V':   // --verbose
      opt_verbose++;
      break;

    case 'D':   // --damage
      opt_damage = optarg;
      break;

    case 'S':   // --sanitize
      break;

    case 'a':   // --pattern
      opt_pattern = optarg;
      break;

    case 'O':   // --oldest
      opt_oldest = 1;
      break;

    default:
      usage();
      break;
    }
  }

  //
  // Do a couple option consistency checks.
  //
  if (opt_srom_size && opt_sectors)
  {
    fprintf(stderr, SBIM_NAME ": can't specify both --srom-size "
            "and --sectors\n");
    usage();
  }

  if (opt_id && opt_action && !strchr("inepULD", opt_action))
  {
    //
    // opt_id will be set for --invalidate, --promote, --lock, and --unlock,
    // too, but since it's not a separate option in that case we don't call
    // it out in the error message.
    //
    fprintf(stderr, SBIM_NAME ": --location only valid with --install and "
            "--extract\n");
    usage();
  }

  if (opt_pattern && opt_action != 'S')
  {
    fprintf(stderr, SBIM_NAME ": --pattern only valid with --sanitize\n");
    usage();
  }

  if (opt_file && opt_action == 'S')
  {
    fprintf(stderr, SBIM_NAME ": --file not valid with --sanitize\n");
    usage();
  }

  if (optind < argc)
  {
    fprintf(stderr, SBIM_NAME ": extra arguments on command line\n");
    usage();
  }

  if (opt_oldest && opt_action != 'i')
  {
    fprintf(stderr, SBIM_NAME ": --oldest only valid with --install\n");
    usage();
  }

  //
  // Now process the requested action.
  //

  switch (opt_action)
  {
    default:
    case 'o':
      action_info();
      break;

    case 'i':
      action_install();
      break;

    case 'e':
      action_extract();
      break;

    case 'n':
      action_invalidate();
      break;

    case 'p':
      action_promote();
      break;

    case 'L':
      action_lock_unlock(1);
      break;

    case 'U':
      action_lock_unlock(0);
      break;

    case 'd':
      action_dump();
      break;

    case 'I':
      action_install_booter(0);
      break;

    case 'P':
      action_install_booter(1);
      break;

    case 'E':
      action_extract_booter();
      break;

    case 'N':
      action_invalidate_booter();
      break;

    case 'D':
      action_damage();
      break;

    case 'S':
      action_sanitize();
      break;
  }

  exit(0);
}
