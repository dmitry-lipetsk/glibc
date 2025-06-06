/* Per-thread state.  Linux version.
   Copyright (C) 2020-2025 Free Software Foundation, Inc.
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

#ifndef _TLS_INTERNAL_H
#define _TLS_INTERNAL_H 1

#include <stdlib.h>
#include <pthreadP.h>

static inline struct tls_internal_t *
__glibc_tls_internal (void)
{
  return &THREAD_SELF->tls_state;
}

extern void __glibc_tls_internal_free (void) attribute_hidden;

#endif
