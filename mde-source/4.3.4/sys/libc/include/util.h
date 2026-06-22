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
 * Routines to perform utility operations.
 *
 * @addtogroup libc_util
 * @{
 *
 * C library extensions that provide a variety of utility operations.
 *
 * To include these routines, do:
 *
 * @code
 * #include <util.h>
 * @endcode
 */

#ifndef _SYS_LIBC_UTIL_H
#define _SYS_LIBC_UTIL_H

#include <stdarg.h>

/** Panic the system.
 * @param fmt printf-style format (followed by printf-style arguments).
 */
void panic(const char* fmt, ...)
  __attribute__((__format__(__printf__,1,2),__noreturn__));

/** Start to panic the system; used when you need multi-stage panic output,
 *  like a panic message followed by a register dump.  This routine prints
 *  the initial panic message, but does not actually halt; the caller is
 *  expected to call panic_end() when any additional output is finished.
 * @param fmt printf-style format (followed by printf-style arguments).
 */
void panic_start(const char* fmt, ...)
  __attribute__((__format__(__printf__,1,2)));

/** Finish panicking the system.
 */
void panic_end(void)
  __attribute__((__noreturn__));

#ifndef __DOXYGEN_SHIP__
extern char* tprintf_prefix;
extern char* panic_prefix;
#endif

/** Exit the program.
 * @param status Exit status.
 */
void exit(int status) __attribute__((__noreturn__));

/** Panics with an abort message. Needed for libgcc.a. */
void abort(void) __attribute__((__noreturn__));

/** Convert a string to an integer, a la strtol().
 * We return an error status (or zero for success), and the
 * actual integer is returned through the required val_p pointer.
 */
int str2l(const char* nptr, char** endptr, int base, long* val_p);

/** Convert a string to a long long integer, a la strtoll().
 * We return an error status (or zero for success), and the
 * actual integer is returned through the required val_p pointer.
 */
int str2ll(const char* nptr, char** endptr, int base, long long* val_p);

/** Produce (x != 0) but hint to the compiler it is likely to be 1. */
#define likely(x)   __builtin_expect((x) != 0, 1)

/** Produce (x != 0) but hint to the compiler it is likely to be 0. */
#define unlikely(x) __builtin_expect((x) != 0, 0)

/** Find the minimum of two values.
 * @param a First value.
 * @param b Second value.
 * @return Minimum of a and b. */
#define min(a, b) \
  ({ \
    typeof (a) _a = (a); \
    typeof (b) _b = (b); \
    _a < _b ? _a : _b; \
  })

/** Find the maximum of two values.
 * @param a First value.
 * @param b Second value.
 * @return Maximum of a and b. */
#define max(a, b) \
  ({ \
    typeof (a) _a = (a); \
    typeof (b) _b = (b); \
    _a > _b ? _a : _b; \
  })

/** Find the absolute value of a value.
 * @param a Value.
 * @return a if a >= 0, else -a. */
#define abs(a) \
  ({ \
    typeof (a) _a = (a); \
    _a >= 0 ? _a : -_a; \
  })

/** Swap two values.
 * @param a Value.
 * @param b Another value. */
#define swap(a, b) \
  ({ \
    typeof (a) _tmp = (a); \
    (a) = (b); \
    (b) = _tmp; \
  })

//
// Convert cpu format values to/from little-endian / big-endian.
//
#ifdef __BIG_ENDIAN__

/** Convert a 2-byte value to little-endian format. */
#define cpu_to_le16(x) (__builtin_bswap32(x) >> 16)
/** Convert a 4-byte value to little-endian format. */
#define cpu_to_le32(x) __builtin_bswap32(x)
/** Convert a 8-byte value to little-endian format. */
#define cpu_to_le64(x) __builtin_bswap64(x)

/** Convert a 2-byte value to big-endian format. */
#define cpu_to_be16(x) (x)
/** Convert a 4-byte value to big-endian format. */
#define cpu_to_be32(x) (x)
/** Convert a 8-byte value to big-endian format. */
#define cpu_to_be64(x) (x)

#else

/** Convert a 2-byte value to little-endian format. */
#define cpu_to_le16(x) (x)
/** Convert a 4-byte value to little-endian format. */
#define cpu_to_le32(x) (x)
/** Convert a 8-byte value to little-endian format. */
#define cpu_to_le64(x) (x)

/** Convert a 2-byte value to big-endian format. */
#define cpu_to_be16(x) (__builtin_bswap32(x) >> 16)
/** Convert a 4-byte value to big-endian format. */
#define cpu_to_be32(x) __builtin_bswap32(x)
/** Convert a 8-byte value to big-endian format. */
#define cpu_to_be64(x) __builtin_bswap64(x)

#endif

/** Convert a 2-byte value from little-endian format. */
#define le16_to_cpu cpu_to_le16
/** Convert a 4-byte value from little-endian format. */
#define le32_to_cpu cpu_to_le32
/** Convert a 8-byte value from little-endian format. */
#define le64_to_cpu cpu_to_le64

/** Convert a 2-byte value from big-endian format. */
#define be16_to_cpu cpu_to_be16
/** Convert a 4-byte value from big-endian format. */
#define be32_to_cpu cpu_to_be32
/** Convert a 8-byte value from big-endian format. */
#define be64_to_cpu cpu_to_be64

#endif /* _SYS_LIBC_UTIL_H */

/** @} */
