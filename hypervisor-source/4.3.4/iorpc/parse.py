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
import sys
from token import *

# Only export these symbols.
__all__ = ["Environment",
           "Procedure",
           "parse_procedures",
           ]


## An argument to an rpc routine.
class Argument:
    def __init__(self, type, name):

        # Support shorthand.
        if type == "uint":
            type = "unsigned int"

        # FIXME: "ulong" is dangerous.
        elif type == "ulong":
            type = "unsigned long"

        self.type = type
        self.name = name


## An rpc routine, including argument list and return type.
class Procedure:
    def __init__(self):
        self.name = None
        self.return_values = []
        self.args = []
        self.senders = []
        self.receivers = []
        self.code = None
        self.where = None
        self.is_read = False
        self.has_sub_offset = False
        self.sub_offset_name = ""
        self.has_mem_buffer = False
        self.has_mem_unregister = False
        self.mem_buffer_flags = []
        self.has_mem_buffer_flag_word = False
        self.has_interrupt = False
        self.has_pollfd_setup = False
        self.has_pollfd = False
        self.special_name = ""
        self.has_blob = False
        self.blob_name = ""
        self.is_nouser = False
        self.is_nokernel = False


## Special parameters included in the .idl file to specify unique code
# generation features like including files or adding extra parameters
# to handler routines.
class Environment:
    def __init__(self):
        self.prefix = None
        self.handler_extra_args = []
        self.allincludes = []
        self.hvincludes = []
        self.linuxincludes = []
        self.userincludes = []
        self.use_context_obj = False

    def validate(self):
        if (self.prefix == None):
            sys.stderr.write("IDL must specify 'param prefix'\n")
            sys.exit(-1)


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
        if (first.type == TOK_IDENTIFIER and
            (first.contents == "void" or
             first.contents == "err_t" or
             first.contents == "int")):
            if not last_comma is None:
                last_comma.parse_error("Unexpected comma after flags.")
            return

        seen_flag = True
        id = parse_identifier()
        next = peek_token()
        if id.contents == "read":
            p.is_read = True
        elif id.contents == "nouser":
            p.is_nouser = True
        elif id.contents == "nokernel":
            p.is_nokernel = True
        elif id.contents == "code":
            if next.type != TOK_EQ:
                id.parse_error("Expected 'code = number'.")
            else:
                parse_token()
                val = parse_token()
                if val.type != TOK_NUMBER:
                    id.parse_error("Expected 'code = number'.")
            p.code = val.contents

        else:
            id.parse_error("Unknown procedure flags")

        last_comma = peek_token()
        if last_comma.type != TOK_COMMA:
            break
        parse_token()


def parse_mem_buffer(p):
    p.has_mem_buffer = True
    lparen = parse_token()
    if lparen.type != TOK_LPAREN:
        lparen.parse_error("Expected '(', got '%s'." % lparen.contents)

    if peek_token().type == TOK_RPAREN:
        parse_token()
    else:
        while True:
            tok = parse_identifier()
            flag = tok.contents
            if flag == "ALIGN_4KB":
                p.mem_buffer_flags.append("DRV_IORPC_FLAG_ALIGN_4KB")
            elif flag == "ALIGN_64KB":
                p.mem_buffer_flags.append("DRV_IORPC_FLAG_ALIGN_64KB")
            elif flag == "ALIGN_SELF_SIZE":
                p.mem_buffer_flags.append("DRV_IORPC_FLAG_ALIGN_SELF_SIZE")
            elif flag == "FLAGS":
                p.has_mem_buffer_flag_word = True
            else:
                tok.parse_error("Unknown MEM_BUFFER flag '%s'." % flag)

            tok = parse_token()
            if tok.type == TOK_RPAREN:
                break
            elif tok.type != TOK_COMMA:
                tok.parse_error("Expected ',' or ')', got '%s'." %
                                tok.contents)

    name = parse_identifier()
    p.special_name = name.contents

def parse_mem_unregister(p):
    p.has_mem_unregister = True
    lparen = parse_token()
    if lparen.type != TOK_LPAREN:
        lparen.parse_error("Expected '(', got '%s'." % lparen.contents)

    if peek_token().type == TOK_RPAREN:
        parse_token()
    else:
        while True:
            tok = parse_identifier()
            flag = tok.contents
            if flag == "ALIGN_4KB":
                p.mem_buffer_flags.append("DRV_IORPC_FLAG_ALIGN_4KB")
            elif flag == "ALIGN_64KB":
                p.mem_buffer_flags.append("DRV_IORPC_FLAG_ALIGN_64KB")
            elif flag == "ALIGN_SELF_SIZE":
                p.mem_buffer_flags.append("DRV_IORPC_FLAG_ALIGN_SELF_SIZE")
            else:
                tok.parse_error("Unknown MEM_UNREGISTER flag '%s'." % flag)

            tok = parse_token()
            if tok.type == TOK_RPAREN:
                break
            elif tok.type != TOK_COMMA:
                tok.parse_error("Expected ',' or ')', got '%s'." %
                                tok.contents)

    name = parse_identifier()
    p.special_name = name.contents

def parse_interrupt(p):
    p.has_interrupt = True
    name = parse_identifier()
    p.special_name = name.contents


def parse_pollfd_setup(p):
    p.has_pollfd_setup = True
    name = parse_identifier()
    p.special_name = name.contents


def parse_pollfd(p):
    p.has_pollfd = True
    name = parse_identifier()
    p.special_name = name.contents


def parse_argument_list(p):
    ret = []
    lparen = parse_token()
    if lparen.type != TOK_LPAREN:
        lparen.parse_error("Expected '(', got '%s'." % lparen.contents)
    if peek_token().type == TOK_RPAREN:
        parse_token()
        return ret

    while True:

        type = parse_identifier()
        typename = type.contents
        if p.has_blob:
            type.parse_error("BLOB must be last call parameter.")

        if typename == "void":
            type.parse_error("'void' only allowed as unparenthesized "
                             "return value.")
        elif typename == "OFFSET":
            if len(ret) != 0:
                type.parse_error("OFFSET must be first call parameter.")
            p.has_sub_offset = True
            name = parse_identifier()
            p.sub_offset_name = name.contents
        elif typename == "MEM_BUFFER":
            if p.is_nouser:
                type.parse_error("MEM_BUFFER not compatible with nouser.")
            if len(ret) != 0:
                type.parse_error("MEM_BUFFER() must be first call parameter.")
            elif p.is_read:
                type.parse_error("Cannot use MEM_BUFFER() with 'read' RPCs.")
            parse_mem_buffer(p)
        elif typename == "MEM_UNREGISTER":
            if p.is_nouser:
                type.parse_error("MEM_UNREGISTER not compatible with nouser.")
            if len(ret) != 0:
                type.parse_error("MEM_UNREGISTER() must be first call parameter.")
            elif p.is_read:
                type.parse_error("Cannot use MEM_UNREGISTER() with 'read' RPCs.")
            parse_mem_unregister(p)
        elif typename == "INTERRUPT":
            if p.is_nouser:
                type.parse_error("INTERRUPT not compatible with nouser.")
            if len(ret) != 0:
                type.parse_error("INTERRUPT must be first call parameter.")
            elif p.is_read:
                type.parse_error("Cannot use INTERRUPT with 'read' RPCs.")
            parse_interrupt(p)
        elif typename == "POLLFD_SETUP":
            if p.is_nouser:
                type.parse_error("POLLFD_SETUP not compatible with nouser.")
            if len(ret) != 0:
                type.parse_error("POLLFD_SETUP must be first call parameter.")
            elif p.is_read:
                type.parse_error("Cannot use POLLFD_SETUP with 'read' RPCs.")
            parse_pollfd_setup(p)
        elif typename == "POLLFD":
            if p.is_nouser:
                type.parse_error("POLLFD not compatible with nouser.")
            if len(ret) != 0:
                type.parse_error("POLLFD must be first call parameter.")
            elif p.is_read:
                type.parse_error("Cannot use POLLFD with 'read' RPCs.")
            parse_pollfd(p)
        else:
            is_output = False
            if typename == "struct":
                type = parse_identifier()
                typename = "struct " + type.contents
            if peek_token().type == TOK_STAR:
                is_output = True
                parse_token()

            if p.is_read == True and is_output == False:
                type.parse_error("'read' attribute requires "
                                 "pointer parameters")
            elif p.is_read == False and is_output == True:
                type.parse_error("Pointer params require "
                                 "'read' procedure flag")

            argname = parse_identifier().contents
            if (typename == "BLOB"):
                p.has_blob = True
                p.blob_name = argname
            else:
                ret.append(Argument(typename, argname))

        tok = parse_token()
        if tok.type == TOK_RPAREN:
            break
        elif tok.type != TOK_COMMA:
            tok.parse_error("Expected ',' or ')', got '%s'." % tok.contents)

    if p.is_read and len(ret) == 0 and not p.has_blob:
        lparan.parse_error("RPC read calls must have an output parameter.")

    return ret


def parse_return_vals(p):
    tok = parse_token()
    if tok.type == TOK_IDENTIFIER:
        if tok.contents == "void":
            # Leave "p.return_values" empty.
            return
        elif peek_token().type == TOK_IDENTIFIER:
            # Anonymous non-array return type, like "int foo();"
            # NOTE: We "peek" above to detect, for example, "foo();".
            p.return_values.append(Argument(tok.contents, "result"))
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


def parse_environment(env):
    while True:
        t = parse_token()
        if t.type == TOK_NONE:
            return None

        if t.type == TOK_IDENTIFIER and t.contents == "env":
            var = parse_token()
            if var.type != TOK_IDENTIFIER:
                var.parse_error("Expected identifier after 'env'")
            if var.contents == "prefix":
                val = parse_token()
                if val.type != TOK_IDENTIFIER:
                    var.parse_error("Expected identifier for 'env prefix'")
                env.prefix = val.contents
            elif var.contents == "hvarg":
                type = parse_token()
                name = parse_token()
                if name.type == TOK_STAR:
                    type.contents += "*"
                    name = parse_token()
                if type.type != TOK_IDENTIFIER or name.type != TOK_IDENTIFIER:
                    type.parse_error("Invalid 'env hvarg' syntax")
                env.handler_extra_args.append(Argument(type.contents,
                                                       name.contents))
            elif var.contents == "allinclude":
                val = parse_token()
                if val.type != TOK_IDENTIFIER:
                    var.parse_error("Expected identifier for 'env allinclude'")
                env.allincludes.append(val.contents)
            elif var.contents == "hvinclude":
                val = parse_token()
                if val.type != TOK_IDENTIFIER:
                    var.parse_error("Expected identifier for 'env hvinclude'")
                env.hvincludes.append(val.contents)
            elif var.contents == "linuxinclude":
                val = parse_token()
                if val.type != TOK_IDENTIFIER:
                    var.parse_error("Expected identifier for "
                                    "'env linuxinclude'")
                env.linuxincludes.append(val.contents)
            elif var.contents == "userinclude":
                val = parse_token()
                if val.type != TOK_IDENTIFIER:
                    var.parse_error("Expected identifier for "
                                    "'env userinclude'")
                env.userincludes.append(val.contents)
            else:
                var.parse_error("Unknown environment parameter %s" %
                                (var.contents))
        else:
            # If it didn't start with "env", it's not an env parameter.
            unget_token(t)
            break

    env.validate()


def parse_procedures(result_env, result_procs, filename):
    start_parsing(filename)
    last_code = None

    all_names = {}
    all_codes = {}

    parse_environment(result_env)

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

        result_procs.append(p)
