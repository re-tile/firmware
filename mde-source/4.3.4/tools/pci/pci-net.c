// Copyright 2014 Tilera Corporation. All Rights Reserved.
//
//   The source code contained or described herein and all documents
//   related to the source code ("Material") are owned by Tilera
//   Corporation or its suppliers or licensors.  Title to the Material
//   remains with Tilera Corporation or its suppliers and licensors. The
//   software is licensed under the Tilera MDE License.
//
//   Unless otherwise agreed by Tilera in writing, you may not remove or
//   alter this notice or any other notice embedded in Materials by Tilera
//   or Tilera's suppliers or licensors in any way.

/*
 * This program, running on both the host and the tile systems, establishes
 * an IP tunnel over one of the PCI zero-copy channels.
 *
 * Usage:
 * pci-net [-c <card-number>] [-s <ZC-channel-number>]
 *         [-l <local-IP-address>] [-r <remote-IP-address>]
 *
 * The <card-number> is the desired card number, defaulting to 0.
 *
 * The <ZC-channel-number> is the desired zero-copy channel
 * number, defaulting to 1.
 *
 * The default IP address allocation for card N, ZC channel M, is
 * 192.168.100+N.100+M for HOST, and 192.168.100+N.200+M for TILE.
 * 
 */

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/errno.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <string.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <sched.h>
#include <assert.h>
#include <malloc.h>

#ifndef __tile__
#define TILEPCI_HOST
#endif

#if TILE_CHIP < 10
#include <asm/tilepci.h>
#else
#include <asm/tilegxpci.h>
#endif

#ifndef __tile__
#include <sys/utsname.h>
#endif

#define TUN_DEV_NAME "/dev/net/tun"


#ifdef __tile__

#define IP_ADDR_LOCAL		200
#define IP_ADDR_REMOTE		100

#else

#define IP_ADDR_LOCAL		100
#define IP_ADDR_REMOTE		200

#endif

#define BUF_SIZE		8192

/* File descriptor for the kernel TUN device. */
static int tun_fd;

/* Flag indicating if the tunnel should be brought down. */
static int tunnel_should_reset;

static void
usage(void)
{
  fprintf(stderr, "Usage: "
#ifdef __tile__
          "pci-net"
#else
          "tile-pci-net"
#endif
          " [-c <card-number>]\n"
          "\t[-s <ZC-channel-number>]\n"
          "\t[-l <local-IP-address>] [-r <remote-IP-address>]\n");
  exit(1);
}


void* sender(void* arg)
{
  char *pci_wdev = (char *)arg;

  // Open the PCIe zero-copy channel.

  int pci_wfd = open(pci_wdev, O_RDWR);
  if (pci_wfd < 0)
  {
    fprintf(stderr, "can't open %s: %s\n", pci_wdev, strerror(errno));
    tunnel_should_reset = 1;
    pthread_exit(NULL);
  }

  // Forward traffic from the TUN device to the PCI channel.

#ifdef __tile__

  // On the tile side, packet buffers can come from any memory region,
  // but they must not cross a page boundary.
  void *wbuf = memalign(getpagesize(), BUF_SIZE);
  assert(wbuf != NULL);

#if TILE_CHIP >= 10
  // Register the buffer to IOTLB.
  tilegxpci_buf_info_t buf_info = {
    .va = (uintptr_t) wbuf,
    .size = BUF_SIZE,
  };

  int result = ioctl(pci_wfd, TILEPCI_IOC_REG_BUF, &buf_info);
  assert(result == 0);
#endif

#else

  // On the host side, packet buffers must come from the per-channel
  // 'fast memory' region.
  void* wbuf = mmap(0, BUF_SIZE, PROT_READ | PROT_WRITE,
                    MAP_SHARED, pci_wfd, 0);
  assert(wbuf != MAP_FAILED);

#endif

  tilepci_xfer_req_t send_cmd;
  tilepci_xfer_comp_t comp;
  int cmd_size;
  cmd_size = sizeof(tilepci_xfer_req_t);
  int cpl_size;
  cpl_size = sizeof(tilepci_xfer_comp_t);

  while (!tunnel_should_reset)
  {
    ssize_t len = read(tun_fd, wbuf, BUF_SIZE);
    if (len < 0)
    {
      perror("can't read TUN device");
      break;
    }
    else if (len == 0)
    {
      fprintf(stderr, "Reading from TUN device returns 0\n");
      break;
    }

#if TILE_CHIP < 10
    send_cmd.addr = (void *)((unsigned long)wbuf);
#else
    send_cmd.addr = (uintptr_t)wbuf;
#endif
    send_cmd.len = len;
#if TILE_CHIP < 10
    send_cmd.flags = TILEPCI_SEND_EOP;
#endif 

    if (write(pci_wfd, &send_cmd, cmd_size) != cmd_size)
    {
      perror("can't write to PCI wdevice");
      break;
    }

    if (read(pci_wfd, &comp, cpl_size) != cpl_size)
    {
      perror("can't read from PCI wdevice");
      break;
    }

    if ((comp.len == 0) || (comp.flags & TILEPCI_CPL_RESET))
    {
      fprintf(stderr, "IP pcie-net send channel is broken\n");
      break;
    }
  }
  tunnel_should_reset = 1;
  pthread_exit(NULL);
}

void* receiver(void* arg)
{
  char *pci_rdev = (char *)arg;

  // Open the PCIs zero-copy channel.

  int pci_rfd = open(pci_rdev, O_RDWR);
  if (pci_rfd < 0)
  {
    fprintf(stderr, "can't open %s: %s\n", pci_rdev, strerror(errno));
    tunnel_should_reset = 1;
    pthread_exit(NULL);
  }

  // Forward traffic from the PCI channel to the TUN device.

#ifdef __tile__

  // On the tile side, packet buffers can come from any memory region,
  // but they must not cross a page boundary.
  void *rbuf = memalign(getpagesize(), BUF_SIZE);
  assert(rbuf != NULL);

#if TILE_CHIP >= 10
  // Register the buffer to IOTLB.
  tilegxpci_buf_info_t buf_info = {
    .va = (uintptr_t) rbuf,
    .size = BUF_SIZE,
  };

  int result = ioctl(pci_rfd , TILEPCI_IOC_REG_BUF, &buf_info);
  assert(result == 0);
#endif

#else

  // On the host side, packet buffers must come from the per-channel
  // 'fast memory' region.
  void* rbuf = mmap(0, BUF_SIZE, PROT_READ | PROT_WRITE,
                    MAP_SHARED, pci_rfd, 0);
  assert(rbuf != MAP_FAILED);

#endif

  tilepci_xfer_req_t recv_cmd;
  tilepci_xfer_comp_t comp;
  int cmd_size;
  cmd_size = sizeof(tilepci_xfer_req_t);
  int cpl_size;
  cpl_size = sizeof(tilepci_xfer_comp_t);

  while (!tunnel_should_reset)
  {
#if TILE_CHIP < 10
    recv_cmd.addr = rbuf;
#else
    recv_cmd.addr = (uintptr_t)rbuf;
#endif 
    recv_cmd.len = BUF_SIZE;
#if TILE_CHIP < 10
    recv_cmd.flags = TILEPCI_RCV_MUST_EOP;
#endif 

    if (write(pci_rfd, &recv_cmd, cmd_size) != cmd_size)
    {
      perror("can't write to PCI rdevice");
      break;
    }

    if (read(pci_rfd, &comp, cpl_size) != cpl_size)
    {
      perror("can't read from PCI rdevice");
      break;
    }

    int len = comp.len;
    if ((len == 0) || (comp.flags & TILEPCI_CPL_RESET))
    {
      fprintf(stderr, "IP pcie-net receive channel is broken\n");
      break;
    }

    int written = write(tun_fd, rbuf, len);
    if (written < 0)
    {
      perror("can't write to TUN device");
      break;
    }
    else if (written < len)
    {
      fprintf(stderr, "Insufficient bytes were written to TUN device\n");
      break;
    }
  }
  tunnel_should_reset = 1;
  pthread_exit(NULL);
}

int
main(int argc, char** argv)
{
#ifndef __tile__

  // Exec "EXE-64" if needed.
  struct utsname un;
  if (sizeof(long) == 4 && uname(&un) == 0 && !strcmp(un.machine, "x86_64"))
  {
    size_t len = strlen(argv[0]) + 3 + 1;
    char exe[len];
    snprintf(exe, len, "%s-64", argv[0]);
    argv[0] = exe;
    (void)execvp(exe, argv);
  }

#endif

  uint card = 0;
  uint stream = 1;
  const char* local_addr = NULL;
  const char* remote_addr = NULL;

  int i = 1;
  while (i < argc)
  {
    const char* arg = argv[i++];
    if (!strcmp(arg, "-c") && i < argc)
    {
      card = atoi(argv[i++]);
    }
    else if (!strcmp(arg, "-s") && i < argc)
    {
      stream = atoi(argv[i++]);
    }
    else if (!strcmp(arg, "-l") && i < argc)
    {
      local_addr = argv[i++];
    }
    else if (!strcmp(arg, "-r") && i < argc)
    {
      remote_addr = argv[i++];
    }
    else
    {
      usage();
    }
  }

  char pci_rdev[64];
  char pci_wdev[64];
#ifdef __tile__
#if TILE_CHIP < 10
  snprintf(pci_rdev, sizeof(pci_rdev), "/dev/hostpci/h2t/%d", stream);
  snprintf(pci_wdev, sizeof(pci_wdev), "/dev/hostpci/t2h/%d", stream);
#else
  snprintf(pci_rdev, sizeof(pci_rdev), "/dev/trio0-mac0/h2t/%d", stream);
  snprintf(pci_wdev, sizeof(pci_wdev), "/dev/trio0-mac0/t2h/%d", stream);
#endif
#else
#if TILE_CHIP < 10
  snprintf(pci_rdev, sizeof(pci_rdev), "/dev/tilepci%d/t2h/%d", card, stream);
  snprintf(pci_wdev, sizeof(pci_wdev), "/dev/tilepci%d/h2t/%d", card, stream);
#else
  snprintf(pci_rdev, sizeof(pci_rdev), "/dev/tilegxpci%d/t2h/%d", card, stream);
  snprintf(pci_wdev, sizeof(pci_wdev), "/dev/tilegxpci%d/h2t/%d", card, stream);
#endif
#endif

  char local_ip[64];
  if (local_addr == NULL)
  {
    snprintf(local_ip, sizeof(local_ip), "192.168.%d.%d",
             100 + card, IP_ADDR_LOCAL + stream);
    local_addr = local_ip;
  }

  char remote_ip[64];
  if (remote_addr == NULL)
  {
    snprintf(remote_ip, sizeof(remote_ip), "192.168.%d.%d",
             100 + card, IP_ADDR_REMOTE + stream);
    remote_addr = remote_ip;
  }

  // Open and configure the TUN device.

  tun_fd = open(TUN_DEV_NAME, O_RDWR);
  if (tun_fd < 0)
  {
    fprintf(stderr, "can't open %s: %s\n", TUN_DEV_NAME, strerror(errno));
    exit(1);
  }

  struct ifreq ifr;
  memset(&ifr, 0, sizeof(ifr));
  ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
  if (ioctl(tun_fd, TUNSETIFF, &ifr) < 0)
  {
    perror("can't configure TUN device");
    exit(1);
  }

  // Configure the network device.

  char cmdline[256];
  if (snprintf(cmdline, sizeof(cmdline),
               "/sbin/ifconfig %s %s pointopoint %s up",
               ifr.ifr_name, local_addr, remote_addr) >= sizeof(cmdline))
  {
    fprintf(stderr, "IP addresses too long\n");
    exit(1);
  }

  if (system(cmdline) != 0)
  {
    perror("can't ifconfig TUN device");
    exit(1);
  }

  pthread_t threads[2];
  pthread_create(&threads[0], NULL, sender, (void*)pci_wdev);
  pthread_create(&threads[1], NULL, receiver, (void*)pci_rdev);

  //
  // Normally, the sender is blocked on reading the TUN device while
  // the receiver is blocked on reading the PCIe channel. To reliably
  // exit this problem, we monitor the PCIe ZC channel's reset event
  // in the receiver thread and kills the sender thread when the channel
  // is reset.
  //
  if (pthread_join(threads[1], NULL))
    pthread_cancel(threads[0]);

  return 0;
}
