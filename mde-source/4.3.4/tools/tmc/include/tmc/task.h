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
//! Task management and cleanup.
//!

//! @addtogroup tmc_task
//! @{
//!
//! Task management and cleanup operations.
//!
//! @section tmc_task_shepherd Starting the Shepherd
//!
//! A "shepherd" is a special process that can watch other processes,
//! tracking both the forking of child processes and the creation of
//! threads, and treating the resulting set of processes as a single
//! "application".  If any process crashes (or exits with an exit code
//! between 128 and 255), the shepherd kills all of the other
//! processes in the same application.
//!
//! A shepherd can be started by running @c /bin/shepherd, with
//! optional @c --tile arguments, followed by an initial executable,
//! and its arguments.  This shepherd exits once the application
//! has exited, with an exit status based on how the processes in the
//! application exited.
//!
//! A monitored shepherd can be started by running @c /bin/shepherd
//! with arguments telling it how to communicate with a @c tile-monitor
//! process on the host.  A monitored shepherd helps the monitor
//! manage (and debug) processes, and their consoles.
//!
//! Each process launched by the monitor becomes the first process in
//! a new application.
//!
//! A monitored shepherd can support the use of the "FUSE" filesystem,
//! which exports the host filesystem to Tilera Linux, with the
//! cooperation of the monitor.
//!
//! For more information on tile-monitor, see the shipped tile-monitor
//! man page, also available as the "Tile Processor Monitor" reference
//! in the "Target Platform Tools" section.
//!
//! @section tmc_task_app Interacting with the Shepherd
//!
//! The tmc_task_has_monitor() function can be used to determine if
//! the current process has a monitored shepherd as an ancestor, and
//! the tmc_task_has_shepherd() function can be used to determine if
//! the current process has any shepherd as an ancestor.  In the
//! current implementation, when these functions return true, the shepherd
//! starts watching the process (if it is not already doing so).
//!
//! The tmc_task_watch_forked_children() function tells the shepherd
//! whether forked children should be watched and considered to be
//! part of an application.  By default, this is false, and should
//! remain false, except when actually forking interesting children,
//! to avoid confusing @c system() (and such).  For example:
//!
//! @code
//! int watch_forked_children = tmc_task_watch_forked_children(1);
//! pid_t pid = fork();
//! if (pid == 0)
//!   run_child();
//! tmc_task_watch_forked_children(watch_forked_children);
//! @endcode
//!
//! The tmc_task_assume_impending_exec() function can be used in much
//! the same way as tmc_task_watch_forked_children(), to let the shepherd
//! know that forked children will be calling @c execv() (or similar)
//! sufficiently quickly that attempting to debug them before they do
//! so would only annoy the user.  Note that this is only necessary if
//! the shepherd will be watching the forked child.  For example:
//!
//! @code
//! int watch_forked_children = tmc_task_watch_forked_children(1);
//! int assume_impending_exec = tmc_task_assume_impending_exec(1);
//! pid_t pid = fork();
//! if (pid == 0)
//!   exec_child();
//! tmc_task_watch_forked_children(watch_forked_children);
//! tmc_task_assume_impending_exec(assume_impending_exec);
//! @endcode
//!
//! The tmc_task_monitor_console() function creates pipes to be used as
//! the stdin, stdout, and stderr streams for the current process, and
//! passes the other ends of the pipes to the (monitored) shepherd.
//! The monitor displays any data read from the stdout and stderr
//! pipes, and supplies data to be written to the stdin pipe.
//!

#ifndef __TMC_TASK_H__
#define __TMC_TASK_H__

#include <sys/types.h>

#include <features.h>

__BEGIN_DECLS


//! Write an error message to stderr, and then call @c exit(128).
//!
//! The message is written to stderr, preceded by "ERROR: ", and
//! followed by a newline.
//!
//! If the current process is being watched by a shepherd, the
//! shepherd will kill any other processes in the same application.
//!
//! @param format The format string.
//! @param ... The arguments for the format string.
//!
extern void
tmc_task_die(const char* format, ...)
  __attribute__((format(printf, 1, 2), noreturn));


//! Determine if the current process has a monitored shepherd.
//!
//! Currently, if this function returns true, the shepherd starts
//! watching the current process, if it is not already doing so.
//!
//! If this function returns false, then various other functions
//! have no useful effect.
//!
//! @return 1 for true, 0 for false.
//!
extern int
tmc_task_has_monitor(void);


//! Determine if the current process has a shepherd.
//!
//! Currently, if this function returns true, the shepherd starts
//! watching the current process, if it is not already doing so.
//!
//! If this function returns false, then various other functions
//! will not have any useful effect.
//!
//! @return 1 for true, 0 for false.
//!
extern int
tmc_task_has_shepherd(void);


//! Set the "watch forked children" flag.
//!
//! This flag defaults to false, and, if true, the shepherd, if any,
//! watches any forked processes.
//!
//! @param flag The desired flag value.
//!
//! @return -1 (setting errno) on error, or the previous value (0 or 1).
//!
extern int
tmc_task_watch_forked_children(int flag);


//! Set whether or not the calling process will quickly follow
//! a call to @c fork() with a call to @c exec().
//!
//! This flag defaults to false, and, if true, the shepherd, if any, avoids
//! creating a debugger until @c execv() (or similar) has been called.
//!
//! @param flag The desired flag value.
//!
//! @return -1 (setting errno) on error, or the previous value (0 or 1).
//!
extern int
tmc_task_assume_impending_exec(int flag);


//! Terminate the "application" to which the current process belongs.
//!
//! This causes all processes in the same application to be killed
//! with SIGKILL.
//!
//! @return -1 if tmc_task_has_shepherd() is false, else never returns.
//!
extern int
tmc_task_terminate_app(void);


#ifndef __NEWLIB__

//! Create stdin, stdout, and stderr pipes for the current process
//! and ask the shepherd to monitor them.
//!
//! This function does nothing unless tmc_task_has_monitor() is true.
//!
//! Note that once this function has been called successfully, stdout
//! and stderr from the process are no longer deterministically
//! ordered relative to the stdout and stderr of other processes, due
//! to the actual mechanism by which the pipes are monitored.
//!
//! @return -1 (setting error) on error, 1 (setting errno to EINVAL)
//! if already called successfully, or zero.
//!
extern int
tmc_task_monitor_console(void);

#endif // !__NEWLIB__


//! Get the "task id" for the current thread (aka "gettid()").
//!
//! For a single threaded process, or the initial process of a
//! multi-threaded process, this is the same as the process id.
//!
//! Although technically glibc does not guarantee that a given pthread
//! will stay bound to a single Linux task, in the current
//! implementation that is true, and there are no plans to change it.
//!
//! The task ID can be helpful for various things, including
//! SIGEV_THREAD_ID with timer_create(), sched_setaffinity() or
//! sched_getaffinity() calls, and fcntl() F_SETOWN/F_GETOWN commands.
//! In addition, it can be used to identify threads in "ps".
//!
//! @return The task id.
//!
extern pid_t
tmc_task_gettid(void);


#ifndef __DOXYGEN__

//! Get the "exe" for the current process.
//!
//! This returns a static singleton string derived by reading the
//! "/proc/self/exe" symlink, and removing any " (deleted) suffix.
//!
//! @return The path to the executable, or NULL on various errors
//! (setting errno).
//!
extern const char*
__tmc_task_getexe(void);


//! For internal use only.
//!
//! @return 1 for true, 0 for false.
//!
extern int
__tmc_task_is_linux(void);


//! Get field "n" (starting from zero) of "/proc/PID/stat" for task "tid"
//! (where zero means the current task).
//!
//! The fields in "/proc/PID/stat" are mostly space separated numeric fields,
//! except that field 1 is the executable filename, in parentheses, and may
//! contain spaces, and field 2 is a character.  Currently there are 42 fields
//! (but more fields may be added in the future).
//!
//! Field 0 is PID.  Field 21 is the starttime.  Field 38 is the current CPU.
//!
//! @return -1 on failure (setting errno), or a value
//! (setting errno to zero).
//!
extern int
__tmc_task_parse_proc_stat(pid_t tid, unsigned int n);


//! Canonicalize "path".
//!
//! First, if necessary, "path" is absolutized relative to "dir" (or
//! to the current working directory, if "dir" is NULL), and then, all
//! "."  and ".." path components are collapsed (as by realpath()).
//!
//! @param path A (possibly relative) path.
//! @param dir A (possibly NULL) base directory.
//!
//! @return A canonical path (which must be freed), or NULL on
//! failure (setting errno).
//!
extern char*
__tmc_task_canonicalize(const char* path, const char* dir);


//! Fork off a child to continue running the current executable, and
//! turn the original process into a special unmonitored shepherd,
//! telling it about the child that it just forked.
//!
//! This is ONLY for use by iLib!  Normally iLib applications should
//! be launched via "tile-monitor", or explicitly via an unmonitored
//! "shepherd", so "--tile" can be used.
//!
//! @return -1 on failure (setting errno), or zero.
//!
extern int
__tmc_task_fork_local_shepherd(void);


//! The location of the shepherd executable.
#define __TMC_TASK_SHEPHERD_EXE "/bin/shepherd"

#endif


__END_DECLS


#endif // __TMC_TASK_H__

//! @}
