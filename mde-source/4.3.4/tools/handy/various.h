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

#ifndef TOOLS_HANDY_VARIOUS_H
#define TOOLS_HANDY_VARIOUS_H

#include "common.h"
#include "Buffer.h"

BEGIN_EXTERN_C


//! Call calloc(), or die.
extern void*
calloc_or_die(size_t num, size_t size);

//! Call malloc(), or die.
extern void*
malloc_or_die(size_t size);

//! Call realloc(), or die.
extern void*
realloc_or_die(void* ptr, size_t size);


//! Determine if @param string has @param other as a prefix.
extern bool
has_prefix(const char* string, const char* other);

//! Determine if @param string has @param other as a suffix.
extern bool
has_suffix(const char* string, const char* other);


//! Read the target of the symlink @param path into the buffer @param buf,
//! with size @param size, and terminate, and return the number of chars
//! not including the terminator, if there is room, otherwise, return -1
//! (setting errno, possibly to ENAMETOOLONG).
extern int
readlink_aux(const char* path, char* buf, size_t size);

//! Call "readlink_aux()", or die.
extern int
readlink_aux_or_die(const char* path, char* buf, size_t size);


//! Fill @param buf, with size @param size, with the install directory,
//! plus @param tail, if there is room, or die.
extern void
get_install_path(char* buf, size_t size, const char* tail);

//! Fill @param buf, with size @param size, with the install directory,
//! plus "/tile", plus @param tail, if there is room, or die.
extern void
get_install_tile_path(char* buf, size_t size, const char* tail);


//! Fill "buf" with a canonical version of "path", absolutizing it relative
//! to "base" (or to the current "physical" working directory, if "base" is
//! NULL), handling "." and ".." like "realpath", but not processing symlinks.
extern void
canonicalize_path(Buffer* buf, const char* path, const char* base);


//! Parse @param str as a base 10 int, as if by atoi(), or die.
//! Leading whitespace is allowed.  The entire string must be consumed.
extern int
atoi_or_die(const char* str);

//! Parse @param str as a base 10 uint16_t, as if by atoi(), or die.
//! Leading whitespace is allowed.  The entire string must be consumed.
extern uint16_t
atou16_or_die(const char* str);

//! Parse @param str as a double, as if by atof(), or die.
//! Leading whitespace is allowed.  The entire string must be consumed.
extern double
atod_or_die(const char* str);

//! Parse @param str as a double, as if by atof, with an optional modifier
//! (G, M, or K), multiply in the modifier, and convert it to a uint64_t.  
//! Leading whitespace is allowed.  The entire string must be consumed.
extern uint64_t 
atou64_with_modifier_or_die(const char* str);


//! Set/clear the "close_on_exec" flag on a file descriptor, or die.
extern void
set_close_on_exec_or_die(int fd, bool flag);

//! Set/clear the "blocking" flag on a file descriptor, or die.
extern void
set_blocking_or_die(int fd, bool flag);

//! Set/clear the "delaying" flag on a socket, or die.
extern void
set_delaying_or_die(int fd, bool flag);

//! Set/clear the "keep alive" flag on a socket, or die.
extern void
set_keep_alive_or_die(int fd, bool flag);


//! Call "pipe(fds)", or die.
extern void
pipe_or_die(int fds[2]);

//! Call "socketpair(AF_UNIX, SOCK_STREAM, 0, fds)", or die.
extern void
socketpair_or_die(int fds[2]);


//! Call "mkstemp()", ignoring EINTR.
extern int
mkstemp_boldly(char* path_template);

//! Call "mkstemp_boldly()", or die.
extern int
mkstemp_or_die(char* path_template);


//! Call "open()", ignoring EINTR.
extern int
open_boldly(const char* path, int flags, mode_t mode);

//! Call "open_boldly()", or die.
extern int
open_or_die(const char* path, int flags, mode_t mode);


//! Call "close(fd)", ignoring EINTR.
extern int
close_boldly(int fd);

//! Call "close_boldly(fd)", or die.
extern void
close_or_die(int fd);


//! Call "dup(oldfd)", or die.
extern int
dup_or_die(int oldfd);

//! Call "dup2(oldfd, newfd)", or die.
extern int
dup2_or_die(int oldfd, int newfd);

//! Basically "dup2(oldfd, newfd)" + "close(oldfd)", or die.
extern int
dup2_and_close_or_die(int oldfd, int newfd);


//! Flush the console, then call "fork()", or die.
extern pid_t
fork_or_die(void);


//! Call "waitpid()", ignoring EINTR.
extern pid_t
waitpid_boldly(pid_t pid, int* status, int options);

//! Call "waitpid_boldly()", or die.
extern pid_t
waitpid_or_die(pid_t pid, int* status, int options);


//! Create the directory "path", with mode "0777", if needed.
extern int
create_directory(const char* path);

//! Create the ancestors of "path", with mode "0777", if needed.
extern int
create_ancestors(const char* path);


//! Write @param count bytes from @param buf to @param fd, clearing errno
//! before each attempt, ignoring EINTR, and return the number of bytes
//! written (which may be zero) before any error was encountered.
extern size_t
write_all_bytes(int fd, const void* buf, size_t count);

//! Write @param count bytes from @param buf to @param fd, or die.
//! Do not use this on a non-blocking socket!
extern void
write_all_bytes_or_die(int fd, const void* buf, size_t count);

//! Write @param count bytes from @param buf to @param fd, or until EAGAIN
//! (for non-blocking sockets) or EPIPE (aka EOF) or ECONNRESET, and return
//! the number of bytes written (or -1 for "immediate EOF"), or die on any
//! other error.
extern ssize_t
write_some_bytes_or_die(int fd, const void* buf, size_t count);

//! Write @param count bytes from @param buf to an existing file with pathname 
//! @param path.  Return the number of bytes written before an error was
//! encountered.  If the file was not successfully closed, return 0.
extern ssize_t 
append_to_file_boldly(const char* path, const void* buf, size_t count);

//! Write @param count bytes from @param buf to an existing file with pathname 
//! @param path.  Die if an error occurs.  Do not use this on a non-blocking
//! socket as it uses write_all_bytes_or_die().
extern void
append_to_file_or_die(const char* path, const void* buf, size_t count);

//! Read up to @param count bytes into @param buf from @param fd, or
//! until EAGAIN (for non-blocking sockets) or EOF (treated as EPIPE)
//! or ECONNRESET, and return the number of bytes read (or -1 for
//! "immediate EOF"), or die on any other error.  Partial reads on the
//! underlying descriptor will cause a return of less than @param count bytes.
extern ssize_t
read_uninterrupted_or_die(int fd, void* buf, size_t count);

//! Using read_uninterrupted_or_die(), loop until @param count bytes
//! have been read, EAGAIN is returned, or EOF or an error occurs.
extern ssize_t
read_some_bytes_or_die(int fd, void* buf, size_t count);

//! Read @param count bytes into @param buf from @param fd, or die.
//! Do not use this on a non-blocking socket!
extern void
read_all_bytes_or_die(int fd, void* buf, size_t count);

//! Read up to @param count bytes into @param buf from an existing file
//! with pathname @param path.  Returns the number of bytes read (or 0).
extern size_t 
read_from_file_or_die(const char* path, void* buf, size_t count);


//! Write @param v as eight little endian bytes at @param p.
// ISSUE: Should this return "p"?
extern void
write_uint64(uint8_t* p, uint64_t v);

//! Read eight little endian bytes at @param p.
// ISSUE: Should this modify a "uint64_t*", and return "p"?
extern uint64_t
read_uint64(const uint8_t* p);


//! Write @param v as four little endian bytes at @param p.
// ISSUE: Should this return "p"?
extern void
write_uint(uint8_t* p, uint v);

//! Read four little endian bytes at @param p.
// ISSUE: Should this modify a "uint*", and return "p"?
extern uint
read_uint(const uint8_t* p);


//! Write @param v as two little endian bytes at @param p.
// ISSUE: Should this return "p"?
extern void
write_uint16(uint8_t* p, uint16_t v);

//! Read two little endian bytes at @param p.
// ISSUE: Should this modify a "uint16_t*", and return "p"?
extern uint16_t
read_uint16(const uint8_t* p);


END_EXTERN_C

#endif /* !TOOLS_HANDY_VARIOUS_H */
