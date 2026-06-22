#!/usr/bin/env python
#
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
#
# Convert a binary data file into a C source file
#
# usage: bin2c <name-for-C-array> <infile.bin >outfile.c
#

import sys


def main(argv):
    dataname = argv[1]
    raw_bytes = sys.stdin.read()
    print "const unsigned char %s_data[] = {" % dataname
    for i in range(len(raw_bytes)):
        print "0x%x," % ord(raw_bytes[i])
    print "};"
    print "const unsigned int %s_len = sizeof (%s_data);" % (dataname,
        dataname)


if __name__ == "__main__":
    main(sys.argv)
