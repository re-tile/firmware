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

// =============================================================================
// cgibin.h -- Handy macros for C-based CGI_BIN programs
// Copyright (C) 2010. Tilera Corporation
// =============================================================================

// multiple-inclusion guard
#ifndef CGI_BIN_H
#define CGI_BIN_H

// print formatted line, with a trailing newline
#define P0(FORMAT) printf(FORMAT"\n")
#define P1(FORMAT,A1) printf(FORMAT"\n", A1)
#define P2(FORMAT,A1,A2) printf(FORMAT"\n", A1, A2)
#define P3(FORMAT,A1,A2,A3) printf(FORMAT"\n", A1, A2, A3)
#define P4(FORMAT,A1,A2,A3,A4) printf(FORMAT"\n", A1, A2, A3, A4)
#define P5(FORMAT,A1,A2,A3,A4,A5) printf(FORMAT"\n", A1, A2, A3, A4, A5)
#define P6(FORMAT,A1,A2,A3,A4,A5,A6) printf(FORMAT"\n", A1, A2, A3, A4, A5, A6)
#define P7(FORMAT,A1,A2,A3,A4,A5,A6,A7) printf(FORMAT"\n", A1, A2, A3, A4, A5, A6, A7)
#define P8(FORMAT,A1,A2,A3,A4,A5,A6,A7,A8) printf(FORMAT"\n", A1, A2, A3, A4, A5, A6, A7, A8)

// print simple CGI-BIN header (just the content type)
#define CGI_BIN_HEADER(MIME_TYPE) \
  P0("Content-Type: " MIME_TYPE ); \
  P0("");

// print header for a text file
#define CGI_BIN_HEADER_TEXT() \
  CGI_BIN_HEADER("text/plain");

// print header for an HTML file
#define CGI_BIN_HEADER_HTML() \
  CGI_BIN_HEADER("text/html");

// multiple-inclusion guard
#endif
