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

#ifndef __tile__
#ifndef USE_STDERR
#define USE_STDERR
#endif
#endif

#ifdef USE_STDERR
#include <stdio.h>
#endif

#include <string.h>

#ifdef __tile__
#include <arch/chip.h>
#endif

#include "classifier.h"


#define RELOC_TYPE_ADDRESS 1
#define RELOC_TYPE_DEREF 2

#define SECTION_TEXT 0
#define SECTION_DATA 1
#define SECTION_REGS 2
#define SECTION_VIRT 3
#define SECTION_MAGIC 4
#define SECTION_SYMBOLS 5
#define SECTION_RELOCS 6


//! Convert a "classifier_ptr_t" into an actual pointer.
#define CPD(I,P) ((void*)((I)->xtra + (P)))


//! Magic number in the file header.
static const uint32_t CLASSIFIER_BINARY_MAGIC =
  ('T' << 24) | ('L' << 16) | ('R' << 8) | ('C' << 0);


//! Which file format version number is this?
static const int CLASSIFIER_BINARY_VERSION_NUMBER = 3;


uint
classifier_read_uint(const uint8_t* addr, uint size)
{
  uint32_t ret = 0;
  for (uint i = 0; i < size; i++)
    ret = (ret << 8) | addr[i];
  return ret;
}


void
classifier_write_uint(uint8_t* addr, uint size, uint val)
{
  for (uint i = 0; i < size; i++)
  {
    addr[size - 1 - i] = val & 0xFF;
    val = val >> 8;
  }
}


static bool
valid_range(uint start, uint size, uint limit)
{
  return (start + size <= limit && start + size >= start);
}


//! Get the address for a given section/offset, verifying its size.
//
static uint8_t*
get_addr(classifier_info_t* info, uint section, uint offset, uint size)
{
  uint8_t* base;
  uint limit;

  switch (section)
  {
  case SECTION_TEXT:
    base = info->text;
    limit = sizeof(info->text);
    break;
  case SECTION_DATA:
    base = info->data;
    limit = sizeof(info->data);
    break;
  case SECTION_REGS:
    base = info->regs;
    limit = sizeof(info->regs);
    break;
  case SECTION_VIRT:
    base = CPD(info, info->virt_ptr);
    limit = info->virt_size;
    break;
  case SECTION_MAGIC:
    base = CPD(info, info->magic_ptr);
    limit = info->magic_size;
    break;
  default:
    return NULL;
  }

  if (!valid_range(offset, size, limit))
    return NULL;

  return base + offset;
}


static uint8_t*
get_symbol_addr(classifier_info_t* info, classifier_symbol_t* symbol,
                int offset, uint size)
{
  // FIXME: Document this vomit.
  bool is_text = (symbol->section == SECTION_TEXT); 
  offset += is_text ? symbol->offset * 4 : symbol->offset;
  return get_addr(info, symbol->section, offset, size);
}


static classifier_symbol_t*
find_symbol(classifier_info_t* info, const char* name)
{
  for (uint i = 0; i < info->num_symbols; i++)
  {
    classifier_symbol_t* symbols = CPD(info, info->symbols_ptr);
    classifier_symbol_t* symbol = symbols + i;
    if (!strcmp(CPD(info, symbol->name_ptr), name))
      return symbol;
  }
  return NULL;
}


int
classifier_get_symbol_size(classifier_info_t* info,
                           const char* name)
{
  classifier_symbol_t* symbol = find_symbol(info, name);
  return (symbol != NULL) ? symbol->size : 0;
}


int
classifier_set_symbol(classifier_info_t* info,
                      const char* name, uint16_t section,
                      uint16_t offset, uint16_t size)
{
  classifier_symbol_t* symbol = find_symbol(info, name);
  if (symbol == NULL)
    return GXIO_MPIPE_ERR_CLASSIFIER_INVAL_SYMBOL;

  if ((symbol->flags & CLASSIFIER_FLAG_EXTERN) == 0)
    return GXIO_MPIPE_ERR_CLASSIFIER_INVAL_SYMBOL;

  if (get_addr(info, section, offset, size) == NULL)
    return GXIO_MPIPE_ERR_CLASSIFIER_INVAL_BOUNDS;

  symbol->flags |= CLASSIFIER_FLAG_DEFINED;
  symbol->section = section;
  symbol->offset = offset;
  symbol->size = size;
  return 0;
}


int
classifier_set_memory(classifier_info_t* info,
                      const char* name,
                      const uint8_t* data, uint size)
{
  classifier_symbol_t* symbol = find_symbol(info, name);
  if (symbol == NULL)
    return GXIO_MPIPE_ERR_CLASSIFIER_INVAL_SYMBOL;

  if ((symbol->flags & CLASSIFIER_FLAG_GLOBAL) == 0)
    return GXIO_MPIPE_ERR_CLASSIFIER_INVAL_SYMBOL;

  if (size > symbol->size)
  {
#ifdef USE_STDERR
    fprintf(stderr, "Excessive size (%u > %u) in set_memory('%s').\n",
            size, symbol->size, name);
#endif
    return GXIO_MPIPE_ERR_CLASSIFIER_INVAL_BOUNDS;
  }

  uint8_t* addr = get_symbol_addr(info, symbol, 0, size);
  if (addr == NULL)
    return GXIO_MPIPE_ERR_CLASSIFIER_INVAL_BOUNDS;

  memcpy(addr, data, size);
  return 0;
}


int
classifier_apply_relocs(classifier_info_t* info)
{
  for (uint i = 0; i < info->num_relocs; i++)
  {
    classifier_reloc_t* relocs = CPD(info, info->relocs_ptr);
    classifier_reloc_t* reloc = relocs + i;

    uint8_t* dest = get_addr(info, reloc->section, reloc->offset, reloc->size);
    if (dest == NULL)
      return GXIO_MPIPE_ERR_CLASSIFIER_BAD_CONTENTS;

    classifier_symbol_t* symbols = CPD(info, info->symbols_ptr);
    classifier_symbol_t* symbol = symbols + reloc->symnum;

    if ((symbol->flags & CLASSIFIER_FLAG_EXTERN) != 0 &&
        (symbol->flags & CLASSIFIER_FLAG_DEFINED) == 0)
      return GXIO_MPIPE_ERR_CLASSIFIER_UNDEF_SYMBOL;

    uint val = 0;

    uint8_t* addr;

    switch (reloc->type)
    {
    case RELOC_TYPE_ADDRESS:
      val = symbol->offset + reloc->symoff;
      break;
    case RELOC_TYPE_DEREF:
      addr = get_symbol_addr(info, symbol, reloc->symoff, reloc->size);
      if (addr == NULL)
        return GXIO_MPIPE_ERR_CLASSIFIER_INVAL_RELOCATION;
      val = classifier_read_uint(addr, reloc->size) + reloc->addend;
      break;
    default:
      return GXIO_MPIPE_ERR_CLASSIFIER_BAD_CONTENTS;
    }

    classifier_write_uint(dest, reloc->size, val);
  }

  return 0;
}


// Allocate some memory in "xtra", and return the offset, or return 0.
//
static classifier_ptr_t
alloc(classifier_info_t* info, uint align, uint size)
{
  uint used = info->used;

  // FIXME: Clean this up.
  while (((uintptr_t)&info->xtra[used]) & (align - 1))
    used++;

  if (used + size > sizeof(info->xtra))
  {
#ifdef USE_STDERR
    fprintf(stderr, "Out of memory while parsing classifier.\n");
#endif
    return 0;
  }

  info->used = used + size;
  return used;
}


int
classifier_parse(classifier_info_t* info, uint8_t* file, uint size)
{
  // FIXME: Is this appropriate?
  memset(info, 0, sizeof(*info));

  // HACK: Skip two bytes so "zero" can indicate "failure".
  info->used = 2;

  if (size < 64)
  {
#ifdef USE_STDERR
    fprintf(stderr, "Invalid size %d in classifier, expected >= 64.\n", size);
#endif
    return GXIO_MPIPE_ERR_CLASSIFIER_BAD_HEADER;
  }

  uint32_t magic = classifier_read_uint(file + 0, 4);
  if (magic != CLASSIFIER_BINARY_MAGIC)
  {
#ifdef USE_STDERR
    fprintf(stderr, "Invalid magic number 0x%x in classifier, expected 0x%x.\n",
            magic, CLASSIFIER_BINARY_MAGIC);
#endif
    return GXIO_MPIPE_ERR_CLASSIFIER_BAD_HEADER;
  }

  uint16_t version = classifier_read_uint(file + 4, 2);
  if (version != CLASSIFIER_BINARY_VERSION_NUMBER)
  {
#ifdef USE_STDERR
    fprintf(stderr, "Invalid version number %d in classifier, expected %d.\n",
            version, CLASSIFIER_BINARY_VERSION_NUMBER);
#endif
    return GXIO_MPIPE_ERR_CLASSIFIER_BAD_HEADER;
  }

  uint16_t chip = classifier_read_uint(file + 6, 2);
  if (chip != TILE_CHIP)
  {
#ifdef USE_STDERR
    fprintf(stderr, "Invalid chip number %d in classifier, expected %d.\n",
            chip, TILE_CHIP);
#endif
    return GXIO_MPIPE_ERR_CLASSIFIER_BAD_HEADER;
  }

  uint32_t text_start = classifier_read_uint(file + 8, 4);
  uint32_t text_size  = classifier_read_uint(file + 12, 4);

  uint32_t data_start = classifier_read_uint(file + 16, 4);
  uint32_t data_size  = classifier_read_uint(file + 20, 4);

  uint32_t regs_start = classifier_read_uint(file + 24, 4);
  uint32_t regs_size  = classifier_read_uint(file + 28, 4);

  uint32_t virt_start = classifier_read_uint(file + 32, 4);
  uint32_t virt_size  = classifier_read_uint(file + 36, 4);

  uint32_t magic_start = classifier_read_uint(file + 40, 4);
  uint32_t magic_size  = classifier_read_uint(file + 44, 4);

  uint32_t symbols_start = classifier_read_uint(file + 48, 4);
  uint32_t symbols_size  = classifier_read_uint(file + 52, 4);

  uint32_t relocs_start = classifier_read_uint(file + 56, 4);
  uint32_t relocs_size  = classifier_read_uint(file + 60, 4);

  if (!valid_range(text_start, text_size, size) ||
      !valid_range(data_start, data_size, size) ||
      !valid_range(regs_start, regs_size, size) ||
      !valid_range(virt_start, virt_size, size) ||
      !valid_range(magic_start, magic_size, size) ||
      !valid_range(symbols_start, symbols_size, size) ||
      !valid_range(relocs_start, relocs_size, size))
  {
#ifdef USE_STDERR
    fprintf(stderr, "Corrupt section bounds in classifier.\n");
#endif
    return GXIO_MPIPE_ERR_CLASSIFIER_BAD_CONTENTS;
  }

  if (text_size > sizeof(info->text) ||
      data_size > sizeof(info->data) ||
      regs_size > sizeof(info->regs))
  {
#ifdef USE_STDERR
    fprintf(stderr, "Excessive text/data/regs in classifier.\n");
#endif
    return GXIO_MPIPE_ERR_CLASSIFIER_BAD_CONTENTS;
  }

  if (virt_size >= 65536 ||
      magic_size >= 65536)
  {
#ifdef USE_STDERR
    fprintf(stderr, "Excessive virt/magic in classifier.\n");
#endif
    return GXIO_MPIPE_ERR_CLASSIFIER_BAD_CONTENTS;
  }


  // Latch text/data/regs.
  memcpy(info->text, file + text_start, text_size);
  memcpy(info->data, file + data_start, data_size);
  memcpy(info->regs, file + regs_start, regs_size);

  // Save virt.
  info->virt_size = virt_size;
  info->virt_ptr = alloc(info, 1, virt_size);
  if (info->virt_ptr == 0)
    return GXIO_MPIPE_ERR_CLASSIFIER_TOO_BIG;
  memcpy(CPD(info, info->virt_ptr), file + virt_start, virt_size);

  // Save magic.
  info->magic_size = magic_size;
  info->magic_ptr = alloc(info, 1, magic_size);
  if (info->magic_ptr == 0)
    return GXIO_MPIPE_ERR_CLASSIFIER_TOO_BIG;
  memcpy(CPD(info, info->magic_ptr), file + magic_start, magic_size);


  uint jump;
  uint8_t* scan;


  // Process "symbols".

  jump = 1 + 1 + 2 + 2;

  // Count, and verify, the symbols.
  for (uint k = 0; k < symbols_size; k++)
  {
    if (k + 1 + jump > symbols_size)
    {
#ifdef USE_STDERR
      fprintf(stderr, "Corrupt symbols in classifier.\n");
#endif
      return GXIO_MPIPE_ERR_CLASSIFIER_BAD_CONTENTS;
    }

    if (file[symbols_start + k] == '\0')
    {
      info->num_symbols++;
      k += jump;
    }
  }

  info->symbols_ptr =
    alloc(info, __alignof__(classifier_symbol_t),
          info->num_symbols * sizeof(classifier_symbol_t));
  if (info->symbols_ptr == 0)
    return GXIO_MPIPE_ERR_CLASSIFIER_TOO_BIG;

  scan = file + symbols_start;

  for (uint i = 0; i < info->num_symbols; i++)
  {
    classifier_symbol_t* symbols = CPD(info, info->symbols_ptr);
    classifier_symbol_t* symbol = symbols + i;

    uint len = strlen((char*)scan) + 1;
    symbol->name_ptr = alloc(info, 1, len);
    if (symbol->name_ptr == 0)
      return GXIO_MPIPE_ERR_CLASSIFIER_TOO_BIG;
    memcpy(CPD(info, symbol->name_ptr), scan, len);

    scan += len;

    symbol->flags = scan[0];
    symbol->section = scan[1];
    symbol->offset = classifier_read_uint(scan + 2, 2);
    symbol->size = classifier_read_uint(scan + 4, 2);

    if (symbol->section > SECTION_VIRT)
      return GXIO_MPIPE_ERR_CLASSIFIER_BAD_CONTENTS;

    scan += jump;
  }


  // Process "relocs".

  jump = 1 + 1 + 2 + 1 + 2 + 2 + 2;

  info->num_relocs = relocs_size / jump;

  info->relocs_ptr = 
    alloc(info, __alignof__(classifier_reloc_t),
          info->num_relocs * sizeof(classifier_reloc_t));
  if (info->relocs_ptr == 0)
    return GXIO_MPIPE_ERR_CLASSIFIER_TOO_BIG;

  scan = file + relocs_start;

  for (uint i = 0; i < info->num_relocs; i++)
  {
    classifier_reloc_t* relocs = CPD(info, info->relocs_ptr);
    classifier_reloc_t* reloc = relocs + i;

    reloc->type = scan[0];
    reloc->section = scan[1];
    reloc->offset = classifier_read_uint(scan + 2, 2);
    reloc->size = scan[4];
    reloc->symnum = classifier_read_uint(scan + 5, 2);
    reloc->symoff = classifier_read_uint(scan + 7, 2);
    reloc->addend = classifier_read_uint(scan + 9, 2);

    if (reloc->section > SECTION_MAGIC)
      return GXIO_MPIPE_ERR_CLASSIFIER_BAD_CONTENTS;

    if (reloc->symnum >= info->num_symbols)
      return GXIO_MPIPE_ERR_CLASSIFIER_BAD_CONTENTS;

    scan += jump;
  }


  return 0;
}
