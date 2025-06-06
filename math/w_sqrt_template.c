/* Wrapper to set errno for sqrt.
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
   License along with the GNU C Library; if not, see
   <https://www.gnu.org/licenses/>.  */

/* Only build wrappers from the templates for the types that define the macro
   below.  This macro is set in math-type-macros-<type>.h in sysdeps/generic
   for each floating-point type.  */
#if __USE_WRAPPER_TEMPLATE

# define NO_MATH_REDIRECT
# include <errno.h>
# include <fenv.h>
# define dsqrtl __hide_dsqrtl
# define f32xsqrtf64 __hide_f32xsqrtf64
# define f64xsqrtf128 __hide_f64xsqrtf128
# include <math.h>
# undef dsqrtl
# undef f32xsqrtf64
# undef f64xsqrtf128
# include <math_private.h>
# include <math-narrow-alias.h>

FLOAT
M_DECL_FUNC (__sqrt) (FLOAT x)
{
  if (__glibc_unlikely (isless (x, M_LIT (0.0))))
    /* Domain error: sqrt(x<-0).  */
    __set_errno (EDOM);
  return M_SUF (__ieee754_sqrt) (x);
}
declare_mgen_alias (__sqrt, sqrt)
declare_mgen_alias_narrow (__sqrt, sqrt)

#endif /* __USE_WRAPPER_TEMPLATE.  */
