/* POSIX-specific extra functions.
   Copyright (C) 2016-2025 Free Software Foundation, Inc.
   This file is part of the GNU C Library.

   The GNU C Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2.1 of the License, or (at your option) any later version.

   The GNU C Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with the GNU C Library; if not, see
   <https://www.gnu.org/licenses/>.  */

/* These wrapper functions use POSIX types and therefore cannot be
   declared in <support/support.h>.  */

#ifndef SUPPORT_XUNISTD_H
#define SUPPORT_XUNISTD_H

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

__BEGIN_DECLS

struct statx;

pid_t xfork (void);
pid_t xwaitpid (pid_t, int *status, int flags);
void xpipe (int[2]);
void xdup2 (int, int);
int xdup (int);
int xopen (const char *path, int flags, mode_t);
void support_check_stat_fd (const char *name, int fd, int result);
void support_check_stat_path (const char *name, const char *path, int result);
#define xstat(path, st) \
  (support_check_stat_path ("stat", (path), stat ((path), (st))))
#define xfstat(fd, st) \
  (support_check_stat_fd ("fstat", (fd), fstat ((fd), (st))))
#define xlstat(path, st) \
  (support_check_stat_path ("lstat", (path), lstat ((path), (st))))
#define xstat64(path, st) \
  (support_check_stat_path ("stat64", (path), stat64 ((path), (st))))
#define xfstat64(fd, st) \
  (support_check_stat_fd ("fstat64", (fd), fstat64 ((fd), (st))))
#define xlstat64(path, st) \
  (support_check_stat_path ("lstat64", (path), lstat64 ((path), (st))))
void xstatx (int, const char *, int, unsigned int, struct statx *);
void xmkdir (const char *path, mode_t);
void xchroot (const char *path);
void xunlink (const char *path);
long xsysconf (int name);
long long xlseek (int fd, long long offset, int whence);
void xftruncate (int fd, long long length);
void xsymlink (const char *target, const char *linkpath);
void xchdir (const char *path);
void xfchmod (int fd, mode_t mode);
void xchmod (const char *pathname, mode_t mode);
void xmkfifo (const char *pathname, mode_t mode);

/* Equivalent of "mkdir -p".  */
void xmkdirp (const char *, mode_t);

/* Read the link at PATH.  The caller should free the returned string
   with free.  */
char *xreadlink (const char *path);

/* Close the file descriptor.  Ignore EINTR errors, but terminate the
   process on other errors.  */
void xclose (int);

/* Write the buffer.  Retry on short writes.  */
void xwrite (int, const void *, size_t);

/* Read to buffer.  Retry on short reads.  */
void xread (int, void *, size_t);

/* Invoke mmap with a zero file offset.  */
void *xmmap (void *addr, size_t length, int prot, int flags, int fd);
void xmprotect (void *addr, size_t length, int prot);
void xmunmap (void *addr, size_t length);

ssize_t xcopy_file_range(int fd_in, loff_t *off_in, int fd_out,
			 loff_t *off_out, size_t len, unsigned int flags);

__END_DECLS

#endif /* SUPPORT_XUNISTD_H */
