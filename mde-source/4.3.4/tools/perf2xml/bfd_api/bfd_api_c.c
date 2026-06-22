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

// ============================================================================
// bfdtest.c -- BFD library tests
// ============================================================================

#include "bfd_api.h"

// C/C++ includes
#include <stdio.h>  // printf
#include <stdlib.h> // malloc/free
#include <string.h> // strdup
#include <limits.h> // CHAR_BIT

// custom includes


// ----------------------------------------------------------------------------
// useful cleanup defines
// ----------------------------------------------------------------------------

#define SAFE_FREE(VAR)      if (VAR != NULL) { free(VAR); VAR = NULL; }
#define SAFE_BFD_CLOSE(VAR) if (VAR != NULL) { bfd_close(VAR); VAR = NULL; }


// ----------------------------------------------------------------------------
// bfd_open_object_file
// ----------------------------------------------------------------------------

/** Opens bfd file, allocates and returns bfd_file object. */
bfd_file*
bfd_open_object_file(const char* pathname)
{
  bfd_file* result = NULL;

  // Call bfd_init(), just to be sure.
  // TODO: Currently, this function actually does nothing.
  // If it ever matters whether it's called multiple times,
  // we may need to move this call elsewhere.
  bfd_init();

  // Create BFD data structure for file.
  bfd* abfd = bfd_openr(pathname, NULL);
  if (abfd == NULL)
  {
    printf("bfd_open_object_file: can't open file: %s\n", pathname);
    return result;
  }

  // Get file object format (aka target type), e.g. "elf64-bigtilegx".

  // Confirm that it's an object file.
  // TODO: do we want to extend this to support opening archives, etc.?
  if (! bfd_check_format(abfd, bfd_object))
  {
    printf("bfd_open_object_file: not an executable object file: %s\n", 
           pathname);
    SAFE_BFD_CLOSE(abfd);
    return result;
  }

  // Read symbol table.
  if ((bfd_get_file_flags(abfd) & HAS_SYMS) == 0)
  {
    printf("bfd_open_object_file: "
           "object file does not have symbol information: %s\n",
           pathname);
    SAFE_BFD_CLOSE(abfd);
    return result;
  }

  unsigned int symbol_size = 0;
  long symbol_count = 0;
  asymbol** symbol_table = NULL;
  int symbols_dynamic = 0;

  int load_symbols = (bfd_get_file_flags(abfd) & HAS_SYMS) ? 1 : 0;
  int use_minisymbols = 1;

  if (load_symbols)
  {
    if (use_minisymbols)
    {
      symbol_count =
        bfd_read_minisymbols(abfd, FALSE,
                             (void *)&(symbol_table), &symbol_size);
      if (symbol_count == 0)
      {
        symbols_dynamic = 1;
        symbol_count =
          bfd_read_minisymbols(abfd, TRUE,
                               (void *)&(symbol_table), &symbol_size);
      }
    }
    else
    {
      long space_needed = bfd_get_symtab_upper_bound(abfd);
      if (space_needed > 0)
      {
        symbol_table = (asymbol**) malloc(space_needed);
      }
      symbol_count = bfd_canonicalize_symtab(abfd, symbol_table);
    }
  }

  if (symbol_count < 0)
  {
    printf("bfd_open_object_file: could not read symbol table for: %s\n",
           pathname);
    SAFE_BFD_CLOSE(abfd);
    return result;
  }

  result = malloc(sizeof(bfd_file));
  memset(result, 0, sizeof(bfd_file));
  result->pathname        = strdup(pathname);
  result->target          = strdup(abfd->xvec->name);
  result->symbol_table    = symbol_table;
  result->symbol_count    = symbol_count;
  result->symbol_size     = symbol_size;
  result->symbols_dynamic = symbols_dynamic;
  result->abfd            = abfd;

  return result;
}


// ----------------------------------------------------------------------------
// bfd_close_object_file
// ----------------------------------------------------------------------------

/** Closes bfd file and deallocates bfd_file object. */
void
bfd_close_object_file(bfd_file* objfile)
{
  SAFE_FREE(objfile->pathname);
  SAFE_FREE(objfile->target);
  SAFE_FREE(objfile->symbol_table);
  SAFE_BFD_CLOSE(objfile->abfd);

  free(objfile);
}

// ----------------------------------------------------------------------------
// bfd_find_address_in_section
// ----------------------------------------------------------------------------

/** Called from bfd_addr2line to check symbol_data.address against
    each section. */
void
bfd_find_address_in_section(bfd      *abfd,
                            asection *section,
                            void     *arg)
{
  // Get symbol_data structure we passed to bfd_map_over_sections.
  symbol_data *sd = (symbol_data*) arg; 

  // We can't "break" out of the bfd_map_over_sections() loop,
  // so as soon as the symbol is found in a section,
  // we do nothing for any subsequent section(s).
  if (sd->found) return;

  // Skip sections that are never even allocated when the program is run.
  if ((bfd_get_section_flags(abfd, section) & SEC_ALLOC) == 0)
    return;

  // Get start bfd_vma of the section.
  bfd_vma start_vma = bfd_get_section_vma(abfd, section);

  // If address is lower than section start, skip this section.
  if (sd->vma < start_vma) return;

  // Get section's size and end bfd_vma of the section.
  bfd_size_type size = bfd_get_section_size(section);
  bfd_vma end_vma = start_vma + size;

  // If address is higher than section end, skip this section.
  if (sd->vma >= end_vma) return;

  // Ask BFD library to convert section offset to source line.
  sd->offset = sd->vma - start_vma;
  sd->found = bfd_find_nearest_line(abfd,
                                    section, 
                                    sd->symbol_table,
                                    sd->offset,
                                    &sd->filename,
                                    &sd->functionname, 
                                    &sd->line);
}


// ----------------------------------------------------------------------------
// bfd_addr2line
// ----------------------------------------------------------------------------

/** Returns function, source file, and source line information
    for specified address. */
int
bfd_addr2line(bfd_file*     objfile,
              void*         address,
              int           demangle,
              char*         function_name,
              char*         source_file,
              unsigned int* source_line)
{
  int result = -1;

  bfd* abfd = objfile->abfd;
  if (abfd == NULL) return result;

  asymbol** symbol_table = objfile->symbol_table;
  if (symbol_table == NULL) return result;

  // Convert provided address into a bfd_vma.
#define ADDR_BUF_LEN ((CHAR_BIT/4)*(sizeof(void*))+1)
  char addr[ADDR_BUF_LEN+1] = {0};
  sprintf(addr, "%p", address);

  // Initialize symbol info structure.
  symbol_data sd = {0}; 
  sd.symbol_table = symbol_table;
  sd.vma = bfd_scan_vma(addr, NULL, 16);
  sd.size = 0;
  sd.found = FALSE;

  // Walk sections looking for symbol containing this vma.
  bfd_map_over_sections(abfd, bfd_find_address_in_section, &sd);

  // TODO: handle unwinding inlines, etc.

  if (! sd.found)
  {
    snprintf(function_name, BFD_NAME_MAX, "pc=0x%llx",
             (unsigned long long) sd.vma);
    snprintf(source_file,   BFD_FILE_MAX, "??");
    *source_line = 0;
  }
  else // sd.found
  {
    const char* mangled = sd.functionname;
    char* demangled = NULL;
    if (demangle)
    {
      demangled = bfd_demangle(abfd, mangled, DMGL_ANSI | DMGL_PARAMS);
      if (demangled != NULL)
      {
        sd.functionname = demangled;
      }
    }

    snprintf(function_name, BFD_NAME_MAX, "%s", sd.functionname);
    snprintf(source_file,   BFD_FILE_MAX, "%s", sd.filename);
    *source_line = sd.line;

    if (demangled != NULL)
    {
      sd.functionname = mangled;
      free(demangled);
    }

    result = 0;
  }

  return result;
}


