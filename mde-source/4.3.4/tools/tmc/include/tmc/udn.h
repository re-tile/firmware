// Copyright 2014 Tilera Corporation. All Rights Reserved.
//
//   The source code contained or described herein and all documents
//   related to the source code ("Material") are owned by Tilera
//   Corporation or its suppliers or licensors.  Title to the Material
//   remains with Tilera Corporation or its suppliers and licensors.
//   The software is licensed under the Tilera MDE License.
//
//   However, Licensee may elect to use this file under the terms of the
//   GNU Lesser General Public License version 2.1 as published by the
//   Free Software Foundation and appearing in the file src/COPYING.LIB
//   in the MDE distribution.  Please review the following information to
//   ensure the GNU Lesser General Public License version 2.1 requirements
//   will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.

//! @file
//!
//! Support for the UDN (user dynamic network).
//!

#ifndef __TMC_UDN_H__
#define __TMC_UDN_H__

#include <features.h>

#include <arch/udn.h>
#include <arch/inline.h>

__BEGIN_DECLS


//! @addtogroup tmc_udn
//!
//! Simple packets can be sent between tiles over the UDN.
//!
//! @if __tilegx__
//! The User Dynamic Network (UDN) provides hardware for routing data
//! packets between CPUs.  Each packet starts with a header word,
//! specifying the CPU to which the packet should be routed and which
//! 'demux queue' should receive the packet when it arrives.  When
//! packets arrive at the destination CPU, they are sorted into one
//! of four possible demux queues, and the receiving CPU issues
//! tmc_udn0_receive(), tmc_udn1_receive(), or similar calls to pull data
//! words from a particular queue.
//! @else
//! The User Dynamic Network (UDN) provides hardware for routing data
//! packets between CPUs.  Each packet starts with two header words,
//! specifying the CPU to which the packet should be routed and which
//! 'demux queue' should receive the packet when it arrives.  When
//! packets arrive at the destination CPU, they are sorted into one
//! of four possible demux queues, and the receiving CPU issues
//! tmc_udn0_receive(), tmc_udn1_receive(), or similar calls to pull data
//! words from a particular queue.
//! @endif
//!

#ifndef __BME__

//! @addtogroup tmc_udn
//! @section udn_init UDN Initialization
//!
//! When running under Tilera Linux, access to the UDN is guarded by
//! the operating system.  TMC provides a two-phase process that
//! allows applications to access the UDN.
//!
//! First, the tmc_udn_init() function reserves a rectangle of tiles
//! (referred to as a 'UDN hardwall') for use by the calling
//! application.  This function should be called by the first task in
//! the application, before creating any child tasks.  It takes a
//! cpu_set_t* parameter that specifies all the CPUs that are to be
//! given access to the UDN and allocates the minimum rectangle of
//! tiles that contains those CPUs.  If the cpu_set_t* is NULL, the
//! routine allocates a rectangle that contains all the CPUs in the
//! calling task's affinity set (see tmc_cpus_get_my_affinity()).  For
//! example, the parent process should do something like the following
//! to create a UDN hardwall:
//!
//! @code
//! if (tmc_udn_init(NULL) != 0)
//!   tmc_task_die("Failure in 'tmc_udn_init()'.");
//! @endcode
//!
//! After creating the UDN hardwall, the application can create
//! threads or fork processes as necessary and bind them to particular
//! CPUs using tmc_cpus_set_my_cpu() (or equivalent).  Once a task is
//! bound to a single CPU, it should call tmc_udn_activate() to allow it
//! to send and receive packets within the previously-created UDN
//! hardwall.  For example:
//!
//! @code
//! if (tmc_cpus_set_my_cpu(my_cpu) < 0)
//!   tmc_task_die("Failure in 'tmc_cpus_set_my_cpu()'.");
//! if (tmc_udn_activate() < 0)
//!   tmc_task_die("Failure in 'tmc_udn_activate()'.");
//! @endcode
//!
//! Note that each task must call tmc_udn_activate() individually
//! before it can have access to the UDN, even if another task on that
//! same CPU previously called tmc_udn_activate() successfully; UDN
//! activation is a task property, not a CPU property.
//! 
//! No additional steps are required in an application that only uses
//! @c pthread_create(), or that uses @c fork() without @c exec() to
//! create child tasks.  But when calling @c fork() and @c exec() to
//! create child processes running different binaries, these special
//! steps must be performed:
//!
//! - Call @c tmc_udn_persist_after_exec(1) just before calling @c exec().
//! In general this should be done @e after calling @c fork().  Technically
//! this step is only needed if tmc_udn_init() has been called.
//!
//! - After @c exec(), in the new program, call tmc_udn_init() to gain
//! access to the inherited hardwall, and then affinitize to a CPU
//! using tmc_cpus_set_my_cpu() and activate the hardwall using
//! tmc_udn_activate(), as described above.
//!
//! If the new program does not want to use an inherited hardwall, it
//! should call tmc_udn_close().  Otherwise, the inherited hardwall
//! is kept alive until all such children have exited.
//!
//! The documentation for tmc_udn_init() and tmc_udn_close()
//! describes some special considerations in multi-threaded programs.
//!
//! Various functions in this component set errno when they fail.
//! By convention, unless they are simply passing through errno
//! from a sub-function, they use ENODATA if the UDN hardwall has not
//! been initialized or persisted properly.  Otherwise, they use EINVAL.
//!

#endif // __BME__

//! @addtogroup tmc_udn
//! @section udn_transfer Transferring Data
//!
//! TMC provides convenience functions for sending and receiving data
//! packets.  To send a five-word packet to demux queue two on a
//! remote CPU, do the following:
//!
//! @code
//! DynamicHeader header = tmc_udn_header_from_cpu(dest_cpu);
//! tmc_udn_send_5(header, UDN2_DEMUX_TAG, data0, data1, data2, data3, data4);
//! @endcode
//!
//! When this packet arrives at the destination CPU, the UDN hardware
//! strips off the header and tag information and places the five data words
//! into demux queue 2.  To receive those five data words, do the
//! following:
//!
//! @code
//! data0 = tmc_udn2_receive();
//! data1 = tmc_udn2_receive();
//! data2 = tmc_udn2_receive();
//! data3 = tmc_udn2_receive();
//! data4 = tmc_udn2_receive();
//! @endcode
//!
//! For convenience, tmc_udn_send_buffer() and
//! tmc_udn0_receive_buffer() (and the like) can be used to send and receive
//! blocks of memory.  Using these calls is particularly useful for sending and
//! receiving "structs".  The data buffers passed to these functions
//! must be word-aligned.
//!
//! @section udn_other Other UDN Capabilities
//!
//! The UDN hardware provides a variety of capabilities beyond sending
//! and receiving words.  For more information about the UDN hardware,
//! see arch/udn.h.
//!
//! @{

#ifndef __BME__

#include <tmc/cpus.h>


//! Initialize the hardwall using a rectangle containing @c cpus.
//!
//! If @c cpus is NULL, the current process affinity is used instead
//! (see tmc_task_get_my_affinity()).  Note that if @c cpus is empty,
//! this function fails.
//!
//! The effects of this function are determined as follows:
//!
//! - If the hardwall has already been initialized, this function
//!   fails, returning 1 if the existing hardwall is sufficient, else -1.
//!
//! - Otherwise, if a hardwall was persisted by a parent process, then
//!   if the persisted hardwall contains the requested CPUs, then that
//!   hardwall is used, otherwise, this function fails.
//!
//! - Otherwise, a new hardwall is created, using the minimal
//!   rectangle that contains the CPUs.
//!
//! Since this function is often called implicitly by other functions,
//! a positive return value can often be ignored.  However, a negative
//! return value normally indicates a serious problem.
//!
//! In a multi-threaded process, tmc_udn_init() uses locking to ensure
//! that two threads can not invoke it at the same time.  In general, the
//! initial parent thread should call tmc_udn_init() before any other
//! threads are created; if not, each thread should individually
//! ensure that it calls tmc_udn_init() prior to using the UDN.
//! A thread should not assume that some other thread's call to
//! tmc_udn_init() happens early enough to allow the first thread to
//! invoke tmc_udn functions.
//!
//! @param cpus The set of CPUs, or NULL to use the current "affinity".
//!
//! @return -1 (setting errno) on error, 1 (setting errno to EINVAL)
//! if already initialized properly, or zero.
//!
extern int
tmc_udn_init(cpu_set_t* cpus);


//! Stop using any created (or inherited) hardwall.
//!
//! tmc_udn_close() can be called after tmc_udn_init() has been called
//! and a hardwall region has been set up, used, and is no longer
//! necessary; alternately, it can be called before tmc_udn_init() as
//! a way of ensuring that any inherited hardwall is ignored.
//!
//! Note that simply closing the current process's hardwall will not
//! cause the hardwall to be destroyed unless it is the last process
//! referencing the hardwall.  If other processes are still
//! referencing the previous hardwall, tmc_udn_init() will fail if it
//! attempts to include CPUs that are within that hardwall.
//!
//! The tmc_udn_close() function should only be called when no other
//! thread can be calling tmc_udn functions or accessing the UDN directly.
//!
//! @return -1 (setting errno) on error, 1 (setting errno to ENODATA)
//! if there is nothing to close, or zero.
//!
extern int
tmc_udn_close(void);


//! Activate the hardwall to provide access to the network.
//!
//! @return -1 (setting errno) on error, or zero.
//!
extern int
tmc_udn_activate(void);


//! Set whether or not the hardwall should be available after @c exec().
//!
//! Note that this function affects the entire process and should
//! thus normally be called just before calling @c exec(), after @c fork()
//! is called.
//!
//! @param flag The desired flag value.
//!
//! @return -1 (setting errno) on error, or the previous value (0 or 1).
//!
extern int
tmc_udn_persist_after_exec(int flag);


//! Convert a CPU number to a dynamic network header.
//!
//! The result of this function is undefined if 'cpu' is not a legal CPU.
//!
//! @param cpu A CPU number.
//!
//! @return The dynamic header for the given CPU.
//!
extern DynamicHeader
tmc_udn_header_from_cpu(int cpu);

#endif // __BME__


#ifndef __DOXYGEN__

//! Send a header (with a size) and a tag on the UDN.
//!
static __USUALLY_INLINE void
__tmc_udn_send_header_with_size_and_tag(DynamicHeader dest,
                                        uint32_t data_words,
                                        uint32_t tag)
{
#ifdef __tilegx__
  dest.bits.demux = tag;
  udn_send(dest.word + data_words);
#else  
  udn_send(dest.word + data_words + 1);
  udn_send(tag);
#endif
}

#endif

//! The default tag for UDN demux queue 0.
#define TMC_UDN0_DEMUX_TAG UDN0_DEMUX_TAG

//! The default tag for UDN demux queue 1.
#define TMC_UDN1_DEMUX_TAG UDN1_DEMUX_TAG

//! The default tag for UDN demux queue 2.
#define TMC_UDN2_DEMUX_TAG UDN2_DEMUX_TAG

//! The default tag for UDN demux queue 3.
#define TMC_UDN3_DEMUX_TAG UDN3_DEMUX_TAG


//! Send a packet of words on the UDN.
//!
//! @param dest The destination (with no length).
//! @param tag The tag (e.g. UDN0_DEMUX_TAG).
//! @param buf A buffer aligned to int_reg_t.
//! @param words The size of the buffer (in int_reg_t units).
//!  This function can send a maximum of 126 int_reg_t words.
//!
static __inline void
tmc_udn_send_buffer(DynamicHeader dest, unsigned long tag,
                    const void* buf, unsigned long words)
{
  unsigned long i;
  const uint_reg_t* ptr = (const uint_reg_t*)buf;
  __tmc_udn_send_header_with_size_and_tag(dest, words, tag);
  for (i = 0; i < words; i++)
    udn_send(ptr[i]);
}


//! Receive a packet of words from UDN demux 0.
//!
//! @param buf A buffer aligned to int_reg_t.
//! @param words The size of the buffer (in int_reg_t units).
//!
static __inline void
tmc_udn0_receive_buffer(void* buf, unsigned long words)
{
  unsigned long i;
  uint_reg_t* ptr = (uint_reg_t*)buf;
  for (i = 0; i < words; i++)
    ptr[i] = udn0_receive();
}


//! Receive a packet of words from UDN demux 1.
//!
//! @param buf A buffer aligned to int_reg_t.
//! @param words The size of the buffer (in int_reg_t units).
//!
static __inline void
tmc_udn1_receive_buffer(void* buf, unsigned long words)
{
  unsigned long i;
  uint_reg_t* ptr = (uint_reg_t*)buf;
  for (i = 0; i < words; i++)
    ptr[i] = udn1_receive();
}


//! Receive a packet of words from UDN demux 2.
//!
//! @param buf A buffer aligned to int_reg_t.
//! @param words The size of the buffer (in int_reg_t units).
//!
static __inline void
tmc_udn2_receive_buffer(void* buf, unsigned long words)
{
  unsigned long i;
  uint_reg_t* ptr = (uint_reg_t*)buf;
  for (i = 0; i < words; i++)
    ptr[i] = udn2_receive();
}


//! Receive a packet of words from UDN demux 3.
//!
//! @param buf A buffer aligned to int_reg_t.
//! @param words The size of the buffer (in int_reg_t units).
//!
static __inline void
tmc_udn3_receive_buffer(void* buf, unsigned long words)
{
  unsigned long i;
  uint_reg_t* ptr = (uint_reg_t*)buf;
  for (i = 0; i < words; i++)
    ptr[i] = udn3_receive();
}


//! Inject a word into the UDN.
//!
//! All packets must consist of a DynamicHeader word, followed by a
//! tag word, followed by data words.  This function should be avoided
//! in favor of tmc_udn_send_1() and the like, which provide static
//! compilation errors if the header length does not match the number
//! of data words injected.
//!
//! @param word The word to be sent.
//!
static __USUALLY_INLINE void 
tmc_udn_send(uint_reg_t word)
{
  udn_send(word);
}


//! Send a packet of one word.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __USUALLY_INLINE void
tmc_udn_send_1(DynamicHeader dest, uint32_t tag,
               uint_reg_t n0)
{
  __tmc_udn_send_header_with_size_and_tag(dest, 1, tag);
  udn_send(n0);
}


//! Send a packet of two words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __USUALLY_INLINE void
tmc_udn_send_2(DynamicHeader dest, uint32_t tag,
               uint_reg_t n0, uint_reg_t n1)
{
  __tmc_udn_send_header_with_size_and_tag(dest, 2, tag);
  udn_send(n0);
  udn_send(n1);
}


//! Send a packet of three words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __USUALLY_INLINE void
tmc_udn_send_3(DynamicHeader dest, uint32_t tag,
               uint_reg_t n0, uint_reg_t n1, uint_reg_t n2)
{
  __tmc_udn_send_header_with_size_and_tag(dest, 3, tag);
  udn_send(n0);
  udn_send(n1);
  udn_send(n2);
}


//! Send a packet of four words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __USUALLY_INLINE void
tmc_udn_send_4(DynamicHeader dest, uint32_t tag,
               uint_reg_t n0, uint_reg_t n1, uint_reg_t n2, uint_reg_t n3)
{
  __tmc_udn_send_header_with_size_and_tag(dest, 4, tag);
  udn_send(n0);
  udn_send(n1);
  udn_send(n2);
  udn_send(n3);
}


//! Send a packet of five words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __USUALLY_INLINE void
tmc_udn_send_5(DynamicHeader dest, uint32_t tag,
               uint_reg_t n0, 
               uint_reg_t n1, 
               uint_reg_t n2, 
               uint_reg_t n3,
               uint_reg_t n4)
{
  __tmc_udn_send_header_with_size_and_tag(dest, 5, tag);
  udn_send(n0);
  udn_send(n1);
  udn_send(n2);
  udn_send(n3);
  udn_send(n4);
}


//! Send a packet of six words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __USUALLY_INLINE void
tmc_udn_send_6(DynamicHeader dest, uint32_t tag,
               uint_reg_t n0, 
               uint_reg_t n1, 
               uint_reg_t n2, 
               uint_reg_t n3,
               uint_reg_t n4, 
               uint_reg_t n5)
{
  __tmc_udn_send_header_with_size_and_tag(dest, 6, tag);
  udn_send(n0);
  udn_send(n1);
  udn_send(n2);
  udn_send(n3);
  udn_send(n4);
  udn_send(n5);
}


//! Send a packet of seven words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __USUALLY_INLINE void
tmc_udn_send_7(DynamicHeader dest, uint32_t tag,
               uint_reg_t n0, 
               uint_reg_t n1, 
               uint_reg_t n2, 
               uint_reg_t n3,
               uint_reg_t n4, 
               uint_reg_t n5, 
               uint_reg_t n6)
{
  __tmc_udn_send_header_with_size_and_tag(dest, 7, tag);
  udn_send(n0);
  udn_send(n1);
  udn_send(n2);
  udn_send(n3);
  udn_send(n4);
  udn_send(n5);
  udn_send(n6);
}


//! Send a packet of eight words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __USUALLY_INLINE void
tmc_udn_send_8(DynamicHeader dest, uint32_t tag,
               uint_reg_t n0, 
               uint_reg_t n1, 
               uint_reg_t n2, 
               uint_reg_t n3,
               uint_reg_t n4, 
               uint_reg_t n5, 
               uint_reg_t n6, 
               uint_reg_t n7)
{
  __tmc_udn_send_header_with_size_and_tag(dest, 8, tag);
  udn_send(n0);
  udn_send(n1);
  udn_send(n2);
  udn_send(n3);
  udn_send(n4);
  udn_send(n5);
  udn_send(n6);
  udn_send(n7);
}


//! Send a packet of nine words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __USUALLY_INLINE void
tmc_udn_send_9(DynamicHeader dest, uint32_t tag,
               uint_reg_t n0, 
               uint_reg_t n1, 
               uint_reg_t n2, 
               uint_reg_t n3,
               uint_reg_t n4, 
               uint_reg_t n5, 
               uint_reg_t n6, 
               uint_reg_t n7,
               uint_reg_t n8)
{
  __tmc_udn_send_header_with_size_and_tag(dest, 9, tag);
  udn_send(n0);
  udn_send(n1);
  udn_send(n2);
  udn_send(n3);
  udn_send(n4);
  udn_send(n5);
  udn_send(n6);
  udn_send(n7);
  udn_send(n8);
}


//! Send a packet of 10 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __USUALLY_INLINE void
tmc_udn_send_10(DynamicHeader dest, uint32_t tag,
                uint_reg_t n0, 
                uint_reg_t n1, 
                uint_reg_t n2, 
                uint_reg_t n3,
                uint_reg_t n4, 
                uint_reg_t n5, 
                uint_reg_t n6, 
                uint_reg_t n7,
                uint_reg_t n8, 
                uint_reg_t n9)
{
  __tmc_udn_send_header_with_size_and_tag(dest, 10, tag);
  udn_send(n0);
  udn_send(n1);
  udn_send(n2);
  udn_send(n3);
  udn_send(n4);
  udn_send(n5);
  udn_send(n6);
  udn_send(n7);
  udn_send(n8);
  udn_send(n9);
}


//! Send a packet of 11 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __USUALLY_INLINE void
tmc_udn_send_11(DynamicHeader dest, uint32_t tag,
                uint_reg_t n0, 
                uint_reg_t n1, 
                uint_reg_t n2, 
                uint_reg_t n3,
                uint_reg_t n4, 
                uint_reg_t n5, 
                uint_reg_t n6, 
                uint_reg_t n7,
                uint_reg_t n8, 
                uint_reg_t n9, 
                uint_reg_t n10)
{
  __tmc_udn_send_header_with_size_and_tag(dest, 11, tag);
  udn_send(n0);
  udn_send(n1);
  udn_send(n2);
  udn_send(n3);
  udn_send(n4);
  udn_send(n5);
  udn_send(n6);
  udn_send(n7);
  udn_send(n8);
  udn_send(n9);
  udn_send(n10);
}


//! Send a packet of 12 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __USUALLY_INLINE void
tmc_udn_send_12(DynamicHeader dest, uint32_t tag,
                uint_reg_t n0, 
                uint_reg_t n1, 
                uint_reg_t n2, 
                uint_reg_t n3,
                uint_reg_t n4, 
                uint_reg_t n5, 
                uint_reg_t n6, 
                uint_reg_t n7,
                uint_reg_t n8, 
                uint_reg_t n9, 
                uint_reg_t n10, 
                uint_reg_t n11)
{
  __tmc_udn_send_header_with_size_and_tag(dest, 12, tag);
  udn_send(n0);
  udn_send(n1);
  udn_send(n2);
  udn_send(n3);
  udn_send(n4);
  udn_send(n5);
  udn_send(n6);
  udn_send(n7);
  udn_send(n8);
  udn_send(n9);
  udn_send(n10);
  udn_send(n11);
}


//! Send a packet of 13 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __USUALLY_INLINE void
tmc_udn_send_13(DynamicHeader dest, uint32_t tag,
                uint_reg_t n0, 
                uint_reg_t n1, 
                uint_reg_t n2, 
                uint_reg_t n3,
                uint_reg_t n4, 
                uint_reg_t n5, 
                uint_reg_t n6, 
                uint_reg_t n7,
                uint_reg_t n8, 
                uint_reg_t n9, 
                uint_reg_t n10, 
                uint_reg_t n11,
                uint_reg_t n12)
{
  __tmc_udn_send_header_with_size_and_tag(dest, 13, tag);
  udn_send(n0);
  udn_send(n1);
  udn_send(n2);
  udn_send(n3);
  udn_send(n4);
  udn_send(n5);
  udn_send(n6);
  udn_send(n7);
  udn_send(n8);
  udn_send(n9);
  udn_send(n10);
  udn_send(n11);
  udn_send(n12);
}


//! Send a packet of 14 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __USUALLY_INLINE void
tmc_udn_send_14(DynamicHeader dest, uint32_t tag,
                uint_reg_t n0, 
                uint_reg_t n1, 
                uint_reg_t n2, 
                uint_reg_t n3,
                uint_reg_t n4, 
                uint_reg_t n5, 
                uint_reg_t n6, 
                uint_reg_t n7,
                uint_reg_t n8, 
                uint_reg_t n9, 
                uint_reg_t n10, 
                uint_reg_t n11,
                uint_reg_t n12, 
                uint_reg_t n13)
{
  __tmc_udn_send_header_with_size_and_tag(dest, 14, tag);
  udn_send(n0);
  udn_send(n1);
  udn_send(n2);
  udn_send(n3);
  udn_send(n4);
  udn_send(n5);
  udn_send(n6);
  udn_send(n7);
  udn_send(n8);
  udn_send(n9);
  udn_send(n10);
  udn_send(n11);
  udn_send(n12);
  udn_send(n13);
}


//! Send a packet of 15 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __USUALLY_INLINE void
tmc_udn_send_15(DynamicHeader dest, uint32_t tag,
                uint_reg_t n0,
                uint_reg_t n1,
                uint_reg_t n2,
                uint_reg_t n3,
                uint_reg_t n4,
                uint_reg_t n5,
                uint_reg_t n6,
                uint_reg_t n7,
                uint_reg_t n8,
                uint_reg_t n9,
                uint_reg_t n10,
                uint_reg_t n11,
                uint_reg_t n12,
                uint_reg_t n13,
                uint_reg_t n14)
{
  __tmc_udn_send_header_with_size_and_tag(dest, 15, tag);
  udn_send(n0);
  udn_send(n1);
  udn_send(n2);
  udn_send(n3);
  udn_send(n4);
  udn_send(n5);
  udn_send(n6);
  udn_send(n7);
  udn_send(n8);
  udn_send(n9);
  udn_send(n10);
  udn_send(n11);
  udn_send(n12);
  udn_send(n13);
  udn_send(n14);
}


//! Send a packet of 16 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __USUALLY_INLINE void
tmc_udn_send_16(DynamicHeader dest, uint32_t tag,
                uint_reg_t n0, 
                uint_reg_t n1, 
                uint_reg_t n2, 
                uint_reg_t n3,
                uint_reg_t n4, 
                uint_reg_t n5, 
                uint_reg_t n6, 
                uint_reg_t n7,
                uint_reg_t n8, 
                uint_reg_t n9, 
                uint_reg_t n10, 
                uint_reg_t n11,
                uint_reg_t n12, 
                uint_reg_t n13, 
                uint_reg_t n14, 
                uint_reg_t n15)
{
  __tmc_udn_send_header_with_size_and_tag(dest, 16, tag);
  udn_send(n0);
  udn_send(n1);
  udn_send(n2);
  udn_send(n3);
  udn_send(n4);
  udn_send(n5);
  udn_send(n6);
  udn_send(n7);
  udn_send(n8);
  udn_send(n9);
  udn_send(n10);
  udn_send(n11);
  udn_send(n12);
  udn_send(n13);
  udn_send(n14);
  udn_send(n15);
}


//! Send a packet of 17 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __USUALLY_INLINE void
tmc_udn_send_17(DynamicHeader dest, uint32_t tag,
                uint_reg_t n0, 
                uint_reg_t n1, 
                uint_reg_t n2, 
                uint_reg_t n3,
                uint_reg_t n4, 
                uint_reg_t n5, 
                uint_reg_t n6, 
                uint_reg_t n7,
                uint_reg_t n8, 
                uint_reg_t n9, 
                uint_reg_t n10, 
                uint_reg_t n11,
                uint_reg_t n12, 
                uint_reg_t n13, 
                uint_reg_t n14, 
                uint_reg_t n15,
                uint_reg_t n16)
{
  __tmc_udn_send_header_with_size_and_tag(dest, 17, tag);
  udn_send(n0);
  udn_send(n1);
  udn_send(n2);
  udn_send(n3);
  udn_send(n4);
  udn_send(n5);
  udn_send(n6);
  udn_send(n7);
  udn_send(n8);
  udn_send(n9);
  udn_send(n10);
  udn_send(n11);
  udn_send(n12);
  udn_send(n13);
  udn_send(n14);
  udn_send(n15);
  udn_send(n16);
}


//! Send a packet of 18 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __USUALLY_INLINE void
tmc_udn_send_18(DynamicHeader dest, uint32_t tag,
                uint_reg_t n0, 
                uint_reg_t n1, 
                uint_reg_t n2, 
                uint_reg_t n3,
                uint_reg_t n4, 
                uint_reg_t n5, 
                uint_reg_t n6, 
                uint_reg_t n7,
                uint_reg_t n8, 
                uint_reg_t n9, 
                uint_reg_t n10, 
                uint_reg_t n11,
                uint_reg_t n12, 
                uint_reg_t n13, 
                uint_reg_t n14, 
                uint_reg_t n15,
                uint_reg_t n16, 
                uint_reg_t n17)
{
  __tmc_udn_send_header_with_size_and_tag(dest, 18, tag);
  udn_send(n0);
  udn_send(n1);
  udn_send(n2);
  udn_send(n3);
  udn_send(n4);
  udn_send(n5);
  udn_send(n6);
  udn_send(n7);
  udn_send(n8);
  udn_send(n9);
  udn_send(n10);
  udn_send(n11);
  udn_send(n12);
  udn_send(n13);
  udn_send(n14);
  udn_send(n15);
  udn_send(n16);
  udn_send(n17);
}


//! Send a packet of 19 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __USUALLY_INLINE void
tmc_udn_send_19(DynamicHeader dest, uint32_t tag,
                uint_reg_t n0, 
                uint_reg_t n1, 
                uint_reg_t n2, 
                uint_reg_t n3,
                uint_reg_t n4, 
                uint_reg_t n5, 
                uint_reg_t n6, 
                uint_reg_t n7,
                uint_reg_t n8, 
                uint_reg_t n9, 
                uint_reg_t n10, 
                uint_reg_t n11,
                uint_reg_t n12, 
                uint_reg_t n13, 
                uint_reg_t n14, 
                uint_reg_t n15,
                uint_reg_t n16, 
                uint_reg_t n17, 
                uint_reg_t n18)
{
  __tmc_udn_send_header_with_size_and_tag(dest, 19, tag);
  udn_send(n0);
  udn_send(n1);
  udn_send(n2);
  udn_send(n3);
  udn_send(n4);
  udn_send(n5);
  udn_send(n6);
  udn_send(n7);
  udn_send(n8);
  udn_send(n9);
  udn_send(n10);
  udn_send(n11);
  udn_send(n12);
  udn_send(n13);
  udn_send(n14);
  udn_send(n15);
  udn_send(n16);
  udn_send(n17);
  udn_send(n18);
}


//! Send a packet of 20 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __USUALLY_INLINE void
tmc_udn_send_20(DynamicHeader dest, uint32_t tag,
                uint_reg_t n0, 
                uint_reg_t n1, 
                uint_reg_t n2, 
                uint_reg_t n3,
                uint_reg_t n4, 
                uint_reg_t n5, 
                uint_reg_t n6, 
                uint_reg_t n7,
                uint_reg_t n8, 
                uint_reg_t n9, 
                uint_reg_t n10, 
                uint_reg_t n11,
                uint_reg_t n12, 
                uint_reg_t n13, 
                uint_reg_t n14, 
                uint_reg_t n15,
                uint_reg_t n16, 
                uint_reg_t n17, 
                uint_reg_t n18, 
                uint_reg_t n19)
{
  __tmc_udn_send_header_with_size_and_tag(dest, 20, tag);
  udn_send(n0);
  udn_send(n1);
  udn_send(n2);
  udn_send(n3);
  udn_send(n4);
  udn_send(n5);
  udn_send(n6);
  udn_send(n7);
  udn_send(n8);
  udn_send(n9);
  udn_send(n10);
  udn_send(n11);
  udn_send(n12);
  udn_send(n13);
  udn_send(n14);
  udn_send(n15);
  udn_send(n16);
  udn_send(n17);
  udn_send(n18);
  udn_send(n19);
}



//! Receive a word from UDN demux queue 0.
//!
//! @return A word from the UDN.
//!
static __USUALLY_INLINE uint_reg_t
tmc_udn0_receive(void)
{
  return udn0_receive();
}


//! Receive a word from UDN demux queue 1.
//!
//! @return A word from the UDN.
//!
static __USUALLY_INLINE uint_reg_t
tmc_udn1_receive(void)
{
  return udn1_receive();
}


//! Receive a word from UDN demux queue 2.
//!
//! @return A word from the UDN.
//!
static __USUALLY_INLINE uint_reg_t
tmc_udn2_receive(void)
{
  return udn2_receive();
}


//! Receive a word from UDN demux queue 3.
//!
//! @return A word from the UDN.
//!
static __USUALLY_INLINE uint_reg_t
tmc_udn3_receive(void)
{
  return udn3_receive();
}

//! Get the number of words in UDN demux queue 0.
//!
//! @return The number of words in the demux queue.
//!
static __USUALLY_INLINE unsigned int 
tmc_udn0_available_count(void)
{
  return udn0_available_count();
}


//! Get the number of words in UDN demux queue 1.
//!
//! @return The number of words in the demux queue.
//!
static __USUALLY_INLINE unsigned int 
tmc_udn1_available_count(void)
{
  return udn1_available_count();
}


//! Get the number of words in UDN demux queue 2.
//!
//! @return The number of words in the demux queue.
//!
static __USUALLY_INLINE unsigned int 
tmc_udn2_available_count(void)
{
  return udn2_available_count();
}


//! Get the number of words in UDN demux queue 3.
//!
//! @return The number of words in the demux queue.
//!
static __USUALLY_INLINE unsigned int 
tmc_udn3_available_count(void)
{
  return udn3_available_count();
}


//! Determine which UDN demuxes have data available.
//!
//! @return A mask of "SPR_UDN_DATA_AVAIL__AVAIL_*_MASK" values, based
//! on which UDN demuxes currently have data available.
//!
static __USUALLY_INLINE unsigned int 
tmc_udn_available_mask(void)
{
  return udn_available_mask();
}

__END_DECLS

//! @}

#endif // __TMC_UDN_H__
