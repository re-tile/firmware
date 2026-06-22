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

#
# Create a bootable "bootrom" file, consisting of the hypervisor binaries
# (booters plus the hypervisor executable), followed by a hypervisor file
# system image containing files determined by the user.  Normally those
# files consist of a hypervisor configuration file and one or more client
# executables (often these are operating system images).
#
# See "usage()" (below) for usage details.
#

import binascii
import os
import platform
import pwd
import re
import socket
import struct
import sys
import time
import warnings

import hvc


#
# Several things change slightly if we're running natively on Tile.
#
is_native = (platform.machine() == "tilepro" or platform.machine() == "tilegx")

#
# We can't just use the subprocess module, since we have to run on Python 2.3,
# where it doesn't exist.  However, if we use popen2 on Python 2.6 or later, we
# get a deprecation warning, and our native Python is 2.6.  So, we use both
# depending on which version of Python we're using.
#
if sys.version_info >= (2, 4):
    import subprocess

    def run(args, verbose=False):
        """Run a process.  Return True if the process terminated normally,
           and False otherwise.
        """

        try:
            proc = subprocess.Popen(args, stdout=subprocess.PIPE,
                                    stderr=subprocess.STDOUT, close_fds=True)

        except OSError, exc:
            warn("Couldn't run " + args[0] + ": " + exc.strerror + ".")
            return False

        err, dummy = proc.communicate()
        status = proc.wait()

        okay = os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0

        if err and (not okay or verbose):
            sys.stderr.write(err)

        return okay
else:
    import popen2

    def run(args, verbose=False):
        """Run a process.  Return True if the process terminated normally,
           and False otherwise.
        """

        proc = popen2.Popen4(args)

        err = proc.fromchild.read()
        status = proc.wait()

        okay = os.WIFEXITED(status) and os.WEXITSTATUS(status) == 0

        if err and (not okay or verbose):
            sys.stderr.write(err)

        return okay

#
# Python warns about "tempnam", used below, but there isn't really a good
# alternative since strip insists on creating an output file, and wants to
# seek in it.  So, turn the warning off.
#
# ISSUE: Python is so lame that "tempnam" only uses the first five chars
# of the requested "prefix".
#
warnings.filterwarnings("ignore",
            "tempnam is a potential security risk to your program",
            RuntimeWarning)


def unlink_blindly(path):
    try:
        os.unlink(path)
    except OSError:
        pass


def warn(str):
    sys.stderr.write("tile-mkboot: Warning: " + str + "\n")


def die(str):
    sys.stderr.write("tile-mkboot: " + str + "\n")
    sys.exit(1)


# Maximum length of a file name
HV_PATH_MAX = 256

# Filesystem magic number
FS_MAGIC = 0x73467648   # "HvFs"


# A file which CRC's anything written to it.
class CRCFile(file):
    def __init__(self, *args, **kwargs):
        super(CRCFile, self).__init__(*args, **kwargs)
        self.file_crc = 0

    def reset_crc(self):
        self.file_crc = 0

    def crc(self):
        return self.file_crc

    def write(self, data):
        self.file_crc = binascii.crc32(data, self.file_crc)
        return super(CRCFile, self).write(data)

# Format/length of a fs header
hdr_fmt = "<IIII"
hdr_len = struct.calcsize(hdr_fmt)


# Write out a fs header, without its CRC.
def out_hdr(file, magic, ninode, len, desc_off):
    return file.write(struct.pack(hdr_fmt, magic, ninode, len, desc_off))

# Format/length of a fs CRC
crc_fmt = "<i"
crc_len = struct.calcsize(crc_fmt)


# Write out a fs CRC
def out_crc(file, csum):
    return file.write(struct.pack(crc_fmt, csum))

# Format/length of an inode
inode_fmt = "<IIII"
inode_len = struct.calcsize(inode_fmt)


# Write out an inode
def out_ino(file, name, data, len, flags):
    return file.write(struct.pack(inode_fmt, name, data, len, flags))


# Round up to next multiple of our word size
def roundup(len):
    return (len + 7) & ~7


# Split up an argument into infile and outname parts, if necessary
def argsplit(str):
    tok = str.split("=", 1)
    if len(tok) == 2:
        return (tok[0], tok[1])
    else:
        return (tok[0], tok[0])


# Check a list of filenames for duplicates, print error and exit if any found
def ckdups(namelist):
    sorted_names = list(namelist)
    sorted_names.sort()

    for i in range(len(sorted_names) - 1):
        if sorted_names[i] == sorted_names[i + 1]:
            die("Duplicate filename '%s'." % sorted_names[i])


# Print a usage message and exit
#
# The "--compress" option causes files listed in an input configuration file
# as client files will be compressed before inclusion in the output
# image, making the output image smaller.  Only files which are
# stripped are also compressed.
#
# The "--no-strip" option causes ELF executables specified as input
# files are copied unchanged into the output image.  Normally, such
# files are first processed to have their symbol information removed,
# making the output image smaller.
#
# The "--relative" option causes the longest common pathname prefix is
# removed from the file names before they're written to the filesystem's
# index.  This processing occurs after any outname=infile renamings, and
# may interact badly with them.
#
# The "--fixed" option causes the generated bootrom to not contain
# any host- or time-varying portions.
#
# The "--hvc" option specifies a hypervisor configuration file (.hvc) to
# read in.  The filename may itself contain $ expressions to be expanded.
# The --hvi search path is used to find this file.  More than one file
# may be specified, in which case their contents are concatenated.
#
# The "--hvi" option specifies a directory to append to the search path for
# "--hvc", and for 'include' statements within those files.  The directory
# name may itself contain $ expressions to be expanded.
#
# The "--hvd" option defines a variable used when expanding $VAR expressions
# in .hvc files.  Its value may itself contain $ expressions to be expanded.
#
# The "--hvx" option extends the definition of the special "XARGS" variable.
#
# The "--output" option is mandatory unless the "--dump-hvc" option is used.
#
# The "--hv-bin-dir" option specifies a directory in which to find the
# hypervisor binary files ("hv", "hv_l1boot", and "hv_lhboot"), defaulting
# to ".../tile/boot".
#
# The "--linux-dir" option specifies a directory in which to find the
# Linux binary file ("vmlinux") defaulting to ".../tile/boot".  This is
# only used with the "--linux" option, and in fact implies it.
#
# The "--linux" option requests that the file named "vmlinux" from the Linux
# binary file directory be added to the filesystem.
#
# If the outname=infile form is used for an input argument, the input file
# infile is added to the filesystem under the name "outname".  Otherwise,
# the name in the output filesystem is identical to the input file name.
#
def usage(str):
    sys.stderr.write("tile-mkboot: " + str + "\n\n")
    sys.stderr.write("""\
Usage: tile-mkboot [options] [--] [ [<outname>=]<infile> ... ]

Options:
  --compress (or -c) = Compress client executables.
  --dump-hvc = Dump out preprocessed hvc file and exit.
  --fixed (or -f) = Avoid adding host- or time-varying data to bootrom.
  --hv-bin-dir <directory> = Specify hypervisor binary directory.
  --hvc <file> = Process hypervisor configuration file.
  --hvd <var>[=<value>] = Define a variable for hvc files.
  --hvi <directory> = Add include directory for hvc files.
  --hvx <value> = Extend definition of the 'XARGS' hvc variable.
  --linux (or -l) = Add the file 'vmlinux' to the output filesystem.
  --linux-dir <directory> = Specify Linux directory; implies --linux.
  --no-strip (or -n) = Do not strip or compress any input files.
  --output (or -o) <outfile> = Specify output file.
  --relative (or -r) = Remove common path prefixes.
  --verbose (or -v) = Emit extra diagnostic output.

The '--output' option is required unless the '--dump-hvc' option is used.
Multiple '--hvc', '--hvd', '--hvi', and '--hvx' options can be used.
At least one '--hvc' option is required.

""")
    sys.exit(1)


ELF_MAGIC = "\177ELF"


# Read a file into a string.  If dostrip is true, and the file is an ELF file,
# try to strip it first; if dostrip and compress are true, and the file is an
# ELF file, the file is compressed after it's stripped.
#
def get_and_process(name, dostrip, compress, verbose):
    try:
        f = file(name)
    except IOError, (errno, strerror):
        die("Cannot open input file '%s': %s" % (name, strerror))

    if not dostrip:
        return f.read()
    magic = f.read(len(ELF_MAGIC))
    if magic == ELF_MAGIC:
        #
        # If we're compressing, let's see if there's a prestripped and
        # precompressed version of the file that we can use instead.  We won't
        # use it if it's older than the original version.
        #
        if compress:
            try:
                prename = name + ".strip.bz2"
                prefile = file(prename)
                if os.path.getmtime(prename) >= os.path.getmtime(name):
                    retval = prefile.read()
                    f.close()
                    prefile.close()
                    if verbose:
                        print "Using precompressed file %s" % prename
                    return retval
                else:
                    prefile.close()
                    if verbose:
                        print "Not using precompressed file %s " \
                            "(older than uncompressed file)." % prename
            except IOError, (errno, strerror):
                if verbose:
                    print "Cannot open precompressed file %s: %s" % \
                            (prename, strerror)

        tmpfn = os.tempnam(None, "mkb-s")

        #
        # Run the strip command in a subprocess.  We save stdout
        # and stderr and only print them out if the strip didn't
        # succeed, or if we're in verbose mode; this is to avoid
        # cluttering the output with unimportant warning messages
        # like "warning: allocated section 'foo' not in segment".
        #
        if is_native:
            strip_args = (basedir + "/../bin/strip", "-o", tmpfn, name)
        else:
            strip_args = (basedir + "/../bin/tile-strip", "-o", tmpfn, name)
        strip_succeeded = run(strip_args, verbose)

        #
        # If the strip worked, remember we need to use the data from
        # the temporary file.
        #
        if not strip_succeeded:
            warn("Couldn't strip input file '%s', using "
                 "unstripped version." % name)

        #
        # Now compress the temporary file if needed.
        #
        if strip_succeeded and compress:
            bzip_args = ("bzip2", tmpfn)
            bzip_succeeded = run(bzip_args, verbose)

            #
            # If the compress worked, use the new compressed
            # temporary file.
            #
            if bzip_succeeded:
                tmpfn = tmpfn + ".bz2"
            else:
                warn("Couldn't compress input file '%s', "
                     "using uncompressed version." % name)

        #
        # If there was no error, replace the original file with the
        # new one.
        #
        if strip_succeeded:
            f.close()
            f = file(tmpfn)
            magic = ""

        #
        # Clean up (blindly, in case we didn't create a file).
        #
        unlink_blindly(tmpfn)

    #
    # Read the rest of the original file, or the new one, and return it.
    #
    retval = magic + f.read()
    f.close()
    return retval


# Main program.
#
def main(argv):
    #
    # Parse arguments
    #
    relative = False
    fixed = False
    dostrip = True
    docompress = False
    linux = False
    verbose = False
    dump_hvc = False
    outfilename = None

    # Default directories for various files.
    if is_native:
        hv_bin_dir = "/boot"
        linux_dir = "/boot"
        hvc_dir = "/etc/hvc"
    else:
        hv_bin_dir = basedir + "/../tile/boot"
        linux_dir = basedir + "/../tile/boot"
        hvc_dir = basedir + "/../tile/etc/hvc"

    # All --hvc and config= arguments, in order.
    config_files = []

    # Positional arguments like "foo=bar" or simply "foo".
    positional_args = []

    # Variable definitions, for our CONFIG_VERSION string.
    vardefs = []

    while len(argv) > 1:
        if argv[1] == "-r" or argv[1] == "--relative":
            relative = True
            del argv[1]
        if argv[1] == "-f" or argv[1] == "--fixed":
            fixed = True
            del argv[1]
        elif argv[1] == "-n" or argv[1] == "--no-strip":
            dostrip = False
            del argv[1]
        elif argv[1] == "-c" or argv[1] == "--compress":
            docompress = True
            del argv[1]
        elif argv[1] == "-v" or argv[1] == "--verbose":
            verbose = True
            del argv[1]
        elif argv[1] == "-o" or argv[1] == "--output":
            if len(argv) < 3:
                usage("Missing args for '%s'." % argv[1])
            outfilename = argv[2]
            del argv[1:3]
        elif argv[1] == "--hvc":
            if len(argv) < 3:
                usage("Missing args for '%s'." % argv[1])
            config_files.append(argv[2])
            del argv[1:3]
        elif argv[1] == "--hvd":
            if len(argv) < 3:
                usage("Missing args for '%s'." % argv[1])
            match = re.match('^([^=]+)=(.*)$', argv[2])
            if match:
                hvc.process_define(match.group(1),
                           match.group(2))
            else:
                hvc.process_define(argv[2], "1")
            vardefs.append(argv[2])
            del argv[1:3]
        elif argv[1] == "--hvi":
            if len(argv) < 3:
                usage("Missing args for '%s'." % argv[1])
            hvc.process_include_dir(argv[2])
            del argv[1:3]
        elif argv[1] == "--hvx":
            if len(argv) < 3:
                usage("Missing args for '%s'." % argv[1])
            hvc.process_extend_define("XARGS", argv[2])
            vardefs.append("XARGS+=%s" % argv[2])
            del argv[1:3]
        elif argv[1] == "--dump-hvc":
            dump_hvc = True
            del argv[1]
        elif argv[1] == "--hv-bin-dir":
            if len(argv) < 3:
                usage("Missing args for '%s'." % argv[1])
            hv_bin_dir = argv[2]
            del argv[1:3]
        elif argv[1] == "--linux-dir":
            if len(argv) < 3:
                usage("Missing args for '%s'." % argv[1])
            linux_dir = argv[2]
            linux = True
            del argv[1:3]
        elif argv[1] == "-l" or argv[1] == "--linux":
            linux = True
            del argv[1]
        elif argv[1] == "--":
            # "--" stops all argument processing, allowing paths
            # with leading dashes.
            positional_args += argv[2:]
            break
        elif argv[1][:1] == "-":
            usage("Unrecognized option '%s'." % argv[1])
        else:
            positional_args.append(argv[1])
            del argv[1]

    # Get list of files.
    argpairs = [argsplit(i) for i in positional_args]
    names = [i for (i, j) in argpairs]
    files = [j for (i, j) in argpairs]

    # If --linux was specified, add its files to the list.
    if linux:
        names.append("vmlinux")
        files.append(linux_dir + "/vmlinux")

    # If in relative mode, trim filenames.
    if relative:
        dirnames = [os.path.dirname(i[1]) for i in names]
        preflen = len(os.path.commonprefix(dirnames)) + 1
        if preflen > 1:
            names = [i[preflen:] for i in names]

    # Make sure we don't have any duplicate names.
    ckdups(names)

    # Extract out old-style "config=" arguments (there can be at
    # most one) so we can treat them just like "--hvc".
    for i in xrange(len(files)):
        if names[i] == "config":
            warn("Deprecated 'config=filename' argument "
                 "found. Use '--hvc filename' instead.\n")

            config_files.append(files[i])
            del names[i]
            del files[i]
            break

    # Require at least one "--hvc".
    if len(config_files) == 0:
        usage("Missing mandatory '--hvc' argument.")

    # If we're native, and CHIP_WIDTH and CHIP_HEIGHT haven't been defined
    # by command-line arguments, define them.
    if is_native and not hvc.is_defined("CHIP_WIDTH") and \
       not hvc.is_defined("CHIP_HEIGHT"):
        try:
            width_file = open("/sys/devices/system/cpu/chip_width")
            w = width_file.read().strip()
            hvc.process_define("CHIP_WIDTH", w)
            vardefs.append("CHIP_WIDTH=%s" % w)
            width_file.close()
            height_file = open("/sys/devices/system/cpu/chip_height")
            h = height_file.read().strip()
            hvc.process_define("CHIP_HEIGHT", h)
            vardefs.append("CHIP_HEIGHT=%s" % h)
            height_file.close()

        except IOError:
            # If we couldn't open or read the /sys files, try the /proc file
            try:
                grid_file = open("/proc/tile/grid")
                grid = grid_file.read()
                grid_file.close()
                grid_mat = re.match(r"\s*(\d+)\s+(\d+)\s*", grid)
                if grid_mat:
                    (w, h) = grid_mat.group(1, 2)
                    hvc.process_define("CHIP_WIDTH", w)
                    hvc.process_define("CHIP_HEIGHT", h)
                    vardefs.append("CHIP_WIDTH=%s" % w)
                    vardefs.append("CHIP_HEIGHT=%s" % h)
                    
            except IOError:
                # If we couldn't open or read the grid file, just continue.
                pass

    # Before we process config files, add the default config directory
    # to the include search path.
    hvc.process_include_dir(hvc_dir)

    # Now that we know all of the config filenames, build our default
    # CONFIG_VERSION string, if the user hasn't defined one already.
    if not hvc.is_defined("CONFIG_VERSION"):
        # We want to use the absolute pathnames of the config files.
        abs_config_files = []
        for i in config_files:
            path = hvc.include_pathname(i)
            if path:
                abs_config_files.append(os.path.abspath(path))

        if fixed:
            build_time = ""
        else:
            build_time = " on %s" % time.asctime()
        config_ver = "Built by %s%s from %s, %s" % \
            (pwd.getpwuid(os.getuid())[0], build_time,
            ", ".join(abs_config_files),
            os.path.normpath(hv_bin_dir))

        if len(vardefs) > 0:
            config_ver = config_ver + " with " + " ".join(vardefs)

        hvc.process_define("CONFIG_VERSION", config_ver)

    # Read in all of the config files.
    config_contents = ""
    for f in config_files:
        config_contents += hvc.process_include(f)

    # If preprocessing only, dump out the preprocessed config file.
    if dump_hvc:
        if outfilename:
            out = open(outfilename, "w")
            out.write(config_contents)
            out.close()
        else:
            sys.stdout.write(config_contents)
        sys.exit(0)

    if not outfilename:
        usage("Missing mandatory '--output' argument.")

    #
    # Collect "client" names so we know to compress them, figure out
    # if we're striping, and collect memory, CPU, shim speed, and POST
    # requests.
    #
    client_names = {}
    client_regexp = re.compile("^client\s+([^\s]+)")
    stripe_regexp = \
        re.compile("^options[^#]*[\s]stripe_memory(|=|=silent|=default)"
                   "([\s#]|$)")
    striping = False

    force_stripe_regexp = \
        re.compile("^options[^#]*[\s]stripe_memory=always([\s#]|$)")
    force_striping = False

    mem_speed_regexp = \
        re.compile("^options[^#]*[\s]mem_speed=([0-9,-]+)([\s#]|$)")
    mem_speed = None

    cpu_speed_regexp = \
        re.compile("^options[^#]*[\s]cpu_speed=([0-9.]+)([\s#]|$)")
    cpu_speed = None

    dev_regexp = re.compile("^device\??\s+([^#\s]+)([\s#]|$)")
    last_dev = None
    speed_regexp = \
        re.compile("^[\s]+speed\s+([^\s#,]+[\s,]+[^\s#,]+|[^\s#,]+)([\s#]|$)")
    device2speed = {}

    post_regexp = \
        re.compile("^options[^#]*[\s]post=([^\s#,]+)([\s#]|$)")
    post_override = None

    for i in config_contents.splitlines():
        client_match = client_regexp.match(i)
        if client_match:
            client_names[client_match.group(1)] = 1
            continue

        dev_match = dev_regexp.match(i)
        if dev_match:
            last_dev = dev_match.group(1)
            device2speed[last_dev] = None
            continue

        speed_match = speed_regexp.match(i)
        if speed_match:
            speed = speed_match.group(1)
            if last_dev:
                device2speed[last_dev] = ",".join(speed.split())
            continue

        if stripe_regexp.match(i):
            striping = True

        if force_stripe_regexp.match(i):
            force_striping = True

        mem_speed_match = mem_speed_regexp.match(i)
        if mem_speed_match:
            mem_speed = mem_speed_match.group(1)

        cpu_speed_match = cpu_speed_regexp.match(i)
        if cpu_speed_match:
            cpu_speed = cpu_speed_match.group(1)
            cpu_speed = int(round(float(cpu_speed)))

        post_match = post_regexp.match(i)
        if post_match:
            post_override = post_match.group(1)
            if post_override == "quick":
                post_override = "q"
            elif post_override == "thorough":
                post_override = "t"
            elif post_override == "query_quick":
                post_override = "Qq"
            elif post_override == "query_thorough":
                post_override = "Qt"
            else:
                die("Invalid value %s for post option." % post_override)

    # Make sure that any client binaries are actually going to be included
    # in the output hvfs.
    for i in client_names:
        if i not in names:
            die("Client binary '%s' not in input file list." % i)

    # Read in the files.
    filecontent = [get_and_process(files[i], dostrip,
                       docompress and names[i] in client_names,
                       verbose) for i in range(len(files))]

    # Add in the "config" file.
    names.append("config")
    filecontent.append(config_contents)

    # Compute the FS identifier for the header.
    fsid = "Tilera Hypervisor file system built by %s" % (
                os.getenv("LOGNAME", "unknown"))

    # For each file, calculate the offset of its name in the name section,
    # and calculate the offset of its data in the data section.
    nameoffset = len(fsid) + 1
    nameoffsets = []   # offset of name i from start of names

    fileoffset = 0
    fileoffsets = []   # offset of file i from start of files
    filelengths = []   # length of file i

    for i in range(len(names)):
        nameoffsets.append(nameoffset)
        nameoffset += len(names[i]) + 1

        if len(names[i]) > HV_PATH_MAX:
            die("Filename '%s' is too long." % names[i])

        flen = len(filecontent[i])

        fileoffsets.append(fileoffset)
        filelengths.append(flen)
        fileoffset += roundup(flen)

    # Figure out where the name and data areas start relative to the start
    # of the fs.
    namebase = (len(names) + 1) * inode_len
    filebase = namebase + roundup(nameoffset)
    total_len = filebase + roundup(fileoffset)

    # Process the hypervisor binary files and copy them to the output.

    tmp_boot = os.tempnam(None, "mkb-b")
    tmp_l1b = os.tempnam(None, "mkb-1")
    tmp_hv = os.tempnam(None, "mkb-x")

    mkrom_cmd = basedir + "/tile-mkrom"
    cmd1 = (mkrom_cmd, "-n", "-H", "-b", hv_bin_dir + "/hv", "-o", tmp_hv)

    cmd2 = [mkrom_cmd,
        "-d", "-H", "-b", hv_bin_dir + "/hv_l1boot",
        "-o", tmp_l1b, "-f", tmp_hv]
    if striping:
        cmd2.append("-t")
    if force_striping:
        cmd2.append("-T")
    if mem_speed:
        cmd2.extend(("-m", mem_speed))
    if cpu_speed:
        cmd2.extend(("-D", "core:%s" % cpu_speed))
    else:
        cmd2.extend(("-D", "core"))
    if post_override:
        cmd2.extend(("-p", post_override))

    for (i, j) in device2speed.iteritems():
        if j:
            cmd2.extend(("-D", "%s:%s" % (i, j)))
        else:
            cmd2.extend(("-D", i))

    cmd3 = (mkrom_cmd,
        "-C", "-n", "-b", hv_bin_dir + "/hv_lhboot",
        "-o", tmp_boot, "-f", tmp_l1b)

    if not (run(cmd1) and run(cmd2) and run(cmd3)):
        unlink_blindly(tmp_hv)
        unlink_blindly(tmp_l1b)
        unlink_blindly(tmp_boot)

        die("Cannot process hypervisor binary files in '%s'." %
            hv_bin_dir)

    bootfile = file(tmp_boot)
    bootbits = bootfile.read()
    bootfile.close()

    os.unlink(tmp_hv)
    os.unlink(tmp_l1b)
    os.unlink(tmp_boot)

    outfile = CRCFile(outfilename, "w")

    outfile.write(bootbits)

    # Initialize the CRC.
    outfile.reset_crc()

    # Write out the header, without its CRC.
    out_hdr(outfile, FS_MAGIC, len(names), total_len, namebase)

    # Write out the header CRC, and then reset it.
    out_crc(outfile, outfile.crc())
    outfile.reset_crc()

    # Write out the set of inodes.
    for i in range(len(names)):
        out_ino(outfile, namebase + nameoffsets[i],
            filebase + fileoffsets[i], filelengths[i], 0)

    outfile.write(fsid + "\0")

    # Write out the file names.
    for i in names:
        outfile.write(i + "\0")

    # Word-align start of data section.
    if nameoffset & 7 != 0:
        outfile.write("\0" * (8 - (nameoffset & 7)))

    # Copy each file to output, word-aligning the start of each one.
    for i in range(len(names)):
        outfile.write(filecontent[i])
        if filelengths[i] & 7 != 0:
            outfile.write("\0" * (8 - (filelengths[i] & 7)))

    # Write out the trailer CRC and close the output file.
    out_crc(outfile, outfile.crc())
    outfile.close()


if __name__ == "__main__":
    basedir = os.path.dirname(sys.argv[0])
    main(sys.argv)
