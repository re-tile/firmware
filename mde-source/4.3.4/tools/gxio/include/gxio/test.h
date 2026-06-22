/*
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
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

#ifndef _GXIO_TEST_H_
#define _GXIO_TEST_H_

/**
 * @file
 *
 * IO RPC framework test driver.
 */

#include <stdint.h>
#include <stddef.h>
#include <hv/drv_test_intf.h>
#include <hv/iorpc.h>

#include <gxio/common.h>

__BEGIN_DECLS

/** A context object used to manage TEST hardware resources. */
typedef struct {

  /** File descriptor for HV access. */
  int fd;

} gxio_test_context_t;


/** Initialize the test driver. */
int gxio_test_init(gxio_test_context_t* context);

/** Close the test driver, reseting all internal state. */
int gxio_test_release(gxio_test_context_t* context);

/** Write a virtual register. */
int gxio_test_write_reg(gxio_test_context_t* context,
                        unsigned int index, unsigned long value);

/** Read a virtual register. */
int gxio_test_read_reg(gxio_test_context_t* context,
                       unsigned int index, unsigned long* value_out);

/** Reset all virtual registers to zero. */
int gxio_test_reset_regs(gxio_test_context_t* context);

/** Read the current value of all registers. */
int gxio_test_read_all_regs(gxio_test_context_t* context,
                            struct test_iorpc_regs *regs_out);

/** Map a 64kB aligned buffer. */
int gxio_test_map_large_buffer(gxio_test_context_t* context,
                               void* mem, size_t size, unsigned int slot);

/** Map a 4kB aligned buffer. */
int gxio_test_map_small_buffer(gxio_test_context_t* context,
                               void* mem, size_t size, unsigned int slot);

/** Map a self-size aligned buffer. */
int gxio_test_map_self_size_buffer(gxio_test_context_t* context,
                                   void* mem, size_t size, unsigned int slot);

/** Map an arbitrarily aligned buffer. */
int gxio_test_map_buffer(gxio_test_context_t* context,
                         void* mem, size_t size, unsigned int slot);

/** Read back the translated memory parameters for the last mapped buffer. */
int gxio_test_read_buffer_params(gxio_test_context_t* context,
                                 unsigned int index,
                                 uint64_t* pa, size_t* size,
                                 struct iorpc_mem_attr* attr);

/** Write the entire large data array. */
int gxio_test_write_data_array(gxio_test_context_t* context,
                               struct test_data_array data);

/** Read back the entire large data array. */
int gxio_test_read_data_array(gxio_test_context_t* context,
                              struct test_data_array *data_out);

/** Write part of the data array, starting at offset 0. */
int gxio_test_write_data_array_segment(gxio_test_context_t* context,
                                       const void* data, size_t data_size);

/** Write part of the data array, with offset. */
int gxio_test_write_data_array_segment_ext(gxio_test_context_t* context,
                                           unsigned int offset,
                                           const void* data,
                                           size_t data_size);

/** Read part of the data array, starting at offset 0. */
int gxio_test_read_data_array_segment(gxio_test_context_t* context,
                                      void* data, size_t data_size);

/** Read part of the data array, with offset and resulting size. */
int gxio_test_read_data_array_segment_ext(gxio_test_context_t* context,
                                          unsigned int offset,
                                          size_t *actually_read,
                                          void* data, size_t data_size);

__END_DECLS

#endif /* !_GXIO_TEST_H_ */
