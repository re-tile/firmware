/*
 * Copyright 2014 Tilera Corporation. All Rights Reserved.
 *
 *   The source code contained or described herein and all documents
 *   related to the source code ("Material") are owned by Tilera
 *   Corporation or its suppliers or licensors.  Title to the Material
 *   remains with Tilera Corporation or its suppliers and licensors. The
 *   software is licensed under the Tilera MDE License.
 *
 *   Unless otherwise agreed by Tilera in writing, you may not remove or
 *   alter this notice or any other notice embedded in Materials by Tilera
 *   or Tilera's suppliers or licensors in any way.
 */

/**
 * @file
 * Definitions for the configuration file parser and the data it produces.
 */

#ifndef _SYS_HV_CONFIG_H
#define _SYS_HV_CONFIG_H

#include "misc.h"
#include "tile_mask.h"
#include "types.h"

#define CONFIG_NAME "config"   /**< Name of our configuration file */

//
// Hypervisor configuration file format
//
//
// The hypervisor configuration file supplies parameters used to configure
// the hypervisor and control its operation.  The configuration file is
// contained within the hypervisor filesystem, and is named "config".
//
// Commands must start in column 1 of the file.  Some commands take
// optional subcommands; these follow the command they are associated
// with and must be preceded by at least one space.  Each command or
// subcommand must be entirely contained within one line.
//
// Numeric command arguments may be in decimal, octal (with a leading 0)
// or hexadecimal (with a leading 0x or 0X).  If a number is immediately
// followed by a k, m, or g, it will be multiplied by 2^10, 2^20, or 2^30
// respectively.
//
// Device names are in the standard hypervisor format: <name>/<instance #>.
//
// The configuration file may contain comments, which begin with a pound
// sign and go to the end of the line.  They're legal anywhere, but note
// that comments after an 'args' subcommand will be treated as part of
// the arguments.
//
// Commands:
//
// debug <numeric-value>
//
//   Set the hypervisor debug flags to <numeric-value>.
//
// console <device-name>
//
//   Request that the named device be used as the hypervisor (and
//   supervisor) console.  (This command is not yet implemented.)
//
// options <option> [ <option> ... ]
//
//   Set hypervisor options.  Options are of the form <foo> or <foo>=<bar>.
//   The currently available options are:
//
//     cpu_speed=<speed>       Set the CPU clock frequency to <speed>, which
//                             is given in MHz; fractional values are
//                             permitted.
//
//     default_shared=<x>,<y>  Use tile <x>,<y> as the shared tile for devices
//                             which need exactly one shared tile and whose
//                             device stanza does not explicitly specify one.
//                             (If this option is not present, tile 0,0 is
//                             used as the default shared tile.)
//
//     console_debug=<string>  Set the string which, when entered on the UART
//                             console, puts the hardware in protocol (debug)
//                             mode.  This may enable external debug tools to
//                             retrieve useful information from a hung system.
//                             The string may be no longer than 16 characters,
//                             and may contain the following escape codes:
//
//                               \r               Carriage return
//                               \n               Newline
//                               \t               Tab
//                               \<octal-digits>  Any arbitrary character
//                               \x<hex-digits>   Any arbitrary character
//
//                             Note that when the debug string is
//                             entered, it will not be passed to the
//                             client supervisor; it is thus advisable
//                             to pick debug strings which are not likely
//                             to be typed by users at the console prompt.
//
//                             If this option is not specified, the string
//                             used to enter debug mode will be "\x1e", the
//                             control-caret character.  If this option
//                             is specified without a string, or with
//                             an empty string, it will not be possible
//                             to enter console debug mode.  This may be
//                             useful for systems whose applications need
//                             to read arbitrary binary data from the console.
//
//     stripe_memory[=<mode>]  Request that the system be configured to stripe
//                             memory accesses across all available memory
//                             controllers.  If <mode> is present, it specifies
//                             one of the following behaviors:
//
//                             never     Do not stripe memory.
//
//                             default   Stripe memory if all memory
//                                       controllers have equal amounts of
//                                       memory; if not, do not stripe, and
//                                       print a warning.
//
//                             silent    Stripe memory if all memory
//                                       controllers have equal amounts
//                                       of memory; if not, do not stripe,
//                                       and do not print a warning.
//
//                             always    Stripe memory as long as all
//                                       controllers have memory attached,
//                                       even if this means reducing the
//                                       amount of memory made available
//                                       (when striping, all controllers
//                                       must have the same size).  If
//                                       memory is lost by this, print a
//                                       warning.
//
//     contig_pa               Attempt to map memory shims contiguously
//                             for the client.  This option will validate
//                             that all the shims are the same size and
//                             that the size is a power of two, and place
//                             all shims contiguously in the client PA
//                             space.  Shim zero is handled specially,
//                             since it is allowed to have less memory than
//                             the others (due to the hypervisor taking up
//                             some of it); it is renumbered to be the
//                             largest-numbered shim for the client, since
//                             that places the small hole created by the
//                             hypervisor's presence at the end of shim
//                             zero at the end of the client PA space.
//
//                             This arrangement requires the hypervisor to
//                             report the shims as a single controller to
//                             the client (or possibly two, depending on the
//                             exact amount of memory), since the high
//                             bits of the client PA are part of the API
//                             for identifying which shim a PA comes from.
//
//                             Note that in some cases applications wish to
//                             be able to map entire controllers with very
//                             huge pages.  By default (without contig_pa),
//                             all but controller zero can be allocated
//                             this way (with the hypervisor using part of
//                             the memory at the end of controller zero,
//                             and the client using part of the memory at
//                             the beginning).  With contig_pa, both the
//                             first and last controller have some memory
//                             unavailable, so application writers must be
//                             aware of this potential downside.
//
//     panic=<mode>            Specify the system's behavior after a
//                             hypervisor panic.  <mode> must be one of the
//                             following:
//
//                             halt      Halt the system.  What that means
//                                       depends upon the setting of the
//                                       halt option; see below.  This is
//                                       the default if the option is not
//                                       specified.
//
//                             reboot    Reboot the system, if possible.
//
//     halt=<mode>             Specify the system's behavior when asked to
//                             halt, either via client request, or after a
//                             panic when the panic=reboot option is not
//                             active.  <mode> must be one of the following:
//
//                             tile      Halt the requesting tile only.
//                                       Other tiles may continue to run.
//
//                             chip      Halt all tiles on the chip.  This
//                                       is the default if the option is not
//                                       specified.
//
//     mem_speed=<speeds>      Set the memory frequency.  <speeds> is a
//                             comma-separated list of speeds in millions
//                             of transactions per second, one speed for each
//                             memory controller.  Fractional values are
//                             not permitted.  If speeds are not specified
//                             for some controllers, the last value in the
//                             list will be used for them; thus, the speed
//                             for all controllers may be set with a list
//                             of just one value.  A value of zero is
//                             interpreted as the default speed for that
//                             controller, and a negative value disables
//                             it.
//
//     post=<mode>             Set the power-on self-test mode, which may
//                             be one of:
//
//                             quick           Run quick POST.
//
//                             query_quick     Ask on the console whether
//                                             thorough POST should be run;
//                                             if no response, run quick POST.
//
//                             thorough        Run thorough POST.
//
//                             query_thorough  Ask on the console whether
//                                             thorough POST should be run;
//                                             if no response, run thorough
//                                             POST.  This is the default.
//
//                             Note that disabling the POST query via the
//                             quick or thorough options also disables the
//                             ability to request booting from the
//                             alternate SROM image; this may be a bad idea
//                             on a system which boots from SROM.
//
//     dvfs                    Set dynamic voltage and frequency scaling
//                             availability.  Legal values are:
//
//                             off        Do not allow the primary client to
//                                        modify any frequencies during
//                                        operation.
//
//                             core       Allow the primary client to modify
//                                        the core frequency during operation,
//                                        but do not make any corresponding
//                                        changes in core voltage.  This is
//                                        the default.
//
//                             core_volt  Allow the primary client to modify
//                                        the core frequency during operation,
//                                        and make corresponding changes in
//                                        core voltage.  This setting may
//                                        lead to instability in some
//                                        system configurations.
//
//                             Note that when voltage is adjusted, it will be
//                             set to accommodate the frequencies of the I/O
//                             shims as well as the core.  To permit maximum
//                             voltage decrease at lower core frequencies,
//                             and thus maximum power savings, users may wish
//                             to lower I/O shim frequencies to the minimum
//                             required for their application (via the speed
//                             subcommand in the relevant device stanzas),
//                             and disable unused devices (by omitting the
//                             device stanza entirely).
//
//     stats                   Enable collection of hypervisor statistics.
//                             When this option is present, the hypervisor
//                             counts and times various operations
//                             (interrupt handlers, hypervisor messages,
//                             hypervisor syscalls) and makes summary
//                             statistics available via a console escape,
//                             and also via the hv_confstr() interface.
//
//     mem_error=<mode>        Configure the hypervisor's response to
//                             non-fatal processor errors (for instance,
//                             correctable cache ECC errors).  <mode> must be
//                             one of the following:
//
//                             silent  Do not issue any console messages for
//                                     non-fatal errors.
//
//                             warn    Issue console warning messages for
//                                     non-fatal errors.  This is the default
//                                     if the option is not specified.
//
//                             panic   Panic on non-fatal errors.
//
//
// client <exe-name> [ <wd> x <ht> [ <ulhcx> , <ulhcy> ] ]
//
//   Defines a client supervisor.  exe-name is the executable file
//   to run as the supervisor program; this must be a file within the
//   hypervisor file system.  wd and ht specify the width and height of
//   the tile rectangle allocated to the client; if omitted, the entire
//   chip is used.  ulhcx and ulhcy specify the upper-left-hand-corner of
//   the tile rectangle; if omitted, (0,0) is used.  If more than one
//   client is specified, their tile rectangles must be disjoint.  The
//   first client specified in the configuration is termed the "primary
//   client", and has certain special properties documented below.
//
//   Client subcommands:
//
//     memory <size-ctl-0> [ <size-ctl-1> [ <size-ctl-2> [ <size-ctl-3> ] ] ]
//
//       This defines the amount of memory the client is allowed to use
//       on each memory controller.  The value is specified in bytes;
//       if this line is omitted, a particular value is omitted, or the
//       value "default" is given, the client will be given a share on
//       that controller equal to that of all other clients who don't
//       have explicit sizes specified.  A value of zero disables that
//       memory controller for this client.
//
//     args <args>
//
//       Specify arguments which will be passed to the client supervisor.
//
//     device <devname>
//
//       Designate a specific device as being available to this client.
//       Normally, shareable devices are made available to all clients,
//       and non-shareable devices are made available to the primary
//       client.  If a device is non-shareable, it can be named in only
//       one device subcommand.
//
//     hfh_tiles tilespec [ tilespec ... ]
//
//       Designate the set of tiles which will be used to cache data from
//       pages designated as "hash-for-home".  tilespec is one of:
//
//       <x>,<y>              One tile
//       <w>x<h>              A rectangle of tiles <w> wide by <h> high whose
//                            upper-left-hand corner is (0,0)
//       <w>x<h>@<x>,<y>      A rectangle of tiles <w> wide by <h> high whose
//                            upper-left-hand corner is (<x>,<y>)
//       <cpunum>             Tile number <cpunum>, using Linux's CPU number
//                            mapping scheme
//       <cpunum0>-<cpunum1>  Tiles ranging from <cpunum0> through <cpunum1>,
//                            inclusive
//       ^tilespec            Subtract the named tiles from those previously
//                            specified; if the first tilespec is a
//                            subtraction, we start with the default set of
//                            tiles, else we start with none.
//
//       All specifications are relative to the client's tile grid.
//       By default, all tiles accessible to the client are used for
//       hash-for-home.  After the set is computed, we silently remove
//       from it any tiles which are being used as dedicated driver tiles.
//
//
// bme exe-name { private | shared } <tilespec> [ <tilespec> ... ]
//
//   Defines an application running under the bare metal environment (BME).
//
//   exe-name is the Tile ELF executable file to run as the BME application;
//   this must be a file within the hypervisor file system.
//
//   One of "private" or "shared" must be specified; this keyword defines
//   the treatment of each tile's data, stack, and heap areas. If "private"
//   is specified, each tile will get a separate copy of the application's
//   data, and each tile's stack and heap will be private: allocated at an
//   identical virtual address, and not mapped into other tiles' virtual
//   address space. If "shared" is specified, each tile will share one
//   copy of the application's data, and while each tile will have its
//   own stack and heap, they will be mapped at a unique virtual address,
//   and will be mapped into all tiles' virtual address spaces. See the
//   bme subcommands below for details on exactly how various regions
//   are placed and mapped.
//
//   The set of tilespecs specifies the tiles running this application.
//   The tilespecs are as documented above for the hfh_tiles client
//   subcommand, with the exception that they are relative to the full chip.
//
//   bme subcommands:
//
//   memory <size-ctl-0> [ <size-ctl-1> [ <size-ctl-2> [ <size-ctl-3> ] ] ]
//
//     This defines the amount of memory reserved for the application
//     on each memory controller. The value is specified in bytes; a
//     'k', 'm', or 'g' can be appended, meaning kilobytes, megabytes,
//     or gigabytes, respectively. If this line is omitted, a specific
//     particular controller's value is omitted, or the value "default"
//     is given for a controller, the application will be given a share on
//     that controller equal to that of all other applications or clients
//     who don't have explicit sizes specified. A value of zero disables
//     that memory controller for this application.
//
//     The hypervisor is placed at the very top of physical memory
//     on controller zero.  The remaining physical memory on that
//     controller, and all of the memory on the other controllers, is
//     allocated to BME applications and client supervisors. Allocations
//     for BME applications start at the beginning of the controller; if,
//     in the future, we support more than one BME application, physical
//     addresses will be allocated to them based on the order they appear
//     in the configuration file. Thus, if the first BME command specifies
//     that an application gets 1 GB of memory on controller 0, it will
//     get physical addresses 0x0 - 0x3FFFFFFF on that controller.
//
//   args args
//
//     Specify arguments which will be passed to the application. 
//
//   group <tilespec> [ <tilespec> ... ]
//
//     Specify tile grouping. This subcommand designates a set of tiles
//     which will use the same options for memory placement. The tilespecs
//     are interpreted relative to the bounding rectangle of the set of
//     tiles specified on the bme command. No tile may be in more than
//     one group; tiles need not be in any group.
//
//     The various placement subcommands (text, rodata, rwdata, pertile,
//     nearest, and hfh_tiles), when entered before any group command,
//     apply to all tiles; when entered after a group command, they apply
//     only to tiles in that group.  Each argument is treated separately;
//     if a within-group subcommand does not specify an argument, then
//     its value for that group is not affected.  Thus, for each group,
//     the value for each placement subcommand argument is:
//
//     - The value specified inside the current group, if so specified;
//
//     - The value specified before any groups, if so specified; or
//
//     - The default value, if not otherwise specified. 
//
//     Tiles included within a particular BME application but not within
//     any group are only affected by the placement subcommands which
//     appear before any group definitions.
//
//   text [ ctl={<cnum> | nearest} ] [ pa={bottom | top | <pa> | exe} ]
//        [ cache={<x>,<y> | hash | none | local} ]
//        [ dtlb={read | write | none} ]
//
//     Specify the placement of an application's text (i.e., its segments
//     which are marked as executable).  Typically this option would be
//     given before any grouping commands, so that there is only one copy
//     of the text; multiple text lines may be used to cause the text to
//     be replicated on multiple memory controllers.
//
//     ctl specifies which memory controller the text will be resident on.
//     This is either a controller number, or nearest, which means the
//     controller set with the nearest subcommand; the default is nearest.
//
//     pa specifies the physical address at which the text will be placed:
//
//       bottom  the lowest appropriate unused range of physical addresses
//               on the target controller will be used.  This is the default.
//
//       top     the highest appropriate unused range of physical addresses
//               on the target controller will be used.
//
//       <pa>    a particular physical address will be used, relative to 
//               the target controller.  Note that if this address is not
//               appropriately aligned with the corresponding virtual
//               address, it may cause more ITLB entries to be used in
//               mapping the text, or may even make it impossible to map
//               the text.  This option may not be used if the executable
//               has more than one text segment.
//
//       exe     the physical address specified in the ELF header for
//               the text segment will be used.  Note that typically this
//               value is not particularly useful unless you have linked
//               your application with a custom linker script; it is by
//               default set to the virtual address of the segment.  Also
//               note that the ELF header address is only 32 bits.  We
//               interpret this address relative to the target controller,
//               not as an absolute system-wide PA, but this still means
//               that items placed via this option must begin within the
//               first 4 GB of the target controller.  Finally, note that
//               the alignment caveats mentioned under pa above also apply.
//
//     cache specifies how the text will be cached:
//
//       <x>,<y>  the text will be cached coherently on the specified tile,
//                which is interpreted relative to the bounding rectangle
//                of the set of tiles specified on the bme command.
//
//       hash     the text will be cached coherently on a set of tiles,
//                using the TILE-Gx hash-for-home feature.
//
//       none     the text will be uncached.
//
//       local    the text will be cached noncoherently on each tile. This
//                is the default.
//
//     dtlb specifies whether the text will be mapped into the DTLB as
//     well as the ITLB, so that it may be accessed via load and store
//     instructions:
//
//       read   The text will be mapped read-only in the DTLB. 
//
//       write  The text will be mapped read-write in the DTLB. 
//
//       none   The text will not be mapped in the DTLB. This is the default. 
//
//     The virtual address at which the text segment is mapped is taken
//     from the executable. The hypervisor will use the minimum number
//     of TLB entries to map the segment in the ITLB.
//
//   rodata [ ctl={<cnum> | nearest} ] [ pa={bottom | top | <pa> | exe} ]
//          [ cache={<x>,<y> | hash | none | local} ]
//          [ sharemap={ rwdata | none } ]
//
//     Specify the placement of an application's read-only data (i.e.,
//     its segments which are marked as readable, not writable, and
//     not executable).
//
//     ctl specifies which memory controller the read-only data will
//     be resident on. The default is nearest; see the text subcommand
//     for specifics.
//
//     pa specifies the physical address at which the read-only data will be
//     placed. The default is bottom; see the text subcommand for specifics.
//
//     cache specifies how the read-only data will be cached. The default
//     is local; see the text subcommand for specifics.
//
//     sharemap=rwdata specifies that we should try to share the DTLB
//     mapping for the read-only data with that for the read-write
//     data. This will potentially conserve DTLB entries, but will lead
//     to the read-only data being writable. sharemap=none specifies that
//     we will not try to share the DTLB entry; this is the default. Note
//     that if this option is on, the cache option is ignored, since the
//     caching mode used by read-write data will be used instead.
//
//     The virtual address at which the read-only data segment is mapped
//     is taken from the executable. The hypervisor will use the minimum
//     number of TLB entries to map the segment in the DTLB.
//
//  rwdata [ ctl={<cnum> | nearest} ] [ pa={bottom | top | <pa> | exe} ]
//         [ cache={<x>,<y> | hash | none | local} ]
//
//    Specify the placement of an application's read-write data (i.e.,
//    its segments which are marked as readable, writable, and not
//    executable).
//
//    ctl specifies which memory controller the read-write data will
//    be resident on. The default is nearest; see the text subcommand
//    for specifics.
//
//    pa specifies the physical address at which the read-write data
//    will be placed. The default is bottom; see the text subcommand for
//    specifics. Note that in private mode, where each tile has its own
//    copy of the read-write data, this address specifies the starting
//    point for the array of per-tile data areas.
//
//    cache specifies how the read-write data will be cached. In
//    private mode, the default is local; in shared mode, the default is
//    hash-for-home.  See the text subcommand for specifics.
//
//    The virtual address at which the read-write data segment is mapped
//    is taken from the executable. The hypervisor will use the minimum
//    number of TLB entries to map the segment in the DTLB.
//
//  pertile [ ctl={<cnum> | nearest} ] [ pa={bottom | top | <pa> | exe} ]
//          [ cache={<x>,<y> | hash | none | local} ] [ stack=stacksize ]
//          [ heap=heapsize ] [ va=va ] [ sharemap={ rwdata | none } ]
//
//    Specify the placement of an application's per-tile data area,
//    which contains the tile's stack and runtime heap. The stack is
//    used for automatic variables within C functions, and in some cases
//    for function arguments; the runtime heap is primarily intended to
//    support the dynamic allocation of data by BME runtime routines, but
//    may also be used by the application directly if desired.
//
//    ctl specifies which memory controller the per-tile data will be
//    resident on. The default is nearest; see the text subcommand for
//    specifics.
//
//    pa specifies the physical address at which the base of the per-tile
//    region will be placed; this region contains all per-tile data areas
//    for this group. The default is bottom; see the text subcommand
//    for specifics.
//
//    cache specifies how the per-tile data area will be cached. In
//    private mode, the default is local; in shared mode, the default is
//    hash-for-home.  See the text subcommand for specifics.
//
//    stacksize specifies the size of the stack in bytes; a 'k', 'm', or
//    'g' can be appended, meaning kilobytes, megabytes, or gigabytes,
//    respectively. The default is 64k. Note that in private mode, extra
//    padding may be added to the total size of the per-tile area so that
//    each is individually mappable by its respective tiles.
//
//    heapsize specifies the size of the heap in bytes; a 'k', 'm', or
//    'g' can be appended, meaning kilobytes, megabytes, or gigabytes,
//    respectively. The default is 64k.
//
//    va specifies the lowest virtual address of the stack. Note that in
//    shared mode, where each tile's stack has a unique VA, this address
//    specifies the virtual starting point for the array of per-tile data
//    areas. The default is 0xFC000000, minus the size of the array of
//    per-tile data areas. Each tile's heap directly follows its stack.
//
//    sharemap=rwdata specifies that we should try to share the DTLB mapping
//    for the per-tile data area with that for the read-write data. This
//    is only possible in private mode, where it is the default, and has
//    the effect of extending the read-write data segment by the size of
//    the per-tile data. sharemap=none specifies that we will not try to
//    share the DTLB entry. Note that if we are sharing the mapping, the
//    cache option is ignored, since the caching mode used by read-write
//    data will be used instead, and the VA option is ignored, since we
//    will be using a VA derived from the read-write data VA.
//
//  nearest ctl=<cnum>
//
//    Specify which memory controller should be considered "nearest" for
//    the purpose of evaluating ctl options on placement subcommands. By
//    default, the "nearest" controller is the one which has the lowest
//    mean physical distance to all of the tiles in a group.
//
//    ctl specifies which memory controller is treated as nearest. 
//
//  extra filename [ ctl=<cnum> ] [ pa={<pa> | bottom | top} ]
//
//    Specify the location for the contents of a file in the hypervisor
//    filesystem. This data will be read from the filesystem and
//    written to physical memory at the given location, for use by the
//    application. Unlike all of the other segments described here, this
//    does not cause a segment to be mapped into any tile's virtual address
//    space; each tile must install appropriate mappings locally using
//    the BME runtime interfaces once the application is running. More
//    than one extra subcommand may be specified.
//
//    ctl specifies which memory controller the extra data will be resident
//    on. The default is nearest; see the text subcommand for specifics.
//
//    pa specifies the physical address at which the extra data will be
//    placed. The default is bottom; see the text subcommand for specifics.
//
//  hfh_tiles tilespec [ tilespec ... ]
//
//    Designate the set of tiles which will be used to cache data from
//    pages designated as "hash-for-home". tilespec is as described above,
//    and is relative to the bounding rectangle of the set of tiles in
//    the BME application. By default, all tiles in the BME application
//    are used for hash-for-home. It is legal to specify different sets of
//    hash-for-home tiles in different groups, but in that case, different
//    groups cannot share data mapped as hash-for-home. Note that such
//    nonconsistent hash-for-home mapping may also cause problems if
//    coherent I/O is used.
//
//
//  { device | device? } <devname> [ <drivername> ]
//
//   Request that a specific device be included in the configuration.
//   Most devices are not included by default, and must be specified in this
//   manner if they are to be used.  devname is the name of the device.
//   drivername is the name of the driver to be used; if omitted, the
//   first driver in the driver list which knows how to handle the named
//   is selected.  If the device? form is used, it is not an error, and
//   the hypervisor will not print a diagnostic message, if the named
//   device is not present.
//
//   Device subcommands:
//
//     shared <x0> , <y0> [ <x1>, <y1> [ <x2> , <y2> ] ]
//
//       Designate tiles to be used as "shared tiles": tiles which
//       process device requests, but also run the hypervisor and
//       supervisor.  The precise usage of these tiles, and the number
//       required, depends upon the device driver being used.
//
//     dedicated <x0> , <y0> [ <x1>, <y1> [ <x2> , <y2> ] ]
//
//       Designate tiles to be used as "dedicated tiles": tiles which
//       process device requests exclusively, and do not run a client
//       supervisor.  The usage of these tiles, and the number required,
//       depends upon the device driver being used.
//
//     args <args>
//
//       Specify arguments which will be passed to the device driver.
//       The maximum size of these arguments is 1024 bytes.
//
//     speed <speed> [ <speed1> ]
//
//       Set the device's clock frequency to <speed>, which is given in
//       MHz; fractional values are permitted.  For devices which have two
//       separate clock domains, <speed1> may be specified to set the speed
//       of the second domain.  This subcommand is only accepted on TILE-Gx
//       systems, and not all devices accept it.
//
//
// print <message>
//
//   Print a message on the console during hypervisor boot.
//
//
// config_version <text>
//
//   Define a version string for this hypervisor configuration, which is
//   printed on the console at boot time, and is retrievable via the
//   hypervisor's confstr interface.  The string is intended for use by
//   humans, not programs, and is uninterpreted; it may contain whitespace.
//   Only the last such command within the configuration file has any effect.
//
// Example:
//
// # Example config file
// debug 3   # Enable debug flags
//
// # Run "hello world" under Bogux in a quarter of the chip; give it memory
// # on controller 2 only.
// client bogux.tilexe 4x4
//     memory 0 0 default 0
//     args hello.tilexe
//
// # Support one PCI-E interface and one XAUI interface.
// device pcie/0
//     dedicated 1,4
//
// device xgbe/1 xgbe_ipp   # 2-tile IPP, no EPP
//     dedicated 1,5 1,6
//

/** Descriptor for a string in an HVFS file. */
struct hvfs_str
{
  int ino;  /**< Inode of file containing string */
  int off;  /**< Offset of string within file */
  int len;  /**< Length of string */
};

/** Configuration data for clients. */
struct client_config
{
  /** Inode of client binary */
  int bin_ino;
  /** Client upper-left-hand-corner */
  pos_t ulhc;
  /** Client lower-right-hand-corner */
  pos_t lrhc;
  /** Tile we'll initially start this client on */
  pos_t start_tile;
  /** Mask of valid tiles for this client */
  tile_mask tiles;
  /** Tiles which this client uses for hash-for-home */
  tile_mask home_map_tiles;
  /** Requested client memory length on each memory controller */
  PA req_mem_len[MAX_MSHIMS];
  /** Assigned client memory base address on each memory controller */
  PA mem_base[MAX_MSHIMS];
  /** Assigned client memory length on each memory controller */
  PA mem_len[MAX_MSHIMS];
  /** Client entry point */
  CPA client_entry;
  /** Client arguments */
  struct hvfs_str arg;
  /** Line number of definition in config file (for error messages) */
  int lineno;
  /** Client flags (CLIENT_xxx) */
  unsigned int flags;
  /** Additional state for BME clients */
  struct bme_mem_placement_group* bme_groups;
};

//
// Client flags, etc.
//

/** Client is a BME client. */
#define CLIENT_BME             0x1

/** Client uses the BME private application model. */
#define CLIENT_BME_PRIVATE     0x2

/** Client is the primary client.  This is the client which by default
 *  owns things (like devices) in a multi-client environment.  In the
 *  future this client may have other special abilities. */
#define CLIENT_PRIMARY         0x4

/** Give client an equal share of any remaining memory on this controller;
 *  used in client_config.mem[]. */
#define CLIENT_MEM_DEFAULT (~0ULL)

//
// BME data structures
//

/** Use the memory controller nearest to this tile */
#define BME_CTL_NUM_NEAREST    -1

/** Cache the data for this tile's app on this tile */
#define BME_CACHE_MODE_LOCAL    0
/** Use hash-for-home caching for this tile's app's data */
#define BME_CACHE_MODE_HASH     1
/** Do not cache the data for this tile's app */
#define BME_CACHE_MODE_NONE     2
/** Cache the data for this tile's app on a specific tile */
#define BME_CACHE_MODE_COORDS   3

/** Place the data at the bottom (lowest address) of the unused range on the
 * target controller */
#define BME_CTL_PLACE_BOTTOM    0
/** Place the data at the top (highest address) of the unused range on the
 * target controller  */
#define BME_CTL_PLACE_TOP       1
/** Place the data at the physical address specified in the executable */
#define BME_CTL_PLACE_EXE       2
/** Place the data at the physical address specified in the config file */
#define BME_CTL_PLACE_CONFIG    3

/** Do not map the text in the DTLB */
#define BME_DTLB_MAP_MODE_NONE  0
/** Map the text read-only in the DTLB */
#define BME_DTLB_MAP_MODE_READ  1
/** Map the text read-write in the DTLB */
#define BME_DTLB_MAP_MODE_RW    2

/** Maximum number of bytes in a name string */
#define BME_MAX_NAME_SIZE 32

/** Description of how to place a segment in memory */
struct bme_mem_desc {
  /** PA to use if pa mode is BME_CTL_PLACE_CONFIG */
  PA ctl_pa;
  /** Coordinates of home tile, if cache_mode is coords */
  pos_t cache_coords;
  /** Memory controller number */
  int mem_ctl_num:4;
  /** Relative placement of memory on controller:
   * BME_CTL_PLACE_{BOTTOM,TOP,EXE,CONFIG} */
  unsigned int pa_mode:2;
  /** Cache mode: BME_CACHE_MODE_{LOCAL,HASH,NONE,COORDS} */
  unsigned int cache_mode:2;
};

/** Description of how to place a text segment in memory */
struct bme_text_mem_desc {
  /** Memory description */
  struct bme_mem_desc mem_desc;
  /** DTLB mapping: BME_DTLB_MAP_MODE_{NONE,READ,RW} */
  unsigned int mapped_in_dtlb:2;
};

/** Description of how to place a rodata segment in memory */
struct bme_rodata_mem_desc {
  /** Memory description */
  struct bme_mem_desc mem_desc;
  /** If true, try to share the DTLB mapping with the r/w data */
  unsigned int sharemap:1;
};

/** Description of how to place a rwdata segment in memory */
struct bme_rwdata_mem_desc {
  /** Memory description */
  struct bme_mem_desc mem_desc;
};

/** Description of how to place an extra file in memory */
struct bme_extrafile_desc {
  /** Memory description */
  struct bme_mem_desc mem_desc;
  /** Inode from which to read file */
  int bin_ino;
  /** Name of the file read in from the file system. */
  char name[BME_MAX_NAME_SIZE];
  /** Pointer to the next extra file descriptor. */ 
  struct bme_extrafile_desc* next;
};

/** Description of stack/heap placement */
struct bme_pertile_mem_desc {
  /** Memory description */
  struct bme_mem_desc mem_desc;
  /** Size of stack */
  uint32_t stacksize;
  /** Size of heap */
  uint32_t heapsize;
  /** Virtual address of stack */
  VA va;
  /** If true, try to share the DTLB mapping with the r/w data */
  unsigned int sharemap:1;
};

/** Placement of BME segments in memory */
struct bme_mem_placement_group {
  /** Tiles which comprise this memory placement group */
  tile_mask tiles;
  /** Placement of text segments in memory */
  struct bme_text_mem_desc text;
  /** Placement of read-only data segments in memory */
  struct bme_rodata_mem_desc rodata;
  /** Placement of read/write data segments in memory */
  struct bme_rwdata_mem_desc rwdata;
  /** Placement of app's stack and heap */
  struct bme_pertile_mem_desc pertile;
  /** List of placements of extra files in memory */
  struct bme_extrafile_desc* extra;
  /** Forced "nearest" memory controller for this group; BME_CTL_NUM_NEAREST
   * if we want to use the controller which is actually nearest. */
  int nearest_ctl;
  /** Pointer to next group in the linked list. */
  struct bme_mem_placement_group* next;
};

/** Configuration data. */
struct config
{
  /** Debug flags */
  int debug;
  /** Clients */
  struct client_config clients[MAX_CLIENTS + MAX_BME];
  /** Total number of clients */
  int nclients;
  /** Number of regular (non-BME) clients */
  int nregclients;
  /** Number of BME clients */
  int nbmeclients;
  /** Requested tile clock in Hz */
  uint32_t cpu_speed;
  /** config_version string */
  struct hvfs_str config_ver;
  /** Mask of shared tiles */
  tile_mask shr_tile_mask;
  /** Did the user request memory striping? */
  uint8_t striping_requested:1;
  /** Did the user request contiguous client PAs? */
  uint8_t contig_pa:1;
  /** Did the user request a reboot after a hypervisor panic? */
  uint8_t reboot_on_panic_requested:1;
  /** Did the user request the full chip be stopped on halt? */
  uint8_t halt_full_chip_requested:1;
  /** Is dynamic frequency control enabled for the core? */
  uint8_t dfs_core:1;
  /** Is dynamic voltage control enabled? */
  uint8_t dvs:1;
  /** Should statistics be collected? */
  uint8_t stats:1;
  /** First dynamically-allocatable interrupt channel. */
  int first_dyn_intchan;
  /** Should we suppress hv_warning messages for non-fatal MEM_ERRORs? */
  uint8_t mem_error_silent:1;
  /** Should we panic on non-fatal MEM_ERRORs? */
  uint8_t mem_error_panic:1;
};

extern struct config config;
extern int my_client;

/** Parse the configuration file, if it exists, and set up data structures
 *  in various subsystems which will be used later to configure the system.
 */
void parse_config(void);

/** Dump the parsed client configuration data structures. */
void dump_client_config(void);

#endif // _SYS_HV_CONFIG_H
