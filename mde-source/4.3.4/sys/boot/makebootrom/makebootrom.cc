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
 * Converts an ELF executable into one of the formats understood by the
 * various booters.
 */

#include <fstream>
#include <iostream>
#include <errno.h>

#include <elf.h>
#include <arch/chip.h>

#include <set>
#include <vector>
#include <string>
#include <bitset>

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#if TILE_CHIP >= 10
#define TILEGX
#endif

#ifdef TILEGX
#include "../../hv/tilegx/boot_params.h"
#endif

//
// There are actually three different boot stream formats which can be
// produced by tile-mkrom.
//
// 1. The level-0 boot stream is understood by the level-0 booter (which is
//    built into the hardware); it is used to encode the level 0.5 booter,
//    and is the default format produced by tile-mkrom.  This consists
//    of one or more segments.  Each segment consists of:
//
//    - a 1-word length, specifying the number of words of data in this
//      segment;
//
//    - a 1-word address, specifying where the data will be loaded;
//
//    - 0 or more words of data;
//
//    - a 1-word jump-to address, to which we transfer execution once
//      the data has been loaded.
//
//    If more than one segment must be loaded by the level-0 booter, the
//    jump-to address on all but the last segment should be set to 48
//    (0x30) on Pro, or 0 on Gx; this is the address of the start of the
//    "process next segment" part of the level-0 booter, so jumping to it
//    will read in another segment.
//
//    If the -C flag is on, we add a CRC to the L0 boot stream by adding
//    an extra segment to the stream.  This segment appears first in the
//    stream, and has the same load address as the second segment, with a
//    length of one.  The one word of data is the CRC of the stream,
//    calculated over all words following the extra segment.
//
// 2. The hypervisor boot stream is understood by the level-0.5 booter,
//    and the level-1 booter; it is used to encode the level 1 booter
//    and the hypervisor, and is produced by tile-mkrom when the -H flag
//    is specified.  This consists of one or more segments.  Each segment
//    consists of:
//
//    - a 1-word length, specifying the number of words of data in this
//      segment, or zero if this is the last segment;
//
//    - a 1-word address, specifying where the data will be loaded (if
//      the length is nonzero), or the address to jump to (if the length
//      is zero);
//
//    - a 1-word cumulative CRC; and
//
//    - 0 or more words of data.
//
//    Each segment but the last segment contains a nonzero length;
//    the last one, with a zero length, contains the jump-to address,
//    to which we transfer execution when all segments are loaded.
//
//    The cumulative CRC covers all words from the start of the
//    hypervisor boot stream, up to the actual CRC word itself, but
//    omitting any previous CRC words.  So, the first CRC word covers
//    just the first segment's address and length; the second one covers
//    the first segment's address, length, and data, and the second
//    segment's address and length; etc.  This arrangement allows
//    each address and length to be validated before they are used
//    to read in a segment's data.  The CRC used is the CCITT-32 CRC,
//    as implemented by the Tile Architecture's crc32_32 instruction.
//    Note that a bootrom file may contain more than one hypervisor boot
//    stream (say, one for the L1 booter and one for the hypervisor);
//    the CRC is restarted for each stream.
//
// 3. The secondary boot stream is understood by various bits of DV code;
//    it is produced by tile-mkrom when the -S flag is specified.  This
//    consists of one or more segments.  Each segment consists of:
//
//    - a 1-word address, specifying where the data will be loaded (if
//      the length is nonzero), or the address to jump to (if the length
//      is zero);
//
//    - a 1-word length, specifying the number of words of data in this
//      segment, or zero if this is the last segment; and
//
//    - 0 or more words of data.
//
//    Each segment but the last segment contains a nonzero length;
//    the last one, with a zero length, contains the jump-to address,
//    to which we transfer execution when all segments are loaded.
//


// The hardware reserves the first 64 bundles for the built-in level-0
// booter, so we can't load any level-1 boot code there.
static const unsigned int L0_BOOT_BUNDLES = 64;
static const unsigned int L0_BOOT_SIZE = 8 * L0_BOOT_BUNDLES;

/** Size of 1 way of the L2 cache */
static const unsigned int L2_WAY_SIZE = CHIP_L2_CACHE_SIZE() / CHIP_L2_ASSOC();

/** Size of an L2 cache line */
static const unsigned int L2_CL_SIZE = CHIP_L2_LINE_SIZE();

/** Size of an index into an L2 way */
static const unsigned int L2_INDEX_SIZE = L2_WAY_SIZE / L2_CL_SIZE;

#if CHIP_WORD_SIZE() > 32
#  define ElfW(x)  Elf64_ ## x
#  define ELFW(x)  ELF64_ ## x
typedef uint64_t word;
# else
#  define ElfW(x)  Elf32_ ## x
#  define ELFW(x)  ELF32_ ## x
typedef uint32_t word;
#endif

/** Address of the "hw_l0boot_next_pkt" label in the L0 booter.  We write
 *  this out so the previous loop iteration will jump there to copy more
 *  data. */
#if CHIP_WORD_SIZE() > 32
// FIXME: GX: this is actually a bit kludgy; we probably ought to have a
// CHIP_L0BOOT_RESTART() value or something.
const word hw_l0boot_next_pkt = 0;
#else
const word hw_l0boot_next_pkt = 48;
#endif

using namespace std;

/** Compute a CRC-32 digest of a string.
 * @param ptr Pointer to bytes to digest.
 * @param len Number of bytes to digest.
 * @param old_crc Initial CRC value.
 * @return New CRC value.
 */
static word
crc(const void* ptr, size_t len, word old_crc)
{
  //
  // Precomputed CRC-32 table.  This holds the 32-bit CRC32 of each possible
  // 8-bit byte, which allows us to compute 8 CRC bits with a table lookup
  // and a couple of XORs.  The table can be reproduced with the following
  // bit of Python:
  //
  // for input in range(256):
  //   crc = 0
  //   for i in range(8):
  //     if ((input >> i) ^ crc) & 1:
  //       crc = (crc >> 1) ^ 0xEDB88320
  //     else:
  //       crc = crc >> 1
  //   print "0x%08x, " % crc,
  //   if input & 0x3 == 3:
  //     print
  //
  static const uint32_t crc_table[256] = {
    0x00000000,  0x77073096,  0xee0e612c,  0x990951ba,
    0x076dc419,  0x706af48f,  0xe963a535,  0x9e6495a3,
    0x0edb8832,  0x79dcb8a4,  0xe0d5e91e,  0x97d2d988,
    0x09b64c2b,  0x7eb17cbd,  0xe7b82d07,  0x90bf1d91,
    0x1db71064,  0x6ab020f2,  0xf3b97148,  0x84be41de,
    0x1adad47d,  0x6ddde4eb,  0xf4d4b551,  0x83d385c7,
    0x136c9856,  0x646ba8c0,  0xfd62f97a,  0x8a65c9ec,
    0x14015c4f,  0x63066cd9,  0xfa0f3d63,  0x8d080df5,
    0x3b6e20c8,  0x4c69105e,  0xd56041e4,  0xa2677172,
    0x3c03e4d1,  0x4b04d447,  0xd20d85fd,  0xa50ab56b,
    0x35b5a8fa,  0x42b2986c,  0xdbbbc9d6,  0xacbcf940,
    0x32d86ce3,  0x45df5c75,  0xdcd60dcf,  0xabd13d59,
    0x26d930ac,  0x51de003a,  0xc8d75180,  0xbfd06116,
    0x21b4f4b5,  0x56b3c423,  0xcfba9599,  0xb8bda50f,
    0x2802b89e,  0x5f058808,  0xc60cd9b2,  0xb10be924,
    0x2f6f7c87,  0x58684c11,  0xc1611dab,  0xb6662d3d,
    0x76dc4190,  0x01db7106,  0x98d220bc,  0xefd5102a,
    0x71b18589,  0x06b6b51f,  0x9fbfe4a5,  0xe8b8d433,
    0x7807c9a2,  0x0f00f934,  0x9609a88e,  0xe10e9818,
    0x7f6a0dbb,  0x086d3d2d,  0x91646c97,  0xe6635c01,
    0x6b6b51f4,  0x1c6c6162,  0x856530d8,  0xf262004e,
    0x6c0695ed,  0x1b01a57b,  0x8208f4c1,  0xf50fc457,
    0x65b0d9c6,  0x12b7e950,  0x8bbeb8ea,  0xfcb9887c,
    0x62dd1ddf,  0x15da2d49,  0x8cd37cf3,  0xfbd44c65,
    0x4db26158,  0x3ab551ce,  0xa3bc0074,  0xd4bb30e2,
    0x4adfa541,  0x3dd895d7,  0xa4d1c46d,  0xd3d6f4fb,
    0x4369e96a,  0x346ed9fc,  0xad678846,  0xda60b8d0,
    0x44042d73,  0x33031de5,  0xaa0a4c5f,  0xdd0d7cc9,
    0x5005713c,  0x270241aa,  0xbe0b1010,  0xc90c2086,
    0x5768b525,  0x206f85b3,  0xb966d409,  0xce61e49f,
    0x5edef90e,  0x29d9c998,  0xb0d09822,  0xc7d7a8b4,
    0x59b33d17,  0x2eb40d81,  0xb7bd5c3b,  0xc0ba6cad,
    0xedb88320,  0x9abfb3b6,  0x03b6e20c,  0x74b1d29a,
    0xead54739,  0x9dd277af,  0x04db2615,  0x73dc1683,
    0xe3630b12,  0x94643b84,  0x0d6d6a3e,  0x7a6a5aa8,
    0xe40ecf0b,  0x9309ff9d,  0x0a00ae27,  0x7d079eb1,
    0xf00f9344,  0x8708a3d2,  0x1e01f268,  0x6906c2fe,
    0xf762575d,  0x806567cb,  0x196c3671,  0x6e6b06e7,
    0xfed41b76,  0x89d32be0,  0x10da7a5a,  0x67dd4acc,
    0xf9b9df6f,  0x8ebeeff9,  0x17b7be43,  0x60b08ed5,
    0xd6d6a3e8,  0xa1d1937e,  0x38d8c2c4,  0x4fdff252,
    0xd1bb67f1,  0xa6bc5767,  0x3fb506dd,  0x48b2364b,
    0xd80d2bda,  0xaf0a1b4c,  0x36034af6,  0x41047a60,
    0xdf60efc3,  0xa867df55,  0x316e8eef,  0x4669be79,
    0xcb61b38c,  0xbc66831a,  0x256fd2a0,  0x5268e236,
    0xcc0c7795,  0xbb0b4703,  0x220216b9,  0x5505262f,
    0xc5ba3bbe,  0xb2bd0b28,  0x2bb45a92,  0x5cb36a04,
    0xc2d7ffa7,  0xb5d0cf31,  0x2cd99e8b,  0x5bdeae1d,
    0x9b64c2b0,  0xec63f226,  0x756aa39c,  0x026d930a,
    0x9c0906a9,  0xeb0e363f,  0x72076785,  0x05005713,
    0x95bf4a82,  0xe2b87a14,  0x7bb12bae,  0x0cb61b38,
    0x92d28e9b,  0xe5d5be0d,  0x7cdcefb7,  0x0bdbdf21,
    0x86d3d2d4,  0xf1d4e242,  0x68ddb3f8,  0x1fda836e,
    0x81be16cd,  0xf6b9265b,  0x6fb077e1,  0x18b74777,
    0x88085ae6,  0xff0f6a70,  0x66063bca,  0x11010b5c,
    0x8f659eff,  0xf862ae69,  0x616bffd3,  0x166ccf45,
    0xa00ae278,  0xd70dd2ee,  0x4e048354,  0x3903b3c2,
    0xa7672661,  0xd06016f7,  0x4969474d,  0x3e6e77db,
    0xaed16a4a,  0xd9d65adc,  0x40df0b66,  0x37d83bf0,
    0xa9bcae53,  0xdebb9ec5,  0x47b2cf7f,  0x30b5ffe9,
    0xbdbdf21c,  0xcabac28a,  0x53b39330,  0x24b4a3a6,
    0xbad03605,  0xcdd70693,  0x54de5729,  0x23d967bf,
    0xb3667a2e,  0xc4614ab8,  0x5d681b02,  0x2a6f2b94,
    0xb40bbe37,  0xc30c8ea1,  0x5a05df1b,  0x2d02ef8d,
  };

  uint32_t crc = ~old_crc;

  const uint8_t* ptr8 = (const uint8_t*) ptr;

  while (len--)
    crc = (crc >> 8) ^ crc_table[(uint8_t) crc ^ *ptr8++];

  return ~crc;
}

/** Description of executable's section header */
class SectionHeader
{
public:
  /** Enumeration of possible section types */
  enum Type {
    TEXT,   /**< Read-only executable data */
    RODATA, /**< Read-only data */
    DATA,   /**< Read/write data */
    RWTEXT, /**< Combined data and executable code segment */
    NOLOAD, /**< Special type saying 'don't load this' */
  };

  SectionHeader() :
    m_filesz(0), m_memsz(0), m_file_offset(0), m_load_address(0),
    m_type(TEXT) { }
 
  word m_filesz;         /**< Size of this section in the file */
  word m_memsz;          /**< Size of this section when loaded */
  word m_file_offset;    /**< Start offset within the file */
  word m_load_address;   /**< Start address within memory to load */
  Type m_type;           /**< Section type */
};

/** Relevant executable format information */
class BinaryHeader
{
public:
  /** Constructor.
   * @param in The stream from which to read the binary
   * @param binary_name The name of the binary
   */
  BinaryHeader(std::istream &in, bool ignore_bad_ise)
  {
    uint32_t magic = read4(in);
    // Four bytes of the elf magic number
    if (magic == (ELFMAG0 | (ELFMAG1 << 8) | (ELFMAG2 << 16) |
                  (ELFMAG3 << 24)))
    {
      // We have an ELF-formatted binary.
      ElfW(Ehdr) elf_header;
      ElfW(Phdr) program_header;

      // Read the elf header into elf_header (the +4 and -4 is because
      // we'e already read the magic number).
      in.read(((char*) &elf_header) + 4, sizeof (elf_header) - 4);

      bool big_endian = elf_header.e_ident[EI_DATA] == ELFDATA2MSB;
      if (big_endian)
      {
        elf_header.e_type = endian_swap(elf_header.e_type,
                                        sizeof(elf_header.e_type));
        elf_header.e_machine = endian_swap(elf_header.e_machine,
                                        sizeof(elf_header.e_machine));
        elf_header.e_version = endian_swap(elf_header.e_version,
                                        sizeof(elf_header.e_version));
        elf_header.e_entry = endian_swap(elf_header.e_entry,
                                        sizeof(elf_header.e_entry));
        elf_header.e_phoff = endian_swap(elf_header.e_phoff,
                                        sizeof(elf_header.e_phoff));
        elf_header.e_shoff = endian_swap(elf_header.e_shoff,
                                        sizeof(elf_header.e_shoff));
        elf_header.e_flags = endian_swap(elf_header.e_flags,
                                        sizeof(elf_header.e_flags));
        elf_header.e_ehsize = endian_swap(elf_header.e_ehsize,
                                        sizeof(elf_header.e_ehsize));
        elf_header.e_phentsize = endian_swap(elf_header.e_phentsize,
                                        sizeof(elf_header.e_phentsize));
        elf_header.e_phnum = endian_swap(elf_header.e_phnum,
                                        sizeof(elf_header.e_phnum));
        elf_header.e_shentsize = endian_swap(elf_header.e_shentsize,
                                        sizeof(elf_header.e_shentsize));
        elf_header.e_shnum = endian_swap(elf_header.e_shnum,
                                        sizeof(elf_header.e_shnum));
        elf_header.e_shstrndx = endian_swap(elf_header.e_shstrndx,
                                        sizeof(elf_header.e_shstrndx));
      }

      // Now sanity-check the rest of the header.
      //
      // e_machine:   Architecture
      // 
      // e_ehsize:    Elf header size in bytes (elf_header is a struct
      // used to hold the elf file header. The elf file header occurs at
      // the front of every elf file.)
      //
      // e_phentsize: Program header table entry size (program_header is a
      // struct used to hold a program header)
      //
      // e_type:      Object file type (ET_EXEC is an executable file)

      if ((elf_header.e_machine != CHIP_ELF_TYPE() &&
           elf_header.e_machine != CHIP_COMPAT_ELF_TYPE()) ||
          elf_header.e_ehsize < sizeof(elf_header))
      {
        cerr << "Bad ELF header" << endl;
        exit(EXIT_FAILURE);
      }
      if (elf_header.e_type != ET_EXEC)
      {
        cerr << "Can't make boot ROM from bare object file; link it first"
             << endl;
        exit(EXIT_FAILURE);
      }
#if TILE_CHIP >= 11
      if ((elf_header.e_flags & (1 << EF_TILEGX_ISE0)) && !ignore_bad_ise)
      {
        cerr << "File uses unsupported feature (ISE0)" << endl;
        exit(EXIT_FAILURE);
      }
#endif
#if TILE_CHIP < 11
      if ((elf_header.e_flags & (1 << EF_TILEGX_ISE1)) && !ignore_bad_ise)
      {
        cerr << "File uses unsupported feature (ISE1)" << endl;
        exit(EXIT_FAILURE);
      }
#endif
      if (elf_header.e_phentsize < sizeof(program_header))
      {
        cerr << "Can't make boot ROM from executable with no program "
                "headers"
             << endl;
        exit(EXIT_FAILURE);
      }
      // Get the start virtual address from the elf header; we'll translate
      // this to physical below
      word start_va = elf_header.e_entry;

      // Move the input pointer to point to the program header table.
      // e_phoff: The program header table offset.
      in.seekg(elf_header.e_phoff, ios::beg);

      // Loop through the number of program header table entries
      for (int i = 0; i < elf_header.e_phnum; i++)
      {
        // Read a program header table entry
        in.read((char*)&program_header, sizeof(program_header));

        if (big_endian)
        {
          program_header.p_type = endian_swap(program_header.p_type,
                                        sizeof(program_header.p_type));
          program_header.p_flags = endian_swap(program_header.p_flags,
                                        sizeof(program_header.p_flags));
          program_header.p_offset = endian_swap(program_header.p_offset,
                                        sizeof(program_header.p_offset));
          program_header.p_vaddr = endian_swap(program_header.p_vaddr,
                                        sizeof(program_header.p_vaddr));
          program_header.p_paddr = endian_swap(program_header.p_paddr,
                                        sizeof(program_header.p_paddr));
          program_header.p_filesz = endian_swap(program_header.p_filesz,
                                        sizeof(program_header.p_filesz));
          program_header.p_memsz = endian_swap(program_header.p_memsz,
                                        sizeof(program_header.p_memsz));
          program_header.p_align = endian_swap(program_header.p_align,
                                        sizeof(program_header.p_align));
        }

        // Do we have to check if the program header table entry is valid?

        // It is possible for a program table entry to be larger than
        // the struct defined to hold a program table entry. Move the
        // input pointer to the next program table entry.
        if (elf_header.e_phentsize > sizeof(program_header))
          in.seekg(elf_header.e_phentsize - sizeof(program_header), ios::cur);

        // Work out from the flags whether this is a data, rodata or text
        // segment.
        // PF_X (executable) PF_W (writable) PF_R (Readable)
        assert(PF_X == 1 && PF_W == 2 && PF_R == 4);

        const SectionHeader::Type section_types[8] = 
          { SectionHeader::NOLOAD, 
            SectionHeader::NOLOAD, 
            SectionHeader::NOLOAD, 
            SectionHeader::NOLOAD, 
            SectionHeader::RODATA, 
            SectionHeader::TEXT, 
            SectionHeader::DATA, 
            SectionHeader::RWTEXT };

        // p_flags: Segment flags
        SectionHeader::Type section_type = 
          section_types[program_header.p_flags & 7];
        if (section_type == SectionHeader::NOLOAD)
          continue; // nope -- doesn't exist. Might be a symtab.

        SectionHeader s;
        s.m_filesz = program_header.p_filesz;
        s.m_memsz = program_header.p_memsz;
        s.m_file_offset = program_header.p_offset;
        s.m_load_address = program_header.p_paddr;
        s.m_type = section_type;
        m_section.push_back(s);

        // If the start address is in this section, get its physical address
        if (start_va >= program_header.p_vaddr &&
            start_va < program_header.p_vaddr + program_header.p_memsz)
          m_start_address = (start_va - program_header.p_vaddr) +
                             program_header.p_paddr;
      }
    }
    else
    {
      cerr << "Bad magic number" << endl;
      exit(EXIT_FAILURE);
    }
  
  }

  std::vector<SectionHeader> m_section;  /**< Section info per-type */
  word m_start_address;  /**< Physical address at which to start execution */

private:
  /** Read a little-endian word from a stream.
   * @param in The stream to read from
   * @return The 32-bit variable read
   */
  uint32_t
  read4(std::istream &in)
  {
    unsigned char buf[4];
    in.read((char*)buf, 4);
    if (!in)
    {
      cerr << "Failed to read Binary header: "
           << strerror(errno) << endl;
      exit(EXIT_FAILURE);
    }
    return (buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24));
  }

  uint64_t
  endian_swap(uint64_t val, size_t size)
  {
    switch (size)
    {
      case 8: return __builtin_bswap64(val);

      case 4: return __builtin_bswap32(val);

      case 2: return ((val >> 8) & 0xff) | ((val << 8) & 0xff00);

      // Won't happen!
      default: return val;
    }
  }
};

void usage(char* progname)
{
  fprintf(stderr,
"Usage: %s [-b] bootfile [-f] filesys [-o] outputfile\n"
"                  [-c config] [-s start] [-u ulhc] [-dnSHCt]\n"
"                  [-x word | x,y] [ -m s0[,s1[,s2[,s3]]]]\n"
#ifdef TILEGX
"                  [-D device[:speed[,speed]]] [-p [Q]{t|q}]\n"
#endif
"where: bootfile is the ELF binary for first-level boot\n"
"       filesys is the filesystem image or other second-level boot\n"
"       outputfile is the output filename where the ROM will be written\n"
"       config is the tile configuration (default 8x8)\n"
"       start is the tile that the boot shim is attached to (default 7,7)\n"
"       ulhc is the tile in the upper left-hand corner (default 0,0)\n"
"       -d specifies that the configuration be determined dynamically;\n"
"          this option is incompatible with -c, -s, or -u\n"
"       -n specifies that L1 boot configuration data be omitted;\n"
"          this option is incompatible with -c, -s, -u, or -x\n"
"       -S builds a secondary booter, using the L1 boot format, and\n"
"          bypassing size/address checks\n"
"       -H encodes a hypervisor image, using the L1 boot format,\n"
"          adding a CRC to each segment, and bypassing size/address checks\n"
"       -C adds a CRC to a file in the L0 boot format\n"
"       -t requests that the system be configured for memory striping\n"
"       -T requests that the system be configured for memory striping if at\n"
"          all possible, even if that reduces memory capacity\n"
"       -x adds a word or a tile coordinate to the L1 boot configuration\n"
"          data; multiple -x options may be specified\n"
"       -m adds memory speed settings\n"
#ifdef TILEGX
"       -D adds device and speed settings\n"
"       -p overrides the POST mode (t=thorough, q=quick, Q=query)\n"
#endif
        "", progname);
  exit(EXIT_FAILURE);
}

typedef std::pair<int,int> Pos;

int make_pos(Pos pos)
{
  return ((pos.first << 18) | (pos.second << 7));
}

/** L2 cache lines that we've written to so far */
std::bitset<L2_INDEX_SIZE> l2_lines_touched;

/* Does this address range overlap the L0 boot code, or does
   it alias data we've already written in the L2$? */
int
code_aliases(word start_addr, word end_addr)
{
  word start_cl = start_addr / L2_CL_SIZE;
  word end_cl = end_addr / L2_CL_SIZE;

  for (word i = start_cl; i < end_cl; i++)
  {
    if (start_addr < L0_BOOT_SIZE)
    {
      cerr << "Boot program overlaps L0 boot code at address 0x"
           << hex << start_addr << dec << "!" << endl;
      return (1);
    }

    word l2index = i & (L2_INDEX_SIZE - 1);

    if (l2_lines_touched.test(l2index))
    {
      cerr << "Boot program aliases itself in L2 cache at address 0x"
           << hex << i * L2_CL_SIZE << dec << "!" << endl;
      return (1);
    }

    l2_lines_touched.set(l2index);
  }

  return (0);
}


#ifdef TILEGX

/** Boot parameters we'll write to the output stream.  We leave everything
 *  initialized to zero, but note that if we don't see any -m or -D
 *  options, we may not even write this structure to the boot stream, and
 *  then the booter's defaults will be used. */
static union boot_params boot_params;

/** Number of valid 8-byte words in boot_params. */
static int boot_param_words;

/** Handle a --dev option.
 * @param opt Option value.
 */
static void
handle_dev_option(char* opt)
{
  //
  // If this is just --dev <device>, with no speed, use the default.
  //
  long speed[2] = { SPEED_DEFAULT, SPEED_DEFAULT };

  char* dev = opt;

  char* p = strchr(opt, ':');
  if (p)
  {
    *p++ = '\0';
    char *endptr;
    speed[0] = strtol(p, &endptr, 10);
    if (*endptr == ',')
    {
      //
      // Only mPIPE supports more than one PLL.
      //
      if (strncmp(dev, "mpipe", strlen("mpipe")))
      {
        fprintf(stderr, "tile-mkrom: device %s has too many speeds\n", dev);
        exit(1);
      }
      speed[1] = strtol(endptr + 1, &endptr, 10);
    }

    //
    // Don't accept any trailing junk after the speed.
    //
    if (*endptr)
    {
      fprintf(stderr, "tile-mkrom: speed for device %s has bad format\n", dev);
      exit(1);
    }

    //
    // Speeds larger than this will get clipped, so complain.
    //
    if (speed[0] > SPEED_MAX || speed[1] > SPEED_MAX)
    {
      fprintf(stderr, "tile-mkrom: speed for device %s out of range\n", dev);
      exit(1);
    }
  }

  if (!strcmp(dev, "core"))
    boot_params.cfg.speed_core = speed[0];
  else if (!strcmp(dev, "mpipe/0"))
  {
    boot_params.cfg.speed_mpipe_0_core = speed[0];
    boot_params.cfg.speed_mpipe_0_cls = speed[1];
  }
  else if (!strcmp(dev, "mpipe/1"))
  {
    boot_params.cfg.speed_mpipe_1_core = speed[0];
    boot_params.cfg.speed_mpipe_1_cls = speed[1];
  }
  else if (!strcmp(dev, "trio/0"))
    boot_params.cfg.speed_trio_0 = speed[0];
  else if (!strcmp(dev, "trio/1"))
    boot_params.cfg.speed_trio_1 = speed[0];
  else if (!strcmp(dev, "crypto/0"))
    boot_params.cfg.speed_crypto_0 = speed[0];
  else if (!strcmp(dev, "crypto/1"))
    boot_params.cfg.speed_crypto_1 = speed[0];
  else if (!strcmp(dev, "comp/0"))
    boot_params.cfg.speed_comp_0 = speed[0];
  else if (!strcmp(dev, "comp/1"))
    boot_params.cfg.speed_comp_1 = speed[0];
  else
  {
    //
    // We only complain about unrecognized devices if they tried to set a
    // speed; many legal devices, including pseudo-devices, don't have a
    // PLL, and we don't want to have to list them all here.
    //
    if (speed[0] != SPEED_DEFAULT)
    {
      fprintf(stderr, "tile-mkrom: device %s is not speed-settable\n", dev);
      exit(1);
    }
  }

  //
  // If we got this far, we set something in the speed section of the
  // boot_params structure, so mark it as destined for output.  If we add
  // things to it after the speed information we might want to limit this
  // length.
  //
  boot_param_words =
    max((size_t) boot_param_words,
        sizeof (boot_params) / sizeof (uint64_t));
}

#else

static void
handle_dev_option(char* opt)
{
  //
  // This option does nothing on Pro, but we implement/ignore it so that
  // tile-mkboot doesn't have to know what architecture it runs on.
  //
}

#endif


int
main(int argc, char **argv)
{
  char* boot = NULL;
  char* filesys = NULL;
  char* output = NULL;
  bool do_snake = true;          // true if -n option not used
  bool secondary = false;        // true if -S or -H options used
  bool do_crc = false;           // true if -H or -C option used
  bool boot_config_set = false;  // true if -c, -s, -u, or -x option used
  bool dynamic_boot = false;     // true if -d option used
  bool striping = false;         // true if -t option used
  bool force_striping = false;   // true if -T option used
  bool ignore_bad_ise = false;   // true if -I option used
  std::vector<unsigned int> x_args;
  std::vector<word> m_args;
  Pos config(8,8);
  Pos ulhc(0,0);
  Pos start(7,7);

  // First process options.
  for (int i = 1; i < argc; i++)
  {
    if (argv[i][0] == '-')
    {
      if (strcmp(argv[i],"--help") == 0 || strcmp(argv[i], "-h") == 0)
        usage(argv[0]);
      else if (strcmp(argv[i],"--config") == 0 || strcmp(argv[i], "-c") == 0)
      {
        char* configstr;
        if (argv[i+1] == NULL)
          usage(argv[0]);
        config.first = strtoul(argv[i+1], &configstr, 10);
        if (*configstr != ':' && *configstr != ',' && *configstr != 'x')
          usage(argv[0]);
        config.second = strtoul(configstr+1, &configstr, 10);
        if (*configstr)
          usage(argv[0]);
        i++;
        boot_config_set = true;
      }
      else if (strcmp(argv[i],"--ulhc") == 0 || strcmp(argv[i], "-u") == 0)
      {
        char* ulhcstr;
        if (argv[i+1] == NULL)
          usage(argv[0]);
        ulhc.first = strtoul(argv[i+1], &ulhcstr, 10);
        if (*ulhcstr != ':' && *ulhcstr != ',' && *ulhcstr != 'x')
          usage(argv[0]);
        ulhc.second = strtoul(ulhcstr+1, &ulhcstr, 10);
        if (*ulhcstr)
          usage(argv[0]);
        i++;
        boot_config_set = true;
      }
      else if (strcmp(argv[i],"--start") == 0 || strcmp(argv[i], "-s") == 0)
      {
        char* startstr;
        if (argv[i+1] == NULL)
          usage(argv[0]);
        start.first = strtoul(argv[i+1], &startstr, 10);
        if (*startstr != ':' && *startstr != ',' && *startstr != 'x')
          usage(argv[0]);
        start.second = strtoul(startstr+1, &startstr, 10);
        if (*startstr)
          usage(argv[0]);
        i++;
        boot_config_set = true;
      }
      else if (strcmp(argv[i],"--extra") == 0 || strcmp(argv[i], "-x") == 0)
      {
        char* xstr;
        word xval;
        Pos xpos(0,0);
        if (argv[i+1] == NULL)
          usage(argv[0]);
        xval = strtoul(argv[i+1], &xstr, 0);
        if (*xstr == '\0')
        {
          x_args.push_back(xval);
        }
        else
        {
          xpos.first = xval;
          if (*xstr != ',')
            usage(argv[0]);
          xpos.second = strtoul(xstr+1, &xstr, 0);
          if (*xstr)
            usage(argv[0]);
          x_args.push_back(make_pos(xpos));
        }
        i++;
        boot_config_set = true;
      }
      else if (strcmp(argv[i],"--memory") == 0 || strcmp(argv[i], "-m") == 0)
      {
        char* str;
        word m[4] = { 0, 0, 0, 0 };
        int last_m_set = 0;

        if (argv[i+1] == NULL)
          usage(argv[0]);

        str = argv[i + 1];

        for (int j = 0; j < 4; j++)
        {
          m[j] = strtol(str, &str, 10) & 0xFFFF;
          last_m_set = j;
          if (*str == ',')
            str++;
          else
            break;
        }

        if (*str != '\0')
          usage(argv[0]);

        //
        // If we don't have 4 values, replicate the last one in the
        // remaining slots.  This lets you set all 4 slots with one value.
        //
        for (int j = last_m_set + 1; j < 4; j++)
          m[j] = m[last_m_set];

#ifdef TILEGX
        for (int j = 0; j < 4; j++)
          boot_params.cfg.mem_speed[j] = m[j];
        boot_param_words = max(boot_param_words, 1);
#else
        m_args.push_back((m[1] << 16) | (m[0] <<  0));
        m_args.push_back((m[3] << 16) | (m[2] <<  0));
#endif
        i++;
      }
#ifdef TILEGX
      else if (strcmp(argv[i], "--post") == 0 || strcmp(argv[i], "-p") == 0)
      {
        char* post_arg = argv[i+1];
        boot_params.cfg.post_override = 1;
        while (*post_arg)
        {
          switch (*post_arg++)
          {
          case 'Q':
            boot_params.cfg.post_query = 1;
            break;
          case 't':
            boot_params.cfg.post_thorough = 1;
            break;
          case 'q':
            boot_params.cfg.post_thorough = 0;
            break;
          default:
            usage(argv[0]);
            break;
          }
        }
        boot_param_words = max(boot_param_words, 2);
        i++;
      }
#endif
      else if (strcmp(argv[i],"--boot") == 0 || strcmp(argv[i], "-b") == 0)
      {
        boot = argv[i+1];
        i++;
      }
      else if (strcmp(argv[i],"--filesystem") == 0 ||
               strcmp(argv[i], "-f") == 0)
      {
        filesys = argv[i+1];
        i++;
      }
      else if (strcmp(argv[i],"--output") == 0 || strcmp(argv[i], "-o") == 0)
      {
        output = argv[i+1];
        i++;
      }
      else if (strcmp(argv[i],"--no-snake") == 0 || strcmp(argv[i], "-n") == 0)
      {
        do_snake = false;
      }
      else if (strcmp(argv[i],"--secondary") == 0 || strcmp(argv[i], "-S") == 0)
      {
        secondary = true;
      }
      else if (strcmp(argv[i],"--hypervisor") == 0 ||
               strcmp(argv[i], "-H") == 0)
      {
        secondary = true;
        do_crc = true;
      }
      else if (strcmp(argv[i],"--dynamic") == 0 || strcmp(argv[i], "-d") == 0)
      {
        dynamic_boot = true;
      }
      else if (strcmp(argv[i],"--stripe") == 0 || strcmp(argv[i], "-t") == 0)
      {
        striping = true;
      }
      else if (strcmp(argv[i],"--force-stripe") == 0 ||
               strcmp(argv[i], "-T") == 0)
      {
        force_striping = true;
      }
      else if (strcmp(argv[i],"--crc") == 0 || strcmp(argv[i], "-C") == 0)
      {
        do_crc = true;
      }
      else if (strcmp(argv[i],"--ignore-bad-ise") == 0 ||
               strcmp(argv[i], "-I") == 0)
      {
        ignore_bad_ise = true;
      }
      else if (strcmp(argv[i],"--dev") == 0 || strcmp(argv[i], "-D") == 0)
      {
        handle_dev_option(argv[i + 1]);
        i++;
      }
    }
    else
    {
      // Positional option.
      if (boot == NULL)
        boot = argv[i];
      else if (filesys == NULL)
        filesys = argv[i];
      else if (output == NULL)
        output = argv[i];
      else
        usage(argv[0]);
    }
  }
  if (boot == NULL || output == NULL)
    usage(argv[0]);

  if (boot_config_set && !do_snake)
  {
    cerr << "The -n option is incompatible with the -c, -s, and -u options."
         << endl;
    exit(EXIT_FAILURE);
  }

  if (boot_config_set && dynamic_boot)
  {
    cerr << "The -d option is incompatible with the -c, -s, and -u options."
         << endl;
    exit(EXIT_FAILURE);
  }

  if (start.first < ulhc.first || start.second < ulhc.second ||
      start.first >= ulhc.first + config.first ||
      start.second >= ulhc.second + config.second)
  {
    cerr << "The start tile must be somewhere in the configured space!"
         << endl;
    exit(EXIT_FAILURE);
  }
  if (start.first != ulhc.first && start.second != ulhc.second && 
      start.first != ulhc.first + config.first - 1 &&
      start.second != ulhc.second + config.second - 1)
  {
    cerr << "The start tile is not on the edge of the configured space!"
         << endl;
    exit(EXIT_FAILURE);
  }
  
  std::ifstream bootstream(boot, std::istream::binary);
  if (!bootstream)
  {
    cerr << "Cannot open input file " << boot << endl;
    exit(EXIT_FAILURE);
  }

  word address, length;

  BinaryHeader header(bootstream, ignore_bad_ise);
  word launch_addr = header.m_start_address;
  
  char* out_tempfilename = new char[strlen(output)+7];
  strcpy(out_tempfilename, output);
  strcat(out_tempfilename, "XXXXXX");
  int out_tempfd = mkstemp(out_tempfilename);
  if (out_tempfd < 0)
  {
    // grab strerror while errno is definitely still valid.
    const char* errmsg = strerror(errno);
    cerr << "Error opening output file " << output << ": " << errmsg << endl;
    exit(EXIT_FAILURE);
  }
  FILE* out_file = fdopen(out_tempfd, "wb");

  // Set up a CRC generator in case we need it below
  word crc_32 = 0;
  long crc_pos = 0;

  //
  // Copy each input section to the output file with the appropriate header
  //
  for (unsigned int i = 0; i < header.m_section.size(); ++i)
  {
    SectionHeader &sec = header.m_section[i];
    word start_addr = sec.m_load_address;
    word end_addr = start_addr + sec.m_memsz;

    if (!secondary && code_aliases(start_addr, end_addr))
    {
        unlink(out_tempfilename);
        exit(EXIT_FAILURE);
    }

    // Don't write anything for empty segments; the booter will handle it, but
    // it just clutters up the output.
    if (sec.m_memsz == 0)
      continue;

    // Write the header and buffer here.
    address = start_addr;
    length = (sec.m_memsz + sizeof (word) - 1) / sizeof (word);

    if (!secondary && do_crc && i == 0)
    {
      // If we're doing a CRC for an L0 boot stream, we need to put in an
      // extra segment.  We'll come back later and stuff the resulting
      // CRC here.
      word dummy_length = 1;
      fwrite(&dummy_length, sizeof (dummy_length), 1, out_file);
      fwrite(&address, sizeof (address), 1, out_file);
      crc_pos = ftell(out_file);
      word dummy_data = 0;
      fwrite(&dummy_data, sizeof (dummy_data), 1, out_file);
      fwrite(&hw_l0boot_next_pkt, sizeof (hw_l0boot_next_pkt), 1, out_file);
    }

    if (!secondary && i > 0)
    {
      fwrite(&hw_l0boot_next_pkt, sizeof (hw_l0boot_next_pkt), 1, out_file);
      if (do_crc)
        crc_32 = crc(&hw_l0boot_next_pkt, sizeof (hw_l0boot_next_pkt), crc_32);
    }

    fwrite(&length, sizeof (length), 1, out_file);
    fwrite(&address, sizeof (address), 1, out_file);

    if (do_crc)
    {
      crc_32 = crc(&length, sizeof (length), crc_32);
      crc_32 = crc(&address, sizeof (address), crc_32);
      if (secondary)
        fwrite(&crc_32, sizeof (crc_32), 1, out_file);
    }

    unsigned char* buf = new unsigned char[sec.m_memsz + sizeof (word)];
    bootstream.seekg(sec.m_file_offset, ios::beg);
    bootstream.read((char*)buf, sec.m_filesz);
    // This is slightly bogus.  If there's less data in the file than there
    // is in memory, we're required to zero the remainder.  What we ought
    // to do here is define an opcode for zeroing stuff, rather than writing
    // a bunch of zeroes to the output file.  In the meantime, though, since
    // we're already prepared to write some trailing zeroes, we also take
    // advantage of the opportunity to round up the data written to a whole
    // number of words.
    uint32_t output_bytes = (sec.m_memsz + sizeof (word) - 1) & -sizeof (word);
    if (sec.m_filesz < output_bytes)
      memset((void*)&buf[sec.m_filesz], 0, output_bytes - sec.m_filesz);
    fwrite(buf, 1, output_bytes, out_file);
    if (do_crc)
      crc_32 = crc(buf, output_bytes, crc_32);

    delete[] buf;
  }

  address = launch_addr;
  if (secondary)
  {
    length = 0;
    fwrite(&length, sizeof (length), 1, out_file);
    if (do_crc)
      crc_32 = crc(&length, sizeof (length), crc_32);
  }
  fwrite(&address, sizeof (address), 1, out_file);

  if (do_crc)
  {
    crc_32 = crc(&address, sizeof (address), crc_32);
    if (!secondary)
    {
      // For an L0 stream being CRC'ed, we need to go back to our extra segment
      // to write the CRC.
      fseek(out_file, crc_pos, SEEK_SET);
      fwrite(&crc_32, sizeof (crc_32), 1, out_file);
      fseek(out_file, 0, SEEK_END);
    }
    else
      fwrite(&crc_32, sizeof (crc_32), 1, out_file);
  }
  
  if (do_snake)
  {
    // Now write out the booter config data:
    // - Predecessor tile coordinates, with flag bits in unused fields.
    //   Bit 31 means this tile is the master; bit 30 means memory
    //   striping is requested; bit 29 means memory striping is mandated;
    //   bit 0 means this this is a dynamic boot; bits [6:1] count the
    //   number of extra words after the LRHC word.
    // - Tile coordinates of master boot tile
    // - Tile coordinates of chip's upper left hand corner
    // - Tile coordinates of chip's lower right hand corner
    // - Memory speed extra words, if requested by the -m option (4 12-bit
    //   values in one word on Gx, or in 2 words for Pro)
    // - 0 or more arbitrary extra words (if requested by -x options)
    // - CRC of the config data words

    // Set up a CRC generator
    word crc_32_config = 0;

    word coord = (dynamic_boot) ? 0x80000001 : 0x80000000;
    if (striping)
      coord |= 0x40000000;
    if (force_striping)
      coord |= 0x60000000;
    if (dynamic_boot)
#ifdef TILEGX
      coord |= (x_args.size() + boot_param_words) << 1;
#else
      coord |= (x_args.size() + m_args.size()) << 1;
#endif
    fwrite(&coord, sizeof (coord), 1, out_file);
    crc_32_config = crc(&coord, sizeof (coord), crc_32_config);

    coord = make_pos(start);
    fwrite(&coord, sizeof (coord), 1, out_file);
    crc_32_config = crc(&coord, sizeof (coord), crc_32_config);

    coord = make_pos(ulhc);
    fwrite(&coord, sizeof (coord), 1, out_file);
    crc_32_config = crc(&coord, sizeof (coord), crc_32_config);

    coord = make_pos(Pos(ulhc.first + config.first - 1,
                         ulhc.second + config.second - 1));
    fwrite(&coord, sizeof (coord), 1, out_file);
    crc_32_config = crc(&coord, sizeof (coord), crc_32_config);

#ifdef TILEGX
    if (boot_param_words)
    {
      fwrite(&boot_params, sizeof (uint64_t) * boot_param_words, 1, out_file);
      crc_32_config = crc(&boot_params, sizeof (uint64_t) * boot_param_words,
                          crc_32_config);
    }
#else
    for (unsigned int i = 0; i < m_args.size(); i++)
    {
      coord = m_args[i];
      fwrite(&coord, sizeof (coord), 1, out_file);
      crc_32_config = crc(&coord, sizeof (coord), crc_32_config);
    }
#endif

    for (unsigned int i = 0; i < x_args.size(); i++)
    {
      coord = x_args[i];
      fwrite(&coord, sizeof (coord), 1, out_file);
      crc_32_config = crc(&coord, sizeof (coord), crc_32_config);
    }

    if (do_crc)
      fwrite(&crc_32_config, sizeof (crc_32_config), 1, out_file);
  }

  if (filesys)
  {
    std::ifstream filesysstream(filesys, std::istream::binary);
    if (!filesysstream)
    {
      cerr << "Cannot open input file " << filesys << endl;
      unlink(out_tempfilename);
      exit(EXIT_FAILURE);
    }
    filesysstream.seekg(0,ios::end);
    word filesysdatalength = filesysstream.tellg();
    filesysstream.seekg(0,ios::beg);
    unsigned char* filesysdata = new unsigned char[filesysdatalength];
    filesysstream.read((char*)filesysdata, filesysdatalength);
    fwrite(filesysdata, 1, filesysdatalength, out_file);
    delete[] filesysdata;
  }

  /* Apply the user's umask to the temporary file, since mkstemp created it as
   * mode 0600.  We need to call umask() to get the umask, but then we need
   * to call it again to restore the current mode.
   */

  mode_t current_umask = umask(0);
  umask(current_umask);

  mode_t new_mask = (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH |
                     S_IWOTH) & ~current_umask;
  fchmod(out_tempfd, new_mask);

  /* Now close the file, delete the real file, and move the temp file
   * into the real file's spot.
   */
  if (fclose(out_file) != 0)
  {
    // grab strerror while errno is definitely still valid.
    const char* errmsg = strerror(errno);
    cerr << "Error closing output file " << output << ": " << errmsg << endl;
    unlink(out_tempfilename);
    exit(EXIT_FAILURE);
  }
  /* We don't care about errors deleting this file -- if something bad
   * happens we'll worry about it on the rename, and if the file wasn't
   * there, that's absolutely fine!
   */
  unlink(output);
  if (rename(out_tempfilename, output) < 0)
  {
    const char* errmsg = strerror(errno);
    cerr << "Error putting output file " << output << " into place: "
         << errmsg << endl;
    exit(EXIT_FAILURE);
  }
  delete[] out_tempfilename;

  return EXIT_SUCCESS;
}
