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
 * Virtual client console support.
 */

#include <stdio.h>
#include <string.h>

#include "config.h"
#include "console.h"
#include "debug.h"
#include "mapping.h"


/** Size of each client's output buffer, in bytes.  This seems to be about
 *  right for holding the boot spew, plus a little more.
 */
#define OUTPUT_BUF_SIZE 4096

/** Data structure to hold the console output buffer for a client, and
 *  pointers to the start and end of data to be output.
 */
struct client_output_buf
{
  /** The actual buffer. */
  char buf[OUTPUT_BUF_SIZE];
  /** Points to the first character to be output. */
  char* head;
  /** Points to the character after the last character to be output.  When
   *  head == tail, the buffer is empty.  Note that tail may be < head if
   *  the set of valid characters wraps around the end of the buffer. */
  char* tail;
};

/** This points to an array of output buffer structures, one for each
 *  client. */
static struct client_output_buf* output_buf;

/** The index of the client that is actively using the console. */
static int active_client;

//
// The next four variables make our output somewhat cleaner by preventing
// us from changing the active client in the middle of a console write.
// This is possible because we can change the active client based on
// serial input, and we check for said input while we're doing serial
// output.  Without this scheme, which is implemented via cooperation
// between cons_set_active_client() and cons_flush_output_buffer(), we
// can end up with output that looks like this:
//
// 1. Start of output from client A
// 2. Command to switch console to client B
// 3. Queued output from client B
// 4. End of output from client A
//
// which is confusing.  Insted, we force 1 and 4 to happen before 2 and 3.
//

/** If nonzero, cons_set_active_client shouldn't actually change the active
 *  client, but should arrange for it to happen later. */
static int wait_set_active_client;

/** If nonzero, cons_set_active_client has requested that the active client
 *  be changed. */
static int did_set_active_client;

/** If did_set_active_client is nonzero, this variable holds what the
 *  active client should be set to. */
static int new_active_client;

/** If did_set_active_client is nonzero, this variable holds the value of
 *  silent which should be passed to the eventual real cons_set_active_client
 *  call. */
static int new_active_client_silent;

/** Real client output device. */
FILE *client_outdev;
/** Real client input device. */
FILE *client_indev;

// remote_client_out.
/** Send the contents of a client's output buffer to the UART, and reset
 *  the buffer's pointers to indicate that it is empty.
 * @param client Client which owns this buffer.
 * @returns Number of characters written to UART.
 */
static void
cons_flush_output_buffer(int client)
{
  int len, len_written;

  //
  // If we don't have multiple regular clients, we aren't using the client
  // output buffers, and we thus didn't allocate them; just do nothing.
  //
  if (!output_buf)
    return;

  assert(client < config.nclients);

  struct client_output_buf* obuf = &output_buf[client];

  wait_set_active_client = 1;

  //
  // Send the first part of the data to the UART: from the head
  // pointer to the tail pointer, or to the end of the buffer.
  //
  char* end_of_buf = obuf->buf + OUTPUT_BUF_SIZE;
  len = obuf->head <= obuf->tail ? obuf->tail - obuf->head :
                                   end_of_buf - obuf->head;

  len_written = client_outdev->ops->write(obuf->head, len, 0,
                                          client_outdev->pvt);
  assert (len_written == len);

  obuf->head += len;
  if (obuf->head >= end_of_buf)
    obuf->head = obuf->buf;

  // 
  // Send the second part of the data, if necessary: from the beginning
  // of the buffer to the tail pointer.
  //
  len = obuf->tail - obuf->head;
  if (len > 0)
  {
    len_written = client_outdev->ops->write(obuf->head, len, 0,
                                            client_outdev->pvt);
    assert (len_written == len);
  }

  //
  // Buffer is now empty.
  //
  obuf->head = obuf->buf;
  obuf->tail = obuf->buf;

  wait_set_active_client = 0;
  if (did_set_active_client)
  {
    did_set_active_client = 0;
    cons_set_active_client(new_active_client, new_active_client_silent);
  }
}

/** Allocate and configure buffers for per-client console output.  Also
 *  initialize the active client. */
void
cons_alloc_output_buffers()
{
  //
  // Note: to simplify things, we allocate a buffer for every client, of
  // any type, even though BME clients don't use the console buffering
  // subsystem.
  //
  output_buf = local_alloc(config.nclients * sizeof (*output_buf), 0);
  assert(output_buf);

  for (int i = config.nclients - 1; i >= 0; i--)
  {
    output_buf[i].head = output_buf[i].buf;
    output_buf[i].tail = output_buf[i].buf;

    //
    // We pick the first non-BME client to be the initially active client.
    //
    if (!(config.clients[i].flags & CLIENT_BME))
      active_client = i;
  }
}


/** Write to the client's output buffer.  If the number of characters to be
 *  written exceeds the space remaining in the output buffer, the buffer
 *  will wrap and the oldest data will be overwritten.  If we're writing to
 *  the buffer for the active client, we flush it to the real console
 *  device after we've appended the new data to it.
 * @param client Client which owns this output buffer.
 * @param buf String to write.
 * @param len Number of characters to write.
 * @return Number of characters written.
 */
int
cons_write_to_output_buffer(int client, const char* buf, int len)
{
  assert(client < config.nclients);
  assert(len < OUTPUT_BUF_SIZE);

  struct client_output_buf* obuf = &output_buf[client];

  //
  // Figure out how many bytes are currently in the buffer; we use this
  // below as part of the head pointer overwrite check.
  //
  int initial_bytes =
    (obuf->head <= obuf->tail) ? obuf->tail - obuf->head
                               : OUTPUT_BUF_SIZE - (obuf->head - obuf->tail);

  //
  // Put new data into buffer, starting at the tail pointer and ending
  // at the end of the buffer.
  //
  char* end_of_buf = obuf->buf + OUTPUT_BUF_SIZE;
  int len_to_end_of_buf = end_of_buf - obuf->tail;
  int copy_len = (len < len_to_end_of_buf) ? len : len_to_end_of_buf;
  memcpy(obuf->tail, buf, copy_len);
  buf += copy_len;
  obuf->tail += copy_len;
  copy_len = len - copy_len;

  // 
  // If we still have some new data left, then we wrapped around the end of
  // the buffer; write the remaining data at the start of the buffer.
  //
  if (copy_len > 0)
  {
    memcpy(obuf->buf, buf, copy_len);
    obuf->tail = obuf->buf + copy_len;
  }

  //
  // We might have written too much data to fit in the empty space in the
  // buffer.  If that happened, we need to move the head pointer.
  //
  if (initial_bytes + len >= OUTPUT_BUF_SIZE)
    obuf->head = obuf->tail + 1;

  //
  // Ensure that our pointers are inside our buffer array.
  //
  if (obuf->head >= end_of_buf)
    obuf->head = obuf->buf;
  if (obuf->tail >= end_of_buf)
    obuf->tail = obuf->buf;

  if (cons_client_is_active(client))
    cons_flush_output_buffer(client);

  return len;
}

/** Read from the client's input buffer.  At the moment, we don't actually
 *  maintain an input buffer per client; instead, we just check to see if
 *  this client is attached to the console; if so, we read from the real
 *  console, and if not, we act as if no data is available.
 * @param client Client which owns this input buffer.
 * @param buf String to write.
 * @param len Number of characters to write.
 * @return Number of characters written.
 */
int
cons_read_from_input_buffer(int client, char* buf, int len)
{
  if (cons_client_is_active(client))
    return client_indev->ops->read(buf, len, 0, client_indev->pvt);
  else
    return 0;
}


/** Change our idea of the active client (i.e., the one to whom the
 *  physical console is connected).  Note that this routine must run on
 *  the console tile to have any effect.
 * @param new_client New client number, or CONS_{NEXT,PREV}_CLIENT to
 *  move to the next or previous client.
 * @param silent If nonzero, we won't print any confirmation or error
 *  messages to the console.
 * @return Zero if the change was successful, nonzero otherwise.
 */
int
cons_set_active_client(int new_client, int silent)
{
  if (new_client == CONS_NEXT_CLIENT)
  {
    //
    // Pick the next client, wrapping at the end of the list, skipping any
    // BME clients, and stopping if we end up back where we started.
    //
    for (new_client = active_client + 1; new_client != active_client;
         new_client++)
    {
      if (new_client >= config.nclients)
        new_client = 0;
      if (!(config.clients[new_client].flags & CLIENT_BME))
        break;
    }
  }
  else if (new_client == CONS_PREV_CLIENT)
  {
    //
    // Pick the previous client, wrapping at the start of the list, skipping
    // any BME clients, and stopping if we end up back where we started.
    //
    for (new_client = active_client - 1; new_client != active_client;
         new_client--)
    {
      if (new_client < 0)
        new_client = config.nclients - 1;
      if (!(config.clients[new_client].flags & CLIENT_BME))
        break;
    }
  }
  else if (new_client < 0 || new_client >= config.nclients)
  {
    if (!silent)
      printf("hv: client #%d does not exist\n", new_client);

    return (1);
  }
  else if (config.clients[new_client].flags & CLIENT_BME)
  {
    if (!silent)
      printf("hv: cannot connect to BME client #%d\n", new_client);

    return (1);
  }

  if (wait_set_active_client)
  {
    did_set_active_client = 1;
    new_active_client = new_client;
    new_active_client_silent = silent;
    return (0);
  }

  active_client = new_client;

  if (!silent)
  {
    printf("hv: console now connected to client #%d\n", active_client);
    cons_flush_output_buffer(active_client);
  }

  return (0);
}


/** Test whether a specified client is active.
 * @param client Number of client to test.
 * @return Nonzero if the specified client is the active client, zero
 *   otherwise.
 */
int
cons_client_is_active(int client)
{
  return (client == active_client);
}


/** Write to the client console.
 * @param s String to write.
 * @param len Number of characters to write.
 * @param private Private data pointer, unused for this file.
 * @return Number of characters written.
 */
static int
client_cons_write(char* s, int len, unsigned int offset, void* private)
{
  return cons_write_to_output_buffer(my_client, s, len);
}


/** Read from the client console.
 * @param s Destination string.
 * @param len Number of characters to read.
 * @param private Private data pointer, unused for this file.
 * @return Number of characters read.
 */
static int
client_cons_read(char* s, int len, unsigned int offset, void* private)
{
  return cons_read_from_input_buffer(my_client, s, len);
}

/** Client console file operations vector. */
static struct _file_ops client_fops =
{
  .write = client_cons_write,
  .read = client_cons_read
};

/** Buffer for client console output file. */
static char client_outbuf[256];

/** Remote console output file. */
FILE client_out =
{
  .buf = client_outbuf,
  .len = sizeof (client_outbuf),
  .ptr = client_outbuf,
  .wrem = sizeof (client_outbuf),
  .rrem = 0,
  .pos = 0,
  .flg = _FLG_W,
  .ops = &client_fops
};

/** Buffer for client console input file.  This is intentionally limited
 *  to 1 character so that we provide a consistent, serialized, view
 *  of console input to the supervisor; see comments in cons_remote.c
 *  for more detail.
 */
static char client_inbuf[1];

/** Remote console input file. */
FILE client_in =
{
  .buf = client_inbuf,
  .len = sizeof (client_inbuf),
  .ptr = client_inbuf,
  .wrem = 0,
  .rrem = 0,
  .pos = 0,
  .flg = _FLG_R,
  .ops = &client_fops
};
