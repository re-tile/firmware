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
 * The SROM device table.
 */

#include "srom_table.h"

//
// This table defines the SROM devices that the hypervisor knows about.  To
// add a new device, you can do one of two things:
//
// 1. Edit this file to add your new device.  (This has the drawback that
//    you will need to merge your changes into this file every time you get
//    a new hypervisor releases.)
//
// 2. Copy this file to a new file, delete all of the table entries, and
//    insert just the entry/entries for your new device/devices.  Then
//    add that new file to the list of hypervisor source files when
//    building your hypervisor, using the EXTRA_SROM_SOURCES Makefile
//    variable.  The entries in the new file will add to, not replace, the
//    list of devices known to the hypervisor.
//
// See srom_table.h for the precise definitions of an entry's parameters.
//

#ifndef __DOXYGEN__

//
// Note: this table is sorted by manufacturer, and then by part number.
// The part number sorting is not strictly alphabetical; we sort parts in
// the same family in order of increasing size.  Micron acquired Numonyx,
// so parts from both brands are grouped together.
//
SROM_TABLE_START
  //
  // Macronix
  //
  SROM_ENTRY(
    "Macronix MX25L128{45E,05D}",    // Name
    0xc220180000ULL,                 // ID bytes
    0xFFFFFF0000ULL,                 // ID mask
    256,                             // Page size
    64 * 1024,                       // Sector size
    16 * 1024 * 1024                 // SROM size
  )
  SROM_ENTRY(
    "Macronix MX25U12835F",          // Name
    0xc225380000ULL,                 // ID bytes
    0xFFFFFF0000ULL,                 // ID mask
    256,                             // Page size
    64 * 1024,                       // Sector size
    16 * 1024 * 1024                 // SROM size
  )

  //
  // Micron/Numonyx
  //
  SROM_ENTRY(
    "Numonyx M25P64",                // Name
    0x2020170000ULL,                 // ID bytes
    0xFFFFFF0000ULL,                 // ID mask
    256,                             // Page size
    64 * 1024,                       // Sector size
    8 * 1024 * 1024                  // SROM size
  )
  SROM_ENTRY(
    "Numonyx M25P128",               // Name
    0x2020180000ULL,                 // ID bytes
    0xFFFFFF0000ULL,                 // ID mask
    256,                             // Page size
    256 * 1024,                      // Sector size
    16 * 1024 * 1024                 // SROM size
  )
  SROM_ENTRY(
    "Numonyx N25Q128",               // Name
    0x20BB181000ULL,                 // ID bytes
    0xFFFFFFFF00ULL,                 // ID mask
    256,                             // Page size
    64 * 1024,                       // Sector size
    16 * 1024 * 1024                 // SROM size
  )
  SROM_ENTRY_LG(
    "Micron N25Q256A",               // Name
    0x20BA191000ULL,                 // ID bytes
    0xFFFFFF0000ULL,                 // ID mask
    256,                             // Page size
    64 * 1024,                       // Sector size
    32 * 1024 * 1024,                // SROM size
    0xc8,                            // A24 read command
    0xc5                             // A24 write command
  )

  //
  // Spansion
  //
  SROM_ENTRY(
    "Spansion SF25FL064A",           // Name
    0x0102160000ULL,                 // ID bytes
    0xFFFFFF0000ULL,                 // ID mask
    256,                             // Page size
    64 * 1024,                       // Sector size
    8 * 1024 * 1024                  // SROM size
  )
  SROM_ENTRY(
    "Spansion SF25FL128Pxxxxx00x",   // Name
    0x0120180301ULL,                 // ID bytes
    0xFFFFFFFFFFULL,                 // ID mask
    256,                             // Page size
    64 * 1024,                       // Sector size
    16 * 1024 * 1024                 // SROM size
  )
  SROM_ENTRY(
    "Spansion SF25FL128Pxxxxx01x",   // Name
    0x0120180300ULL,                 // ID bytes
    0xFFFFFFFFFFULL,                 // ID mask
    256,                             // Page size
    256 * 1024,                      // Sector size
    16 * 1024 * 1024                 // SROM size
  )
  SROM_ENTRY(
    "Spansion SF25FL128Sxxxxx00x",   // Name
    0x0120184D01ULL,                 // ID bytes
    0xFFFFFFFFFFULL,                 // ID mask
    256,                             // Page size
    64 * 1024,                       // Sector size
    16 * 1024 * 1024                 // SROM size
  )
  SROM_ENTRY(
    "Spansion SF25FL128Sxxxxx01x",   // Name
    0x0120184D00ULL,                 // ID bytes
    0xFFFFFFFFFFULL,                 // ID mask
    256,                             // Page size
    256 * 1024,                      // Sector size
    16 * 1024 * 1024                 // SROM size
  )
  SROM_ENTRY_LG(
    "Spansion SF25FL256Sxxxxx00x",   // Name
    0x0102194D01ULL,                 // ID bytes
    0xFFFFFFFFFFULL,                 // ID mask
    256,                             // Page size
    64 * 1024,                       // Sector size
    32 * 1024 * 1024,                // SROM size
    0x16,                            // A24 read command
    0x17                             // A24 write command
  )
  SROM_ENTRY_LG(
    "Spansion SF25FL512S",           // Name
    0x0102204D00ULL,                 // ID bytes
    0xFFFFFFFFFFULL,                 // ID mask
    512,                             // Page size
    256 * 1024,                      // Sector size
    64 * 1024 * 1024,                // SROM size
    0x16,                            // A24 read command
    0x17                             // A24 write command
  )

  //
  // ST Micro
  //
  SROM_ENTRY(
    "ST Micro M25P64",               // Name
    0x2020171000ULL,                 // ID bytes
    0xFFFFFF0000ULL,                 // ID mask
    256,                             // Page size
    64 * 1024,                       // Sector size
    8 * 1024 * 1024                  // SROM size
  )

  //
  // Winbond
  //
  SROM_ENTRY(
    "Winbond W25Q128BV/FV",          // Name
    0xef40180000ULL,                 // ID bytes
    0xFFFFFF0000ULL,                 // ID mask
    256,                             // Page size
    64 * 1024,                       // Sector size
    16 * 1024 * 1024                 // SROM size
  )
SROM_TABLE_END

#endif // __DOXYGEN__
