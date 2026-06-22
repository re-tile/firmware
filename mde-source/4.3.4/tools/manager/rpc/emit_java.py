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

# Code for emitting Java code for RPC.

import string
import os

# Only export these symbols.
__all__ = ["generate_java_output_file"]


def emit_java_rpc_decl(p, out):
    out.write("\n\t\t@RPC (Senders=\"%s\", Receivers=\"%s\"" %
              (",".join(p.senders), ",".join(p.receivers)))
    out.write(", Code=\"0x%04X\")\n" % p.code)

    out.write("\t")
    if len(p.return_values) == 1:
        out.write(p.return_values[0].type.get_java_type())
    else:
        out.write("void")

    args = []
    names_used = {}
    for arg in p.args:
        args.append("%s %s" % (arg.type.get_java_type(), arg.name))
        names_used[arg.name] = True

    if len(p.return_values) > 1:
        # Multi-value return.
        for ret in p.return_values:
            # Disallow a multi-return value (an @OUT parameter) having
            # the same name as a parameter, since Java reflection
            # won't allow that. So prepend underscores until the name
            # is unique.
            name = ret.name
            while name in names_used:
                name = "_" + name
            names_used[name] = True
            args.append("@OUT %s %s" % (ret.type.get_java_type(), name))

    out.write(" %s(%s);\n" % (p.name, ", ".join(args)))


def generate_java_output_file(all_procedures, target, output_filename):
    out = open(output_filename, "wb")

    out.write("// This file is machine-generated; DO NOT EDIT!\n\n"
              "package com.tilera.ide.monitor.rpc.idl;\n"
              "\n"
              "import com.tilera.ide.monitor.rpc.idl.types.*;\n"
              "\n"
              "\n"
              "public interface IDL\n"
              "{")

    # Emit all relevant codes.
    for p in all_procedures:
        if target in p.senders or target in p.receivers:
            emit_java_rpc_decl(p, out)
    out.write("}\n")
    out.close()
