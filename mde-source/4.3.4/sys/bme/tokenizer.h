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
 */

 /*
 * @file
 * Header for tokenizer routines
 */

#ifndef _SYS_TOKENIZER_H
#define _SYS_TOKENIZER_H

/** Internal state for tokenizer. */
typedef struct
{
  const char* ptr;  /**< Where in the string we are. */

  /** Parsing state. */
  enum STATE {
    WhiteSpace,     /**< Consuming inter-token whitespace. */
    PlainToken,     /**< In a plain token. */
    SingleQuote,    /**< In single-quoted text. */
    DoubleQuote     /**< In double-quoted text. */
  } state;

} TokenState;

/** Get ready to tokenize a string.  Skip any leading whitespace.
 * @param string String to tokenize.
 */
TokenState tokenizer_init(const char* string);

/** Return the next token character from a string.
 * @param ts Current parsing state structure.
 * @return Next character.  When we return a NUL, that is the end of a token.
 */
char tokenizer_next(TokenState* ts);

/** Returns nonzero when we are done with the string.  Only becomes true
 *  before any tokenizer_next() calls, or else after tokenizer_next()
 *  returns a NUL.
 * @param ts Current parsing state structure.
 * @return Nonzero iff we're done parsing the string.
 */
static inline int tokenizer_done(TokenState* ts) { return ts->ptr == 0; }

#endif /* _SYS_TOKENIZER_H */
