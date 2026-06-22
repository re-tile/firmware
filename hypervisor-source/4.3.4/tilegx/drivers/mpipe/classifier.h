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


// HACK: See "common/include/tilera.h".
#ifndef BEGIN_EXTERN_C
#ifdef __cplusplus
#define BEGIN_EXTERN_C extern "C" {
#define END_EXTERN_C }
#else
#define BEGIN_EXTERN_C
#define END_EXTERN_C
#include <stdbool.h>
#endif
#endif


#include <stdint.h>
#include <sys/types.h>



// TODO: Add some docs.

#ifndef __DOXYGEN__


#ifdef __tile__

#include <hv/iorpc.h>

#else

// HACK: This file is used on the host by the simulator and the standalone
// classifier simulator, which need definitions for these error codes.

#define GXIO_MPIPE_ERR_CLASSIFIER_TOO_BIG -1000
#define GXIO_MPIPE_ERR_CLASSIFIER_TOO_COMPLEX -1001
#define GXIO_MPIPE_ERR_CLASSIFIER_BAD_HEADER -1002
#define GXIO_MPIPE_ERR_CLASSIFIER_BAD_CONTENTS -1003
#define GXIO_MPIPE_ERR_CLASSIFIER_INVAL_SYMBOL -1004
#define GXIO_MPIPE_ERR_CLASSIFIER_INVAL_BOUNDS -1005
#define GXIO_MPIPE_ERR_CLASSIFIER_INVAL_RELOCATION -1006
#define GXIO_MPIPE_ERR_CLASSIFIER_UNDEF_SYMBOL -1007

#endif


// NOTE: The "section" is 0 for insn, 1 for data, 2 for regs, 3 for virt,
// 4 for magic, 5 for symbols, and 6 for relocs.


// We use (small) offsets into the "xtra" field of "classifier_info_t"
// instead of actual pointers because this takes up much less space,
// and because it allows RPC to function properly.
typedef uint16_t classifier_ptr_t;


// Flags from the classifier.
#define CLASSIFIER_FLAG_GLOBAL 0x01
#define CLASSIFIER_FLAG_EXTERN 0x02

// Flags from our current state.
#define CLASSIFIER_FLAG_DEFINED 0x80


typedef struct {

  classifier_ptr_t name_ptr;

  uint8_t flags;

  uint8_t section;

  uint16_t offset;

  uint16_t size;

} classifier_symbol_t;


typedef struct {

  uint8_t type;

  uint8_t section;

  uint16_t offset;

  uint16_t size;

  uint16_t symnum;

  int16_t symoff;

  int16_t addend;

} classifier_reloc_t;


typedef struct {

  uint8_t text[4096];
  uint8_t data[4096];
  uint8_t regs[25*2];

  bool blasted;

} classifier_blast_t;


typedef struct {

  uint8_t text[4096];
  uint8_t data[4096];
  uint8_t regs[25*2];

  classifier_ptr_t virt_ptr;
  uint16_t virt_size;

  classifier_ptr_t magic_ptr;
  uint16_t magic_size;

  classifier_ptr_t symbols_ptr;
  uint16_t num_symbols;

  classifier_ptr_t relocs_ptr;
  uint16_t num_relocs;

  classifier_ptr_t used;

  // NOTE: The first two bytes are never used.
  uint8_t xtra[52 * 1024 - 68];

} classifier_info_t;

#endif


BEGIN_EXTERN_C

//! Reads a big-endian value of a certain size (in bytes).
//
extern uint
classifier_read_uint(const uint8_t* addr, uint size);

//! Write a big-endian value of a certain size (in bytes).
//
extern void
classifier_write_uint(uint8_t* addr, uint size, uint val);

//! Get the size of a classifier symbol.
//
extern int
classifier_get_symbol_size(classifier_info_t* info, const char* name);

//! Set the offset and size of a classifier symbol.
//
extern int
classifier_set_symbol(classifier_info_t* info,
                      const char* name, uint16_t section,
                      uint16_t offset, uint16_t size);

//! Set the contents of a classifier symbol to some (big endian) data.
//
// ISSUE: Support "offset"?
//
extern int
classifier_set_memory(classifier_info_t* info,
                      const char* name,
                      const uint8_t* data, uint size);

//! Apply classifier relocations.
//
extern int
classifier_apply_relocs(classifier_info_t* info);

//! Parse a classifier binary blob.
//
extern int
classifier_parse(classifier_info_t* info, uint8_t* file, uint size);

END_EXTERN_C
