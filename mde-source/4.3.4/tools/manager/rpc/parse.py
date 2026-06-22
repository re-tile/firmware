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

# Code for parsing IDL files.

from token import *

# Only export these symbols.
__all__ = ["Procedure"
           "parse_procedures",
          ]


class ArgumentType:
    def __init__(self, name, c_name, is_c_array, marshal_func, unmarshal_func,
                 unmarshal_c_name=None):
        self.name = name
        self.c_name = c_name
        if unmarshal_c_name is None:
            unmarshal_c_name = c_name
        self.unmarshal_c_name = unmarshal_c_name
        self.is_c_array = is_c_array
        self.marshal_func = marshal_func
        self.unmarshal_func = unmarshal_func

    def get_java_type(self):
        if self.name == "string":
            return "String"
        elif self.name == "string[]":
            return "String[]"
        else:
            return self.name


# Array of all possible argument and return value types.
argument_types = [
  ArgumentType("string", "char*", False, "marshal_ztstr", "unmarshal_ztstr"),
  ArgumentType("string[]", "StringArray*", False, "marshal_StringArray",
               "unmarshal_StringArray", "StringArray"),
  ]


# Helper function for adding the standard types and arrays of same.
# Note that most of the "array" functions have not been written yet.

def add_standard_argument_type(t, ct):
    argument_types.append(ArgumentType(t, ct,
                                       False,
                                       "marshal_" + t,
                                       "unmarshal_" + t))
    argument_types.append(ArgumentType(t + "[]", ct + "*",
                                       True,
                                       "marshal_" + t + "_array",
                                       "unmarshal_" + t + "_array"))

# Append standard types.
add_standard_argument_type("uint", "uint")
add_standard_argument_type("uint64", "uint64_t")
add_standard_argument_type("uint16", "uint16_t")
add_standard_argument_type("uint8", "uint8_t")
add_standard_argument_type("int", "int")
add_standard_argument_type("int64", "int64_t")
add_standard_argument_type("int16", "int16_t")
add_standard_argument_type("int8", "int8_t")
add_standard_argument_type("bool", "bool")


class Argument:
    def __init__(self, type, name):
        self.type = type
        self.name = name


class Procedure:
    def __init__(self):
        self.name = None
        self.return_values = []
        self.args = []
        self.senders = []
        self.receivers = []
        self.code = None
        self.where = None


def parse_identifier():
    id = parse_token()
    if id.type != TOK_IDENTIFIER:
        id.parse_error("Expected identifier, got '%s'." % id.contents)
    return id


def parse_flags(p):
    last_comma = None

    while True:
        # Check for return type of procedure, which means end of flags.
        first = peek_token()
        if (first.type == TOK_LPAREN or
            (first.type == TOK_IDENTIFIER and first.contents == "void")):
            if not last_comma is None:
                last_comma.parse_error("Unexpected comma after flags.")
            return

        seen_flag = True
        id = parse_identifier()

        op = parse_token()
        if op.type == TOK_EQ:
            val = op
            if id.contents == "code":
                val = parse_token()
            if val.type != TOK_NUMBER:
                id.parse_error("Expected 'code = number'.")
            p.code = val.contents
        elif op.type == TOK_ARROW:
            # Snap a->b->c->d into "a" and "d".
            while True:
                id2 = parse_identifier()
                if peek_token().type != TOK_ARROW:
                    break
                # Eat the arrow.
                parse_token()
            if not id.contents in p.senders:
                p.senders.append(id.contents)
            if not id2.contents in p.receivers:
                p.receivers.append(id2.contents)
        else:
            id.parse_error("Expected '->' or '=', got '%s'." % t.contents)

        last_comma = peek_token()
        if last_comma.type != TOK_COMMA:
            break
        parse_token()


def parse_argument(typename, argname):
    argtype = None
    for t in argument_types:
        if t.name == typename:
            argtype = t
            break
    if argtype is None:
        type.parse_error("Unrecognized type '%s'." % typename)
    return Argument(argtype, argname)


def parse_argument_list(p):
    ret = []
    lparen = parse_token()
    if lparen.type != TOK_LPAREN:
        lparen.parse_error("Expected '(', got '%s'.", lparen.contents)
    if peek_token().type == TOK_RPAREN:
        parse_token()
        return ret

    while True:
        type = parse_identifier()
        typename = type.contents
        if typename == "void":
            type.parse_error("'void' only allowed as unparenthesized "
                             "return value.")

        if peek_token().type == TOK_BRACKETS:
            # Append brackets to the type name.
            typename += "[]"
            parse_token()

        argname = parse_identifier().contents
        ret.append(parse_argument(typename, argname))

        tok = parse_token()
        if tok.type == TOK_RPAREN:
            break
        elif tok.type != TOK_COMMA:
            tok.parse_error("Expected ',' or ')', got '%s'." % tok.contents)

    return ret


def parse_return_vals(p):
    tok = parse_token()
    if tok.type == TOK_IDENTIFIER:
        if tok.contents == "void":
            # Leave "p.return_values" empty.
            return
        elif peek_token().type == TOK_BRACKETS:
            # Anonymous array return type, like "int[] foo();"
            typename = tok.contents + "[]"
            p.return_values.append(parse_argument(typename, "result"))
            parse_token()
            return
        elif peek_token().type == TOK_IDENTIFIER:
            # Anonymous non-array return type, like "int foo();"
            # ISSUE: Why do we peek ahead here, but not elsewhere?
            # That is, why not just use "else" instead of "elif"?
            p.return_values.append(parse_argument(tok.contents, "result"))
            return
    elif tok.type == TOK_LPAREN:
        # Parse a parenthesized list of return values just like parameters.
        unget_token(tok)
        p.return_values = parse_argument_list(p)
        return

    tok.parse_error("Malformed return value specifier '%s'." % tok.contents)


def parse_procedure():
    t = parse_token()
    if t.type == TOK_NONE:
        return None
    unget_token(t)

    ret = Procedure()
    parse_flags(ret)
    parse_return_vals(ret)
    name = parse_identifier()
    ret.where = name
    ret.name = name.contents
    ret.args = parse_argument_list(ret)

    tok = parse_token()
    if tok.type != TOK_SEMICOLON:
        tok.parse_error("Expected semicolon, got '%s'." % tok.contents)
    return ret


def parse_procedures(result, filename):
    start_parsing(filename)
    last_code = None

    all_names = {}
    all_codes = {}

    while True:
        p = parse_procedure()
        if p is None:
            break

        if p.code is None:
            # Auto-compute the code as previous code plus one.
            if last_code is None:
                p.where.parse_error("No code specified for this or previous "
                                    "procedure.")
            else:
                p.code = last_code + 1
        last_code = p.code

        if p.name in all_names:
            p.where.parse_error("Duplicate procedure name '%s'." % p.name)
        all_names[p.name] = 1
        if p.code in all_codes:
            p.where.parse_error("Duplicate code '0x%x'." % p.code)
        all_codes[p.code] = 1

        result.append(p)
