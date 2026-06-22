/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
 *
 *   The source code contained or described herein and all documents
 *   related to the source code ("Material") are owned by Tilera
 *   Corporation or its suppliers or licensors.  Title to the Material
 *   remains with Tilera Corporation or its suppliers and licensors.
 *   The software is licensed under the Tilera MDE License.
 *
 *   However, Licensee may elect to use this file under the terms of the
 *   GNU Lesser General Public License version 2.1 as published by the
 *   Free Software Foundation and appearing in the file src/COPYING.LIB
 *   in the MDE distribution.  Please review the following information to
 *   ensure the GNU Lesser General Public License version 2.1 requirements
 *   will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
 */

#ifndef _GXIO_COMMON_H_
#define _GXIO_COMMON_H_

/**
 * @file
 * Routines shared between the various GXIO device components.
 */

#include <hv/iorpc.h>


#include <stdbool.h>
#include <features.h>
#include <stdint.h>
#include <arch/atomic.h>
#include <arch/inline.h>

__BEGIN_DECLS

/**
 * @addtogroup gxio_error
 * @{
 *
 * Most gxio_ API calls return 0 on success or a negative error code
 * on failure.  This section lists all the error codes exported by the
 * gxio_ API.
 */

/** Convert a GXIO error code to a string. */
const char* gxio_strerror(int error);

/** @} */

#ifndef __DOXYGEN__

#ifndef __BIG_ENDIAN__

#define MMIO_READ_WRITE(type, suffix)                   \
static __USUALLY_INLINE type                            \
__gxio_mmio_read ## suffix (void* addr)                 \
{                                                       \
  return *((volatile type*)addr);                       \
}                                                       \
                                                        \
static __USUALLY_INLINE void                            \
__gxio_mmio_write ## suffix(void* addr, type val)       \
{                                                       \
  *((volatile type*)addr) = val;                        \
}

MMIO_READ_WRITE(uint64_t, 64);
MMIO_READ_WRITE(uint32_t, 32);
MMIO_READ_WRITE(uint16_t, 16);
MMIO_READ_WRITE(uint8_t, 8);

#undef MMIO_READ_WRITE

#else

static __USUALLY_INLINE uint8_t
__gxio_mmio_read8(void* addr)
{
  return *((volatile uint8_t*)addr);
}

static __USUALLY_INLINE uint16_t
__gxio_mmio_read16(void* addr)
{
  return __builtin_bswap32(*((volatile uint16_t*)addr)) >> 16;
}

static __USUALLY_INLINE uint32_t
__gxio_mmio_read32(void* addr)
{
  return __builtin_bswap32(*((volatile uint32_t*)addr));
}

static __USUALLY_INLINE uint64_t
__gxio_mmio_read64(void* addr)
{
  return __builtin_bswap64(*((volatile uint64_t*)addr));
}

static __USUALLY_INLINE void
__gxio_mmio_write8(void* addr, uint8_t val)
{
  *((volatile uint8_t*)addr) = val;
}

static __USUALLY_INLINE void
__gxio_mmio_write16(void* addr, uint16_t val)
{
  *((volatile uint16_t*)addr) = __builtin_bswap32(val) >> 16;
}

static __USUALLY_INLINE void
__gxio_mmio_write32(void* addr, uint32_t val)
{
  *((volatile uint32_t*)addr) = __builtin_bswap32(val);
}

static __USUALLY_INLINE void
__gxio_mmio_write64(void* addr, uint64_t val)
{
  *((volatile uint64_t*)addr) = __builtin_bswap64(val);
}

#endif /* __BIG_ENDIAN__ */

/* Default size is 64-bit. */
#define __gxio_mmio_read(addr) __gxio_mmio_read64(addr)
#define __gxio_mmio_write(addr, val) __gxio_mmio_write64(addr, val)

#endif /* !__DOXYGEN__ */

__END_DECLS


#endif /* !_GXIO_COMMON_H_ */
