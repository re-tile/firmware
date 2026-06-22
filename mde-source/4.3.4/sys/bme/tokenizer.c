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
 * Implements the tokenizer
 */

#include <stdlib.h>
#include <ctype.h>

#include "tokenizer.h"

TokenState
tokenizer_init(const char* string)
{
  while (isspace(*string))
    ++string;
  TokenState ts;
  ts.state = PlainToken;
  ts.ptr = *string ? string : NULL;
  return ts;
}

char
tokenizer_next(TokenState* ts)
{
  while (1)
  {
    char c = *ts->ptr++;

    /* Clear pointer so tokenizer_done() will return true */
    if (c == '\0')
    {
      ts->ptr = NULL;
      return '\0';
    }

    /* Backslashes take priority over everything else. */
    if (c == '\\' && *ts->ptr)
    {
      return *ts->ptr++;
    }

    switch (ts->state)
    {
    case WhiteSpace:
      if (isspace(c))
        continue; /* keep consuming whitespace */
      ts->state = PlainToken;
      --ts->ptr;  /* back up since we're not consuming this charater yet */
      return '\0';

    case PlainToken:
      if (isspace(c))
        ts->state = WhiteSpace;
      else if (c == '"')
        ts->state = DoubleQuote;
      else if (c == '\'')
        ts->state = SingleQuote;
      else
        return c;
      break;

    case SingleQuote:
      if (c == '\'')
        ts->state = PlainToken;
      else
        return c;
      break;

    case DoubleQuote:
      if (c == '"')
        ts->state = PlainToken;
      else
        return c;
      break;
    }
  }
}
