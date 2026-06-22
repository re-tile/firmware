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

# TODO: Convert "need_comma" (True/False) into "next_delim" (", "/"").

# Code for emitting C code for RPC.

import string
import os

# License text, hardcoded within the distributed source.
gpl_license_block = """/*
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

"""


# Compute an iorpc opcode name.
def compute_query_code_symbol(p, prefix, is_user):
    if is_user:
        type = "_USER_OP_"
    else:
        type = "_OP_"
    return prefix.upper() + type + p.name.upper()


# Emit the definition of an opcode value.
def emit_code_define(p, dot_h, prefix, is_user):
    symbol = compute_query_code_symbol(p, prefix, is_user)
    if p.has_mem_buffer:
        if is_user:
            format = "IORPC_FORMAT_USER_MEM"
        else:
            format = "IORPC_FORMAT_KERNEL_MEM"
    elif p.has_mem_unregister:
        if is_user:
            format = "IORPC_FORMAT_USER_MEM_UNREGISTER"
        else:
            format = "IORPC_FORMAT_KERNEL_MEM_UNREGISTER"
    elif p.has_interrupt:
        if is_user:
            format = "IORPC_FORMAT_USER_INTERRUPT"
        else:
            format = "IORPC_FORMAT_KERNEL_INTERRUPT"
    elif p.has_pollfd_setup:
        if is_user:
            format = "IORPC_FORMAT_USER_POLLFD_SETUP"
        else:
            format = "IORPC_FORMAT_KERNEL_POLLFD_SETUP"
    elif p.has_pollfd:
        if is_user:
            format = "IORPC_FORMAT_USER_POLLFD"
        else:
            format = "IORPC_FORMAT_KERNEL_POLLFD"
    else:
        if p.is_nouser:
            format = "IORPC_FORMAT_NONE_NOUSER"
        else:
            format = "IORPC_FORMAT_NONE"
    dot_h.write("#define %-30s IORPC_OPCODE(%s, 0x%04x)\n"
                % (symbol, format, p.code))


# Compute a C-style argument list given a parameter set.
def compute_argument_list(args, need_comma=False, show_types=True,
                          arg_prefix=""):
    ret = ""
    for arg in args:
        if need_comma:
            ret += ", "
        need_comma = True
        if show_types:
            ret += arg.type + " "
        ret += arg_prefix + arg.name

    return ret


def emit_argument_list(out, args, need_comma=False):
    out.write(compute_argument_list(args, need_comma))


def start_header(output_prefix, header_guard, license_block=""):
    dot_h = open(output_prefix + ".h", "wb")
    dot_h.write(license_block)
    dot_h.write(("/* This file is machine-generated; DO NOT EDIT! */\n"
                 "#ifndef %s\n"
                 "#define %s\n\n"
                 "#include <hv/iorpc.h>\n\n")
                % (header_guard, header_guard))

    return dot_h


# Emit includes from a list specified by the IDL file.
def emit_includes(out, includes):
    for i in includes:
        out.write("#include <%s>\n" % i)


# The name of the structure used to pack together all of a request's
# RPC parameters.
def compute_param_struct_name(p):
    return "struct " + p.name + "_param"


# Determine whether an RPC call requires a parameter struct.
def needs_params(p):
    return p.has_mem_buffer or p.has_interrupt or p.has_pollfd_setup or \
           p.has_pollfd or p.has_mem_unregister or len(p.args) > 0


# Emit the struct definition for packing a request's RPC parameters.
def emit_param_struct(dot_c, p):
    if not needs_params(p):
        return
    dot_c.write("%s {\n" % (compute_param_struct_name(p)))
    if p.has_mem_buffer:
        dot_c.write("  union iorpc_mem_buffer buffer;\n")
    if p.has_mem_unregister:
        dot_c.write("  union iorpc_mem_unregister buffer;\n")
    if p.has_interrupt:
        dot_c.write("  union iorpc_interrupt interrupt;\n")
    if p.has_pollfd_setup:
        dot_c.write("  union iorpc_pollfd_setup pollfd_setup;\n")
    if p.has_pollfd:
        dot_c.write("  union iorpc_pollfd pollfd;\n")
    for arg in p.args:
        dot_c.write("  %s %s;\n" % (arg.type, arg.name))
    dot_c.write("};\n\n")


#######################################################################
#                             HV code generation                      #
#######################################################################


# Declare a handler function that must be implemented by the HV driver.
def emit_handler_declaration(p, dot_h, env):
    decl = ""
    if len(p.return_values) > 0:
        decl = p.return_values[0].type + " "
    else:
        decl = "err_t "

    decl += "handle_%s_%s(" % (env.prefix, p.name)
    need_comma = False
    if len(env.handler_extra_args) > 0:
        decl += compute_argument_list(env.handler_extra_args, False)
        need_comma = True

    if p.has_sub_offset:
        if need_comma:
            decl += ", "
        decl += "unsigned int %s" % p.sub_offset_name
        need_comma = True

    if p.has_mem_buffer:
        if need_comma:
            decl += ", "
        decl += "PA %s_pa, size_t %s_size, struct iorpc_mem_attr %s_attr" % \
                (p.special_name, p.special_name, p.special_name)
        need_comma = True

    if p.has_mem_unregister:
        if need_comma:
            decl += ", "
        decl += "PA %s_pa" % (p.special_name)
        need_comma = True

    if p.has_interrupt:
        if need_comma:
            decl += ", "
        decl += "int %s_x, int %s_y, int %s_ipi, int %s_event" % \
                (p.special_name, p.special_name,
                 p.special_name, p.special_name)
        need_comma = True

    if p.has_pollfd_setup:
        if need_comma:
            decl += ", "
        decl += "int %s_x, int %s_y, int %s_ipi, int %s_event" % \
                (p.special_name, p.special_name,
                 p.special_name, p.special_name)
        need_comma = True

    if p.has_pollfd:
        if need_comma:
            decl += ", "
        decl += "int %s_cookie" % (p.special_name, )
        need_comma = True

    if p.is_read:
        arg_prefix = "*"
    else:
        arg_prefix = ""
    decl += compute_argument_list(p.args, need_comma, arg_prefix=arg_prefix)
    if len(p.args) > 0:
        need_comma = True

    if p.has_blob:
        if need_comma:
            decl += ", "
        decl += "void* %s, size_t %s_size" % (p.blob_name, p.blob_name)

    decl += ");\n\n"

    dot_h.write(decl)


# Declare the HV dispatch_read and dispatch_write methods.
def emit_dispatch_declaration(dot_c, env, is_read):
    if is_read:
        dot_c.write("int dispatch_%s_read(uint64_t offset, "
                    "void* buf, uint32_t len" % env.prefix)
    else:
        dot_c.write("int dispatch_%s_write(uint64_t offset, "
                    "void* buf, uint32_t len" % env.prefix)
    emit_argument_list(dot_c, env.handler_extra_args, True)
    dot_c.write(")")


# Emit the code for dispatching a particular IORPC opcode to its handler.
def emit_dispatch_case(dot_c, proc, env):
    opcode = compute_query_code_symbol(proc, env.prefix, False)
    dot_c.write("  case %s:\n" % (opcode))
    struct = compute_param_struct_name(proc)
    dot_c.write("    {\n")

    if proc.has_mem_buffer:
        if len(proc.mem_buffer_flags) > 0:
            flags = proc.mem_buffer_flags[0]
            for flag in proc.mem_buffer_flags[1:]:
                flags += " | " + flag
        else:
            flags = "0"
        dot_c.write("      int err = drv_translate_iorpc("
                    "offset, buf, len, %s);\n" % flags)
        dot_c.write("      if (err)\n"
                    "        return err;\n")

    if needs_params(proc):
        dot_c.write("      %s* params = (%s*) buf;\n" % (struct, struct))

    if proc.has_blob:
        if needs_params(proc):
            dot_c.write("      void* blob = (void*)(params + 1);\n" \
                        "      size_t blob_size = len - sizeof(*params);\n")
        else:
            dot_c.write("      void* blob = buf;\n" \
                        "      size_t blob_size = len;\n")
        if proc.is_read:
            # no stack leaks
            dot_c.write("      memset(blob, 0, blob_size);\n")

    # Compute parameters: extra args first, then RPC params
    if proc.is_read:
        arg_prefix = "&params->"
    else:
        arg_prefix = "params->"
    args = ""
    if len(env.handler_extra_args) > 0:
        args += compute_argument_list(env.handler_extra_args,
                                      show_types=False)
        need_comma = True

    if proc.has_sub_offset:
        if need_comma:
            args += ", "
        args += "off.sub_offset"
        need_comma = True

    if proc.has_mem_buffer:
        if need_comma:
            args += ", "
        args += ("params->buffer.hv.pa, "
                 "params->buffer.hv.size, "
                 "params->buffer.hv.attr")
        need_comma = True

    if proc.has_mem_unregister:
        if need_comma:
            args += ", "
        args += "params->buffer.hv.pa"
        need_comma = True

    if proc.has_interrupt:
        if need_comma:
            args += ", "
        args += ("params->interrupt.kernel.x, "
                 "params->interrupt.kernel.y, "
                 "params->interrupt.kernel.ipi, "
                 "params->interrupt.kernel.event")
        need_comma = True

    if proc.has_pollfd_setup:
        if need_comma:
            args += ", "
        args += ("params->pollfd_setup.kernel.x, "
                 "params->pollfd_setup.kernel.y, "
                 "params->pollfd_setup.kernel.ipi, "
                 "params->pollfd_setup.kernel.event")
        need_comma = True

    if proc.has_pollfd:
        if need_comma:
            args += ", "
        args += "params->pollfd.kernel.cookie"
        need_comma = True

    args += compute_argument_list(proc.args, need_comma=need_comma,
                                  show_types=False, arg_prefix=arg_prefix)
    if len(proc.args) > 0:
        need_comma = True

    if proc.has_blob:
        if need_comma:
            args += ", "
        args += "blob, blob_size"

    dot_c.write("      return handle_%s_%s(%s);\n" %
                (env.prefix, proc.name, args))
    dot_c.write("    }\n")


# Emit code for HV dispatch_read or dispatch_write methods.
def emit_dispatch(dot_c, procedures, env, is_read):
    emit_dispatch_declaration(dot_c, env, is_read)
    dot_c.write("\n"
                "{\n"
                "  union iorpc_offset off = { .offset = offset };\n"
                "\n"
                "  switch(off.opcode)\n"
                "  {\n")

    for proc in procedures:
        if proc.is_read == is_read:
            emit_dispatch_case(dot_c, proc, env)

    dot_c.write("  default:\n"
                "    return GXIO_ERR_OPCODE;\n"
                "  }\n"
                "}\n\n")


def generate_hv_files(all_procedures, output_prefix, env):
    # Start the header file.
    guard = "__" + env.prefix.upper() + "_HV_RPC_H__"
    dot_h = start_header(output_prefix, guard)
    emit_includes(dot_h, env.allincludes)
    emit_includes(dot_h, env.hvincludes)

    # Emit all relevant codes.
    for p in all_procedures:
        emit_code_define(p, dot_h, env.prefix, False)
    dot_h.write("\n")

    # Emit handler declarations
    emit_dispatch_declaration(dot_h, env, is_read=True)
    dot_h.write(";\n\n")
    emit_dispatch_declaration(dot_h, env, is_read=False)
    dot_h.write(";\n\n\n")
    for p in all_procedures:
        emit_handler_declaration(p, dot_h, env)

    # Start the source file
    dot_c = open(output_prefix + ".c", "wb")
    dot_c.write("/* This file is machine-generated; DO NOT EDIT! */\n")
    dot_c.write("#include <stdint.h>\n\n")
    dot_c.write("#include <drvintf.h>\n\n")
    dot_c.write("#include \"%s.h\"\n\n" % os.path.basename(output_prefix))

    # Generate parameter structs and dispatch functions.
    for p in all_procedures:
        emit_param_struct(dot_c, p)
    emit_dispatch(dot_c, all_procedures, env, is_read=True)
    emit_dispatch(dot_c, all_procedures, env, is_read=False)

    dot_h.write("#endif  /* !%s */\n" % guard)
    dot_h.close()
    dot_c.close()


#######################################################################
#                        common code for callers                      #
#######################################################################

def emit_export(p, out, env):
    prefix = ""
    if not env.use_context_obj:
        prefix = "__"
    out.write("\nEXPORT_SYMBOL(%s%s_%s);\n\n" % (prefix, env.prefix, p.name))


# Declare a function to be called when sending an RPC request.
def emit_call_signature(p, out, env, is_user,
                        semicolon=True, arg_prefix=""):
    if len(p.return_values) > 0:
        sig = p.return_values[0].type + " "
    else:
        sig = "err_t "

    if is_user or env.use_context_obj:
        sig += "%s_%s(%s_context_t* context" % (env.prefix, p.name, env.prefix)
    else:
        sig += "__%s_%s(int fd" % (env.prefix, p.name)

    if p.has_sub_offset:
        sig += ", unsigned int %s" % p.sub_offset_name

    if p.has_mem_buffer:
        sig += (", void* %s_va, size_t %s_size" %
                (p.special_name, p.special_name))
        if p.has_mem_buffer_flag_word:
            sig += ", unsigned int %s_flags" % p.special_name

    if p.has_mem_unregister:
        sig += ", void* %s_va" % (p.special_name)

    if p.has_interrupt:
        if is_user:
            sig += (", int %s_cpu, int %s_event" %
                    (p.special_name, p.special_name))
        else:
            sig += (", int %s_x, int %s_y, int %s_ipi, int %s_event" %
                    (p.special_name, p.special_name,
                     p.special_name, p.special_name))

    if p.has_pollfd_setup:
        if is_user:
            sig += ", int %s_fd" % (p.special_name, )
        else:
            sig += (", int %s_x, int %s_y, int %s_ipi, int %s_event" %
                    (p.special_name, p.special_name,
                     p.special_name, p.special_name))

    if p.has_pollfd:
        if is_user:
            sig += ", int %s_fd" % (p.special_name, )
        else:
            sig += ", int %s_cookie" % (p.special_name, )

    if p.is_read:
        arg_prefix = "*"
    else:
        arg_prefix = ""
    sig += compute_argument_list(p.args, True, arg_prefix=arg_prefix)

    if p.has_blob:
        if p.is_read:
            sig += ", void* %s, size_t %s_size" % \
                   (p.blob_name, p.blob_name)
        else:
            sig += ", const void* %s, size_t %s_size" % \
                   (p.blob_name, p.blob_name)

    if semicolon:
        sig += ");\n\n"
    else:
        sig += ")\n"

    out.write(sig)


# Emit the on-stack storage required to read or write a block of RPC
# data. In some cases we need to combine the parameter struct with the
# 'blob' of bytes.
def emit_stack_temp(dot_c, p):
    param_struct_name = compute_param_struct_name(p)
    if p.has_blob:
        if needs_params(p):
            dot_c.write("  char temp[sizeof(%s) + %s_size];\n" % \
                        (param_struct_name, p.blob_name))
            dot_c.write("  %s *params = (%s*) temp;\n" % \
                        (param_struct_name, param_struct_name))
            dot_c.write("  memcpy(temp + sizeof(*params), %s, %s_size);\n\n" %
                        (p.blob_name, p.blob_name))
        else:
            if (p.is_read):
                dot_c.write("  void* params = %s;\n" % p.blob_name)
            else:
                dot_c.write("  const void* params = %s;\n" % p.blob_name)
    else:
        dot_c.write("  %s temp;\n" % param_struct_name)
        dot_c.write("  %s *params = &temp;\n" % param_struct_name)
    dot_c.write("\n")


# Return the size to use.
def param_size(p):
    param_struct_name = compute_param_struct_name(p)
    if p.has_blob:
        if needs_params(p):
            return "sizeof(%s) + %s_size" % (param_struct_name, p.blob_name)
        else:
            return "%s_size" % p.blob_name
    else:
        return "sizeof(*params)"


# Emit a write call for use in Linux or userspace.  Write methods
# needs to insert the parameters into a param struct and call pwrite
# or hv_dev_pwrite to pass that structure to the hypervisor.
def emit_write_call(dot_c, p, env, is_user):
    emit_call_signature(p, dot_c, env, is_user, False)
    has_args = needs_params(p) or p.has_blob

    dot_c.write("{\n");

    if is_user or p.has_mem_buffer or p.has_mem_unregister:
        dot_c.write("  int __result;\n")

    if not is_user and (p.has_mem_buffer or p.has_mem_unregister):
        dot_c.write("  unsigned long long __cpa;\n"
                    "  pte_t __pte;\n")

    # Pack parameters into the params struct.
    if has_args:
        size = param_size(p)
        emit_stack_temp(dot_c, p)
        if p.has_mem_buffer:
            if is_user:
                dot_c.write("  params->buffer.user.va = "
                            "(int64_t)(intptr_t)%s_va;\n"
                            "  params->buffer.user.size = %s_size;\n" %
                            (p.special_name, p.special_name))
                if p.has_mem_buffer_flag_word:
                    dot_c.write("  params->buffer.user.flags = %s_flags;\n" %
                                p.special_name)
            else:
                dot_c.write("  __result = va_to_cpa_and_pte"
                            "(%s_va, &__cpa, &__pte);\n"
                            "  if (__result != 0)\n"
                            "    return __result;\n"
                            "  params->buffer.kernel.cpa = __cpa;\n"
                            "  params->buffer.kernel.size = %s_size;\n"
                            "  params->buffer.kernel.pte = __pte;\n" %
                            (p.special_name, p.special_name))
                if p.has_mem_buffer_flag_word:
                    dot_c.write("  params->buffer.kernel.flags = %s_flags;\n" %
                                p.special_name)
        if p.has_mem_unregister:
            if is_user:
                dot_c.write("  params->buffer.user.va = "
                            "(int64_t)(intptr_t)%s_va;\n" % p.special_name)
            else:
                dot_c.write("  __result = va_to_cpa_and_pte"
                            "(%s_va, &__cpa, &__pte);\n"
                            "  if (__result != 0)\n"
                            "    return __result;\n"
                            "  params->buffer.kernel.cpa = __cpa;\n" %
                            p.special_name)
        if p.has_interrupt:
            if is_user:
                dot_c.write("  params->interrupt.user.cpu = %s_cpu;\n"
                            "  params->interrupt.user.event = %s_event;\n" %
                            (p.special_name, p.special_name))
            else:
                dot_c.write("  params->interrupt.kernel.x = %s_x;\n"
                            "  params->interrupt.kernel.y = %s_y;\n"
                            "  params->interrupt.kernel.ipi = %s_ipi;\n"
                            "  params->interrupt.kernel.event = %s_event;\n" %
                            (p.special_name, p.special_name,
                             p.special_name, p.special_name))

        if p.has_pollfd_setup:
            if is_user:
                dot_c.write("  params->pollfd_setup.user.fd = %s_fd;\n" %
                            (p.special_name, ))
            else:
                dot_c.write("  params->pollfd_setup.kernel.x = %s_x;\n"
                            "  params->pollfd_setup.kernel.y = %s_y;\n"
                            "  params->pollfd_setup.kernel.ipi = %s_ipi;\n"
                            "  params->pollfd_setup.kernel.event = "
                            "%s_event;\n" %
                            (p.special_name, p.special_name,
                             p.special_name, p.special_name))

        if p.has_pollfd:
            if is_user:
                dot_c.write("  params->pollfd.user.fd = %s_fd;\n" %
                            (p.special_name, ))
            else:
                dot_c.write("  params->pollfd.kernel.cookie = %s_cookie;\n" %
                            (p.special_name, ))
        for arg in p.args:
            dot_c.write("  params->%s = %s;\n" % (arg.name, arg.name))
        dot_c.write("\n")

    # Compute the opcode, including a possible sub_offset.
    opcode = compute_query_code_symbol(p, env.prefix, is_user)
    if p.has_sub_offset:
        offset = "(((uint64_t)%s << 32) | %s)" % (p.sub_offset_name, opcode)
    else:
        offset = opcode

    # Call the appropriate pwrite variant.
    if is_user:
        write_func = "__result = pwrite(context->fd, "
    elif env.use_context_obj:
        write_func = "return hv_dev_pwrite(context->fd, 0, (HV_VirtAddr) "
    else:
        write_func = "return hv_dev_pwrite(fd, 0, (HV_VirtAddr) "
    if has_args:
        dot_c.write("  %s params, %s, %s);\n" %
                    (write_func, size, offset))
    else:
        dot_c.write("  %s NULL, 0, %s);\n" % (write_func, offset))

    if is_user:
        dot_c.write("  if (__result < 0)\n"
                    "    __result = -errno;\n")
        dot_c.write("  return __result;\n")

    dot_c.write("}\n\n")

    if not is_user:
        emit_export(p, dot_c, env)


# Emit a read call for use in Linux or userspace.  Read calls need to
# call pread or hv_dev_pread and return the resulting parameter
# struct.
def emit_read_call(dot_c, p, env, is_user):
    emit_call_signature(p, dot_c, env, is_user, False, "*")
    dot_c.write("{\n"
                "  int __result;\n")
    size = param_size(p)
    emit_stack_temp(dot_c, p)

    # Compute the opcode, including a possible sub_offset.
    opcode = compute_query_code_symbol(p, env.prefix, is_user)
    if p.has_sub_offset:
        offset = "(((uint64_t)%s << 32) | %s)" % (p.sub_offset_name, opcode)
    else:
        offset = opcode

    # Call the appropriate pread variant.
    if is_user:
        read_func = "pread(context->fd, "
    elif env.use_context_obj:
        read_func = "hv_dev_pread(context->fd, 0, (HV_VirtAddr) "
    else:
        read_func = "hv_dev_pread(fd, 0, (HV_VirtAddr) "
    dot_c.write("  __result = %s params, %s, %s);\n" %
                (read_func, size, offset))

    # Unpack results.
    for arg in p.args:
        dot_c.write("  *%s = params->%s;\n" % (arg.name, arg.name))
    if p.has_blob:
        # If there was no args struct, the read call filled the blob
        # directly and we don't need to memcpy.
        if needs_params(p):
            dot_c.write("  memcpy(%s, temp + sizeof(*params), %s_size);\n" % \
                        (p.blob_name, p.blob_name))

    if is_user:
        dot_c.write("  if (__result < 0)\n"
                    "    __result = -errno;\n\n")

    dot_c.write("\n"
                "  return __result;\n"
                "}\n\n")

    if not is_user:
        emit_export(p, dot_c, env)


#######################################################################
#                      user library code generation                   #
#######################################################################


def generate_user_files(all_procedures, output_prefix, env):
    is_user = True
    guard = "__" + env.prefix.upper() + "_USER_RPC_H__"
    dot_h = start_header(output_prefix, guard)
    emit_includes(dot_h, env.allincludes)
    emit_includes(dot_h, env.userincludes)
    dot_h.write("\n\n")

    # Emit all relevant codes.
    for p in all_procedures:
        if p.is_nouser:
            continue
        emit_code_define(p, dot_h, env.prefix, is_user)
    dot_h.write("\n")

    # Emit API calls.
    for p in all_procedures:
        if p.is_nouser:
            continue
        if p.is_read:
            emit_call_signature(p, dot_h, env, is_user, arg_prefix="*")
        else:
            emit_call_signature(p, dot_h, env, is_user)

    # Emit implementation.
    dot_c = open(output_prefix + ".c", "wb")
    dot_c.write("/* This file is machine-generated; DO NOT EDIT! */\n"
                "#define _XOPEN_SOURCE 500 // for pwrite()\n"
                "#define _FILE_OFFSET_BITS 64\n"
                "#include \"%s.h\"\n"
                "#include <errno.h>\n"
                "#include <stddef.h>\n"
                "#include <unistd.h>\n\n" % os.path.basename(output_prefix))
    for p in all_procedures:
        if p.is_nouser:
            continue
        emit_param_struct(dot_c, p)
        if p.is_read:
            emit_read_call(dot_c, p, env, is_user)
        else:
            emit_write_call(dot_c, p, env, is_user)

    dot_h.write("#endif  /* !%s */\n" % guard)
    dot_h.close()
    dot_c.close()


#######################################################################
#                          Linux code generation                      #
#######################################################################

class Ifdef(object):
    """Class to handle #ifdef'ing some of our output values but not others."""
    def __init__(self, fd, key, ndef):
        """
        fd - file on which to emit the #ifdefs.
        key - symbol to use in the #ifdefs.
        ndef - if True, emit #ifndef instead of #ifdef.
        """
        self.fd = fd
        self.is_on = False
        self.key = key
        if ndef:
            self.ndef = "n"
        else:
            self.ndef = ""

    def set_state(self, new_state):
        """
        Modify the current #ifdef state.
        new_state - if True, output subsequent to this point should be under
                    the #ifdef.
        """
        if new_state:
            if not self.is_on:
                self.fd.write("#if%sdef %s\n\n" % (self.ndef, self.key))
                self.is_on = True
        else:
            if self.is_on:
                self.fd.write("\n#endif /* %s */\n\n" % self.key)
                self.is_on = False


def generate_linux_files(all_procedures, output_prefix, env):
    is_user = False
    guard = "__" + env.prefix.upper() + "_LINUX_RPC_H__"
    dot_h = start_header(output_prefix, guard, gpl_license_block)
    emit_includes(dot_h, env.allincludes)
    emit_includes(dot_h, env.linuxincludes)
    dot_h.write("\n\n")

    id_h = Ifdef(dot_h, "TILERA_PUBLIC", True)

    # Emit all relevant codes.
    for p in all_procedures:
        id_h.set_state(p.is_nokernel)
        emit_code_define(p, dot_h, env.prefix, is_user)
    dot_h.write("\n")

    # Emit declarations for API calls.
    for p in all_procedures:
        id_h.set_state(p.is_nokernel)
        if p.is_read:
            emit_call_signature(p, dot_h, env, is_user, arg_prefix="*")
        else:
            emit_call_signature(p, dot_h, env, is_user)

    id_h.set_state(False)
    dot_h.write("#endif  /* !%s */\n" % guard)
    dot_h.close()

    # Emit implementation.
    dot_c = open(output_prefix + ".c", "wb")
    dot_c.write(gpl_license_block)
    base = "gxio/" + os.path.basename(output_prefix)
    dot_c.write("/* This file is machine-generated; DO NOT EDIT! */\n")
    dot_c.write("#include \"%s.h\"\n\n" % base)

    id_c = Ifdef(dot_c, "TILERA_PUBLIC", True)

    # Emit definitions for API calls.
    for p in all_procedures:
        id_c.set_state(p.is_nokernel)
        emit_param_struct(dot_c, p)
        if p.is_read:
            emit_read_call(dot_c, p, env, is_user)
        else:
            emit_write_call(dot_c, p, env, is_user)

    id_c.set_state(False)

    dot_c.close()
