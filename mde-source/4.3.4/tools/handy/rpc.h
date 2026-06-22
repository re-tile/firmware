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

#ifndef TOOLS_HANDY_RPC_H
#define TOOLS_HANDY_RPC_H

#include "common.h"
#include "Buffer.h"

#include "StringArray.h"
#include "Pollable.h"


BEGIN_EXTERN_C


// Total Packet Size (uint) + Code (uint16_t) + Tag (uint16_t).
#define RPC_HEADER_SIZE 8


// Helpful macros.

// Determine if a code indicates a query (or "other").
#define RPC_IS_QUERY(code) (code < 0x8000)

// Determine if a code indicates a reply.
#define RPC_IS_REPLY(code) (code >= 0x8000 && code < 0xC000)

// Determine if a code indicates a query.
#define RPC_IS_ERROR(code) (code >= 0xC000)

// Turn a query code into a reply code.
#define RPC_REPLY(code) ((code) ^ 0xC000)

// Turn a query code into an error code.
#define RPC_ERROR(code) ((code) | 0x8000)


//! Write @param v to @param output as eight little endian bytes.
extern void
marshal_uint64(Buffer* output, uint64_t v);

//! Read a uint from @param input as eight little endian bytes.
extern uint64_t
unmarshal_uint64(Buffer* input);


//! Write @param v to @param output as four little endian bytes.
extern void
marshal_uint(Buffer* output, uint v);

//! Read a uint from @param input as four little endian bytes.
extern uint
unmarshal_uint(Buffer* input);


//! Write @param v to @param output as two little endian bytes.
extern void
marshal_uint16(Buffer* output, uint16_t v);

//! Read a uint16 from @param input as two little endian bytes.
extern uint16_t
unmarshal_uint16(Buffer* input);


//! Write @param v to @param output as a single byte.
extern void
marshal_uint8(Buffer* output, uint8_t v);

//! Read a uint8 from @param input as a single byte.
extern uint8_t
unmarshal_uint8(Buffer* input);


//! Write @param V to @param O using @ref marshal_uint.
#define marshal_int(O, V) marshal_uint(O, (uint)(V))

//! Read an int from @param I using @ref unmarshal_uint.
#define unmarshal_int(I) (int)unmarshal_uint(I)


//! Write @param V to @param O using @ref marshal_uint16.
#define marshal_int16(O, V) marshal_uint16((O), (uint16_t)(V))

//! Read an int16 from @param I using @ref unmarshal_uint16.
#define unmarshal_int16(I) (int16_t)unmarshal_uint16(I)


//! Write @param V to @param O using @ref marshal_uint8.
#define marshal_int8(O, V) marshal_uint8(O, (uint8_t)(V))

//! Read an int8 from @param I using @ref unmarshal_uint8.
#define unmarshal_int8(I) (int8_t)unmarshal_uint8(I)


//! Write @param V to @param O using @ref marshal_uint8.
#define marshal_bool(O, V) marshal_uint8(O, (V) ? 1 : 0)

//! Read a bool from @param I using @ref unmarshal_uint8.
#define unmarshal_bool(I) (unmarshal_uint8(I) != 0)


//! Write @param str to @param output as raw bytes, including the final nul.
extern void
marshal_ztstr(Buffer* output, const char* str);

//! Read a string from @param input as raw bytes, including the final nul.
extern char*
unmarshal_ztstr(Buffer* input);


extern void
marshal_uint8_array(Buffer* output, const uint8_t* val, size_t size);

extern uint8_t*
unmarshal_uint8_array(Buffer* input, size_t* sizep);


extern void
marshal_StringArray(Buffer* output, const StringArray* array);

extern StringArray
unmarshal_StringArray(Buffer* input);


//! Start writing to @param pollable the packet with the given @param code
//! and @param tag, and return a value for use with @ref packet_finish or
//! @ref packet_cancel.
extern uint
packet_start(Pollable* pollable, uint16_t code, uint16_t tag);

//! Finish writing to @param pollable the given @param packet.
extern void
packet_finish(Pollable* pollable, uint packet);

//! Cancel writing to @param pollable the given @param packet.
extern void
packet_cancel(Pollable* pollable, uint packet);

//! Start and finish a packet to @param pollable using the given @code and
//! @param tag, containing @param size other bytes starting at @param data.
extern void
packet_write(Pollable* pollable, uint16_t code, uint16_t tag,
             uint size, const uint8_t* data);


typedef struct _RPC RPC;

struct _RPC
{
  Pollable* socket;

  uint16_t code;
  uint16_t tag;
};


//! The "handle_packets" function calls a "handle_packet" function with an
//! "rpc" object, which contains a "socket", and the "code" and "tag" for
//! the incoming packet.  The "body" of the packet is in "socket->input",
//! starting at "input.head", and ending at "input.size".  It is allowed
//! to modify the actual bytes of the "body", and "input.head", as is done
//! by the various "unmarshal" functions, and thus also by "dispatch_packet".
//! It is not allowed to modify "input" in any other way, and in particular,
//! "Buffer_append", "Buffer_excise", and similar functions, may not be used.
//! It should write a reply/error packet into "socket->output", presumably
//! using the "reply_xxx" or "rpc_error" functions.
//!
//! A fancy "handle_packet" functions can send "deferred" reply/error packets,
//! by saving "rpc" aside for later use, but it must be careful that "socket"
//! will still point to legal memory, and it may not use "input".
//!
//! A very fancy "handle_packet" function can make use of the fact that the
//! full packet, including its header, is contained in "input", and includes
//! an 8 byte header immediately preceding the body, plus the body itself, so
//! the full packet starts at "input.head - 8", and ends at "input.size".  It
//! may "forward" the packet to another socket, as raw bytes, if it is sure
//! that the "tag" for the packet will not conflict with other packets sent
//! on that socket.
//!
typedef void (*handle_packet_func)(RPC rpc);


//! Read some bytes from @param socket, using @ref Pollable_acquire, and then
//! handle packets using @param handler.  Return the number of packets handled,
//! or return -1 after calling "Pollable_close()" on "immediate EOF" or corrupt
//! packet (treated as EPIPE).  Die on various (other) errors.
extern int
handle_packets(Pollable* socket, handle_packet_func handler);


//! Like @ref handle_packets, but reading at most one packet at a time.
extern int
handle_packets_slowly(Pollable* socket, handle_packet_func handler);


extern void
rpc_verify_consumed(Buffer* buffer, uint16_t code);

extern void
rpc_error(RPC rpc, const char* format, ...)
  __attribute__((format(printf, 2, 3)));

extern void
rpc_error_with_errno(RPC rpc, const char* format, ...)
  __attribute__((format(printf, 2, 3)));

extern void
rpc_set_tag_range(uint16_t min, uint16_t max);


typedef struct _AnswerHandler AnswerHandler;

struct _AnswerHandler
{
  void* reply_handler; // void (*reply_handler)(void* info, [...])
  void* reply_handler_info;
  void (*error_handler)(void* info, char* msg);
  void* error_handler_info;

  uint16_t code;
  uint16_t tag;

  AnswerHandler *next;
};


extern void
rpc_add_answer_handler(AnswerHandler* ah);

extern AnswerHandler*
rpc_get_answer_handler(uint16_t code, uint16_t tag);


END_EXTERN_C

#endif /* !TOOLS_HANDY_RPC_H */
