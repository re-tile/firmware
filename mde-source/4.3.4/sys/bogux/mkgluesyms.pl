#!/usr/bin/perl -s

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

# Generate symbols for the hv glue area.

use warnings;

my $start;
my $size;
my $index;

print "/* Hypervisor call vector addresses; see <hv/hypervisor.h> */\n";

print ".section .hvglue,\"x\",\@nobits\n";
print ".align 8\n";

sub print_sym_size {
    my ($sym, $val, $symsize) = @_;
    printf ".org %#x\n", $val;
    printf ".global $sym\n";
    printf ".type $sym,function\n";
    printf "$sym:\n";
    printf ".size $sym,$symsize\n";
}

sub print_sym {
    my ($sym, $val) = @_;
    print_sym_size($sym, $val, $size);
}

while (<>)
{
    if (/\#define HV_GLUE_START_CPA (.*)/) {
        $start = $1;
        $start = oct($start) if $start =~ /^0/;
        $start += 0xfd000000;
    }
    elsif (/\#define HV_DISPATCH_ENTRY_SIZE (.*)/)
    {
        $size = $1;
        $size = oct($size) if $size =~ /^0/;
    }
    elsif (/\#define (HV_DISPATCH_\S*)\s*(\d*)/) {
        my $name = $1;
        $index = $2;
        $name =~ tr/A-Z/a-z/;
        $name =~ s/hv_dispatch_/hv_/;
        print_sym("$name", ($size*$index));
    }
}

# Glue is 32KB, so cover the remainder with a generic symbol
# to make the backtracer's life easier.

my $end = ($index+1) * $size;
print_sym_size("hv_glue_internals", $end, (32*1024-$end));

