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

//! @file
//!
//! This file contains definitions that are used internally and are
//! always bound to symbols defined inside libtmc.

#include <tmc/alloc.h>

#define strong_hidden_alias(name, aliasname) \
  extern __typeof (name) aliasname \
    __attribute__ ((alias (#name), __visibility__ ("hidden")));

extern __typeof (tmc_alloc_map_at) tmc_alloc_map_at_internal;
extern __typeof (tmc_alloc_unmap) tmc_alloc_unmap_internal;
extern __typeof (tmc_alloc_remap) tmc_alloc_remap_internal;

static __inline void*
tmc_alloc_map_internal(const tmc_alloc_t* alloc, size_t length)
{
  return tmc_alloc_map_at_internal(alloc, 0, length);
}
