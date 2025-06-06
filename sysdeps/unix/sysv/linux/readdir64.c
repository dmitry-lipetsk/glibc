/* Read a directory.  Linux LFS version.
   Copyright (C) 1997-2025 Free Software Foundation, Inc.
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

/* When _DIRENT_MATCHES_DIRENT64 is defined we can alias 'readdir64' to
   'readdir'.  However the function signatures are not equal due
   different return types, so we need to suppress {__}readdir so weak
   and strong alias do not throw conflicting types errors.  */
#define readdir   __no_readdir_decl
#define __readdir __no___readdir_decl
#include <dirent.h>
#undef __readdir
#undef readdir

/* Read a directory entry from DIRP.  No locking.  */
static struct dirent64 *
__readdir64_unlocked (DIR *dirp)
{
  struct dirent64 *dp;
  int saved_errno = errno;

  if (dirp->offset >= dirp->size)
    {
      /* We've emptied out our buffer.  Refill it.  */

      size_t maxread = dirp->allocation;
      ssize_t bytes;

      bytes = __getdents64 (dirp->fd, dirp->data, maxread);
      if (bytes <= 0)
	{
	  /* Linux may fail with ENOENT on some file systems if the
	     directory inode is marked as dead (deleted).  POSIX
	     treats this as a regular end-of-directory condition, so
	     do not set errno in that case, to indicate success.  */
	  if (bytes == 0 || errno == ENOENT)
	    __set_errno (saved_errno);
	  return NULL;
	}
      dirp->size = (size_t) bytes;

      /* Reset the offset into the buffer.  */
      dirp->offset = 0;
    }

  dp = (struct dirent64 *) &dirp->data[dirp->offset];
  dirp->offset += dp->d_reclen;
  dirp->filepos = dp->d_off;

  return dp;
}

/* Read a directory entry from DIRP.  */
struct dirent64 *
__readdir64 (DIR *dirp)
{
  __libc_lock_lock (dirp->lock);
  struct dirent64 *dp = __readdir64_unlocked (dirp);
  __libc_lock_unlock (dirp->lock);
  return dp;
}
libc_hidden_def (__readdir64)

#if _DIRENT_MATCHES_DIRENT64
strong_alias (__readdir64, __readdir)
weak_alias (__readdir64, readdir64)
weak_alias (__readdir64, readdir)
#else
/* The compat code expects the 'struct direct' with d_ino being a __ino_t
   instead of __ino64_t.  */
# include <shlib-compat.h>
# if IS_IN(rtld)
weak_alias (__readdir64, readdir64)
# else
versioned_symbol (libc, __readdir64, readdir64, GLIBC_2_2);
# endif
# if SHLIB_COMPAT(libc, GLIBC_2_1, GLIBC_2_2)
#  include <olddirent.h>

attribute_compat_text_section
struct __old_dirent64 *
__old_readdir64 (DIR *dirp)
{
  struct __old_dirent64 *dp;
  int saved_errno = errno;

  __libc_lock_lock (dirp->lock);

  while (1)
    {
      errno = 0;
      struct dirent64 *newdp = __readdir64_unlocked (dirp);
      if (newdp == NULL)
	{
	  if (errno == 0 && dirp->errcode != 0)
	    __set_errno (dirp->errcode);
	  else if (errno == 0)
	    __set_errno (saved_errno);
	  dp = NULL;
	  break;
	}

      /* Convert to the target layout.  Use a separate struct and
	 memcpy to side-step aliasing issues.  */
      struct __old_dirent64 result;
      result.d_ino = newdp->d_ino;
      result.d_off = newdp->d_off;
      result.d_reclen = newdp->d_reclen;
      result.d_type = newdp->d_type;

      /* Check for ino_t overflow.  */
      if (__glibc_unlikely (result.d_ino != newdp->d_ino))
	{
	  dirp->errcode = ENAMETOOLONG;
	  continue;
	}

      /* Overwrite the fixed-sized part.  */
      dp = (struct __old_dirent64 *) newdp;
      memcpy (dp, &result, offsetof (struct __old_dirent64, d_name));

      /* Move  the name.  */
      _Static_assert (offsetof (struct __old_dirent64, d_name)
		      <= offsetof (struct dirent64, d_name),
		      "old struct must be smaller");
      if (offsetof (struct __old_dirent64, d_name)
	  != offsetof (struct dirent64, d_name))
	memmove (dp->d_name, newdp->d_name, strlen (newdp->d_name) + 1);

      __set_errno (saved_errno);
      break;
    }

  __libc_lock_unlock (dirp->lock);
  return dp;
}
libc_hidden_def (__old_readdir64)
compat_symbol (libc, __old_readdir64, readdir64, GLIBC_2_1);
# endif /* SHLIB_COMPAT(libc, GLIBC_2_1, GLIBC_2_2)  */
#endif /* _DIRENT_MATCHES_DIRENT64  */
