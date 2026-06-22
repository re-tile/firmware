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

// This file supports RSP traffic.


// Includes "tools/handy/handy.h" and thus "common/include/tilera.h".
#include "common.h"

// For "elf_gregset_t".
#include <sys/procfs.h>

#include <arch/abi.h>


//! Maps rsp signal numbers to host (tile/x86) signal numbers.
//!
static const uint8_t rsp_signal_to_host_signal_table[33] = {
  0,         // 0
  SIGHUP,    // 1
  SIGINT,    // 2
  SIGQUIT,   // 3
  SIGILL,    // 4
  SIGTRAP,   // 5
  SIGABRT,   // 6
  0,         // 7 (SIGEMT)
  SIGFPE,    // 8
  SIGKILL,   // 9
  SIGBUS,    // 10
  SIGSEGV,   // 11
  SIGSYS,    // 12
  SIGPIPE,   // 13
  SIGALRM,   // 14
  SIGTERM,   // 15
  SIGURG,    // 16
  SIGSTOP,   // 17
  SIGTSTP,   // 18
  SIGCONT,   // 19
  SIGCHLD,   // 20
  SIGTTIN,   // 21
  SIGTTOU,   // 22
  SIGIO,     // 23
  SIGXCPU,   // 24
  SIGXFSZ,   // 25
  SIGVTALRM, // 26
  SIGPROF,   // 27
  SIGWINCH,  // 28
  0,         // 29 (SIGLOST)
  SIGUSR1,   // 30
  SIGUSR2,   // 31
  SIGPWR     // 32
};


//! Maps an RSP signal number to a host signal number, or zero.
//!
static int
map_rsp_signal_to_host_signal(int rsp_sig)
{
  if ((unsigned int)rsp_sig < NELEM(rsp_signal_to_host_signal_table))
    return rsp_signal_to_host_signal_table[rsp_sig];

  // FIXME: Handle "SIGRTMIN+" (see below).

  return 0;
}


//! Maps a host signal number to an RSP signal number, or zero.
//!
static int
map_host_signal_to_rsp_signal(int host_sig)
{
  for (uint i = 0; i < NELEM(rsp_signal_to_host_signal_table); i++)
    if (rsp_signal_to_host_signal_table[i] == host_sig)
      return i;

  // The RSP numbers for the "realtime" signals are:
  //
  //   32 - 32 = 76 + 1.
  //   33 - 32 = 45.
  //   [...]
  //   63 - 32 = 75.
  //   64 - 32 = 76 + 2.
  //   [...]
  //   127 - 32 = 76 + 2 + 63.
  //
  if (host_sig > SIGRTMIN && host_sig <= SIGRTMAX)
    return (host_sig - SIGRTMIN) + 45 - 1;
  if (host_sig == SIGRTMIN)
    return 77;

  return 0;
}



// The PRIs (ptrace register ids) include tile registers 0 thru 55, plus
// four fake registers 56 (PC), 57 (EX1), 58 (SYSCALL), and 59 (ORIG_R0),
// while the GRIs (gnu register ids) include tile registers 0 thru 63,
// plus a single fake register 64 (PC).
//
// ISSUE: Since GRIs 56 thru 63 are totally useless, they should not exist.

#define GRI_PC 64

#define GRI_NUM_GENERAL (GRI_PC + 1)

// Aka "PTREGS_OFFSET_PC / 4".
#define PRI_PC 56

// Aka "NELEM(elf_gregset_t)".
#define NUM_PRI 60

//! Convert from GRI ("gnu register id") to PRI ("ptrace register id").
//
static int
gri_to_pri(int gri)
{
  if ((unsigned)gri < PRI_PC)
    return gri;
  if (gri == GRI_PC)
    return PRI_PC;
  return -1;
}



//! Extract a big endian hex number from "data + *offsetp", processing
//! up to 8 or 16 chars, or until the first non-digit.
//! 
static ulong
rsp_extract_ulong(const uint8_t* data, uint* offsetp)
{
  uint offset = *offsetp;

  ulong val = 0;

  uint i;
  for (i = 0; i < sizeof(ulong) * 2; i++)
  {
    const int digit = hex_char_to_int(data[offset + i]);

    // Stop on non-hex digit.
    if (digit == -1)
      break;

    val = (val << 4) | digit;
  }

  *offsetp = offset + i;

  return val;
}



//! Extract a big endian hex number from "data + *offsetp", processing
//! up to 8 chars, or until the first non-digit.
//! 
static uint
rsp_extract_uint(const uint8_t* data, uint* offsetp)
{
  uint offset = *offsetp;

  uint val = 0;

  uint i;
  for (i = 0; i < sizeof(uint) * 2; i++)
  {
    const int digit = hex_char_to_int(data[offset + i]);

    // Stop on non-hex digit.
    if (digit == -1)
      break;

    val = (val << 4) | digit;
  }

  *offsetp = offset + i;

  return val;
}



//! Decode an 8 bit hex number from the two chars starting at
//! "data + *offsetp", advancing "*offsetp" by two, or die.
//! 
static uint8_t
rsp_decode_byte(const uint8_t* data, uint* offsetp)
{
  int offset = *offsetp;
  int b1 = hex_char_to_int(data[offset++]);
  int b2 = hex_char_to_int(data[offset++]);
  *offsetp = offset;
  assert(b1 >= 0 && b2 >= 0);
  return 16 * b1 + b2;
}


//! Decode a "size * 8" bit host-endian hex number from the "size * 2" chars
//! starting at "data + *offsetp", advancing "*offsetp" by "size * 2", or die.
//!
static void
rsp_decode_bytes(void* mem, size_t size, const uint8_t* data, uint* offsetp)
{
  uint8_t* bytes = mem;
  for (uint i = 0; i < size; i++)
  {
    bytes[i] = rsp_decode_byte(data, offsetp);
  }
}


//! Decode a 4 or 8 byte host-endian hex number from the 8 or 16 chars
//! starting at "data + *offsetp", advancing "*offsetp" by 8 or 16, or die.
//!
static uint_reg_t
rsp_decode_ureg(const uint8_t* data, uint* offsetp)
{
  uint_reg_t val;
  rsp_decode_bytes(&val, sizeof(val), data, offsetp);
  return val;
}



static void
rsp_encode_byte(Buffer* output, uint8_t byte)
{
  // Optimized: Buffer_printf(output, "%02x", byte);
  Buffer_append(output, hex_chars_lower[byte / 16]);
  Buffer_append(output, hex_chars_lower[byte % 16]);
}


static void
rsp_encode_bytes(Buffer* output, const void* mem, size_t size)
{
  const uint8_t* bytes = mem;
  for (int i = 0; i < size; i++)
  {
    rsp_encode_byte(output, bytes[i]);
  }
}


static void
rsp_encode_ureg(Buffer* output, uint_reg_t val)
{
  rsp_encode_bytes(output, &val, sizeof(val));
}



void
rsp_note_bytes(const void* data, size_t size, const char* format, ...)
{
  uint8_t pre[1023];
  Buffer prefix;
  Buffer_init_hack(&prefix, pre, sizeof(pre));

  va_list args;
  va_start(args, format);
  Buffer_vprintf(&prefix, format, args);
  va_end(args);

  uint8_t buf[1023];
  Buffer buffer;
  Buffer_init_hack(&buffer, buf, sizeof(buf));

  const uint8_t* bytes = data;

  for (uint i = 0; i < size; i++)
  {
    uint8_t b = bytes[i];
    if (i + 5 < size && bytes[i + 1] == '*')
    {
      i += 2;
      int n = bytes[i] - 29;
      while (n-- != 0)
        Buffer_append(&buffer, b);
    }
    Buffer_append(&buffer, b);
  }

  note_bytes(buffer.data, buffer.size, "%s", prefix.data);

  Buffer_destroy(&buffer);

  Buffer_destroy(&prefix);
}



static void
rsp_packet_start(Process* process)
{
  Buffer* output = &process->rsp_buffer;

  Buffer_clear(output);

  Buffer_append(output, '$');
}


static void
rsp_packet_finish(Process* process)
{
  Buffer* output = &process->rsp_buffer;

  uint8_t checksum = 0;
  for (uint i = 1; i < output->size; i++)
    checksum += output->data[i];

  Buffer_append(output, '#');
  rsp_encode_byte(output, checksum);

  rsp_spew_bytes(4, output->data, output->size,
                 "%s RSP send: ", process->name);

  do_rsp_s2m(&g_monitor_socket, process->pid, output->data, output->size);

  Buffer_clear(output);
}


static void
rsp_packet_print(Process* process, const char* str)
{
  Buffer* output = &process->rsp_buffer;
  rsp_packet_start(process);
  Buffer_print(output, str);
  rsp_packet_finish(process);
}


// An empty packet normally means "unsupported request".
//
static void
rsp_unsupported(Process* process)
{
  rsp_packet_start(process);
  rsp_packet_finish(process);
}


static void
rsp_handle_query_auxv(Thread* reportee, const uint8_t* data, size_t size)
{
  Process* process = reportee->process;

  // Start after "$qXfer:auxv:read::".
  uint offset = 18;

  uint start = rsp_extract_uint(data, &offset);
  assert(data[offset] == ',');

  offset++;

  uint len = rsp_extract_uint(data, &offset);
  assert(data[offset] == '#');

  char path[128];
  snprintf(path, sizeof(path), "/proc/%u/auxv", reportee->tid);

  char buf[len];
  int n = -1;
  int fd = open_or_die(path, O_RDONLY, 0);
  if (start == 0 || lseek(fd, (off_t)start, SEEK_SET) == (off_t)start)
  {
    n = read_some_bytes_or_die(fd, buf, len);
  }
  int err = errno;
  close_or_die(fd);

  Buffer* output = &process->rsp_buffer;

  rsp_packet_start(process);

  if (n >= 0)
  {
    // Use 'm' only if more data may be available.
    Buffer_append(output, n < len ? 'l' : 'm');

    // TODO: Use RLE, at least for zeros.
    for (uint i = 0; i < n; i++)
    {
      uint8_t b = buf[i];
      if (b == '$' || b == '#' || b == '*' || b == '}')
      {
        Buffer_append(output, '}');
        Buffer_append(output, b ^ 0x20);
      }
      else
      {
        Buffer_append(output, b);
      }
    }
  }
  else
  {
    Buffer_append(output, 'E');
    rsp_encode_byte(output, err);
  }

  rsp_packet_finish(process);
}


// Handle a "$qfThreadInfo" or "$qsThreadInfo" packet.
//
// NOTE: To handle "info threads", first, on every reported thread,
// gdb calls "rsp_handle_validate_thread(), and then, it uses this
// function to discover any new threads.
//
// NOTE: In theory no threads can "die" during this interaction.
//
static void
rsp_handle_query_threads(Thread* reportee, bool first)
{
  Process* process = reportee->process;

  Buffer* output = &process->rsp_buffer;

  if (process->detected_threads != 0)
  {
    if (first)
    {
      spew(2, "Querying available threads.");

      // Reset scanner.
      process->scan_query_threads = 0;
    }

    uint scan_query_threads = 0;

    for (uint i = 0; i < process->threads.size; i++)
    {
      Thread* other = Array_get(&process->threads, i);

      if (process->scan_query_threads == scan_query_threads)
      {
        report_thread_if_needed(other);

        rsp_packet_start(process);
        Buffer_printf(output, "m%04x", other->tid);
        rsp_packet_finish(process);

        process->scan_query_threads++;
        return;
      }

      scan_query_threads++;
    }
  }

  // There are no more threads ids.
  rsp_packet_print(process, "l");
}


// Handle a "$T" packet.
//
// See "rsp_handle_query_threads()" for more info.
//
static void
rsp_handle_validate_thread(Thread* reportee, const uint8_t* data, size_t size)
{
  Process* process = reportee->process;

  uint offset = 2;
  uint tid = rsp_extract_uint(data, &offset);

  bool okay = (find_thread_with_tid(process, tid) != NULL);

  rsp_packet_print(process, okay ? "OK" : "E01");
}


// Handle a "$H<kind><tid>" packet.
//
// The "kind" is "g" for "general" (read/write registers), or "c" for
// "continue" or "s" or "step".  We only make use of "g".
//
// The "tid" can be a legal thread id, or 0 for "any", or -1 for "all".
//
// Note that "gdbserver" treats "Hg-1" as "Hg0", and then treats any
// unknown non-negative thread id, including zero, as an error.
//
// Note that "Hs" was added to the RSP protocol later than "Hc".
//
static void
rsp_handle_set_current(Thread* reportee, const uint8_t* data, size_t size)
{
  Process* process = reportee->process;

  char kind = data[2];

  // Ignore any "future extensions".
  if (kind != 'g' && kind != 'c' && kind != 's')
  {
    rsp_unsupported(process);
    return;
  }

  uint offset = 3;
  uint tid = rsp_extract_uint(data, &offset);

  bool okay = false;

  if (process->detected_threads != 0)
  {
    Thread* other = find_thread_with_tid(process, tid);
    if (other != NULL)
    {
      okay = true;
      if (kind == 'g')
      {
        spew(3, "%s is the current '%c' thread.", other->name, kind);
        process->current_thread_g = other;
      }
    }
    else if (offset == 3)
    {
      if (kind == 'c' || kind == 's')
        okay = true;
    }
  }
  else
  {
    if (tid == reportee->tid)
    {
      okay = true;
    }
    else if (offset == 3)
    {
      if (kind == 'c' || kind == 's')
        okay = true;
    }
  }

  rsp_packet_print(process, okay ? "OK" : "E01");
}


// Handle a "$?" packet.
//
// This is also called when a debuggable process is first "reported".
//
void
rsp_handle_last_signal(Thread* reportee)
{
  Process* process = reportee->process;

  elf_gregset_t regs;
  memset(&regs, 0, sizeof(regs));

  pti_ptrace(reportee, PTRACE_GETREGS, NULL, &regs);

  Buffer* output = &process->rsp_buffer;

  rsp_packet_start(process);

  uint8_t sig = reportee->detected_signal;

  Buffer_append(output, 'T');
  rsp_encode_byte(output, map_host_signal_to_rsp_signal(sig));

  // Note that "52" is the "FP" register.
  static const uint8_t useful[] = { 52, TREG_SP, TREG_LR, GRI_PC };

  for (uint i = 0; i < NELEM(useful); i++)
  {
    uint gri = useful[i];
    uint_reg_t val = regs[gri_to_pri(gri)];
    rsp_encode_byte(output, gri);
    Buffer_append(output, ':');
    rsp_encode_ureg(output, val);
    Buffer_append(output, ';');
  }

#ifdef SUPPORT_WATCHPOINT_PACKETS

  if (reportee->watchpoint_type != NULL)
  {
    Buffer_printf(output, "%s:%08lx;",
                  reportee->watchpoint_type,
                  reportee->watchpoint_addr);
  }

#endif

  if (process->detected_threads != 0)
  {
    report_thread_if_needed(reportee);

    Buffer_printf(output, "thread:%08x;", reportee->tid);
  }

  rsp_packet_finish(process);
}


static void
rsp_handle_read_registers(Thread* reportee, const uint8_t* data, size_t size)
{
  Process* process = reportee->process;

  Thread* thread = process->current_thread_g ?: reportee;

  elf_gregset_t regs;
  memset(&regs, 0, sizeof(regs));

  pti_ptrace(thread, PTRACE_GETREGS, NULL, &regs);

  Buffer* output = &process->rsp_buffer;

  rsp_packet_start(process);

  for (uint gri = 0; gri < GRI_NUM_GENERAL; gri++)
  {
    uint_reg_t val = 0;

    int pri = gri_to_pri(gri);
    if (pri >= 0)
      val = regs[pri];

    rsp_encode_ureg(output, val);
  }

  rsp_packet_finish(process);
}


static void
rsp_handle_write_registers(Thread* reportee, const uint8_t* data, size_t size)
{
  Process* process = reportee->process;

  Thread* thread = process->current_thread_g ?: reportee;

  elf_gregset_t regs;
  memset(&regs, 0, sizeof(regs));

  pti_ptrace(thread, PTRACE_GETREGS, NULL, &regs);

  uint offset = 2;

  for (uint gri = 0; gri < GRI_NUM_GENERAL; gri++)
  {
    assert(data[offset] != '#');

    uint_reg_t val = rsp_decode_ureg(data, &offset);

    int pri = gri_to_pri(gri);
    if (pri >= 0)
      regs[pri] = val;
  }

  pti_ptrace(thread, PTRACE_SETREGS, NULL, &regs);

  assert(data[offset] == '#');

  rsp_packet_print(process, "OK");
}



static void
rsp_handle_read_memory(Thread* reportee, const uint8_t* data, size_t size)
{
  Process* process = reportee->process;

  Buffer* output = &process->rsp_buffer;

  uint offset = 2;

  ulong addr = rsp_extract_ulong(data, &offset);
  assert(data[offset] == ',');

  offset++;

  uint num = rsp_extract_uint(data, &offset);
  assert(data[offset] == '#');

  uint8_t bytes[num];
  size_t n = ptrace_read_bytes(reportee->tid, addr, bytes, num);

  // TODO: Use RLE, at least for zeros.
  rsp_packet_start(process);
  for (uint i = 0; i < n; i++)
    rsp_encode_byte(output, bytes[i]);
  rsp_packet_finish(process);
}


static void
rsp_handle_write_memory(Thread* reportee, const uint8_t* data, size_t size)
{
  Process* process = reportee->process;

  uint offset = 2;

  ulong addr = rsp_extract_ulong(data, &offset);
  assert(data[offset] == ',');

  offset++;

  uint num = rsp_extract_uint(data, &offset);
  assert(data[offset] == ':');

  offset++;

  assert(offset + num * 2 + 3 == size);
  assert(data[offset + num * 2] == '#');

  uint8_t bytes[num];
  rsp_decode_bytes(bytes, num, data, &offset);

  assert(offset + 3 == size);
  assert(data[offset] == '#');

  // NOTE: There is no way to indicate "partial" writes.
  size_t n = ptrace_write_bytes(reportee->tid, addr, bytes, num);

  rsp_packet_print(process, (n == num) ? "OK" : "E01");
}


#ifdef SUPPORT_WRITE_BINARY_PACKETS

static void
rsp_handle_write_binary(Thread* reportee, const uint8_t* data, size_t size)
{
  Process* process = reportee->process;

  uint offset = 2;

  ulong addr = rsp_extract_ulong(data, &offset);
  assert(data[offset] == ',');

  offset++;

  uint num = rsp_extract_uint(data, &offset);
  assert(data[offset] == ':');

  offset++;

  assert(offset + num + 3 <= size);

  uint8_t bytes[num];

  for (uint i = 0; i < num; i++)
  {
    uint8_t byte = data[offset++];
    if (byte == '}')
      byte = data[offset++] ^ 0x20;
    bytes[i] = byte;
    assert(offset < size);
  }

  assert(offset + 3 == size);
  assert(data[offset] == '#');

  // NOTE: There is no way to indicate "partial" writes.
  size_t n = ptrace_write_bytes(reportee->tid, addr, bytes, num);

  rsp_packet_print(process, (n == num) ? "OK" : "E01");
}

#endif


#ifdef SUPPORT_WATCHPOINT_PACKETS

static void
rsp_handle_watchpoint(Thread* reportee, const uint8_t* data, size_t size)
{
  Process* process = reportee->process;

  // Breakpoints.
  if (data[2] == '0')
  {
    uint offset = 4;

    ulong addr = rsp_extract_ulong(data, &offset);
    assert(data[offset] == ',');

    offset++;

    ulong length = rsp_extract_ulong(data, &offset);
    assert(data[offset] == '#');

    pid_t tid = reportee->tid;

    spew(3, "%s wants to %s breakpoint at 0x%lx.",
         reportee->name, (data[1] == 'Z') ? "add" : "remove", addr);

#ifdef __tilegx__
    tile_bundle_bits bpt_bits = TILEGX_BPT_BUNDLE;
#else
    tile_bundle_bits bpt_bits = TILEPRO_BPT_BUNDLE;
#endif

    if (length != sizeof(bpt_bits))
      punt("Unexpected breakpoint size!");

    Array* breakpoints = &process->breakpoints;

    if (data[1] == 'Z')
    {
      // Add a breakpoint.

      int i;
      for (i = 0; i < breakpoints->size; i++)
      {
        Breakpoint* breakpoint = Array_get(breakpoints, i);
        if (breakpoint->addr == addr || breakpoint->addr == -1UL)
          break;
      }

      if (i == breakpoints->size)
        Array_append(breakpoints, malloc_or_die(sizeof(Breakpoint)));

      Breakpoint* breakpoint = Array_get(breakpoints, i);

      breakpoint->addr = -1UL;

      if (ptrace_read_bytes(tid, addr, &breakpoint->insn, length) != length ||
          ptrace_write_bytes(tid, addr, &bpt_bits, length) != length)
      {
        warn("%s cannot install breakpoint at 0x%lx.", process->name, addr);
        rsp_packet_print(process, "E01");
        return;
      }

      breakpoint->addr = addr;
    }
    else
    {
      // Remove a breakpoint.

      int i;
      for (i = 0; i < breakpoints->size; i++)
      {
        Breakpoint* breakpoint = Array_get(breakpoints, i);
        if (breakpoint->addr == addr)
          break;
      }

      if (i == breakpoints->size)
      {
      cannot_remove:
        warn("%s cannot remove breakpoint at 0x%lx.", process->name, addr);
        rsp_packet_print(process, "E01");
        return;
      }

      Breakpoint* breakpoint = Array_get(breakpoints, i);
      if (ptrace_write_bytes(tid, addr, &breakpoint->insn, length) != length)
        goto cannot_remove;

      breakpoint->addr = -1UL;
    }

    rsp_packet_print(process, "OK");

    return;
  }

  const char* type = NULL;
  uint mask = 0;

  switch (data[2])
  {
  case '2':
    type = "watch";
    mask = SIM_WATCHPOINT_WRITE;
    break;
  case '3':
    type = "rwatch";
    mask = SIM_WATCHPOINT_READ;
    break;
  case '4':
    type = "awatch";
    mask = SIM_WATCHPOINT_READ | SIM_WATCHPOINT_WRITE;
    break;
  }

  if (sim_is_simulator() && type != NULL && data[3] == ',')
  {
    uint offset = 4;

    ulong addr = rsp_extract_ulong(data, &offset);
    assert(data[offset] == ',');

    offset++;

    ulong length = rsp_extract_ulong(data, &offset);
    assert(data[offset] == '#');

    pid_t tid = reportee->tid;
    int user_data = (intptr_t)(void*)type;
    int res = 0;
    if (data[1] == 'Z')
      res = sim_add_watchpoint(tid, addr, length, mask, user_data);
    else
      res = sim_remove_watchpoint(tid, addr, length, mask, user_data);

    rsp_packet_print(process, (res == 0) ? "OK" : "E01");
  }
  else
  {
    rsp_unsupported(process);
  }
}

#endif


// Called by "rsp_handle_kill_or_detach()", in which case one thread
// will be "REPORTED", and the rest will all be "STOPPED", "FORCED",
// or "PAUSED".
//
// HACK: This is also used to handle the debugger crashing, in which
// case "kill" will be true, and threads may be in surprising states.
//
static void
rsp_handle_kill_or_detach_aux(Process* process, bool kill)
{
  // Detach all the threads.
  process->reported_threads = 0;
  for (uint i = 0; i < process->threads.size; i++)
  {
    Thread* thread = Array_get(&process->threads, i);

    if (thread->reported_index != 0)
    {
      thread->reported_index = 0;
      rename_thread(thread);
    }

    // Paranoia.
    thread->resume_step = false;
    thread->resume_signal = 0;

    // Detach the debugger.
    switch (thread->state)
    {
    case THREAD_STOPPED:
    case THREAD_FORCED:
    case THREAD_REPORTED:
      pti_change_state(thread, THREAD_PAUSED);
      break;
    }

    if (!kill && thread->process->ignore_on_detach)
    {
      spew(1, "%s is no longer being ptraced.", thread->name);
      pti_ptrace(thread, PTRACE_DETACH, NULL, NULL);
      handle_death(thread, -1);
    }
  }

  process->debugging = false;

  process->expecting_magic_rsp_eof = true;
}


// Handle an explicit "kill" or "detach" request from the debugger.
//
static void
rsp_handle_kill_or_detach(Thread* reportee, bool kill)
{
  Process* process = reportee->process;

  // Detach from the process.
  rsp_handle_kill_or_detach_aux(process, kill);

  if (kill)
  {
    // No reply is allowed!

    // Kill the process.
    kill_process(process, "debugger");
  }
  else
  {
    // A reply is required!
    rsp_packet_print(process, "OK");
  }
}


// NOTE: There is no immediate reply to this RSP packet.
//
static void
rsp_handle_step_or_continue(Thread* reportee, bool step, int sig)
{
  static int count = 0;

  // HACK: Reduce spew for repeated stepping.
  count = (step && sig == 0) ? count + 1 : 0;

  if (sig != 0)
    spew(1, "%s wants to %s with %s.",
         reportee->name, step ? "step" : "continue", signal_name(sig));
  else
    spew((count < 10) ? 1 : 2, "%s wants to %s.",
         reportee->name, step ? "step" : "continue");

  // Prepare to step/continue.
  reportee->resume_step = step;
  reportee->resume_signal = sig;
  pti_change_state(reportee, THREAD_PAUSED);

  // Handle deferred "kill_process()".
  if (reportee->process->killing)
  {
    kill_process(reportee->process, NULL);
    return;
  }

  // If any other process in the same app has a REPORTED thread,
  // then do NOT resume.
  for (uint k = 0; k < g_processes.size; k++)
  {
    Process* process = Array_get(&g_processes, k);
    if (process->app_id == reportee->process->app_id &&
        find_thread_with_state(process, THREAD_REPORTED) != NULL)
      return;
  }

  // Otherwise, resume this thread (and no others).
  pti_resume(reportee, true);
}


#ifdef SUPPORT_VCONT_PACKETS

static void
rsp_handle_vcont(Thread* reportee, const uint8_t* data, size_t size)
{
  Process* process = reportee->process;

  bool step = false;
  bool has_signal = false;

  switch (data[7])
  {
  case 'S':
    has_signal = true;
    // Fall through.
  case 's':
    step = true;
    break;
  case 'C':
    has_signal = true;
    // Fall through.
  case 'c':
    break;
  default:
    rsp_unsupported(process);
    return;
  }

  // Aka strlen("$vCont;_").
  uint offset = 8;

  uint sig = 0;

  // Handle "<sig>" (after "S" or "C").
  if (has_signal)
  {
    uint rsp_sig = rsp_decode_byte(data, &offset);
    sig = map_rsp_signal_to_host_signal(rsp_sig);
  }

  uint tid = 0;

  // Handle optional ":<tid>".
  if (data[offset] == ':')
  {
    offset++;
    // HACK: Treat "-1" ("all threads") like "0" ("current thread").
    if (data[offset] == '-' && data[offset + 1] == '1')
      offset += 2;
    else
      tid = rsp_extract_uint(data, &offset);
  }

  // HACK: Handle "$vCont;s:47;c#fb" by ignoring everything after the
  // second semicolon.  Technically, we can be given a list of things
  // to do to each thread, and then a default for unmentioned threads,
  // but currently, we only support doing something to a single thread,
  // and continuing all other threads.  FIXME: This seems important!
  if (data[offset] == ';')
  {
    if (offset + 2 != size - 3 || data[offset + 1] != 'c')
    {
      warn("%s ignoring extra actions in '$vCont;%c' packet.", 
           process->name, data[7]);
      rsp_note_bytes(data, size, "%s RSP oops: ", process->name);
    }

    offset = size - 3;
  }

  assert(data[offset] == '#');

  rsp_spew_bytes(2, data, size, "%s handling: ", process->name);

  // Change the "reported" thread if needed.
  if (tid != 0 && tid != reportee->tid)
  {
    Thread* other = find_thread_with_tid(process, tid);
    if (other != NULL)
    {
      spew(1, "%s is now the reported thread.", other->name);
      pti_change_state(reportee, THREAD_PAUSED);
      pti_change_state(other, THREAD_REPORTED);
      reportee = other;
    }
    else
    {
      warn("%s ignoring tid %d in '$vCont' packet.", process->name, tid);
    }
  }

  rsp_handle_step_or_continue(reportee, step, sig);
}

#endif


// Handle asynchronous Ctrl+C mini-packet.
//
static void
rsp_handle_interrupt(Process* process)
{
  spew(1, "%s wants to be interrupted.", process->name);

  for (uint i = 0; i < process->threads.size; i++)
  {
    Thread* other = Array_get(&process->threads, i);

    switch (other->state)
    {
    case THREAD_FORCED:
    case THREAD_REPORTED:
      warn("%s cannot be interrupted.", other->name);
      return;
    }
  }

  for (uint i = 0; i < process->threads.size; i++)
  {
    Thread* other = Array_get(&process->threads, i);

    switch (other->state)
    {
    case THREAD_PAUSED:

      // Forget any existing step/continue request.
      other->resume_step = false;
      other->resume_signal = 0;

      // Fall through.

    case THREAD_STOPPED:

      // HACK: See "pti_handle_stopped()".
      other->detected_signal = SIGINT;

      pti_change_state(other, THREAD_FORCED);
      return;
    }
  }

  for (uint i = 0; i < process->threads.size; i++)
  {
    Thread* other = Array_get(&process->threads, i);

    switch (other->state)
    {
    case THREAD_ACTIVE:
    case THREAD_TRAPPING:
      pti_pause(other);
      // Fall through.

    case THREAD_PAUSING:
    case THREAD_STOPPING:
      pti_change_state(other, THREAD_FORCING);
      break;
    }
  }
}



// Handle an incoming RSP request, or a magic EOF indication.
//
// Standard packet order (gdbserver with threads):
//
// $qSupported#
// $Hc-1# -> $OK#
// $qC# -> $#
// $qOffsets# -> $#
// $?# -> $T...#
// $qXfer:auxv:read:
// $qSymbol: -> $qSymbol: [multiple]
// $qSymbol: -> $OK#
// [break]
// $m
// [continue]
// $vCont?# -> $vCont;c;C;s;S#
// $vCont;s# -> $T
// $p40# -> $#
// $g#
// $Z0,
// $m / $X [multiple]
// $vCont;c# -> $T...thread:1f7;#
// $m / $X / $g [multiple]
// $vCont;s:1f7# -> $T...thread:1f7;#
// $m / $X [multiple]
// $vCont;c# -> $T...thread:1f7;#
// ...
// [info threads]
// $T000001f7# -> $OK#
// $qfThreadInfo# -> $m1f7#
// $qsThreadInfo# -> $m1fd# ... $m202# $l#
// $qThreadExtraInfo,202# -> $#
// $qP0000001f0000000000000202# -> $#
// $Hg202# -> $OK#
// $g
// [repeat last three lines for all threads]
// [thread 3]
// $T000001ff# -> $OK#
// $qP0000001f00000000000001ff# -> $#
// $Hg1ff# -> $OK#
// $g
// [continue]
// $vCont;c# [yields $T...thread:1fe]
//
void
rsp_handle_packet(Process* process, const uint8_t* data, size_t size)
{
  Thread* reportee = find_thread_with_state(process, THREAD_REPORTED);


  const char* name = process->name;


  if (size == 0)
  {
    // Handle "magic EOF".
    spew(2, "%s got magic EOF from gdb.", process->name);

    // Handle unexpected magic EOF.
    if (!process->expecting_magic_rsp_eof)
    {
      spew(1, "%s lost gdb connection!", name);

      rsp_handle_kill_or_detach_aux(process, true);

      // Kill the process.
      kill_process(process, "gdb crash");
    }

    process->reported_attach = false;

    process->expecting_magic_rsp_eof = false;

    // HACK: See "pti_change_state_reported()".
    pti_check_states_later(process);

    return;
  }


  if (size == 1)
  {
    // Handle "interrupt" packets.
    // NOTE: The monitor handles ACK/NAK packets.
    rsp_handle_interrupt(process);
    return;
  }


  // ISSUE: When a REPORTED thread dies, due to a naughty SIGKILL, we will
  // send "note_detach", and set "debugging" false, but gdb will still think
  // that it can talk to us.  Even if we avoid reaping the dead thread, and
  // using it as "reportee", most RSP queries would fail anyway.

  // NOTE: When gdb sends a "detach" packet, we set "debugging" false, and
  // then send back an "OK" packet, and then gdb sends us one final ACK and
  // a magic EOF, so this check must follow the mini-packet handling code.
  // ISSUE: Should we thus just check "expecting_magic_rsp_eof" instead?
  if (!process->debugging || !process->reported_attach)
  {
    warn("%s got RSP packet while not debugging.", name);
    rsp_note_bytes(data, size, "%s RSP read: ", name);
    return;
  }


  // NOTE: This check must follow the "magic EOF" and "Ctrl+C" handling code.
  if (reportee == NULL)
  {
    warn("%s has no reported thread!", process->name);
    return;
  }


  rsp_spew_bytes(4, data, size, "%s RSP read: ", name);

  char* sdata = (char*)data;

  if (has_prefix(sdata, "$q"))
  {
    if (has_prefix(sdata, "$qSupported#") ||
        has_prefix(sdata, "$qSupported:"))
    {
      // Query supported features.
      // We support "large" packets, and one special feature.
      rsp_packet_print(process, "PacketSize=7cf;qXfer:auxv:read+");
    }

    else if (has_prefix(sdata, "$qC#"))
    {
      // Query current thread id.
      // ISSUE: It seems that "gdb" only uses this once,
      // before we have anything useful to tell it.
#if 0
      if (process->current_thread_g != NULL)
      {
        Buffer* output = &process->rsp_buffer;
        uint tid = process->current_thread_g->tid;
        rsp_packet_start(process);
        Buffer_printf(output, "qC%08x", tid);
        rsp_packet_finish(process);
      }
      else
      {
        rsp_unsupported(process);
      }
#endif

      rsp_unsupported(process);
    }

    else if (has_prefix(sdata, "$qOffsets#"))
    {
      // Query section offsets.
      rsp_unsupported(process);
    }

    else if (has_prefix(sdata, "$qXfer:auxv:read::"))
    {
      // Query "auxiliary vector".
      rsp_handle_query_auxv(reportee, data, size);
    }

    else if (has_prefix(sdata, "$qSymbol:"))
    {
      // Offer to supply symbols.
      // Or, a reply to the response to a previous offer.

      // No more symbols desired.
      rsp_packet_print(process, "OK");
    }

    else if (has_prefix(sdata, "$qfThreadInfo#"))
    {
      // Query initial thread id.
      rsp_handle_query_threads(reportee, true);
    }

    else if (has_prefix(sdata, "$qsThreadInfo#"))
    {
      // Query another thread id.
      rsp_handle_query_threads(reportee, false);
    }

    else if (has_prefix(sdata, "$qThreadExtraInfo,"))
    {
      // Query extra thread info.
      rsp_unsupported(process);
    }

    else if (has_prefix(sdata, "$qP"))
    {
      // Query thread info.
      rsp_unsupported(process);
    }

    else if (has_prefix(sdata, "$qAttached"))
    {
      // Query Attached state.
      rsp_unsupported(process);
    }

    else if (has_prefix(sdata, "$qTStatus"))
    {
      // FIXME: What is this query?
      // Maybe something about tracepoints?
      rsp_unsupported(process);
    }

    else
    {
      warn("%s got an unknown RSP packet.", name);
      rsp_note_bytes(data, size, "%s RSP oops: ", name);
      rsp_unsupported(process);
    }
  }

  else if (has_prefix(sdata, "$T"))
  {
    // Validate thread existence.
    rsp_handle_validate_thread(reportee, data, size);
  }

  else if (has_prefix(sdata, "$H"))
  {
    // Set current thread.
    rsp_handle_set_current(reportee, data, size);
  }

  else if (has_prefix(sdata, "$?#"))
  {
    // Query last signal.
    rsp_handle_last_signal(reportee);
  }

  else if (has_prefix(sdata, "$g"))
  {
    // Read registers.
    rsp_handle_read_registers(reportee, data, size);
  }

  else if (has_prefix(sdata, "$G"))
  {
    // Write registers.
    rsp_handle_write_registers(reportee, data, size);
  }

  else if (has_prefix(sdata, "$m"))
  {
    // Read memory.
    rsp_handle_read_memory(reportee, data, size);
  }

  else if (has_prefix(sdata, "$M"))
  {
    // Write memory.
    rsp_handle_write_memory(reportee, data, size);
  }

  else if (has_prefix(sdata, "$X"))
  {
    // Write binary data.
#ifdef SUPPORT_WRITE_BINARY_PACKETS
    rsp_handle_write_binary(reportee, data, size);
#else
    // NOTE: Falls back to "$M" packets.
    rsp_unsupported(process);
#endif
  }

  else if (has_prefix(sdata, "$Z") ||
           has_prefix(sdata, "$z"))
  {
    // Insert/remove breakpoints/watchpoints.
#ifdef SUPPORT_WATCHPOINT_PACKETS
    rsp_handle_watchpoint(reportee, data, size);
#else
    // NOTE: Falls back to memory manipulation for breakpoints.
    rsp_unsupported(process);
#endif
  }

  else if (has_prefix(sdata, "$D#"))
  {
    // Detach.
    spew(1, "%s wants to be detached.", name);
    rsp_handle_kill_or_detach(reportee, false);
  }

  else if (has_prefix(sdata, "$k#"))
  {
    // Kill.
    spew(1, "%s wants to be killed.", name);
    rsp_handle_kill_or_detach(reportee, true);
  }

  // ISSUE: Maybe we should NOT use "$vCont" packets, which might force
  // the use of "$Hc" and "$Hs" packets, which might be easier to handle.
  // ISSUE: In any case, using "PTRACE_SETSIGINFO" may be necessary.

#ifdef SUPPORT_VCONT_PACKETS

  else if (has_prefix(sdata, "$vCont?#"))
  {
    // We support the normal "$vCont" packets.
    rsp_packet_print(process, "vCont;c;C;s;S");
  }

  else if (has_prefix(sdata, "$vCont;"))
  {
    // Continue or step, with optional signal.
    rsp_handle_vcont(reportee, data, size);
  }

#else

  else if (has_prefix(sdata, "$vCont?#"))
  {
    rsp_unsupported(process);
  }

  // NOTE: For the following four packets, just like "gdbserver", we
  // ignore any optional ";ADDR" suffix, and any unknown signals, and
  // any unexpected trailing characters.

  else if (has_prefix(sdata, "$c#"))
  {
    // Continue.
    rsp_handle_step_or_continue(reportee, false, 0);
  }

  else if (has_prefix(sdata, "$C"))
  {
    // Continue with signal.
    uint offset = 2;
    uint8_t rsp_sig = rsp_decode_byte(data, &offset);
    uint sig = map_rsp_signal_to_host_signal(rsp_sig);
    rsp_handle_step_or_continue(reportee, false, sig);
  }

  else if (has_prefix(sdata, "$s#"))
  {
    // Step.
    rsp_handle_step_or_continue(reportee, true, 0);
  }

  else if (has_prefix(sdata, "$S"))
  {
    // Step with signal.
    uint offset = 2;
    uint8_t rsp_sig = rsp_decode_byte(data, &offset);
    uint sig = map_rsp_signal_to_host_signal(rsp_sig);
    rsp_handle_step_or_continue(reportee, true, sig);
  }

#endif

  else if (has_prefix(sdata, "$p"))
  {
    // Read register.
    // NOTE: Falls back to "g" packets.
    rsp_unsupported(process);
  }

  else if (has_prefix(sdata, "$P"))
  {
    // Write register.
    // NOTE: Falls back to "G" packets.
    rsp_unsupported(process);
  }

  else
  {
    warn("%s got an unknown RSP packet.", name);
    rsp_note_bytes(data, size, "%s RSP oops: ", name);
    rsp_unsupported(process);
  }
}


void
rsp_handle_death(Process* process)
{ 
  // ISSUE: Is this always appropriate?
  process->debugging = false;

  if (!process->reported_attach)
    return;

  if (process->expecting_magic_rsp_eof)
    return;

  process->expecting_magic_rsp_eof = true;

  int status = process->status;

  char type;
  uint8_t val;

  if (WIFEXITED(status))
  {
    int code = WEXITSTATUS(status);
    type = 'W';
    val = code;
  }

  else //--if (WIFSIGNALED(status))
  {
    // NOTE: In theory, sig can only be "SIGKILL".
    int sig = WTERMSIG(status);
    type = 'X';
    val = map_host_signal_to_rsp_signal(sig);
  }

  Buffer* output = &process->rsp_buffer;

  rsp_packet_start(process);
  Buffer_append(output, type);
  rsp_encode_byte(output, val);
  rsp_packet_finish(process);
}
