/* Copyright (C) 1991-2025 Free Software Foundation, Inc.
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

#include <stdarg.h>
#include <libio/libioP.h>


/* Write formatted output to FP from the format string FORMAT.  */
int
___fprintf_chk (FILE *fp, int flag, const char *format, ...)
{
  /* For flag > 0 (i.e. __USE_FORTIFY_LEVEL > 1) request that %n
     can only come from read-only format strings.  */
  unsigned int mode = (flag > 0) ? PRINTF_FORTIFY : 0;
  va_list ap;
  int ret;

  va_start (ap, format);
  ret = __vfprintf_internal (fp, format, ap, mode);
  va_end (ap);

  return ret;
}
ldbl_hidden_def (___fprintf_chk, __fprintf_chk)
ldbl_strong_alias (___fprintf_chk, __fprintf_chk)
