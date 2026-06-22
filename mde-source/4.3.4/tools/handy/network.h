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

#ifndef TOOLS_HANDY_NETWORK_H
#define TOOLS_HANDY_NETWORK_H

#include "common.h"

BEGIN_EXTERN_C


//! Get a pointer to the (cached) full hostname of the current host, or die.
extern const char*
fullhostname_or_die(void);


//! Create a listener on port "*port", or die. If "*port" is zero, then
//! pick a random ephemeral port, and set "*port" to the actual port which
//! was used.  The "reuse address" and "close on exec" flags will be set on
//! the listener.
extern int
simple_listen(uint16_t* port, int backlog);

//! Accept a connection from a listener, or die.  The "blocking" and
//! "delaying" flags will be cleared on the connection.
extern int
simple_accept(int listener);

//! Like @ref simple_connect, but if the connection fails for reasons
//! related to "connection refused", then just return -1 (setting errno).
extern int
simple_connect_aux(const char* host, uint16_t port);

//! Connect to the given host and port, or die.  If the host is NULL, then
//! use "LOOPBACK".  The "blocking" and "delaying" flags will be cleared on
//! the connection.
extern int
simple_connect(const char* host, uint16_t port);

//! Like @ref simple_connect_string, but if the connection fails for reasons
//! related to "connection refused", then just return -1 (setting errno).
extern int
simple_connect_string_aux(const char* portspec);

//! Like @ref simple_connect, but taking a string of the form "port" or
//! "host:port".
extern int
simple_connect_string(const char* portspec);


END_EXTERN_C

#endif /* !TOOLS_HANDY_NETWORK_H */
