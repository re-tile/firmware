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
//! CPU management and task affinitization.

//! @addtogroup tmc_cpus
//! @{
//!
//! Routines that manage sets of CPUs and control Linux processor
//! affinity.
//!
//! The Linux @c sched.h header provides a cpu_set_t object for
//! representing sets of CPUs.  The tmc/cpus.h header provides a
//! variety of utility routines for manipulating these CPU sets,
//! including:
//!
//! - Adding and removing CPUs and performing various set operations.
//! - Binding tasks so that they only execute on a particular set of CPUs.
//! - Converting cpu_set_t objects to array or string representations.
//! - Converting a CPU's index number to and from its UDN coordinates.
//!
//!
//! @section cpus_affinity Controlling a Task's CPU Affinity
//!
//! The Linux scheduler allows applications to restrict the set of
//! CPUs on which a particular task may execute.  A "task" is defined
//! as either a process (with no threads), or an individual thread
//! within a threaded process.  The tmc_cpus_get_my_affinity() call
//! allows a task to determine its current CPU affinity.  Similarly
//! tmc_cpus_set_my_affinity() is used to set the task's affinity,
//! binding it to a set of CPUs.  Applications often need to bind tasks
//! to a single CPU; the tmc_cpus_set_my_cpu() call provides a
//! convenient wrapper for this use case.
//!
//! Tilera recommends that developers avoid hard-coding particular CPU
//! numbers in their applications.  Instead, developers should use
//! tile-monitor or taskset to pass an initial set of CPUs to an
//! application, then have the application code select each task's CPU
//! from the CPUs in that initial CPUs set.  This approach makes it
//! easier to modify the number of CPUs used for a particular
//! application.  It also avoids problems such as when the developer changes
//! the set of tiles used for hypervisor dedicated tiles and the new
//! locations conflict with CPU numbers hard-coded into an
//! application.
//!
//! Use tile-monitor's @c --tile option to control an application's
//! initial CPU affinity.  Alternatively, when running from the Linux
//! console, use either the taskset command or the shepherd:
//!
//! @code
//! shepherd --tile 4x1 <exe> <args>
//! taskset -c 1-4 <exe> <args>
//! @endcode
//!
//! Running via @c taskset simply runs the program with the specified CPU
//! affinity.  The shepherd provides additional functionality, like
//! automatic cleanup of application processes when any process exits
//! due to a failure.  For more information, see @ref tmc_task.
//!
//! Once the application is running, it can use code like the
//! following to assign each task to a CPU:
//!
//! @code
//! // Save the application's CPU set for later use.
//! cpu_set_t cpus;
//! if (tmc_cpus_get_my_affinity(&cpus) != 0)
//!   tmc_task_die("tmc_cpus_get_my_affinity() failed.");
//!
//! // Given this task's rank, bind to the appropriate CPU.
//! if (tmc_cpus_set_my_cpu(tmc_cpus_find_nth_cpu(&cpus, rank)) < 0)
//!   tmc_task_die("tmc_cpus_set_my_cpu() failed.");
//!
//! // Use the saved CPU set to look up UDN coordinates for rank 0.
//! int rank0_cpu = tmc_cpus_find_nth_cpu(&cpus, 0);
//! DynamicHeader header = tmc_udn_header_from_cpu(rank0_cpu);
//! @endcode
//!
//! Tilera Linux also provides a couple of special CPU sets.  The
//! @e online set, accessible via tmc_cpus_get_online_cpus(), includes
//! all CPUs on which Linux tasks may run.  The @e dataplane set,
//! accessible via tmc_cpus_get_dataplane_cpus() or tile-monitor's
//! <tt>--tile dataplane</tt> option, contains only those CPUs that were
//! configured to run in @e dataplane mode as part of the Linux boot
//! arguments.  Dataplane CPUs are optimized for running a single task
//! and attempt to avoid any Linux interrupt overhead so long as the
//! task avoids making any Linux system calls.
//! 
//!
//! @section cpus_grid The CPU Grid
//!
//! The Tile Processor arranges CPUs into a two-dimensional grid.
//! Thus, many applications, particularly those using the UDN, work
//! with CPUs in terms of rectangular arrays, defined by a width and height,
//! plus an upper left-hand (X,Y) CPU coordinate.  TMC provides a variety of
//! functions for determining the CPU grid size and manipulating CPU
//! rectangles.  For example, tmc_cpus_grid_tile_to_cpu() converts a
//! CPU's (X,Y) coordinates to a CPU number.
//!

#ifndef __TMC_CPUS_H__
#define __TMC_CPUS_H__

#include <features.h>
#include <stdint.h>
#include <stddef.h>
#include <sched.h>

#include <sys/types.h>

#include <arch/inline.h>

__BEGIN_DECLS


// Note that sched.h defines several @c __CPU_xxx macros, plus @c cpu_set_t,
// which contains a single field, "<tt>__cpu_mask __bits[CPUS_NUM_WORDS]</tt>",
// with the bits ordered as low bit to high, low word to high.


//! Maximum number of CPUs a @c cpu_set_t can represent.
#define TMC_CPUS_MAX_COUNT __CPU_SETSIZE

//! Number of bits per word in the @c __bits field of @c cpu_set_t.
#define TMC_CPUS_NUM_BITS __NCPUBITS

//! Number of words in the @c __bits field of @c cpu_set_t.
#define TMC_CPUS_NUM_WORDS (__CPU_SETSIZE / __NCPUBITS)


//! Clear a CPU set.
//!
//! @param s The CPU set to be modified.
//!
extern void
tmc_cpus_clear(cpu_set_t* s);


//! Add a CPU to a CPU set.
//!
//! @param s The CPU set to be modified.
//! @param cpu CPU number to be set in @c s.
//!
//! @return 0 on success, -1 and set errno to EINVAL if CPU is illegal.
//!
extern int
tmc_cpus_add_cpu(cpu_set_t* s, unsigned int cpu);

//! Remove a CPU from a CPU set.
//!
//! @param s The CPU set to be modified.
//! @param cpu CPU number to be removed from @c s.
//!
//! @return 0 on success, -1 and set errno to EINVAL if CPU is illegal.
//!
extern int
tmc_cpus_remove_cpu(cpu_set_t* s, unsigned int cpu);


//! Add some CPUs to a CPU set.
//!
//! @param s The CPU set to be modified.
//! @param cpus The set of CPUs to be added to @c s.
//!
extern void
tmc_cpus_add_cpus(cpu_set_t* s, const cpu_set_t* cpus);

//! Remove some CPUs from a CPU set.
//!
//! @param s The CPU set to be modified.
//! @param cpus The set of CPUs to be removed from @c s.
//!
extern void
tmc_cpus_remove_cpus(cpu_set_t* s, const cpu_set_t* cpus);

//! Intersect some CPUs with a CPU set.
//!
//! @param s The CPU set to be modified.
//! @param cpus The set of CPUs that should not be removed from @c s.
//!
extern void
tmc_cpus_intersect_cpus(cpu_set_t* s, const cpu_set_t* cpus);


//! Determine if a CPU is present in a CPU set.
//!
//! @param s The CPU set.
//! @param cpu CPU number to check.
//!
//! @return 1 if @c cpu is present in @c s, else 0.
//!
extern int
tmc_cpus_has_cpu(const cpu_set_t* s, unsigned int cpu);


//! Count the number of CPUs in a CPU set.
//!
//! @param s The CPU set.
//!
//! @return The number of CPUs present in @c s.
//!
extern unsigned int
tmc_cpus_count(const cpu_set_t* s);


//! Find the nth CPU in a CPU set.
//!
//! @param s The CPU set.
//! @param n index (starting with 0).
//!
//! @return The CPU number of the nth CPU present in @c s.  -1 if @c s does
//!   not have at least n+1 CPUs present.
//!
extern int
tmc_cpus_find_nth_cpu(const cpu_set_t* s, unsigned int n);

//! Find the first CPU in a CPU set.
//!
//! @param s The CPU set.
//!
//! @return The CPU number of the first CPU present in @c s.  -1 if @c s
//!   does not contain any CPUs.
//!
extern int
tmc_cpus_find_first_cpu(const cpu_set_t* s);

//! Find the last CPU in a CPU set.
//!
//! @param s The CPU set.
//!
//! @return The CPU number of the last CPU present in @c s.  -1 if @c s
//!   does not contain any CPUs.
//!
extern int
tmc_cpus_find_last_cpu(const cpu_set_t* s);



//! Convert a CPU set into a CPU array.
//!
//! @param s The CPU set to be converted.
//! @param indices An array of integers to be filled with the CPU
//!   numbers that are present in @c s.
//! @param count The number of integers in the @c indices array.
//!
//! @return -1 if the vector was not large enough, else the number of
//!   indices used.
//!
extern int
tmc_cpus_to_array(const cpu_set_t* s,
                  unsigned int* indices, unsigned int count);

//! Convert a CPU array into a CPU set.
//!
//! @param s The CPU set to be filled.
//! @param indices An array of CPU numbers to be set in @c s.
//! @param count The number of integers in the @c indices array.
//!
//! @return -1 if any CPU is illegal (not less than ::TMC_CPUS_MAX_COUNT),
//!   else zero.
//!
extern int
tmc_cpus_from_array(cpu_set_t* s,
                    const unsigned int *indices, unsigned int count);



//! Convert a CPU set into a CPU string.
//!
//! A CPU string has the form "A,B,C-F", where A, B, and C through F
//! inclusive are the CPU numbers present in the CPU set.
//! This routine currently only generates a comma-separated cpu list.
//!
//! @param s The CPU set to be converted.
//! @param string The string to be filled.
//! @param limit The maximum number of characters in @c string.
//!
//! @return -1 if the string was not large enough, else the number of
//!   characters used (not including the final NUL terminator).
//!
extern int
tmc_cpus_to_string(const cpu_set_t* s, char* string, size_t limit);

//! Convert a CPU string into a CPU set.
//!
//! A CPU string has the form "A,B,C-F", where A, B, and C through F
//! inclusive are the CPU numbers present in the CPU set.
//! For backwards compatibility, we accept ";" as a synonym for ",".
//!
//! @param s The CPU set to be filled.
//! @param string A CPU string containing the CPUs to be set in @c s.
//! @return -1 if the string was formatted incorrectly or any CPU is illegal
//!   (negative or not less than ::TMC_CPUS_MAX_COUNT), else zero.
//!
extern int
tmc_cpus_from_string(cpu_set_t* s, const char* string);



//! Set the affinity mask of task @c tid to @c s.
//!
//! @param s The new task affinity.
//! @param tid The task ID whose affinity is set.  Use tid zero to act
//!   like tmc_cpus_set_my_affinity().
//!
//! @return -1 on failure (setting errno), else 0.
//!
extern int
tmc_cpus_set_task_affinity(const cpu_set_t* s, pid_t tid);

//! Get the affinity mask of task @c tid into @c s.
//!
//! @param s The CPU set to be filled with the affinity of task @c tid.
//! @param tid The task ID whose affinity is read. Use tid zero to act
//!   like tmc_cpus_get_my_affinity().
//!
//! @return -1 on failure (setting errno), else 0.
//!
extern int
tmc_cpus_get_task_affinity(cpu_set_t* s, pid_t tid);


//! Bind task @c tid to a single CPU.
//!
//! @param cpu The CPU number on which @c tid should run.
//! @param tid The task ID of the task to be affinitized.  Use tid zero to
//!   act like tmc_cpus_set_my_cpu().
//!
//! @return -1 on failure (setting errno), else 0.
//!
extern int
tmc_cpus_set_task_cpu(int cpu, pid_t tid);

//! Get the single CPU to which task @c tid is bound.
//!
//! Note that this call will return -1 unless the current task is
//! bound with an affinity of a single CPU.  This can be useful to
//! confirm that the task will not migrate among multiple CPUs.
//! See also tmc_cpus_get_task_current_cpu().
//!
//! @param tid The task ID of the task whose affinity is read.  Use tid
//!   zero to act like tmc_cpus_get_my_cpu().
//!
//! @return -1 on failure (setting errno), else a CPU number.  If the task
//!   is not bound to a single CPU, errno is set to ERANGE.
//!
extern int
tmc_cpus_get_task_cpu(pid_t tid);


//! Get the CPU on which task @c tid is currently running.
//!
//! See also tmc_cpus_get_task_cpu().
//!
//! CAUTION: if the given task is @e not bound to a single CPU, it may
//! migrate to another CPU at any time.
//!
//! @param tid The task ID of the task whose affinity is read.  Use tid
//!   zero to act like tmc_cpus_get_my_current_cpu().
//!
//! @return -1 on failure (setting errno), else a CPU number.
//!
extern int
tmc_cpus_get_task_current_cpu(pid_t tid);


//! Set the affinity mask of the current task to @c s.
//!
//! @param s The new task affinity.
//!
//! @return -1 on failure (setting errno), else 0.
//!
static __USUALLY_INLINE int
tmc_cpus_set_my_affinity(const cpu_set_t* s)
{
  return tmc_cpus_set_task_affinity(s, 0);
}

//! Get the affinity mask of the current task into @c s.
//!
//! @param s The CPU set filled with the current task's affinity.
//!
//! @return -1 on failure (setting errno), else 0.
//!
static __USUALLY_INLINE int
tmc_cpus_get_my_affinity(cpu_set_t* s)
{
  return tmc_cpus_get_task_affinity(s, 0);
}


//! Set the single CPU to which the current task is bound.
//!
//! @param cpu The CPU number of the single CPU on which the current
//! task should run.
//!
//! @return -1 on failure (setting errno), else 0.
//!
static __USUALLY_INLINE int
tmc_cpus_set_my_cpu(int cpu)
{
  return tmc_cpus_set_task_cpu(cpu, 0);
}

//! Get the single CPU to which the current task is bound.
//!
//! Note that this call will return -1 unless the current task is
//! bound with an affinity of a single CPU.  This can be useful to
//! confirm that the task will not migrate among multiple CPUs.
//! See also tmc_cpus_get_my_current_cpu().
//!
//! @return -1 on failure (setting errno), else a CPU number.  If the
//!   task is not bound to a single CPU, errno is set to ERANGE.
//!
static __USUALLY_INLINE int
tmc_cpus_get_my_cpu(void)
{
  return tmc_cpus_get_task_cpu(0);
}


//! Get the CPU on which the current task is currently running.
//!
//! This is the same as the glibc sched_getcpu() function (only available
//! when -D_GNU_SOURCE or equivalent is specified).
//!
//! This is faster than tmc_cpus_get_my_cpu().
//!
//! CAUTION: If the current task is @e not bound to a single CPU, it may
//! migrate to another CPU at any time.
//!
//! @return -1 on failure (setting errno), else a CPU number.
//!
extern int
tmc_cpus_get_my_current_cpu(void);


//! Request specific dataplane semantics for this task.
//!
//! The various flags allow a task to minimize kernel interrupts,
//! or to diagnose userspace or kernel issues that cause interrupts.
//! Note that you will also need to include <sys/dataplane.h> to get
//! the flag names to use for the bitwise OR for the "flags" parameter,
//! as this call is just a wrapper for the set_dataplane(2) system call.
//! The flag values are:
//!
//! DP_QUIESCE
//! Quiesce the timer interrupt before returning to user space after a
//! system call.  Normally if a task on a dataplane core makes a
//! syscall, the system will run one or more timer ticks after the
//! syscall has completed, causing unexpected interrupts in userspace.
//! Setting DP_QUIESCE avoids that problem by having the kernel "hold"
//! the task in kernel mode until the timer ticks are complete.  This
//! will make syscalls dramatically slower.
//! This flag also makes the core release any free kernel pages it is
//! holding, so that it won't later need to do housekeeping to release
//! them (for example, if mlockall() is called on another core).
//! If multiple dataplane tasks are scheduled on a single core, neither
//! one will make progress, since for the kernel to run both would require
//! setting up a scheduler timer tick to allow the two tasks to multitask.
//! Accordingly, both stay in the kernel until one is killed manually.
//! This technique allows short-lived kernel tasks which run in response
//! to syscalls to do their work and return to their normal idle state
//! synchronously, allowing the dataplane task to continue in userspace
//! afterwards without further interruption.
//! 
//! DP_STRICT
//! Disallow the application from entering the kernel in any way,
//! unless it calls set_dataplane() again without this bit set.
//! Issuing any other syscall or causing a page fault would generate a
//! kernel message, and "kill -9" the process.
//! Setting this flag automatically sets DP_QUIESCE as well, to hold
//! the process in kernel space until any timer ticks due to the
//! set_dataplane() call have completed.
//! 
//! DP_DEBUG
//! Debug dataplane interrupts, so that if any interrupt source
//! attempts to involve a dataplane cpu, a kernel message and stack
//! backtrace will be generated on the console.  As this warning is a
//! slow event, it may make sense to avoid this mode in production code
//! to avoid making any possible interrupts even more heavyweight.
//! Setting this flag automatically sets DP_QUIESCE as well.
//! 
//! @param flags the specific semantics being requested
//!
//! @return -1 on failure (setting errno), else 0.
//!
extern int
tmc_cpus_set_dataplane(int flags) __asm__("set_dataplane");


//! Get the @e online CPUs.
//!
//! The @e online CPU set includes all CPUs on which Linux tasks can
//! run.  Thus, it excludes CPUs that are dedicated for use by the
//! hypervisor or bare metal environments.
//!
//! @param s The CPU set to be filled.
//!
//! @return -1 on failure (setting errno), else 0.
//!
extern int
tmc_cpus_get_online_cpus(cpu_set_t* s);

//! Get the @e dataplane CPUs.
//!
//! Dataplane CPUs use Linux kernel code that minimizes the number of
//! system interrupts in cases where a single task is homed on a CPU.
//! The dataplane CPU set is configured via the "dataplane=N-M,..."
//! boot argument passed to the Linux kernel.
//!
//! @param s The CPU set to be filled.
//!
//! @return -1 on failure (setting errno), else 0.
//!
extern int
tmc_cpus_get_dataplane_cpus(cpu_set_t* s);


//! Get the width of the grid.
//!
//! @return The number of tiles in the X dimension of the Linux CPU grid,
//! or -1 on failure (setting errno).
//!
extern int
tmc_cpus_grid_width(void);

//! Get the height of the grid.
//!
//! @return The number of tiles in the Y dimension of the Linux CPU grid,
//! or -1 on failure (setting errno).
//!
extern int
tmc_cpus_grid_height(void);

//! Get the total number of tiles in the grid.
//!
//! @return The total number of tiles in the Linux CPU grid,
//! or -1 on failure (setting errno).
//!
extern int
tmc_cpus_grid_total(void);


//! Determine if the grid contains a specific tile.
//!
//! This function verifies that the specified tile coordinate is
//! legal, and does not check if the tile is actually online.
//!
//! @param x The tile's X coordinate.
//! @param y The tile's Y coordinate.
//!
//! @return 1 if the Linux CPU grid contains the specified tile, else 0. 
//!
static __inline int
tmc_cpus_grid_contains_tile(unsigned int x, unsigned int y)
{
  return (x < tmc_cpus_grid_width() && y < tmc_cpus_grid_height());
}


//! Determine if the grid contains a specific CPU.
//!
//! This function verifies that the specified CPU number is legal, and
//! does not check if the CPU is actually online.
//!
//! @param cpu A CPU number.
//!
//! @return 1 if the Linux CPU grid contains the specified CPU, else 0. 
//!
static __inline int
tmc_cpus_grid_contains_cpu(unsigned int cpu)
{
  return (cpu < tmc_cpus_grid_total());
}


//! Convert a tile (assumed to be contained by the grid) into a CPU.
//!
//! @param x The tile's X coordinate.
//! @param y The tile's Y coordinate.
//!
//! @return The CPU number corresponding to the provided tile coordinates.
//!
static __inline unsigned int
tmc_cpus_grid_tile_to_cpu(unsigned int x, unsigned int y)
{
  return x + y * tmc_cpus_grid_width();
}


//! Convert a CPU (assumed to be contained by the grid) into a tile.
//!
//! @param cpu A CPU number
//! @param x Filled with the X coordinate for @c cpu.
//! @param y Filled with the Y coordinate for @c cpu.
//!
static __inline void
tmc_cpus_grid_cpu_to_tile(unsigned int cpu, unsigned int* x, unsigned int* y)
{
  unsigned int grid_width = tmc_cpus_grid_width();
  *x = cpu % grid_width;
  *y = cpu / grid_width;
}



//! Add all the CPUs in the grid to a CPU set.
//!
//! @param s The CPU set to which CPUs are added.
//!
extern void
tmc_cpus_grid_add_all(cpu_set_t* s);



//! Add a rectangle of CPUs to a CPU set.
//!
//! @param s The CPU set to which CPUs are added.
//! @param x Minimum X coordinate of CPU rectangle.
//! @param y Minimum Y coordinate of CPU rectangle.
//! @param w Width (x dimension) of CPU rectangle.
//! @param h Height (y dimension) of CPU rectangle.
//!
//! @return 0 on success.  On failure, returns -1 and sets errno to
//!   EINVAL if the provided coordinates do not fit in the Linux CPU
//!   grid.
//!
extern int
tmc_cpus_grid_add_rect(cpu_set_t* s, unsigned int x, unsigned int y,
                       unsigned int w, unsigned int h);


//! Remove a rectangle of CPUs from a CPU set.
//!
//! @param s The CPU set from which CPUs are removed.
//! @param x Minimum X coordinate of CPU rectangle.
//! @param y Minimum Y coordinate of CPU rectangle.
//! @param w Width (x dimension) of CPU rectangle.
//! @param h Height (y dimension) of CPU rectangle.
//!
//! @return 0 on success.  On failure, returns -1 and sets errno to
//!   EINVAL if the provided coordinates do not fit in the Linux CPU
//!   grid.
//!
extern int
tmc_cpus_grid_remove_rect(cpu_set_t* s, unsigned int x, unsigned int y,
                          unsigned int w, unsigned int h);


//! Determine the bounding rectangle for a CPU set.
//!
//! @param s The CPU set.
//! @param xp The X coordinate of the resulting rectangle.
//! @param yp The Y coordinate of the resulting rectangle.
//! @param wp The width of the resulting rectangle.
//! @param hp The height of the resulting rectangle.
//!
//! @return -1 on failure (setting errno), else 0.
//!
extern int
tmc_cpus_grid_bounding_rect(const cpu_set_t* s,
                            unsigned int* xp, unsigned int* yp,
                            unsigned int* wp, unsigned int* hp);

__END_DECLS

#endif // __TMC_CPUS_H__

//! @}
