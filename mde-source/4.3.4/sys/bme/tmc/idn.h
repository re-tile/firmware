// Copyright 2014 Tilera Corporation. All Rights Reserved.
//
//   The source code contained or described herein and all documents
//   related to the source code ("Material") are owned by Tilera
//   Corporation or its suppliers or licensors.  Title to the Material
//   remains with Tilera Corporation or its suppliers and licensors. The
//   software is licensed under the Tilera MDE License.
//
//   Unless otherwise agreed by Tilera in writing, you may not remove or
//   alter this notice or any other notice embedded in Materials by Tilera
//   or Tilera's suppliers or licensors in any way.

//! @file
//!
//! Support for the IDN (I/O dynamic network).
//!

//! @addtogroup tmc_idn
//! @{
//!
//! Simple packets can be sent between tiles over the IDN.
//!
//! See "arch/idn.h" for more information about the IDN.
//!
//! The IDN offers two demuxes (plus a special "catch all" demux).  Packets
//! of data can be sent from one tile to a specified demux on another tile.
//!
//! To send a packet on the IDN, use "idn_send()" to send a special header
//! word (encoding the destination tile, and the total number of words in the
//! packet, not including the header word itself), then a "tag" (encoding
//! which demux should receive the packet), and then the "payload" words.
//!
//! To receive a packet on one of the two demuxes, use "idnX_receive()"
//! and the like to receive each word of the "payload".  Note that the header
//! and the tag are handled automatically and are NOT seen by "idnX_receive()".
//!
//! For convenience, "tmc_idn_send_buffer()" and "tmc_idnX_receive_buffer()"
//! can be used to send and receive blocks of memory.  This is particularly
//! useful for sending and receiving "structs".
//!
//! For convenience, "tmc_idn_send_N()" can be used to send a packet with
//! "N" words.  Such a packet can be received by "tmc_idnX_receive_buffer()"
//! or by an explicit sequence of "N" calls to "idnX_receive()".
//!

#ifndef __TMC_IDN_H__
#define __TMC_IDN_H__


#include <arch/idn.h>


#include <features.h>

__BEGIN_DECLS


#ifndef __DOXYGEN__

//! Send a header (with a size) and a tag on the IDN.
//!
static __inline void
__tmc_idn_send_header_with_size_and_tag(DynamicHeader dest,
                                        uint32_t data_words,
                                        uint32_t tag)
{
  idn_send(dest.word + data_words + 1);
  idn_send(tag);
}

#endif


//! Send a packet of words on the IDN.
//!
//! @param dest The destination (with no length).
//! @param tag The tag (e.g. IDN0_DEMUX_TAG).
//! @param buf The buffer.
//! @param words The size of the buffer (in words).
//!
static __inline void
tmc_idn_send_buffer(DynamicHeader dest, uint32_t tag,
                    const void* buf, uint32_t words)
{
  unsigned int i;
  const uint32_t* ptr = (const uint32_t*)buf;
  __tmc_idn_send_header_with_size_and_tag(dest, words, tag);
  for (i = 0; i < words; i++)
    idn_send(ptr[i]);
}


//! Receive a packet of words from idn demux 0.
//!
//! @param buf The buffer.
//! @param words The size of the buffer (in words).
//!
static __inline void
tmc_idn0_receive_buffer(void* buf, uint32_t words)
{
  unsigned int i;
  uint32_t* ptr = (uint32_t*)buf;
  for (i = 0; i < words; i++)
    ptr[i] = idn0_receive();
}


//! Receive a packet of words from idn demux 1.
//!
//! @param buf The buffer.
//! @param words The size of the buffer (in words).
//!
static __inline void
tmc_idn1_receive_buffer(void* buf, uint32_t words)
{
  unsigned int i;
  uint32_t* ptr = (uint32_t*)buf;
  for (i = 0; i < words; i++)
    ptr[i] = idn1_receive();
}


//! Send a packet of 1 word.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __inline void
tmc_idn_send_1(DynamicHeader dest, uint32_t tag,
               uint32_t n0)
{
  __tmc_idn_send_header_with_size_and_tag(dest, 1, tag);
  idn_send(n0);
}


//! Send a packet of 2 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __inline void
tmc_idn_send_2(DynamicHeader dest, uint32_t tag,
               uint32_t n0, uint32_t n1)
{
  __tmc_idn_send_header_with_size_and_tag(dest, 2, tag);
  idn_send(n0);
  idn_send(n1);
}


//! Send a packet of 3 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __inline void
tmc_idn_send_3(DynamicHeader dest, uint32_t tag,
               uint32_t n0, uint32_t n1, uint32_t n2)
{
  __tmc_idn_send_header_with_size_and_tag(dest, 3, tag);
  idn_send(n0);
  idn_send(n1);
  idn_send(n2);
}


//! Send a packet of 4 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __inline void
tmc_idn_send_4(DynamicHeader dest, uint32_t tag,
               uint32_t n0, uint32_t n1, uint32_t n2, uint32_t n3)
{
  __tmc_idn_send_header_with_size_and_tag(dest, 4, tag);
  idn_send(n0);
  idn_send(n1);
  idn_send(n2);
  idn_send(n3);
}


//! Send a packet of 5 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __inline void
tmc_idn_send_5(DynamicHeader dest, uint32_t tag,
               uint32_t n0, uint32_t n1, uint32_t n2, uint32_t n3,
               uint32_t n4)
{
  __tmc_idn_send_header_with_size_and_tag(dest, 5, tag);
  idn_send(n0);
  idn_send(n1);
  idn_send(n2);
  idn_send(n3);
  idn_send(n4);
}


//! Send a packet of 6 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __inline void
tmc_idn_send_6(DynamicHeader dest, uint32_t tag,
               uint32_t n0, uint32_t n1, uint32_t n2, uint32_t n3,
               uint32_t n4, uint32_t n5)
{
  __tmc_idn_send_header_with_size_and_tag(dest, 6, tag);
  idn_send(n0);
  idn_send(n1);
  idn_send(n2);
  idn_send(n3);
  idn_send(n4);
  idn_send(n5);
}


//! Send a packet of 7 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __inline void
tmc_idn_send_7(DynamicHeader dest, uint32_t tag,
               uint32_t n0, uint32_t n1, uint32_t n2, uint32_t n3,
               uint32_t n4, uint32_t n5, uint32_t n6)
{
  __tmc_idn_send_header_with_size_and_tag(dest, 7, tag);
  idn_send(n0);
  idn_send(n1);
  idn_send(n2);
  idn_send(n3);
  idn_send(n4);
  idn_send(n5);
  idn_send(n6);
}


//! Send a packet of 8 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __inline void
tmc_idn_send_8(DynamicHeader dest, uint32_t tag,
               uint32_t n0, uint32_t n1, uint32_t n2, uint32_t n3,
               uint32_t n4, uint32_t n5, uint32_t n6, uint32_t n7)
{
  __tmc_idn_send_header_with_size_and_tag(dest, 8, tag);
  idn_send(n0);
  idn_send(n1);
  idn_send(n2);
  idn_send(n3);
  idn_send(n4);
  idn_send(n5);
  idn_send(n6);
  idn_send(n7);
}


//! Send a packet of 9 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __inline void
tmc_idn_send_9(DynamicHeader dest, uint32_t tag,
               uint32_t n0, uint32_t n1, uint32_t n2, uint32_t n3,
               uint32_t n4, uint32_t n5, uint32_t n6, uint32_t n7,
               uint32_t n8)
{
  __tmc_idn_send_header_with_size_and_tag(dest, 9, tag);
  idn_send(n0);
  idn_send(n1);
  idn_send(n2);
  idn_send(n3);
  idn_send(n4);
  idn_send(n5);
  idn_send(n6);
  idn_send(n7);
  idn_send(n8);
}


//! Send a packet of 10 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __inline void
tmc_idn_send_10(DynamicHeader dest, uint32_t tag,
                uint32_t n0, uint32_t n1, uint32_t n2, uint32_t n3,
                uint32_t n4, uint32_t n5, uint32_t n6, uint32_t n7,
                uint32_t n8, uint32_t n9)
{
  __tmc_idn_send_header_with_size_and_tag(dest, 10, tag);
  idn_send(n0);
  idn_send(n1);
  idn_send(n2);
  idn_send(n3);
  idn_send(n4);
  idn_send(n5);
  idn_send(n6);
  idn_send(n7);
  idn_send(n8);
  idn_send(n9);
}


//! Send a packet of 11 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __inline void
tmc_idn_send_11(DynamicHeader dest, uint32_t tag,
                uint32_t n0, uint32_t n1, uint32_t n2, uint32_t n3,
                uint32_t n4, uint32_t n5, uint32_t n6, uint32_t n7,
                uint32_t n8, uint32_t n9, uint32_t n10)
{
  __tmc_idn_send_header_with_size_and_tag(dest, 11, tag);
  idn_send(n0);
  idn_send(n1);
  idn_send(n2);
  idn_send(n3);
  idn_send(n4);
  idn_send(n5);
  idn_send(n6);
  idn_send(n7);
  idn_send(n8);
  idn_send(n9);
  idn_send(n10);
}


//! Send a packet of 12 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __inline void
tmc_idn_send_12(DynamicHeader dest, uint32_t tag,
                uint32_t n0, uint32_t n1, uint32_t n2, uint32_t n3,
                uint32_t n4, uint32_t n5, uint32_t n6, uint32_t n7,
                uint32_t n8, uint32_t n9, uint32_t n10, uint32_t n11)
{
  __tmc_idn_send_header_with_size_and_tag(dest, 12, tag);
  idn_send(n0);
  idn_send(n1);
  idn_send(n2);
  idn_send(n3);
  idn_send(n4);
  idn_send(n5);
  idn_send(n6);
  idn_send(n7);
  idn_send(n8);
  idn_send(n9);
  idn_send(n10);
  idn_send(n11);
}


//! Send a packet of 13 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __inline void
tmc_idn_send_13(DynamicHeader dest, uint32_t tag,
                uint32_t n0, uint32_t n1, uint32_t n2, uint32_t n3,
                uint32_t n4, uint32_t n5, uint32_t n6, uint32_t n7,
                uint32_t n8, uint32_t n9, uint32_t n10, uint32_t n11,
                uint32_t n12)
{
  __tmc_idn_send_header_with_size_and_tag(dest, 13, tag);
  idn_send(n0);
  idn_send(n1);
  idn_send(n2);
  idn_send(n3);
  idn_send(n4);
  idn_send(n5);
  idn_send(n6);
  idn_send(n7);
  idn_send(n8);
  idn_send(n9);
  idn_send(n10);
  idn_send(n11);
  idn_send(n12);
}


//! Send a packet of 14 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __inline void
tmc_idn_send_14(DynamicHeader dest, uint32_t tag,
                uint32_t n0, uint32_t n1, uint32_t n2, uint32_t n3,
                uint32_t n4, uint32_t n5, uint32_t n6, uint32_t n7,
                uint32_t n8, uint32_t n9, uint32_t n10, uint32_t n11,
                uint32_t n12, uint32_t n13)
{
  __tmc_idn_send_header_with_size_and_tag(dest, 14, tag);
  idn_send(n0);
  idn_send(n1);
  idn_send(n2);
  idn_send(n3);
  idn_send(n4);
  idn_send(n5);
  idn_send(n6);
  idn_send(n7);
  idn_send(n8);
  idn_send(n9);
  idn_send(n10);
  idn_send(n11);
  idn_send(n12);
  idn_send(n13);
}


//! Send a packet of 15 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __inline void
tmc_idn_send_15(DynamicHeader dest, uint32_t tag,
                uint32_t n0,
                uint32_t n1,
                uint32_t n2,
                uint32_t n3,
                uint32_t n4,
                uint32_t n5,
                uint32_t n6,
                uint32_t n7,
                uint32_t n8,
                uint32_t n9,
                uint32_t n10,
                uint32_t n11,
                uint32_t n12,
                uint32_t n13,
                uint32_t n14)
{
  __tmc_idn_send_header_with_size_and_tag(dest, 15, tag);
  idn_send(n0);
  idn_send(n1);
  idn_send(n2);
  idn_send(n3);
  idn_send(n4);
  idn_send(n5);
  idn_send(n6);
  idn_send(n7);
  idn_send(n8);
  idn_send(n9);
  idn_send(n10);
  idn_send(n11);
  idn_send(n12);
  idn_send(n13);
  idn_send(n14);
}


//! Send a packet of 16 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __inline void
tmc_idn_send_16(DynamicHeader dest, uint32_t tag,
                uint32_t n0, uint32_t n1, uint32_t n2, uint32_t n3,
                uint32_t n4, uint32_t n5, uint32_t n6, uint32_t n7,
                uint32_t n8, uint32_t n9, uint32_t n10, uint32_t n11,
                uint32_t n12, uint32_t n13, uint32_t n14, uint32_t n15)
{
  __tmc_idn_send_header_with_size_and_tag(dest, 16, tag);
  idn_send(n0);
  idn_send(n1);
  idn_send(n2);
  idn_send(n3);
  idn_send(n4);
  idn_send(n5);
  idn_send(n6);
  idn_send(n7);
  idn_send(n8);
  idn_send(n9);
  idn_send(n10);
  idn_send(n11);
  idn_send(n12);
  idn_send(n13);
  idn_send(n14);
  idn_send(n15);
}


//! Send a packet of 17 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __inline void
tmc_idn_send_17(DynamicHeader dest, uint32_t tag,
                uint32_t n0, uint32_t n1, uint32_t n2, uint32_t n3,
                uint32_t n4, uint32_t n5, uint32_t n6, uint32_t n7,
                uint32_t n8, uint32_t n9, uint32_t n10, uint32_t n11,
                uint32_t n12, uint32_t n13, uint32_t n14, uint32_t n15,
                uint32_t n16)
{
  __tmc_idn_send_header_with_size_and_tag(dest, 17, tag);
  idn_send(n0);
  idn_send(n1);
  idn_send(n2);
  idn_send(n3);
  idn_send(n4);
  idn_send(n5);
  idn_send(n6);
  idn_send(n7);
  idn_send(n8);
  idn_send(n9);
  idn_send(n10);
  idn_send(n11);
  idn_send(n12);
  idn_send(n13);
  idn_send(n14);
  idn_send(n15);
  idn_send(n16);
}


//! Send a packet of 17 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __inline void
tmc_idn_send_18(DynamicHeader dest, uint32_t tag,
                uint32_t n0, uint32_t n1, uint32_t n2, uint32_t n3,
                uint32_t n4, uint32_t n5, uint32_t n6, uint32_t n7,
                uint32_t n8, uint32_t n9, uint32_t n10, uint32_t n11,
                uint32_t n12, uint32_t n13, uint32_t n14, uint32_t n15,
                uint32_t n16, uint32_t n17)
{
  __tmc_idn_send_header_with_size_and_tag(dest, 18, tag);
  idn_send(n0);
  idn_send(n1);
  idn_send(n2);
  idn_send(n3);
  idn_send(n4);
  idn_send(n5);
  idn_send(n6);
  idn_send(n7);
  idn_send(n8);
  idn_send(n9);
  idn_send(n10);
  idn_send(n11);
  idn_send(n12);
  idn_send(n13);
  idn_send(n14);
  idn_send(n15);
  idn_send(n16);
  idn_send(n17);
}


//! Send a packet of 19 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __inline void
tmc_idn_send_19(DynamicHeader dest, uint32_t tag,
                uint32_t n0, uint32_t n1, uint32_t n2, uint32_t n3,
                uint32_t n4, uint32_t n5, uint32_t n6, uint32_t n7,
                uint32_t n8, uint32_t n9, uint32_t n10, uint32_t n11,
                uint32_t n12, uint32_t n13, uint32_t n14, uint32_t n15,
                uint32_t n16, uint32_t n17, uint32_t n18)
{
  __tmc_idn_send_header_with_size_and_tag(dest, 19, tag);
  idn_send(n0);
  idn_send(n1);
  idn_send(n2);
  idn_send(n3);
  idn_send(n4);
  idn_send(n5);
  idn_send(n6);
  idn_send(n7);
  idn_send(n8);
  idn_send(n9);
  idn_send(n10);
  idn_send(n11);
  idn_send(n12);
  idn_send(n13);
  idn_send(n14);
  idn_send(n15);
  idn_send(n16);
  idn_send(n17);
  idn_send(n18);
}


//! Send a packet of 20 words.
//!
//! The parameters are a destination (with no length), a tag, and
//! then the appropriate number of data words.
//!
static __inline void
tmc_idn_send_20(DynamicHeader dest, uint32_t tag,
                uint32_t n0, uint32_t n1, uint32_t n2, uint32_t n3,
                uint32_t n4, uint32_t n5, uint32_t n6, uint32_t n7,
                uint32_t n8, uint32_t n9, uint32_t n10, uint32_t n11,
                uint32_t n12, uint32_t n13, uint32_t n14, uint32_t n15,
                uint32_t n16, uint32_t n17, uint32_t n18, uint32_t n19)
{
  __tmc_idn_send_header_with_size_and_tag(dest, 20, tag);
  idn_send(n0);
  idn_send(n1);
  idn_send(n2);
  idn_send(n3);
  idn_send(n4);
  idn_send(n5);
  idn_send(n6);
  idn_send(n7);
  idn_send(n8);
  idn_send(n9);
  idn_send(n10);
  idn_send(n11);
  idn_send(n12);
  idn_send(n13);
  idn_send(n14);
  idn_send(n15);
  idn_send(n16);
  idn_send(n17);
  idn_send(n18);
  idn_send(n19);
}



//! Receive a word from IDN demux queue 0.
//!
//! @return A word from the idn.
//!
static __inline unsigned long __attribute__((always_inline))
  tmc_idn0_receive(void)
{
  return idn0_receive();
}


//! Receive a word from IDN demux queue 1.
//!
//! @return A word from the idn.
//!
static __inline unsigned long __attribute__((always_inline))
  tmc_idn1_receive(void)
{
  return idn1_receive();
}

__END_DECLS

#endif // __TMC_IDN_H__

// @}
