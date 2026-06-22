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
# Dump a hypervisor filesystem image, or a bootrom.
#
# usage: dumphvfs [ -xvXg ] <input.fs
#
# -x  The files in the filesystem are extracted.  Without this flag, the
#     names and lengths of the files are listed.
#
# -v  More data about the contents of the filesystem and any preceding
#     bootstream is printed.
#
# -X  The components of the bootstream are extracted.
#
# -g  The input image is a TILE-Gx image.  Only needed if the input file
#     is a bootrom.
#

import sys
import os
import struct
from optparse import OptionParser

# Maximum length of a file name
HV_PATH_MAX = 256

# Filesystem magic number
FS_MAGIC = 0x73467648   # "HvFs"

#
# Functions to read 32-bit words.
#
wd1_fmt = "<I"
wd1_len = struct.calcsize(wd1_fmt)


def read1():
    return struct.unpack(wd1_fmt, sys.stdin.read(wd1_len))

wd2_fmt = "<II"
wd2_len = struct.calcsize(wd2_fmt)


def read2():
    return struct.unpack(wd2_fmt, sys.stdin.read(wd2_len))

wd3_fmt = "<III"
wd3_len = struct.calcsize(wd3_fmt)


def read3():
    return struct.unpack(wd3_fmt, sys.stdin.read(wd3_len))

wd4_fmt = "<IIII"
wd4_len = struct.calcsize(wd4_fmt)


def read4():
    return struct.unpack(wd4_fmt, sys.stdin.read(wd4_len))

wd5_fmt = "<IIIII"
wd5_len = struct.calcsize(wd5_fmt)


def read5():
    return struct.unpack(wd5_fmt, sys.stdin.read(wd5_len))

#
# Functions to read 64-bit words.
#
lwd1_fmt = "<Q"
lwd1_len = struct.calcsize(lwd1_fmt)


def lread1():
    return struct.unpack(lwd1_fmt, sys.stdin.read(lwd1_len))

lwd2_fmt = "<QQ"
lwd2_len = struct.calcsize(lwd2_fmt)


def lread2():
    return struct.unpack(lwd2_fmt, sys.stdin.read(lwd2_len))

lwd3_fmt = "<QQQ"
lwd3_len = struct.calcsize(lwd3_fmt)


def lread3():
    return struct.unpack(lwd3_fmt, sys.stdin.read(lwd3_len))

lwd4_fmt = "<QQQQ"
lwd4_len = struct.calcsize(lwd4_fmt)


def lread4():
    return struct.unpack(lwd4_fmt, sys.stdin.read(lwd4_len))

lwd5_fmt = "<QQQQQ"
lwd5_len = struct.calcsize(lwd5_fmt)


def lread5():
    return struct.unpack(lwd5_fmt, sys.stdin.read(lwd5_len))


#
# Function to dump out a file containing bootstream bits.
#
def bdump(comp, addr, len):
    outfile = open("%s@%x" % (comp, addr), "w")
    outfile.write(sys.stdin.read(len))
    outfile.close()


#
# Function to skip bootstream bits.
#
def bskip(comp, addr, len):
    sys.stdin.seek(len, 1)


#
# Read a null-terminated string, return it and the number of bytes read.
#
def read_ntstr():
    bytes = 1
    str = ""
    chr = sys.stdin.read(1)
    while ord(chr):
        str += chr
        bytes += 1
        chr = sys.stdin.read(1)

    return (str, bytes)


#
# Print a tile position in dynamic header format.
#
def pos_fmt(wd):
    return "(%d,%d)" % ((wd >> 18) & 0x7FF, (wd >> 7) & 0x7FF)


def main(argv):
    parser = OptionParser(usage="%prog [options] <input_file",
                  description="%prog analyzes the contents of "
                  "a hypervisor file system image or a bootrom "
                  "file.")
    parser.add_option("-x", "--extract", dest="x", action="store_true",
              help="extract HVFS files; by default, file names "
              "and lengths are listed")
    parser.add_option("-v", "--verbose", dest="v", action="store_true",
              help="provide more verbose output")
    parser.add_option("-X", "--extract-boot", dest="X", action="store_true",
              help="extract segments of bootstream")
    parser.add_option("-g", "--gx", dest="g", action="store_true",
                      default=True,
              help="input file is a TILE-Gx image or bootrom (default)")
    parser.add_option("-p", "--pro", dest="g", action="store_false",
              help="input file is a TILEPro image or bootrom")

    #
    # Parse arguments
    #
    (opt, args) = parser.parse_args()

    if len(args) > 0:
        sys.stderr.write("Unexpected arguments\n")
        sys.exit(1)

    if opt.g:
        word_len = 8
        restart_addr = 0
        mread1 = lread1
        mread2 = lread2
        mread3 = lread3
        mread4 = lread4
        mread5 = lread5
    else:
        word_len = 4
        restart_addr = 48
        mread1 = read1
        mread2 = read2
        mread3 = read3
        mread4 = read4
        mread5 = read5

    if opt.X:
        bout = bdump
    else:
        bout = bskip

    #
    # See if this is an hvfs, or a bootrom file
    #
    (word0,) = read1()

    if word0 != FS_MAGIC:
        if opt.g:
            (nextw,) = read1()
            word0 = (nextw << 32) | word0

        #
        # It's a bootrom file; skip over the bootable stuff.  First
        # get rid of the L0.5 booter.
        #
        boothdr = "<III"
        boothdr_len = struct.calcsize(boothdr)

        if opt.v:
            print "Level-0.5 Boot:"

        #
        # We already read in the first word, so we just get one more
        # in the first segment.
        #
        seglen = word0
        (addr,) = mread1()

        while True:
            if opt.v:
                print "  Load %d words at address %#x" % \
                    (seglen, addr)

            bout("lh", addr, seglen * word_len)

            (jump,) = mread1()
            if jump != restart_addr:
                break

            (seglen, addr) = mread2()

        if opt.v:
            print "  Jump to %#x" % jump

        #
        # Now skip the L1 booter.
        #
        if opt.v:
            print "Level-1 Boot:"

        (seglen, addr, crc) = mread3()

        while seglen:
            if opt.v:
                print "  Load %d words at address %#x, " \
                                      "CRC %#x" % (seglen, addr, crc)

            bout("l1", addr, seglen * word_len)

            (seglen, addr, crc) = mread3()

        if opt.v:
            print "  Jump to %#x, CRC %#x" % (addr, crc)

        #
        # Read L1 boot parameters.
        #
        (pred, master, ulhc, lrhc) = mread4()
        extra = []
        for i in xrange((pred >> 1) & 0x3F):
            extra.extend(mread1())
        if extra:
            extra = "extra " + ", ".join([hex(i) for i in extra]) + "; "
        else:
            extra = ""
        (crc,) = mread1()

        if opt.v:
            print "L1 parameters: predecessor %#x; master %s; " \
                "ulhc %s; lrhc %s; %sCRC %#x" % \
                (pred, pos_fmt(master), pos_fmt(ulhc),
                 pos_fmt(lrhc), extra, crc)

        #
        # Now skip the hypervisor.
        #
        if opt.v:
            print "Hypervisor:"

        (seglen, addr, crc) = mread3()

        while seglen:
            if opt.v:
                print "  Load %d words at address %#x, " \
                                      "CRC %#x" % (seglen, addr, crc)

            bout("hv", addr, seglen * word_len)

            (seglen, addr, crc) = mread3()

        if opt.v:
            print "  Jump to %#x, CRC %#x" % (addr, crc)

        #
        # Finally make sure we've hit the header
        #
        (magic,) = read1()
        if magic != FS_MAGIC:
            sys.stderr.write("Bad magic number %#x\n" % magic)
            sys.exit(1)

    #
    # Read the rest of the header.
    #
    (num_files, total_len, namebase, crc) = read4()

    #
    # Read in the inodes.
    #
    inodes = []
    for i in range(num_files):
        inodes.append(read4())

    #
    # curpos is our file position; we update it when we read or seek.
    #
    curpos = 16 + 16 * num_files

    #
    # Print the filesystem ID string if requested.
    #
    sys.stdin.seek(namebase - curpos, 1)
    (str, bytes) = read_ntstr()
    curpos += bytes

    if not opt.x:
        print str

    #
    # Read in all of the filenames, and print them if requested.  We
    # assume that things are in order; although the filesystem format
    # doesn't technically require that, we know that's how it's built.
    #
    filenames = []

    for (name_off, data_off, file_len, file_flags) in inodes:
        sys.stdin.seek(name_off - curpos, 1)
        (str, bytes) = read_ntstr()
        curpos = name_off + bytes

        filenames.append(str)

        if not opt.x:
            print "%10d %s" % (file_len, str)

    #
    # If we're writing output files, do 'em.
    #
    if opt.x:
        for i in range(len(inodes)):
            (name_off, data_off, file_len, file_flags) = inodes[i]

            #
            # Strip leading slashes from the name.
            #
            fname = filenames[i]
            while fname[1] == "/":
                del fname[1]

            #
            # Create the containing directory if needed.
            #
            dirname = os.path.dirname(fname)
            if dirname and not os.path.exists(dirname):
                os.makedirs(dirname)

            #
            # Write the file.
            #
            outfile = open(fname, "w")

            sys.stdin.seek(data_off - curpos, 1)
            outfile.write(sys.stdin.read(file_len))
            curpos = data_off + file_len

            outfile.close()

    sys.exit(0)

if __name__ == "__main__":
    main(sys.argv)
