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
import emit_java


def usage(me, status):
    print ("Usage: %s -o OUTPUT_PREFIX --target TARGET [--dispatch DISPATCH] "
           "[--c | --java] [--codes-only] [--] [filename...]") % me
    sys.exit(status)


def main(argv):
    all_procedures = []

    parsing_flags = True

    codes_only = False
    language = None
    target = None
    dispatch = "dispatch_packet"
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
            elif arg == "--c" or arg == "-c":
                language = "c"
            elif arg == "-o":
                if i == len(argv):
                    usage(1)
                output_prefix = argv[i]
                i += 1
            elif arg == "--java":
                language = "java"
            elif arg == "--target":
                if i == len(argv):
                    usage(1)
                target = argv[i]
                i += 1
            elif arg == "--dispatch":
                if i == len(argv):
                    usage(1)
                dispatch = argv[i]
                i += 1
            elif arg == "--codes-only":
                codes_only = True
            continue

        parse.parse_procedures(all_procedures, arg)

    if output_prefix is None:
        sys.stderr.write("Must specify -o OUTPUT_PREFIX.\n")
        usage(argv[0], 1)
    elif language is None:
        sys.stderr.write("Must specify --c or --java.\n")
        usage(argv[0], 1)
    elif target is None:
        sys.stderr.write("Must specify --target TARGET.\n")
        usage(argv[0], 1)

    if language == "c":
        emit_c.generate_c_output_files(all_procedures, target, dispatch,
                                       output_prefix, codes_only)
    else:
        emit_java.generate_java_output_file(all_procedures, target,
                                            output_prefix)


# Run main if we are running from the command line
if __name__ == "__main__":
    main(sys.argv)
