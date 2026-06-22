#!/usr/bin/env python
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

# Code for turning an RPC IDL into code.
import sys
import parse
import emit_c


def usage(me, status):
    print ("Usage: %s -o <output_prefix> --target hv|user [--use-context-obj]"
           "[--] filename ...") % me
    sys.exit(status)


def main(argv):
    all_procedures = []
    env = parse.Environment()

    parsing_flags = True

    target = None
    output_prefix = None

    i = 1
    while i < len(argv):
        arg = argv[i]
        i += 1

        if parsing_flags and len(arg) > 0 and arg[0] == '-':
            if arg == "--":
                # Everything remaining will be treated as a filename.
                parsing_flags = False
            elif arg == "--help" or arg == "-h":
                usage(argv[0], 0)
            elif arg == "-o":
                if i == len(argv):
                    usage(argv[0], 1)
                output_prefix = argv[i]
                i += 1
            elif arg == "--target":
                if i == len(argv):
                    usage(argv[0], 1)
                target = argv[i]
                if not target in ("user", "linux", "hv"):
                    usage(argv[0], 1)
                i += 1
            elif arg == "--use-context-obj":
                env.use_context_obj = True

            continue

        parse.parse_procedures(env, all_procedures, arg)

    if output_prefix is None:
        sys.stderr.write("Must specify -o OUTPUT_PREFIX.\n")
        usage(argv[0], 1)
    elif target is None:
        sys.stderr.write("Must specify --target TARGET.\n")
        usage(argv[0], 1)

    # Headers required by the code generator.
    env.hvincludes.append("string.h")
    env.linuxincludes.append("linux/string.h")
    env.userincludes.append("string.h")

    env.linuxincludes.append("linux/module.h")
    env.linuxincludes.append("asm/pgtable.h")

    if target == "hv":
        emit_c.generate_hv_files(all_procedures, output_prefix, env)
    elif target == "user":
        emit_c.generate_user_files(all_procedures, output_prefix, env)
    elif target == "linux":
        emit_c.generate_linux_files(all_procedures, output_prefix, env)


# Run main if we are running from the command line
if __name__ == "__main__":
    main(sys.argv)
