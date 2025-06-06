/* lrintf().  RISC-V version.
   Copyright (C) 2017-2025 Free Software Foundation, Inc.
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
   License along with the GNU C Library.  If not, see
   <https://www.gnu.org/licenses/>.  */

#include <math.h>
#include <libm-alias-float.h>
#include <stdint.h>

#if __WORDSIZE == 64
# define OP "fcvt.l.s"
#elif __WORDSIZE == 32
# define OP "fcvt.w.s"
#else
# error Unsupported
#endif

long int
__lrintf (float x)
{
  long int res;
  asm (OP "\t%0, %1" : "=r" (res) : "f" (x));
  return res;
}

libm_alias_float (__lrint, lrint)
