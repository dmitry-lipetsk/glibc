/* This test checks a memory leak in newlocal function [BZ #25770].
   Copyright The GNU Toolchain Authors.
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
   <http://www.gnu.org/licenses/>.  */

#include <support/check.h>
#include <mcheck.h>
#include <stdio.h>
#include <stdlib.h>
#include <locale.h>

static int
do_test (void)
{
  mtrace ();

  {
    /*  We can use an any valid path here. */
    int const r = setenv ("LOCPATH", ".", 1);

    if (r != 0)
      {
        FAIL_EXIT1 ("putenv: %m");
      }
  }

  {
    locale_t const l = newlocale (1 << LC_CTYPE, "POSIX", NULL);

    if (l == NULL)
      {
        FAIL_EXIT1 ("newlocale failed: %m");
      }

    freelocale (l);
  }

  return 0;
}

#include <support/test-driver.c>
