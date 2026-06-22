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

// Routines to access SROM.

#ifndef TOOLS_MBOOTCLI_PARAMETERS_H
#define TOOLS_MBOOTCLI_PARAMETERS_H

#define BUFSIZE                10240
#define MAX_CHAR_PER_CMD       1024
#define MAX_PARAM_PER_CMD      256
#define MAX_CHAR_PER_PARAM     256
#define MAX_CHAR_PER_FILENAME  128

#define ADDRESS_LEN            64
#define MAX_CHAR_PER_DEV_NAME  16
#define MAX_NUM_OF_NETIF       64
#define NETIF_NAME_LEN         16

#define READ_BUF_SIZE          2048

#define SALT_STATIC            "tl"

// These are the parameter type for the parameters store in boot sector.
#define PARAM_BOOT_DEVICE         0
#define PARAM_BOOT_IMG            1
#define PARAM_BOOT_ARGS           2
#define PARAM_BOOT_HOST           3
#define PARAM_BOOT_INITRD         4

// Parameters types in user sector.
#define PARAM_USER_OFFSET       0x100
#define PARAM_BAUD_RATE         (PARAM_USER_OFFSET + 1)
#define PARAM_IP_ADDRESS        (PARAM_USER_OFFSET + 2)
#define PARAM_NET_MASK          (PARAM_USER_OFFSET + 3)
#define PARAM_MAC_ADDRESS       (PARAM_USER_OFFSET + 4)
#define PARAM_SPEED_DUPLEX      (PARAM_USER_OFFSET + 5)
#define PARAM_DHCP              (PARAM_USER_OFFSET + 8)
#define PARAM_ROUTE             (PARAM_USER_OFFSET + 9)
#define PARAM_DEFAULT_ROUTE     (PARAM_USER_OFFSET + 10)

#define MAX_PARAM  (BUFSIZE/4)  //maximum number of parameter entries.

#define USER_MAGIC_NUMBER       0x55AACC88
#define BOOT_MAGIC_NUMBER       0x88CCAA55

#endif /* TOOLS_MBOOTCLI_PARAMETERS_H */
