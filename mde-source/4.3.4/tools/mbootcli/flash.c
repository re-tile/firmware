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

#include "config.h"
#include "utils.h"
#include "flash.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


static int flash_block_size;

static const char *flash_device_name[DEVICE_NUM] = {
  DEV_BOOT_PARAM,
  DEV_USER_PARAM
};

static const char *error_device_name[DEVICE_NUM] = {
  "boot_param device",
  "user_param device"
};

struct block
{
  int b_size;                    // 32 bits
  int b_vers;
  int b_csum;
} blocks[BLOCKS_IN_FLASH];

static char filebuf[BUFSIZE];

// The file desc of the userparam device.
static int flash_fd[DEVICE_NUM];


/** Calculate the checksum and read the version of the data.
 * @param device user or boot device
 * @param blocknum how many blocks in this device
 * @param blockp the pointer to the block in the header of the device.
 * @return positive number: data version. -1: flash operation failure. 
 *        -2: flash contains invalid data
 */
static int
compute_block(int device, int blocknum, struct block *blockp)
{
  if (lseek(flash_fd[device], flash_block_size * blocknum, SEEK_SET) < 0)
  {
    print_error(0, "Cannot seek to beginning of block %d of %s:  %e",
                blocknum, error_device_name[device]);
    return -1;
  }
  if (read(flash_fd[device], blockp, sizeof (struct block)) <
      sizeof (struct block))
  {
    print_error(0, "Cannot read block 1 of %s:  %e",
                error_device_name[device]);
    return -1;
  }
  // we regard this happens when the flash has not ben written any valid data.
  if (blockp->b_size < 1 ||
      blockp->b_size > flash_block_size - sizeof (struct block))
    return -2;
  int left_to_read = blockp->b_size;
  int csum = 0;
  while (left_to_read > 0)
  {
    int toread = ((left_to_read > BUFSIZE) ? BUFSIZE : left_to_read);
    int numread = read(flash_fd[device], filebuf, toread);
    if (numread <= 0)
    {
      print_error(0, "Cannot read block from %s:  %e");
      return -1;
    }
    int i;
    for (i = 0; i < numread; i++)
      csum += filebuf[i];
    left_to_read -= numread;
  }
  if (csum != blockp->b_csum)
    return -2;
  return blockp->b_vers;
}


static int 
setup_file(int device, int mode)
{

  if ((flash_fd[device] = open(flash_device_name[device], mode)) < 0) {
    print_error(0, "Cannot open %s for %d:  %e", error_device_name[device],
                mode);
    return -1;
  }
  flash_block_size = lseek(flash_fd[device], 0, SEEK_END) / BLOCKS_IN_FLASH;
  if (flash_block_size < 0) {
    close(flash_fd[device]);
    print_error(0, "Cannot seek to end of %s:  %e", error_device_name[device]);
    return -1;
  }

  return 0;
}


/** Read data from desired device
 * @param device user or boot device
 * @param len same with read_len
 * @param read_len how many bytes has been read to ret
 * @return 0 read successfully, otherwise, some errors have occured
 */
int
read_flash_data(int device, int *length, int *read_len, char *ret)
{
  int blocknum;
  int bestvers = -1;
  int bestblock = -1;

  if (setup_file(device, O_RDONLY) < 0)
    return -1;

  for (blocknum = 0; blocknum < BLOCKS_IN_FLASH; blocknum++)
  {
    int vers = compute_block(device, blocknum, &blocks[blocknum]);
    if (vers == -1) {
      close(flash_fd[device]);
      return -1;                // device operation failure.     
    }
    if (vers > bestvers)
    {
      bestblock = blocknum;
      bestvers = vers;
    }
  }
  if (bestvers < 0)
  {
    close(flash_fd[device]);
    return -2;                 // no data in device, or data inconsistent.
  }
  int toxfer = blocks[bestblock].b_size;

  *read_len = toxfer;

  char *retp = ret;
  if (lseek
      (flash_fd[device], bestblock * flash_block_size + sizeof (struct block),
       SEEK_SET) < 0)
  {
    close(flash_fd[device]);
    print_error(0, "Cannot seek %s:  %e", error_device_name[device]);
    return -1;
  }

  while (toxfer > 0)
  {
    int toread = (toxfer > BUFSIZE ? BUFSIZE : toxfer);
    int numread = read(flash_fd[device], retp, toread);
    if (numread <= 0)
    {
      close(flash_fd[device]);
      print_error(0, "read from %s failed:  %e", error_device_name[device]);
      return -1;
    }
    retp += numread;
    toxfer -= numread;
  }
  close(flash_fd[device]);
  if (length)
    *length = blocks[bestblock].b_size;

  return 0;
}


void
write_flash_data(int device, const char *datap, int numtowrite)
{
  int blocknum;
  int blocktorewrite = -1;
  int bestvers = 0;
  int worstblock = -1;
  int worstvers = -1;

  if ((setup_file(device, O_RDWR | O_CREAT) < 0))
    return;

  for (blocknum = 0; blocknum < BLOCKS_IN_FLASH; blocknum++)
  {
    int vers = compute_block(device, blocknum, &blocks[blocknum]);

    /* printf("blocknum=%d vers=%d.\n",blocknum,vers); */
    if (vers < 0)
      blocktorewrite = blocknum;
    if (vers > bestvers)
      bestvers = vers;
    if (worstvers < 0 || vers < worstvers)
    {
      worstvers = vers;
      worstblock = blocknum;
    }
  }
  if (blocktorewrite >= 0)
    worstblock = blocktorewrite;

  if (lseek
      (flash_fd[device], worstblock * flash_block_size + sizeof (struct block),
       SEEK_SET) < 0)
  {
    close(flash_fd[device]);
    print_error(0, "Cannot seek 1 %s:  %e", error_device_name[device]);
    return;
  }

  int csum = 0;
  int i;
  for (i = 0; i < numtowrite; i++)
    csum += datap[i];
  if (write(flash_fd[device], datap, numtowrite) < numtowrite)
  {
    close(flash_fd[device]);
    print_error(0, "Cannot write %s:  %e", error_device_name[device]);
    return;
  }

  if (lseek(flash_fd[device], worstblock * flash_block_size, SEEK_SET) < 0)
  {
    close(flash_fd[device]);
    print_error(0, "Cannot seek 2 %s:  %e", error_device_name[device]);
    return;
  }

  blocks[worstblock].b_size = numtowrite;
  blocks[worstblock].b_csum = csum;
  blocks[worstblock].b_vers = bestvers + 1;

  if (write(flash_fd[device], &blocks[worstblock], sizeof (struct block))
      < sizeof (struct block))
  {
    close(flash_fd[device]);
    print_error(0, "Cannot write %s:  %e", error_device_name[device]);
    return;
  }

  close(flash_fd[device]);
}
