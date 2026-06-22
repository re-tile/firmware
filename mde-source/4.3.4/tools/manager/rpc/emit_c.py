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

# Code for emitting C code for RPC.

import string
import os

# Only export these symbols.
__all__ = ["generate_c_output_files"]


def compute_query_code_symbol(p):
    return "QUERY_CODE_" + p.name.upper()


def emit_code_define(p, dot_h):
    dot_h.write("#define %-30s 0x%04x\n"
                % (compute_query_code_symbol(p), p.code))


def compute_argument_list(args, need_comma=False, need_const=False,
                          name_args=True):
    ret = ""
    for a in args:
        if need_comma:
            ret += ", "
        need_comma = True

        t = a.type
        if need_const:
            ret += "const "
        ret += t.c_name
        if name_args:
            ret += " " + a.name
        if t.is_c_array:
            ret += ", size_t "
            if name_args:
                ret += "%s_size" % a.name
    return ret


def emit_argument_list(out, args, need_comma=False, need_const=False):
    out.write(compute_argument_list(args, need_comma, need_const))


def emit_sender_code(p, dot_h, dot_c):
    dot_h.write("\n\n")
    dot_h.write("extern void\ndo_%s(Pollable* socket" % p.name)
    emit_argument_list(dot_h, p.args, True, True)
    dot_h.write(");\n\n")

    dot_h.write("extern AnswerHandler*\nask_%s(Pollable* _socket" % p.name)
    if len(p.args) > 0:
        emit_argument_list(dot_h, p.args, True, True)
    dot_h.write(");\n\n")

    dot_h.write("extern AnswerHandler* query_%s(\n" % p.name)
    dot_h.write("  Pollable* _socket,\n")
    dot_h.write("  void (*reply_handler)(void* info")
    emit_argument_list(dot_h, p.return_values, True)
    dot_h.write("),\n"
                "  void* reply_handler_info,\n"
                "  void (*error_handler)(void* info, char* msg),\n"
                "  void* error_handler_info")
    if len(p.args) > 0:
        dot_h.write(",\n  ")
        emit_argument_list(dot_h, p.args, False, True)
    dot_h.write("\n);\n\n\n")

    body = ""

    if len(p.args) > 0:
        body += "  Buffer* _output = &_socket->output;\n"

    for arg in p.args:
        body += "  %s(_output, %s" % (arg.type.marshal_func, arg.name)
        if arg.type.is_c_array:
            body += ", %s_size" % arg.name
        body += ");\n"

    dot_c.write("\n\n"
                "void\ndo_%s(Pollable* _socket" % p.name)
    emit_argument_list(dot_c, p.args, True, True)
    dot_c.write(")\n{\n"
                "  const uint _packet = packet_start(_socket, %s, 0);\n"
                % compute_query_code_symbol(p))
    dot_c.write(body +
                "  packet_finish(_socket, _packet);\n"
                "}\n")

    dot_c.write("\n\nAnswerHandler*\nask_%s(Pollable* _socket" % p.name)
    if len(p.args) > 0:
        emit_argument_list(dot_c, p.args, True, True)
    dot_c.write(")\n{\n"
                "  AnswerHandler* _ah = "
                "(AnswerHandler*)calloc_or_die(1, sizeof(AnswerHandler));\n"
                "  _ah->code = %s;\n"
                "  rpc_add_answer_handler(_ah);\n"
                "\n"
                "  const uint _packet = "
                "packet_start(_socket, _ah->code, _ah->tag);\n"
                % compute_query_code_symbol(p))
    dot_c.write(body +
                "  packet_finish(_socket, _packet);\n"
                "\n"
                "  return _ah;\n"
                "}\n")

    dot_c.write("\n\n"
                "AnswerHandler*\nquery_%s(\n  Pollable* _socket,\n" % p.name)
    dot_c.write("  void (*_reply_handler)(void* info")
    emit_argument_list(dot_c, p.return_values, True)
    dot_c.write("),\n"
                "  void* _reply_handler_info,\n"
                "  void (*_error_handler)(void* info, char* msg),\n"
                "  void* _error_handler_info")
    if len(p.args) > 0:
        dot_c.write(",\n  ")
        emit_argument_list(dot_c, p.args, False, True)
    dot_c.write(")\n{\n"
                "  AnswerHandler* _ah = "
                "(AnswerHandler*)calloc_or_die(1, sizeof(AnswerHandler));\n"
                "  _ah->reply_handler = _reply_handler;\n"
                "  _ah->reply_handler_info = _reply_handler_info;\n"
                "  _ah->error_handler = _error_handler;\n"
                "  _ah->error_handler_info = _error_handler_info;\n"
                "  _ah->code = %s;\n"
                "  rpc_add_answer_handler(_ah);\n"
                "\n"
                "  const uint _packet = "
                "packet_start(_socket, _ah->code, _ah->tag);\n"
                % compute_query_code_symbol(p))
    dot_c.write(body +
                "  packet_finish(_socket, _packet);\n"
                "\n"
                "  return _ah;\n"
                "}\n")


def add_case_statement(case_val, body, case_statements):
    if body in case_statements:
        case_statements[body].append(case_val)
    else:
        case_statements[body] = [case_val]


# Returns 'string' with each line indented by 'level' spaces.
def indent(string, level):
    return (("%*s" % (level, "")).join(("\n" + string).splitlines(True)))[1:]


def create_rpc_decoder(args, code, func_name, initial_arg):
    i = 0

    body = ""
    call = "%s(%s" % (func_name, initial_arg)
    destroy = ""

    for arg in args:

        call += ", "
        if arg.type.c_name != arg.type.unmarshal_c_name:
            # HACK: Declare a local "StringArray" but pass it by
            # pointer rather than by value.
            call += "&"
        call += "_arg_%d" % i

        if arg.type.is_c_array:
            body += "size_t _arg_%d_size;\n" % i
            body += ("%s _arg_%d = %s(_body, &_arg_%d_size);\n"
                     % (arg.type.unmarshal_c_name, i,
                        arg.type.unmarshal_func, i))
            call += ", _arg_%d_size" % i
        else:
            body += ("%s _arg_%d = %s(_body);\n"
                     % (arg.type.unmarshal_c_name, i, arg.type.unmarshal_func))

        if arg.type.unmarshal_c_name == "StringArray":
            destroy += "StringArray_destroy(&_arg_%d);\n" % i

        i += 1

    body += ("rpc_verify_consumed(_body, %s);\n" % code)
    body += call + ");\n"
    body += destroy

    return body


def emit_receiver_code_aux(cases, what, dot_c):

    dot_c.write("    // Handle '%s'.\n"
                "    if (_ah->%s_handler != NULL)\n"
                "    {\n"
                "      switch (_rpc.code)\n"
                "      {\n" % (what, what))

    # Write out all of the case statements, with identical bodies
    # grouped together as fall-throughs.
    bodies = cases.keys()
    bodies.sort()
    for body in bodies:
        case_vals = cases[body]
        case_vals.sort()
        for case_val in case_vals:
            dot_c.write("      case %s:\n" % case_val)
        dot_c.write("        {\n" + indent(body, 10) + "        }\n")
        dot_c.write("        break;\n")

    dot_c.write("      default:\n"
                "        punt(\"Unexpected %s packet!\");\n"
                "      }\n"
                "    }\n" % what)


def emit_receiver_code(all_procedures, target, dispatch, dot_h, dot_c):

    dot_h.write("\n\n")

    dot_c.write("\nbool\n"
                "%s(RPC _rpc)\n" % dispatch)
    dot_c.write("{\n"
                "  Buffer* _body = &_rpc.socket->input;\n"
                "\n"
                "  if (_rpc.code < 0x8000)\n"
                "  {\n"
                "    // Handle 'query' (or 'other').\n"
                "    switch (_rpc.code)\n"
                "    {\n")

    reply_cases = {}
    error_cases = {}

    for p in all_procedures:

        if target in p.receivers:

            # Handle query.

            dot_h.write("extern void\nperform_%s(RPC rpc" % p.name)
            emit_argument_list(dot_h, p.args, True)
            dot_h.write(");\n\n")

            case_val = compute_query_code_symbol(p)

            body = create_rpc_decoder(p.args, case_val,
                                      "perform_" + p.name, "_rpc")

            dot_c.write("    case %s:\n" % case_val)
            dot_c.write("      {\n" + indent(body, 8) + "      }\n")
            dot_c.write("      return true;\n")

        if target in p.senders:

            # Handle reply.

            case_val = "RPC_REPLY(%s)" % compute_query_code_symbol(p)
            body = ("void (*_func)(void* _info"
                    + compute_argument_list(p.return_values,
                                            True, False, False)
                    + ") = _ah->reply_handler;\n"
                    + "void* info = _ah->reply_handler_info;\n")

            body += create_rpc_decoder(p.return_values, "_rpc.code",
                                       "_func", "info")

            add_case_statement(case_val, body, reply_cases)

            # Handle error.

            case_val = "RPC_ERROR(%s)" % compute_query_code_symbol(p)

            body = ("char* _err_msg = unmarshal_ztstr(_body);\n"
                    "rpc_verify_consumed(_body, _rpc.code);\n"
                    "void* info = _ah->error_handler_info;\n"
                    "_ah->error_handler(info, _err_msg);\n")

            add_case_statement(case_val, body, error_cases)

    dot_c.write("    default:\n"
                "      // Unknown query (or 'other').\n"
                "      return false;\n"
                "    }\n"
                "  }\n"
                "\n"
                "  AnswerHandler *_ah =\n"
                "    rpc_get_answer_handler(_rpc.code, _rpc.tag);\n"
                "\n"
                "  // Unexpected 'reply' or 'error'.\n"
                "  if (_ah == NULL)\n"
                "    return false;\n"
                "\n"
                "  if (_rpc.code < 0xC000)\n"
                "  {\n")

    emit_receiver_code_aux(reply_cases, "reply", dot_c)

    dot_c.write("  }\n"
                "  else\n"
                "  {\n")

    emit_receiver_code_aux(error_cases, "error", dot_c)

    dot_c.write("  }\n"
                "  free(_ah);\n"
                "  return true;\n"
                "}\n")

    dot_h.write("\n\n"
                "extern bool\n"
                "%s(RPC _rpc);\n\n" % dispatch)


def emit_reply_senders(all_procedures, target, dot_h, dot_c):
    # Maps function body to array of Procedures that share it.
    all_senders = {}

    dot_h.write("\n\n")

    for p in all_procedures:
        if target in p.receivers:

            body = "(RPC _rpc"
            i = 0
            for a in p.return_values:
                body += ", const %s _arg_%d" % (a.type.c_name, i)
                if a.type.is_c_array:
                    body += ", size_t _arg_%d_size" % i
                i += 1

            body += (")\n"
                     "{\n"
                     "  if (_rpc.tag == 0)\n"
                     "    return;\n"
                     "\n")
            body += ("  const uint _packet =\n"
                     "    packet_start(_rpc.socket, RPC_REPLY(_rpc.code), "
                     "_rpc.tag);\n")
            if len(p.return_values) > 0:
                body += "  Buffer* _output = &_rpc.socket->output;\n"

            i = 0
            for arg in p.return_values:
                body += "  %s(_output, _arg_%d" % (arg.type.marshal_func, i)
                if arg.type.is_c_array:
                    body += ", _arg_%d_size" % i
                body += ");\n"
                i += 1
            body += ("  packet_finish(_rpc.socket, _packet);\n"
                     "}\n\n")

            if body in all_senders:
                all_senders[body].append(p)
            else:
                all_senders[body] = [p]

    keys = all_senders.keys()
    keys.sort()
    for body in keys:
        senders = all_senders[body]

        p = senders[0]
        dot_h.write("extern void\nreply_%s(RPC _rpc" % p.name)
        emit_argument_list(dot_h, p.return_values, True, True)
        dot_h.write(");\n\n")

        dot_c.write("\nvoid\nreply_%s%s" % (p.name, body))

        if len(senders) > 1:
            for i in xrange(1, len(senders)):
                dot_h.write("#define reply_%-20s reply_%s\n"
                            % (senders[i].name, p.name))
            dot_h.write("\n")


def generate_c_output_files(all_procedures, target, dispatch,
                            output_prefix, codes_only):
    dot_h = open(output_prefix + ".h", "wb")

    header_guard = "_RPC_INTERFACE_FOR_%s_INCLUDED_" % target.upper()
    dot_h.write(("/* This file is machine-generated; DO NOT EDIT! */\n"
                 "#ifndef %s\n"
                 "#define %s\n\n"
                 "#include <features.h>\n\n"
                 "__BEGIN_DECLS\n\n")
                % (header_guard, header_guard))

    # Emit all relevant codes.
    for p in all_procedures:
        if target in p.senders or target in p.receivers:
            emit_code_define(p, dot_h)
    dot_h.write("\n")

    if not codes_only:
        dot_c = open(output_prefix + ".c", "wb")
        dot_c.write("#include \"tools/handy/message.h\"\n"
                    "#include \"tools/handy/various.h\"\n"
                    "#include \"tools/handy/rpc.h\"\n"
                    "#include \"%s.h\"\n"
                    % os.path.basename(output_prefix))

        # Emit all sender declarations and definitions.
        for p in all_procedures:
            if target in p.senders:
                emit_sender_code(p, dot_h, dot_c)

        # Emit reply sender declarations and definitions.
        emit_reply_senders(all_procedures, target, dot_h, dot_c)

        # Emit all receiver declarations and definitions.
        emit_receiver_code(all_procedures, target, dispatch, dot_h, dot_c)

        dot_c.close()

    dot_h.write("\n__END_DECLS\n\n"
                "#endif  /* !%s */\n" % header_guard)
    dot_h.close()
