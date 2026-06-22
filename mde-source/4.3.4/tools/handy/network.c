// Copyright 2014 Tilera Corporation. All Rights Reserved.
//
//   The source code contained or described herein and all documents
//   related to the source code ("Material") are owned by Tilera
//   Corporation or its suppliers or licensors.  Title to the Material
//   remains with Tilera Corporation or its suppliers and licensors.
//   The software is licensed under the Tilera MDE License.
//
//   However, Licensee may elect to use this file under the terms of the
//   GNU Lesser General Public License version 2.1 as published by the
//   Free Software Foundation and appearing in the file src/COPYING.LIB
//   in the MDE distribution.  Please review the following information to
//   ensure the GNU Lesser General Public License version 2.1 requirements
//   will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.

#include "network.h"

#include "message.h"
#include "various.h"
#include "Buffer.h"

#include <netdb.h>
#include <sys/socket.h>


#ifdef __tile__
// HACK: Apparently "HOST_NAME_MAX" may not be defined.
#ifndef HOST_NAME_MAX
//! Maximum host name size in bytes (not including the terminating NUL).
#define HOST_NAME_MAX 255
#endif
#endif


const char*
fullhostname_or_die(void)
{

#ifdef __NEWLIB__

  return "localhost";

#else

  static const char* fullhostname_value;

  if (fullhostname_value == NULL)
  {
    int saved_errno = errno;
    char name[HOST_NAME_MAX + 1];
    if (gethostname(name, sizeof(name)) != 0)
      punt_with_errno("Failure in 'gethostname()'");
    name[HOST_NAME_MAX] = '\0';

    struct hostent *he = gethostbyname(name);

    if (he != NULL)
    {
      fullhostname_value = strdup_or_die(he->h_name);
    }
    else
    {
      // On misconfigured machines without name service, we may fail to
      // find the full name, so emit a warning, and just use "name".
      warn("Failure in 'gethostbyname(\"%s\")': %s",
           name, hstrerror(h_errno));
      warn("Please contact your network administrator to fix this problem.");
      fullhostname_value = strdup_or_die(name);
    }
    errno = saved_errno;
  }

  return fullhostname_value;

#endif

}


int
simple_listen(uint16_t* port, int backlog)
{
  // Create a listener.
  int one = 1;
  int listener_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if ((listener_fd < 0) ||
      setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR,
                 (void*)&one, sizeof(one)) != 0)
  {
    punt_with_errno("Failure in 'simple_listen(%d)'", *port);
  }

  // Listen on the given port.
  struct sockaddr_in addr = { 0 };
  addr.sin_family = PF_INET;
  addr.sin_port = htons(*port);
  addr.sin_addr.s_addr = INADDR_ANY;
  if (bind(listener_fd, (struct sockaddr*) &addr, sizeof(addr)) != 0 ||
      listen(listener_fd, backlog) != 0)
  {
    punt_with_errno("Failure in 'simple_listen(%d)'", *port);
  }

  // Acquire the actual port being used.
  if (*port == 0)
  {
    socklen_t addrlen = sizeof(addr);
    if (getsockname(listener_fd, (struct sockaddr*) &addr, &addrlen) != 0)
      punt_with_errno("Failure in 'simple_listen(%d)'", *port);
    if (addrlen != sizeof(addr))
      punt("Failure in 'simple_listen(%d)'.", *port);
    *port = ntohs(addr.sin_port);
  }

  // Default to "close_on_exec".
  set_close_on_exec_or_die(listener_fd, true);

  return listener_fd;
}


int
simple_accept(int listener_fd)
{
  int fd;

  while (true)
  {
    fd = accept(listener_fd, NULL, NULL);

    if (fd >= 0)
      break;

    if (errno == EINTR)
      continue;

    punt_with_errno("Failure in 'accept()'");
  }

  set_blocking_or_die(fd, false);
  set_delaying_or_die(fd, false);

  return fd;
}


int
simple_connect_aux(const char* host, uint16_t port)
{
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = PF_INET;
  addr.sin_port = htons(port);

  if (host != NULL)
  {

#ifdef __NEWLIB__

    punt("Failure in 'gethostbyname(\"%s\")': %s.",
         host, strerror(ENOSYS));

#else

    struct hostent* hostent = gethostbyname(host);
    if (hostent == NULL)
      punt("Failure in 'gethostbyname(\"%s\")'.", host);
    //--assert(hostent->h_length == sizeof(struct in_addr));
    memcpy(&addr.sin_addr.s_addr, hostent->h_addr, hostent->h_length);

#endif

  }
  else
  {
    // Basically "localhost", but optimized.
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  }

  int fd = socket(PF_INET, SOCK_STREAM, 0);
  if (fd < 0)
    punt_with_errno("Failure in 'socket()");

  while (true)
  {
    if (connect(fd, (struct sockaddr*) &addr, sizeof(addr)) == 0)
      break;

    if (errno == EINTR)
      continue;

    close_or_die(fd);
    return -1;
  }

  set_blocking_or_die(fd, false);
  set_delaying_or_die(fd, false);

  return fd;
}


int
simple_connect(const char* host, uint16_t port)
{
  int fd = simple_connect_aux(host, port);

  if (fd < 0)
    punt_with_errno("Failure in 'simple_connect(\"%s\", %u)'", host, port);

  return fd;
}


int
simple_connect_string_aux(const char* portspec)
{
  // Assume "port" notation.
  char* host = NULL;
  const char* port = portspec;

  // Accept "host:port" notation.
  const char* colon = strrchr(port, ':');
  if (colon != NULL)
  {
    size_t len = colon - port;
    host = alloca(len + 1);
    strncpy(host, port, len);
    host[len] = '\0';
    port = colon + 1;
  }

  // Analyze the port (ala "atou16_or_die()").
  char* end;
  unsigned long val = strtoul(port, &end, 10);
  if (*end != '\0' || end == port || val > 65535 || val == 0)
    punt("Invalid network specifier '%s' (use '[HOST:]PORT').", portspec);

  return simple_connect_aux(host, (uint16_t)val);
}


int
simple_connect_string(const char* portspec)
{
  int fd = simple_connect_string_aux(portspec);

  if (fd < 0)
    punt_with_errno("Failure in 'simple_connect_string(\"%s\")'", portspec);

  return fd;
}


