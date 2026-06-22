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

#ifndef TOOLS_HANDY_COMMON_H
#define TOOLS_HANDY_COMMON_H

//! Enable various extensions.
#define _GNU_SOURCE 1

#ifndef NELEM
/** Returns the number of elements in array a. */
#define NELEM(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifdef __cplusplus

// Easy way to mark C code.
#define BEGIN_EXTERN_C extern "C" {
#define END_EXTERN_C }

#else // !defined(__cplusplus)

// Easy way to mark C code.
#define BEGIN_EXTERN_C
#define END_EXTERN_C

// Support the "bool" type.
#include <stdbool.h>

#endif

// Support all the basic "int" types.
#include <stdint.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/time.h>


// Yield a string which contains the value of the argument.
// This double-nesting of macros is necessary to turn the value,
// not the name, of the macro argument into a string.
#define STRINGIFY2(s) #s
#define STRINGIFY(s) STRINGIFY2(s)


#endif /* !TOOLS_HANDY_COMMON_H */
