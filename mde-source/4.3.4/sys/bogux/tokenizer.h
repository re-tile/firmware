/**
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
 *
 * Header for tokenizer routines
 * @file
 */

#ifndef _SYS_TOKENIZER_H
#define _SYS_TOKENIZER_H

#include <stdbool.h>

/** Internal state for tokenizer. */
typedef struct
{
  const char* ptr;  /**< where in the string we are */

  enum STATE {
    WhiteSpace,     /**< consuming inter-token whitespace */
    PlainToken,     /**< in a plain token */
    SingleQuote,    /**< in single-quoted text */
    DoubleQuote     /**< in double-quoted text */
  } state;

} TokenState;

/** Get ready to tokenize a string.  Skip any leading whitespace. */
TokenState tokenizer_init(const char* string);

/** Return the next token character from a string.
 * When we return a NUL that is the end of a token.
 */
char tokenizer_next(TokenState*);

/** Returns true when we are done with the string.
 * Only becomes true before any tokenizer_next() calls,
 * or else after tokenizer_next() returns a NUL.
 */
static inline bool tokenizer_done(TokenState* ts) { return ts->ptr == 0; }

#endif
