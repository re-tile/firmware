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

// FIXME: Rename this file to "watchdog.c".

// Includes "tools/handy/handy.h" and thus "common/include/tilera.h".
#include "common.h"

#include <limits.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mount.h>

#include <tmc/task.h>


// NOTE: We assume that TAG_DAEMON < TAG_EXECUTOR.

//! The base tag used by daemon queries.
#define TAG_DAEMON 0x8000

//! The base tag used by executor queries.
#define TAG_EXECUTOR 0xF000

//! The last tag used by executor queries.
#define TAG_EXECUTOR_LAST 0xFFFF



typedef struct _Daemon Daemon;

struct _Daemon
{
  //! The socket to the daemon.
  //! The id is contained in "socket->info".
  Pollable socket;

  //! The pid of the daemon.
  pid_t pid;

  //! The "tile_dir" from the "mount_fuse" query.
  char* tile_dir;

  //! Support deferred RPC reply/error for a "mount_fuse" query.
  //! The "socket" field is set to NULL once this has been done.
  RPC deferred;

  //! True while daemon query is outstanding.
  bool waiting;

  //! True if daemon has been unmounted.
  bool unmounted;
};



//! The stdout pipe given to child processes.
static int g_child_stdout_pipe;

//! The stderr pipe given to child processes.
static int g_child_stderr_pipe;

//! The stdout from child processes.
static Pollable g_child_stdout;

//! The stderr from child processes.
static Pollable g_child_stderr;


//! The Daemons.
static Array g_daemons;


//! The socket to the "shepherd" process.
static Pollable g_shepherd_socket;

//! The pid of the "shepherd" process.
static pid_t g_shepherd_pid;


//! The port for "g_monitor_listener".
static uint16_t g_monitor_listener_port;

//! The global listener for "--listen".
static Pollable g_monitor_listener;

//! The global device for "--pci[-stream]".
static Pollable g_monitor_device_pci;

//! The global device for "--tmfifo[-stream]".
static Pollable g_monitor_device_tmfifo;

//! Whichever of "&g_monitor_socket" or "&g_monitor_device_pci" or
//! "&g_monitor_device_tmfifo" is currently "active", or NULL if there
//! is currently no monitor available.
//! The input from the monitor.
static Pollable* g_monitor_in_pollable;

//! The output to the monitor.
static Pollable* g_monitor_out_pollable;

//! True if the actual "shepherd" process was terminated, for --ssh case.
static bool g_shepherd_exit = false;


static void
forward_packet_to_monitor(RPC rpc)
{
  Pollable* pollable = g_monitor_out_pollable;

  Pollable* socket = rpc.socket;
  Buffer* input = &socket->input;
  uint packet = input->head - RPC_HEADER_SIZE;
  Pollable_write(pollable, input->data + packet, input->size - packet);

  if (sim_is_simulator())
    Pollable_flush_fully(pollable);
}


static void
forward_console_traffic(int cfd, void* data, uint size)
{
  Pollable* pollable = g_monitor_out_pollable;

  if (pollable != NULL)
  {
    if (cfd == STDOUT_FILENO)
      do_console_stdout(pollable, 0, data, size);
    else
      do_console_stderr(pollable, 0, data, size);

    if (sim_is_simulator())
      Pollable_flush_fully(pollable);
  }
  else
  {
    // Just pass through to the console.
    write_all_bytes_or_die(cfd, data, size);
  }
}


#if 0
// A hook for "message_output" which forwards to the monitor.
//
// WARNING: If "message_verbosity >= 2", forwarding to the monitor
// could cause recursive spew about the act of spewing, and so in
// that case, we just write directly to stderr.
//
static void
message_output_forward(char* str)
{
  // HACK: Replace the final '\0' with '\n'.
  uint8_t* data = (uint8_t*)str;
  uint size = strlen(str);
  data[size++] = '\n';

  int cfd = STDERR_FILENO;
  if (message_verbosity < 2)
    forward_console_traffic(cfd, data, size);
  else
    write_all_bytes_or_die(cfd, data, size);
}
#endif


static void
handle_child_console(Pollable* pipe)
{
  Buffer* input = &pipe->input;

  // Consume a lot of data, but not an infinite amount.
  for (int i = 0; i < 1024; i++)
  {
    // Acquire some bytes, leaving room for packet header plus args
    // (2 bytes for pid, 2 bytes for cfd, 4 bytes for array size).
    // ISSUE: Should we pre-grow the input buffer to 32768 bytes?
    int result = Pollable_acquire(pipe, RPC_HEADER_SIZE + 2 + 2 + 4);

    // Stop when done.
    if (result <= 0)
      break;

    uint8_t* data = input->data;
    uint size = input->size;

    bool stdout = (pipe == &g_child_stdout);
    int cfd = stdout ? STDOUT_FILENO : STDERR_FILENO;
    forward_console_traffic(cfd, data, size);

    // Consume.
    Buffer_excise(input, 0, size);

    // Stop after short read.
    if (result == 1)
      break;
  }
}



// Send deferred reply/error for a "mount_fuse" query forwarded to the
// watchdog by the shepherd, if needed.  Used only on the simulator.
//
static void
finish_daemon(Daemon* daemon, bool okay)
{
  if (daemon->deferred.socket != NULL)
  {
    RPC rpc = daemon->deferred;
    if ((rpc.socket == g_monitor_in_pollable))
      rpc.socket = g_monitor_out_pollable;

    if (okay)
      reply_mount_fuse(rpc);
    else
      rpc_error(rpc, "Could not create export mount point.");

    daemon->deferred.socket = NULL;
  }
}


// Kill the shepherd, close its socket, kill fuse daemons by
// unmounting their directories, and restore "/sbin" if needed.
//
// Called whenever the shepherd is reaped, the monitor connection is
// closed, a "request_close" query is handled, or a "request_protocol"
// query is received.
//
static void
forget_shepherd(void)
{
  if (g_shepherd_pid != 0)
  {
    // The shepherd handles SIGTERM by emitting a warning, and then
    // killing its children and exiting cleanly.
    spew(1, "Killing existing shepherd process!");
    (void)kill(g_shepherd_pid, SIGTERM);
    // NOTE: This will cause "check_for_dead_children()" to mention
    // "Somebody" when the shepherd is actually reaped.
    g_shepherd_pid = 0;
  }

  Pollable_close(&g_shepherd_socket);

  // Unmount all the daemons.
  for (int id = 0; id < g_daemons.size; id++)
  {
    Daemon* daemon = Array_get(&g_daemons, id);
    if (daemon == NULL || daemon->unmounted || daemon->pid == 0)
      continue;

    if (umount(daemon->tile_dir) != 0)
    {
      spew(2, "Lazily unmounting '%s'.", daemon->tile_dir);
      if (umount2(daemon->tile_dir, MNT_DETACH) != 0)
        warn_with_errno("Failed to lazily unmount '%s'", daemon->tile_dir);
    }

    // The daemon has been unmounted.
    daemon->unmounted = true;

    // HACK: Avoid deferred reply to "mount_fuse" query.
    daemon->deferred.socket = NULL;
  }

  // HACK: Restore "/etc", "/var", and "/sbin" if appropriate.
  struct stat st;
  if (stat("/etc.orig", &st) == 0 && S_ISDIR(st.st_mode))
  {
    system("rm -rf /etc.tile; mv /etc /etc.tile; mv /etc.orig /etc");
  }
  if (stat("/var.orig", &st) == 0 && S_ISDIR(st.st_mode))
  {
    system("rm -rf /var.tile; mv /var /var.tile; mv /var.orig /var");
  }
  if (stat("/sbin.orig", &st) == 0 && S_ISDIR(st.st_mode))
  {
    system("rm -rf /sbin.tile; mv /sbin /sbin.tile; PATH=$PATH:/sbin.orig busybox mv /sbin.orig /sbin");
  }
}


// Forward declaration.
static void
handle_monitor_packet(RPC rpc);


void
perform_fuse_init(RPC rpc)
{
  Pollable* socket = rpc.socket;

  uint id = (uint)(uintptr_t)socket->info;
  Daemon* daemon = Array_get(&g_daemons, id);
  assert(daemon != NULL);

  if (daemon->deferred.socket != NULL)
    spew(2, "Got fuse_init from fuse daemon %u.", id);
  else
    warn("Got fuse_init from fuse daemon %u.", id);

  finish_daemon(daemon, true);

  reply_fuse_init(rpc);
}


static void
handle_daemon_packet(RPC rpc)
{
  // Handle special queries intended for watchdog.
  if (rpc.code == QUERY_CODE_FUSE_INIT)
  {
    dispatch_packet(rpc);
    return;
  }

  // The packet must be a non-blind daemon query.
  assert(RPC_IS_QUERY(rpc.code) && rpc.tag >= TAG_DAEMON);

  if (g_monitor_in_pollable == NULL)
  {
    // ISSUE: Can this actually happen?
    warn("Declining daemon packet with code 0x%04x.", rpc.code);
    rpc_error(rpc, "Monitor is currently unavailable.");
    return;
  }

  spew(3, "Forwarding daemon packet with code 0x%04x and tag 0x%04x.",
       rpc.code, rpc.tag);

  uint id = (uint)(uintptr_t)rpc.socket->info;
  spew(3, "Forwarding packet from daemon %u.", id);
  Daemon* daemon = Array_get(&g_daemons, id);
  assert(daemon != NULL);

  if (daemon->waiting || daemon->unmounted || daemon->pid == 0)
  {
    warn("Declining unexpected query from daemon.");
    rpc_error(rpc, "Declining unexpected query.");
    return;
  }

  forward_packet_to_monitor(rpc);

  // Daemon is now waiting for a reply/error.
  daemon->waiting = true;

  if (sim_is_simulator())
  {
    // HACK: Read the reply/error and forward it to the daemon,
    // freezing the simulator during the read.
    if (handle_packets_slowly(g_monitor_in_pollable, handle_monitor_packet) < 0)
      punt("Could not read reply/error for daemon.");

    // Paranoia.
    assert(!daemon->waiting);
  }
}


static void
handle_daemon_socket(Pollable* socket)
{
  if (handle_packets(socket, handle_daemon_packet) < 0)
  {
    // NOTE: When a daemon's mount point is unmounted, the socket
    // closes, and the daemon exits, usually in that order.
    spew(3, "Lost connection to daemon process.");
  }
}


static void
handle_shepherd_packet(RPC rpc)
{
  // Handle special queries intended for watchdog.
  if (rpc.code == QUERY_CODE_MOUNT_FUSE)
  {
    dispatch_packet(rpc);
    return;
  }

  if (g_monitor_in_pollable == NULL)
  {
    // ISSUE: Is this actually possible?
    warn("Declining shepherd packet with code 0x%04x.", rpc.code);
    rpc_error(rpc, "Monitor is currently unavailable.");
    return;
  }

  spew(3, "Forwarding shepherd packet with code 0x%04x and tag 0x%04x.",
       rpc.code, rpc.tag);

  forward_packet_to_monitor(rpc);

  if (sim_is_simulator())
  {
    // The packet must be a blind query.
    assert(RPC_IS_QUERY(rpc.code) && rpc.tag == 0);
  }
}


static void
handle_shepherd_socket(Pollable* socket)
{
  if (handle_packets(socket, handle_shepherd_packet) < 0)
  {
    // NOTE: When the shepherd exits, the socket closes, and the
    // shepherd death is detected, usually in that order.
    spew(2, "Lost connection to shepherd process.");
  }
}


static StringArray*
exec_child_start(char* exe)
{
  // Set up stdout/stderr.
  dup2_and_close_or_die(g_child_stdout_pipe, STDOUT_FILENO);
  dup2_and_close_or_die(g_child_stderr_pipe, STDERR_FILENO);

  StringArray* argv = calloc_or_die(1, sizeof(*argv));

  StringArray_init(argv);

  StringArray_append(argv, exe);

  for (int i = 0; i < message_verbosity; i++)
    StringArray_append(argv, "--verbose");

  return argv;
}


static void
exec_child_finish(StringArray* argv)
{
  StringArray_append(argv, NULL);

  (void)execv(argv->data[0], argv->data);
  punt_with_errno("Failed to exec '%s'", argv->data[0]);
}



//! The "executor" sockets.
static Array g_executor_sockets;

//! Listen for new "executor" sockets.
static Pollable g_executor_listener;


// Like "handle_child_packet()" but with "tag" hacking.
//
// These packets come from "shepherd --execute ..." invocations.
//
static void
handle_executor_packet(RPC rpc)
{
  if (g_monitor_in_pollable == NULL ||
      rpc.code != QUERY_CODE_HACKY_EXECUTE ||
      rpc.tag != TAG_EXECUTOR_LAST)
  {
    warn("Declining hacky_execute query with code 0x%04x.", rpc.code);
    rpc_error(rpc, "Monitor is currently unavailable.");
    return;
  }

  spew(3, "Forwarding hacky_execute query to monitor.");

  // HACK: Modify the packet's tag to encode the "id".
  Pollable* socket = rpc.socket;
  Buffer* input = &rpc.socket->input;
  uint packet = input->head - RPC_HEADER_SIZE;
  uint id = (uint)(uintptr_t)socket->info;
  write_uint16(input->data + packet + 6, TAG_EXECUTOR + id);

  forward_packet_to_monitor(rpc);

  if (sim_is_simulator())
  {
    // HACK: Read the reply/error and forward it to the executor,
    // freezing the simulator during the read.
    if (handle_packets_slowly(g_monitor_in_pollable, handle_monitor_packet) < 0)
      punt("Could not read reply/error for executor.");
  }
}


static void
handle_executor_socket(Pollable* socket)
{
  if (handle_packets(socket, handle_executor_packet) < 0)
  {
    // FIXME: If this happens before the reply/error arrives, then we
    // might suffer from "id reuse" issues.

    spew(5, "Lost connection to executor: %s.", strerror(errno));

    uint id = (uint)(uintptr_t)socket->info;

    Pollable_destroy(socket);
    free(socket);

    Array_set(&g_executor_sockets, id, NULL);
  }
}


static void
handle_executor_listener(Pollable* pollable)
{
  spew(3, "Accepting execute connection!");

  // Find (or make) a hole.
  uint id = Array_find_or_append(&g_executor_sockets, NULL);

  // Paranoia.
  if (TAG_EXECUTOR + id >= TAG_EXECUTOR_LAST)
    punt("Too many simultaneous executor sockets!");

  // Accept a connection.
  int fd = simple_accept(pollable->fd);
  set_keep_alive_or_die(fd, true);

  Pollable* socket = (Pollable*)malloc_or_die(sizeof(Pollable));
  Pollable_init(socket, "Executor socket");
  Pollable_open(socket, fd, handle_executor_socket);

  socket->info = (void*)(uintptr_t)id;

  Array_set(&g_executor_sockets, id, socket);
}


static void
create_executor_listener(void)
{
  uint16_t port = g_opt_execute_port;
  int listener_fd = simple_listen(&port, 1);

  Pollable_init(&g_executor_listener, "Executor listener");
  Pollable_open(&g_executor_listener, listener_fd, handle_executor_listener);

  spew(3, "Listening for executor sockets on port %u.", port);
}



void
perform_request_close(RPC rpc)
{
  spew(1, "Closing connection to monitor.");

  // HACK: Send early reply.
  reply_request_close(rpc);

  // Disconnect.
  Pollable_close(g_monitor_in_pollable);
  Pollable_close(g_monitor_out_pollable);

  forget_shepherd();

  // Forget the monitor.
  g_monitor_in_pollable = NULL;
  g_monitor_out_pollable = NULL;
}



static void
free_daemon(Daemon* daemon)
{
  Pollable* socket = &daemon->socket;
  uint id = (uint)(uintptr_t)socket->info;
  Array_set(&g_daemons, id, NULL);

  Pollable_destroy(socket);

  free(daemon->tile_dir);

  free(daemon);
}


static void
free_daemon_callback(void* hack)
{
  free_daemon(hack);
}


// We do not call this function until the daemon has exited, and there
// is no longer any outstanding reply/error coming from the monitor.
// This ensures that we will not get confused by "id reuse" issues.
//
static bool
maybe_free_daemon(Daemon* daemon)
{
  if (daemon->pid == 0 && !daemon->waiting)
  {
    dispatch_events_defer_call(free_daemon_callback, daemon);
    return true;
  }

  return false;
}



// Help the monitor handle as "mount_fuse" query by forking off a
// daemon to create and manage the appropriate FUSE mount point.
//
// Assumes "host_dir" and "tile_dir" are legal, and "tile_dir" exists.
//
// See "perform_mount_fuse()" in "main.c".
//
void
watchdog_mount_fuse(RPC rpc, char* host_dir, char* tile_dir)
{
  spew(2, "Exporting '%s' as '%s'.", host_dir, tile_dir);

  // ISSUE: Verify existence of this executable?
  char* exe = "/sbin/shepherd-fuse";

  uint id = Array_find_or_append(&g_daemons, NULL);

  // Paranoia.
  if (TAG_DAEMON + id >= TAG_EXECUTOR)
    punt("Too many simultaneous fuse mounts!");

  spew(2, "Forking off fuse daemon %u.", id);

  // A socket for communication.
  int socket_pair[2];
  socketpair_or_die(socket_pair);

  pid_t pid = fork_or_die();

  if (pid == 0)
  {
    // Child.

    // Set up socket.
    close_or_die(socket_pair[0]);
    int fd = socket_pair[1];
    set_blocking_or_die(fd, false);
    set_keep_alive_or_die(fd, true);

    // Exec fuse daemon child.
    StringArray* argv = exec_child_start(exe);
    StringArray_append(argv, tile_dir);
    StringArray_append(argv, host_dir);
    StringArray_append(argv, strfmt_or_die("%d", fd));
    StringArray_append(argv, strfmt_or_die("%u", id));
    exec_child_finish(argv);
  }

  spew(2, "Forked off fuse daemon %u with pid %d.", id, pid);

  Daemon* daemon = calloc_or_die(1, sizeof(Daemon));

  Pollable_init(&daemon->socket, "Fuse socket");
  daemon->socket.info = (void*)(uintptr_t)id;

  daemon->pid = pid;

  daemon->tile_dir = strdup_or_die(tile_dir);

  // Set up socket.
  close_or_die(socket_pair[1]);
  int fd = socket_pair[0];
  set_blocking_or_die(fd, false);
  set_keep_alive_or_die(fd, true);
  set_close_on_exec_or_die(fd, true);
  Pollable_open(&daemon->socket, fd, handle_daemon_socket);

  // Defer RPC reply until daemon is ready.
  daemon->deferred = rpc;

  Array_set(&g_daemons, id, daemon);
}


// Fork off the actual "shepherd" process.
//
static void
fork_shepherd(void)
{
  // A socket for communication.
  int socket_pair[2];
  socketpair_or_die(socket_pair);
  set_close_on_exec_or_die(socket_pair[0], true);

  spew(3, "Forking off the shepherd process.");

  pid_t pid = fork_or_die();

  if (pid == 0)
  {
    // Child.

    // Set up socket.
    close_or_die(socket_pair[0]);
    int fd = socket_pair[1];
    set_blocking_or_die(fd, false);
    set_keep_alive_or_die(fd, true);

    // Exec monitored shepherd child.
    StringArray* argv = exec_child_start(__TMC_TASK_SHEPHERD_EXE);
    StringArray_append(argv, "--monitored-shepherd");
    StringArray_append(argv, strfmt_or_die("%d", fd));
    exec_child_finish(argv);
  }

  // Parent.
  spew(1, "Forked off shepherd process with pid %d.", pid);

  // Set up socket.
  close_or_die(socket_pair[1]);
  int fd = socket_pair[0];
  set_blocking_or_die(fd, false);
  set_keep_alive_or_die(fd, true);
  set_close_on_exec_or_die(fd, true);
  Pollable_open(&g_shepherd_socket, fd, handle_shepherd_socket);

  // Save the pid.
  g_shepherd_pid = pid;
}


static void
handle_monitor_packet(RPC rpc)
{
  spew(3, "Handling monitor packet with code 0x%04x and tag 0x%04x.",
       rpc.code, rpc.tag);

  Buffer* input = &rpc.socket->input;
  uint packet = input->head - RPC_HEADER_SIZE;

  Pollable* socket = NULL;

  if (!RPC_IS_QUERY(rpc.code) && rpc.tag >= TAG_EXECUTOR)
  {
    // Handle reply/error for "execute" query.

    // HACK: The tag encodes the index.
    uint id = rpc.tag - TAG_EXECUTOR;
    spew(3, "Handling reply/error for executor %d.", id);
    socket = Array_get(&g_executor_sockets, id);

    // HACK: Ignore reply for reaped socket.  ISSUE: See
    // "handle_executor_socket()" for some "id reuse" issues.
    if (socket == NULL)
      return;

    // HACK: Replace the "tag".
    write_uint16(input->data + packet + 6, TAG_EXECUTOR_LAST);
  }
  else if (!RPC_IS_QUERY(rpc.code) && rpc.tag >= TAG_DAEMON)
  {
    // Handle reply/error for "daemon" query.

    // The tag encodes the index.
    uint id = rpc.tag - TAG_DAEMON;
    spew(3, "Handling reply/error for daemon %d.", id);
    Daemon* daemon = Array_get(&g_daemons, id);
    assert(daemon != NULL);
    assert(daemon->waiting);

    finish_daemon(daemon, false);

    daemon->waiting = false;

    if (maybe_free_daemon(daemon))
      return;

    spew(3, "Forwarding reply/error to daemon %u.", id);

    socket = &daemon->socket;
  }
  else if (rpc.code == QUERY_CODE_REQUEST_CLOSE ||
           rpc.code == QUERY_CODE_MOUNT_FUSE)
  {
    // Handle query intended for the watchdog.

    dispatch_packet(rpc);
    return;
  }
  else
  {
    // Handle query/reply/error intended for the shepherd.
    // NOTE: This should never happen on the simulator.

    socket = &g_shepherd_socket;

    if (rpc.code == QUERY_CODE_REQUEST_PROTOCOL)
    {
      // HACK: Handle previously undetected monitor crashes.
      forget_shepherd();

      // Fork off shepherd.
      fork_shepherd();
    }
    else if (!Pollable_valid(socket))
    {
      rpc_error(rpc, "Declining monitor query for ungreeted shepherd.");
      return;
    }

    spew(3, "Forwarding packet to shepherd.");
  }

  // HACK: Forward the packet to the appropriate child.
  Pollable_write(socket, input->data + packet, input->size - packet);
}


static void
handle_monitor_socket(Pollable* pollable)
{
  if (handle_packets(pollable, handle_monitor_packet) < 0)
  {
    // NOTE: This can only happen on "&g_monitor_socket".
    spew(1, "Lost connection to monitor: %s.", strerror(errno));

    forget_shepherd();

    // Forget the monitor.
    g_monitor_in_pollable = NULL;
    g_monitor_out_pollable = NULL;
  }
}


static void
handle_monitor_listener(Pollable* pollable)
{
  spew(1, "Accepting monitor connection!");

  // Accept a connection.
  int fd = simple_accept(pollable->fd);
  set_keep_alive_or_die(fd, true);

  // Stop listening.
  Pollable_close(pollable);

  Pollable_open(&g_monitor_socket, fd, handle_monitor_socket);

  // Commit to "--listen".
  if (g_monitor_in_pollable == NULL)
  {
    g_monitor_in_pollable = &g_monitor_socket;
    g_monitor_out_pollable = &g_monitor_socket;

    // Close the other "listeners".
    Pollable_close(&g_monitor_device_pci);
    Pollable_close(&g_monitor_device_tmfifo);
  }
}


static void
open_monitor_listener(void)
{
  if (g_monitor_listener_port == 0)
    g_monitor_listener_port = atou16_or_die(g_opt_net_port);

  bool first_and_ephemeral = (g_monitor_listener_port == 0);

  int listener_fd = simple_listen(&g_monitor_listener_port, 1);
  Pollable_open(&g_monitor_listener, listener_fd, handle_monitor_listener);

  if (first_and_ephemeral)
    note("Listening for monitor on port %u.", g_monitor_listener_port);
  else
    spew(2, "Listening for monitor on port %u.", g_monitor_listener_port);
}


static void
handle_monitor_device(Pollable* pollable)
{
  // Commit to "--pci" or "--tmfifo".
  if (g_monitor_in_pollable == NULL)
  {
    spew(1, "%s activity detected!", pollable->name);
    g_monitor_in_pollable = pollable;
    g_monitor_out_pollable = pollable;

    // Close the other "listeners".
    Pollable_close(&g_monitor_listener);
    if (pollable != &g_monitor_device_pci)
      Pollable_close(&g_monitor_device_pci);
    if (pollable != &g_monitor_device_tmfifo)
      Pollable_close(&g_monitor_device_tmfifo);
  }
  else if (g_monitor_in_pollable != pollable)
  {
    // ISSUE: Arguably we should discard traffic.
    warn("%s had unexpected activity!", pollable->name);
  }

  if (handle_packets(pollable, handle_monitor_packet) < 0)
  {
    // NOTE: This cannot actually happen.
    warn_with_errno("%s lost connection to monitor", pollable->name);
  }
}


static bool
open_device(Pollable* pollable, const char* device)
{
  int fd = open(device, O_RDWR);

  if (fd < 0)
  {
    // ISSUE: We need EBUSY for "tmfifo".
    if (errno == ENXIO || errno == EINTR)
    {
      spew(2, "%s not ready, try again later!", pollable->name);
      return false;
    }

#ifdef __tilegx__
    // ISSUE: For "tmfifo".
    if (errno == EBUSY)
    {
      spew(2, "%s not ready, try again later!", pollable->name);
      return false;
    }
#endif

    punt_with_errno("Failure in 'open(\"%s\")'", device);
  }

  set_blocking_or_die(fd, false);

  set_close_on_exec_or_die(fd, true);

  Pollable_open(pollable, fd, handle_monitor_device);

  spew(2, "%s watching for monitor activity on %s.", pollable->name, device);

  return true;
}


static void
check_for_dead_children(void)
{
  while (true)
  {
    int status = 0;
    pid_t pid = waitpid(-1, &status, WNOHANG);

    if (pid > 0)
    {
      const char* who = "Somebody";

      if (g_shepherd_pid == pid)
      {
        who = "The shepherd process";

        if (status != 0)
        {
          char buf[128];
          snprintf(buf, sizeof(buf),
                   "The shepherd process crashed (status 0x%x).\n", status);
          sim_print_string(buf);
        }

        // HACK: Just shut down the entire simulator.
        __insn_mtspr(SPR_SIM_CONTROL, SIM_CONTROL_SHUTDOWN);

        // Notify the monitor.
        do_note_quit(g_monitor_out_pollable);

        // For ssh case, exit shepherd if the monitored "shepherd" child
        // terminated.
        if (g_opt_ssh)
          g_shepherd_exit = true;

        g_shepherd_pid = 0;

        forget_shepherd();
      }

      for (int id = 0; id < g_daemons.size; id++)
      {
        Daemon* daemon = Array_get(&g_daemons, id);
        if (daemon == NULL)
          continue;

        if (daemon->pid == pid)
        {
          who = "A fuse daemon";

          finish_daemon(daemon, false);

          Pollable_close(&daemon->socket);

          daemon->pid = 0;

          maybe_free_daemon(daemon);

          break;
        }
      }

      if (WIFEXITED(status))
      {
        int code = WEXITSTATUS(status);
        spew(1, "%s exited with code %d.", who, code);
      }
      else
      {
        int sig = WTERMSIG(status);
        warn("%s terminated: %s (%d).", who, strsignal(sig), sig);
      }
    }
    else if ((pid == 0) || (errno == ECHILD))
    {
      // ISSUE: I think I saw "ECHILD" happen once.
      break;
    }
    else if (errno != EINTR)
    {
      punt_with_errno("Failure in waitpid()");
    }
  }
}


// Become a "watchdog".
//
void
become_watchdog(void)
{
  message_prefix = "[shepherd watchdog] ";

#if 0
  message_output_hook = message_output_forward;

  if (getenv("VERBOSITY") != NULL)
    message_verbosity = atoi(getenv("VERBOSITY"));
#endif

  Pollable_init(&g_child_stdout, "Child stdout");
  Pollable_init(&g_child_stderr, "Child stderr");

  // A pipe for stdout.
  int stdout_pipe_pair[2];
  pipe_or_die(stdout_pipe_pair);
  g_child_stdout_pipe = stdout_pipe_pair[1];
  int fd1 = stdout_pipe_pair[0];
  set_blocking_or_die(fd1, false);
  set_close_on_exec_or_die(fd1, true);
  Pollable_open(&g_child_stdout, fd1, handle_child_console);

  // A pipe for stderr.
  int stderr_pipe_pair[2];
  pipe_or_die(stderr_pipe_pair);
  g_child_stderr_pipe = stderr_pipe_pair[1];
  int fd2 = stderr_pipe_pair[0];
  set_blocking_or_die(fd2, false);
  set_close_on_exec_or_die(fd2, true);
  Pollable_open(&g_child_stderr, fd2, handle_child_console);


  Pollable_init(&g_shepherd_socket, "Shepherd socket");

  //--Pollable_init(&g_monitor_socket, "Monitor socket");

  Pollable_init(&g_monitor_listener, "Monitor listener");

  Pollable_init(&g_monitor_device_pci, "Monitor device (pci)");
  Pollable_init(&g_monitor_device_tmfifo, "Monitor device (tmfifo)");


  // HACK: Create the "execute" listener if needed.
  if (g_opt_execute_port != 0)
    create_executor_listener();


  // Watch for dead children.
  dispatch_events_expect_signals(NULL);
  if (signal(SIGCHLD, dispatch_events_handle_signal) == SIG_ERR)
    punt("Failed to register signal handler.");


  // Determine the "dataplane" tiles.
  cpu_set_t cpus_dataplane;
  (void)tmc_cpus_get_dataplane_cpus(&cpus_dataplane);

  // Determine the "initial" tiles.
  cpu_set_t cpus_initial;
  (void)tmc_cpus_get_my_affinity(&cpus_initial);

  // Determine the "favored" tiles.
  cpu_set_t cpus_favored = cpus_initial;
  tmc_cpus_remove_cpus(&cpus_favored, &cpus_dataplane);

  // HACK: Temporarily move to the last "favored" tile.
  // NOTE: If "cpus_favored" is empty, this will have no effect.
  // ISSUE: Should we try to avoid the tile used by the shepherd?
  (void)tmc_cpus_set_my_cpu(tmc_cpus_find_last_cpu(&cpus_favored));
  (void)tmc_cpus_set_my_affinity(&cpus_initial);


  if (sim_is_simulator())
  {
    fork_shepherd();

    Pollable* pollable = &g_monitor_socket;
    g_monitor_in_pollable = pollable;
    g_monitor_out_pollable = pollable;

    // Note that we only read from "g_monitor_socket" using special
    // "blocking" reads which freeze the entire simulator.
    int fd = (SIM_SOCKET_ID + 1) | SIM_SOCKET_BLOCKING;
    __insn_mtspr(SPR_SIM_SOCKET, fd);
    Pollable_open(pollable, fd, NULL);

    // ISSUE: Must avoid writing to "g_monitor_socket" until the
    // checkpoint image has been created by the shepherd, if needed.

    while (true)
    {
      dispatch_events(-1);

      check_for_dead_children();

      // HACK: We must flush explicitly.
      if (pollable->output.size != 0)
        Pollable_flush_fully(pollable);
    }
  }

  else
  {
    while (true)
    {
      int msecs = -1;

      if (g_monitor_in_pollable == NULL)
      {
        // Start listening if needed.
        if (g_opt_net_port != NULL && !Pollable_valid(&g_monitor_listener))
        {
          open_monitor_listener();
        }

        if (g_opt_ssh)
        {
          if (g_shepherd_exit)
            exit(0);

          Pollable_open(&g_monitor_stdin, STDIN_FILENO, handle_monitor_socket);
          Pollable_set_fd(&g_monitor_stdout, STDOUT_FILENO);
          g_monitor_in_pollable = &g_monitor_stdin;
          g_monitor_out_pollable = &g_monitor_stdout;

          // Close the other "listeners".
          Pollable_close(&g_monitor_listener);
        }

        if (g_opt_pci_device != NULL && !Pollable_valid(&g_monitor_device_pci))
        {
          // Occasionally try to open the device.
          if (!open_device(&g_monitor_device_pci, g_opt_pci_device))
            msecs = 700;
        }

        if (g_opt_tmfifo_device != NULL &&
            !Pollable_valid(&g_monitor_device_tmfifo))
        {
          // Occasionally try to open the device.
          if (!open_device(&g_monitor_device_tmfifo, g_opt_tmfifo_device))
            msecs = 700;
        }
      }

      dispatch_events(msecs);

      check_for_dead_children();
    }
  }
}
