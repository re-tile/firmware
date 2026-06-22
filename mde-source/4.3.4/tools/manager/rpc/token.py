# Copyright 2014 Tilera Corporation. All Rights Reserved.
#
#   The source code contained or described herein and all documents
#   related to the source code ("Material") are owned by Tilera
#   Corporation or its suppliers or licensors.  Title to the Material
#   remains with Tilera Corporation or its suppliers and licensors. The
#   software is licensed under the Tilera MDE License.
#
#   Unless otherwise agreed by Tilera in writing, you may not remove or
#   alter this notice or any other notice embedded in Materials by Tilera
#   or Tilera's suppliers or licensors in any way.

# Code for tokenizing IDL files.

import string
import sys

# Only export these symbols.
__all__ = ["start_parsing",
           "parse_token",
           "peek_token",
           "unget_token",
           "Token",
           "TOK_NONE",
           "TOK_IDENTIFIER",
           "TOK_LPAREN",
           "TOK_RPAREN",
           "TOK_COMMA",
           "TOK_SEMICOLON",
           "TOK_ARROW",
           "TOK_NUMBER",
           "TOK_BRACKETS",
           "TOK_EQ",
            ]

# Contents of file being parsed.
file = ""

# Current index in file being parsed.
index = 0

# Current filename being parsed.
filename = "<no file opened>"

# Up to one unget_token is allowed.
last_unget_token = None


TOK_NONE = 0
TOK_IDENTIFIER = 1
TOK_LPAREN = 2
TOK_RPAREN = 3
TOK_COMMA = 4
TOK_SEMICOLON = 5
TOK_ARROW = 6
TOK_NUMBER = 7
TOK_BRACKETS = 8
TOK_EQ = 9


def parse_error(filename, file, index, msg):
    line = 1
    i = 0
    last_newline_index = 0

    while i < index:
        if file[i] == '\n':
            last_newline_index = i
            line += 1
        i += 1
    sys.stderr.write("%s:%d:%d %s\n" % (filename, line,
                                        index - last_newline_index, msg))
    sys.exit(-1)


class Token:
    def __init__(self, type, contents, index):
        self.type = type
        self.contents = contents
        self.filename = filename
        self.file = file
        self.index = index

    def parse_error(self, msg):
        parse_error(self.filename, self.file, self.index, msg)


def skip_whitespace():
    global index, file
    while index < len(file) and file[index].isspace():
        index += 1


def isxdigit(c):
    return "0123456789ABCDEFabcdef".find(c) != -1


def parse_number():
    global index, file

    start = index

    # check for hex number
    if (file[index] == '0' and
        index + 2 < len(file) and
        file[index + 1] == 'x' and
        isxdigit(file[index + 2])):

        index += 3
        while index < len(file) and isxdigit(file[index]):
            index += 1
        base = 16
    else:
        while index < len(file) and file[index].isdigit():
            index += 1
        base = 10

    if index < len(file) and file[index].isalpha():
        # Quick check for "123X" or "0x1234X" error.
        parse_error(filename, file, start, "Malformed number")

    return Token(TOK_NUMBER, string.atoi(file[start:index], base), start)


def parse_identifier():
    global index, file

    start = index
    while (index < len(file) and
           (file[index].isalnum() or file[index] == '_')):
        index += 1

    assert(start != index)
    return Token(TOK_IDENTIFIER, file[start:index], start)


def parse_operator():
    global index, file

    c = file[index]
    start = index
    index += 1

    type = None

    if c == '(':
        type = TOK_LPAREN
    elif c == ')':
        type = TOK_RPAREN
    elif c == ',':
        type = TOK_COMMA
    elif c == ';':
        type = TOK_SEMICOLON
    elif c == '=':
        type = TOK_EQ
    elif c == '-' and index < len(file) and file[index] == '>':
        index += 1
        type = TOK_ARROW
    elif c == '[' and index < len(file) and file[index] == ']':
        index += 1
        type = TOK_BRACKETS

    if type is None:
        parse_error(filename, file, index - 1,
                    "Unrecognized operator '%s'" % c)

    return Token(type, file[start:index], start)


def unget_token(token):
    global last_unget_token

    if not last_unget_token is None:
        sys.stderr.write("Internal error, only one unget allowed.\n")
        sys.exit(1)
    last_unget_token = token


def parse_token():
    global index, file, last_unget_token

    if not last_unget_token is None:
        ret = last_unget_token
        last_unget_token = None
        return ret

    while True:
        skip_whitespace()
        if (index + 1 < len(file) and
            file[index] == '/' and file[index + 1] == '/'):
            # Eat comments
            while index < len(file) and file[index] != '\n':
                index += 1
        else:
            break

    if index >= len(file):
        return Token(TOK_NONE, "<EOF>", len(file))

    c = file[index]
    if c.isdigit():
        return parse_number()
    elif c.isalpha() or c == '_':
        return parse_identifier()
    else:
        return parse_operator()


def peek_token():
    ret = parse_token()
    unget_token(ret)
    return ret


def start_parsing(fname):
    global file, index, filename

    x = open(fname, "rb")
    file = x.read()
    x.close()

    filename = fname
    index = 0
