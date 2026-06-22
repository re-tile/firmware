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


// ISSUE: When a socket is closed, and there are outstanding RPC calls,
// should we attempt to generate synthetic failures?

// ISSUE: If somebody saves aside an "RPC" object, to send an async
// reply/error later, and somebody closes (or destroys!) the "socket",
// bad things could easily happen.


#include "rpc.h"

#include "message.h"
#include "various.h"


void
marshal_uint64(Buffer* output, uint64_t v)
{
  Buffer_reserve(output, 8);
  write_uint64(output->data + output->size, v);
  output->size += 8;
}


uint64_t
unmarshal_uint64(Buffer* input)
{
  if (input->size - input->head < 8)
    punt("Unexpected EOF!");

  uint64_t val = read_uint64(input->data + input->head);
  input->head += 8;
  return val;
}



void
marshal_uint(Buffer* output, uint v)
{
  Buffer_reserve(output, 4);
  write_uint(output->data + output->size, v);
  output->size += 4;
}


uint
unmarshal_uint(Buffer* input)
{
  if (input->size - input->head < 4)
    punt("Unexpected EOF!");

  uint val = read_uint(input->data + input->head);
  input->head += 4;
  return val;
}



void
marshal_uint16(Buffer* output, uint16_t v)
{
  Buffer_reserve(output, 2);
  write_uint16(output->data + output->size, v);
  output->size += 2;
}


uint16_t
unmarshal_uint16(Buffer* input)
{
  if (input->size - input->head < 2)
    punt("Unexpected EOF!");

  uint16_t val = read_uint16(input->data + input->head);
  input->head += 2;
  return val;
}



void
marshal_uint8(Buffer* output, uint8_t v)
{
  Buffer_reserve(output, 1);
  output->data[output->size++] = v;
}


uint8_t
unmarshal_uint8(Buffer* input)
{
  if (input->size - input->head < 1)
    punt("Unexpected EOF!");

  return input->data[input->head++];
}



void
marshal_ztstr(Buffer* output, const char* str)
{
  Buffer_write(output, (void*)str, strlen(str) + 1);
}


char*
unmarshal_ztstr(Buffer* input)
{
  char* start = (char*)input->data + input->head;
  char* end = memchr(start, '\0', input->size - input->head);
  if (end == NULL)
    punt("Unexpected EOF!");
  input->head += (end - start) + 1;
  return start;
}



void
marshal_uint8_array(Buffer* output, const uint8_t* val, size_t size)
{
  marshal_uint(output, size);
  Buffer_write(output, val, size);
}


uint8_t*
unmarshal_uint8_array(Buffer* input, size_t* sizep)
{
  *sizep = unmarshal_uint(input);
  uint8_t* val = input->data + input->head;
  input->head += *sizep;
  return val;
}



void
marshal_StringArray(Buffer* output, const StringArray* array)
{
  marshal_uint(output, array->size);
  for (uint i = 0; i < array->size; i++)
  {
    marshal_ztstr(output, StringArray_get(array, i));
  }
}


StringArray
unmarshal_StringArray(Buffer* input)
{
  StringArray val;
  StringArray_init(&val);
  uint size = unmarshal_uint(input);
  StringArray_reserve(&val, size);
  for (uint i = 0; i < size; i++)
  {
    StringArray_append(&val, unmarshal_ztstr(input));
  }
  return val;
}



uint
packet_start(Pollable* pollable, uint16_t code, uint16_t tag)
{
  Buffer* output = &pollable->output;
  uint packet = output->size;
  marshal_uint(output, (uint)-1);
  marshal_uint16(output, code);
  marshal_uint16(output, tag);
  return packet;
}


void
packet_finish(Pollable* pollable, uint packet)
{
  Buffer* output = &pollable->output;
  uint8_t* data = output->data;
  uint size = output->size;

  // Update "packet size".
  write_uint(data + packet, size - packet);

  uint16_t code = read_uint16(data + packet + 4);
  uint16_t tag = read_uint16(data + packet + 6);
  spew(3, "%s finished packet (0x%04x, 0x%04x, %u).",
       pollable->name, code, tag, (size - packet) - RPC_HEADER_SIZE);

  Pollable_flush_later(pollable);
}


void
packet_cancel(Pollable* pollable, uint packet)
{
  Buffer* output = &pollable->output;
  output->size = packet;
}


void
packet_write(Pollable* pollable, uint16_t code, uint16_t tag,
             uint size, const uint8_t* data)
{
  uint packet = packet_start(pollable, code, tag);
  Buffer_write(&pollable->output, data, size);
  packet_finish(pollable, packet);
}



// Help handle packets.  Returns number of packets handled, or -1 on error.
//
static int
handle_packets_helper(Pollable* socket, handle_packet_func handler)
{
  Buffer* input = &socket->input;

  int count = 0;

  while (true)
  {
    uint head = input->head;
    uint avail = input->size - head;

    // Wait for a complete header.
    if (avail < RPC_HEADER_SIZE)
      break;

    uint8_t* data = input->data + head;

    uint need = read_uint(data);

    // Detect illegal and "unreasonable" sizes.
    if ((need < RPC_HEADER_SIZE) || (need >= 0x10000000))
    {
      if (count == 0)
      {
        // HACK: Handle corrupt packets.
        warn("%s got corrupt packet while reading!", socket->name);
        Pollable_close(socket);
        errno = EPIPE;
        return -1;
      }

      break;
    }

    // Wait for a complete packet.
    if (avail < need)
      break;

    // Extract the "code" and "tag".
    uint16_t code = read_uint16(data + 4);
    uint16_t tag = read_uint16(data + 6);

    count++;

    RPC rpc = { socket, code, tag };

    // HACK: Reduce the size.
    input->size = head + need;

    // Skip the header.
    input->head = head + RPC_HEADER_SIZE;

    spew(3, "Handling packet (0x%04x, 0x%04x, %u).",
         code, tag, need - RPC_HEADER_SIZE);

    // Handle the packet.
    (*handler)(rpc);

    // Verify that "input->size" was not modified.
    assert(input->size == head + need);

    // HACK: Restore the size.
    input->size = head + avail;

    // Consume the packet.
    head += need;

#if 0
    if (input->head != head)
      warn("Packet was not properly consumed.");
#endif

    // Advance the head.
    input->head = head;
  }

  // Slide occasionally.
  if (input->head >= input->size / 2)
  {
    Buffer_excise(input, 0, input->head);
    input->head = 0;
  }

  if (input->head < input->size)
  {
    spew(4, "%s has %u unconsumed bytes.",
         socket->name, input->size - input->head);
  }

  return count;
}


int
handle_packets(Pollable* socket, handle_packet_func handler)
{
  int count = 0;

  while (true)
  {
    // Acquire some bytes.
    int result = Pollable_acquire(socket, 0);

    // Handle EOF.
    if (result < 0)
      return -1;

    count = handle_packets_helper(socket, handler);

    // Stop on error, if some packets were handled, or if no more
    // bytes are available.
    if (count != 0 || result != 2)
      return count;
  }
}


int
handle_packets_slowly(Pollable* socket, handle_packet_func handler)
{
  Buffer* input = &socket->input;

  uint head = input->head;
  uint avail = input->size - head;

  if (avail < RPC_HEADER_SIZE)
  {
    size_t want = RPC_HEADER_SIZE - avail;
    ssize_t result = Pollable_read(socket, want);
    if (result != want)
      return (result < 0) ? -1 : 0;
    avail = input->size - head;
  }

  uint8_t* data = input->data + head;

  size_t need = read_uint(data);

  if (avail < need)
  {
    size_t want = need - avail;
    ssize_t result = Pollable_read(socket, want);
    if (result != want)
      return (result < 0) ? -1 : 0;
  }

  return handle_packets_helper(socket, handler);
}



void
rpc_verify_consumed(Buffer* buffer, uint16_t code)
{
  if (buffer->head != buffer->size)
  {
    punt("Unconsumed RPC arguments processing code 0x%04x!", code);
  }
}



static void
rpc_error_aux(RPC rpc, bool with_errno, const char* format, va_list args)
{
  int err = errno;

  Buffer buffer;
  Buffer_init(&buffer);

  Buffer_vprintf(&buffer, format, args);

  if (with_errno)
  {
    Buffer_printf(&buffer, ": (%d) %s.", err, strerror(err));
  }

  if (rpc.tag == 0)
  {
    warn("rpc_error: %s", buffer.data);
  }
  else
  {
    spew(1, "rpc_error: %s", buffer.data);

    Pollable* socket = rpc.socket;
    Buffer* output = &socket->output;

    // See "marshal_ztstr()".
    uint packet = packet_start(socket, RPC_ERROR(rpc.code), rpc.tag);
    Buffer_write(output, buffer.data, buffer.size + 1);
    packet_finish(socket, packet);
  }

  Buffer_destroy(&buffer);
}


void
rpc_error(RPC rpc, const char* format, ...)
{
  va_list args;
  va_start(args, format);
  rpc_error_aux(rpc, false, format, args);
  va_end(args);
}


void
rpc_error_with_errno(RPC rpc, const char* format, ...)
{
  va_list args;
  va_start(args, format);
  rpc_error_aux(rpc, true, format, args);
  va_end(args);
}



// The range of tags for the current process.
static uint16_t answer_handler_min_tag = 0x0001;
static uint16_t answer_handler_max_tag = 0xFFFF;

// The last tag assigned.
static uint16_t answer_handler_old_tag = 0;


// The pending AnswerHandlers.
static AnswerHandler* answer_handlers;


void
rpc_set_tag_range(uint16_t min, uint16_t max)
{
  assert(0 < min && min <= max);

  answer_handler_min_tag = min;
  answer_handler_max_tag = max;
}


void
rpc_add_answer_handler(AnswerHandler* ah)
{
  // Verify "code" is a "query".
  assert((ah->code & 0xC000) == 0x4000);

  uint16_t tag = answer_handler_old_tag;

  while (true)
  {
    // Advance.
    tag++;

    // Handle wrapping and restrictions.
    if (tag < answer_handler_min_tag || tag > answer_handler_max_tag)
      tag = answer_handler_min_tag;

    // TODO: Avoid using any tag which is already in use.

    break;
  }

  answer_handler_old_tag = tag;

  // Save the tag.
  ah->tag = tag;

  // Insert into the active list.
  // TODO: Normally, AnswerHandlers are needed in the order they were
  // added, so "appending" would optimize "rpc_get_answer_handler".
  ah->next = answer_handlers;
  answer_handlers = ah;
}


AnswerHandler*
rpc_get_answer_handler(uint16_t code, uint16_t tag)
{
  // Verify "code" is a "reply" or "error".
  assert((code & 0x8000) != 0);

  AnswerHandler** next = &answer_handlers;
  for (AnswerHandler* ah = *next; ah != NULL; ah = *next)
  {
    if (ah->tag == tag && (ah->code & 0x3FFF) == (code & 0x3FFF))
    {
      // Remove from list.
      *next = ah->next;
      ah->next = NULL;

      return ah;
    }

    // Prepare to advance.
    next = &(ah->next);
  }

  return NULL;
}
