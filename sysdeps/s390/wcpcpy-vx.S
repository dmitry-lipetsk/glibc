/* Vector optimized 32/64 bit S/390 version of wcpcpy.
   Copyright (C) 2015-2025 Free Software Foundation, Inc.
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

#include <ifunc-wcpcpy.h>
#if HAVE_WCPCPY_Z13

# include "sysdep.h"
# include "asm-syntax.h"

	.text

/* wchar_t * wcpcpy (const wchar_t *dest, const wchar_t *src)
   Copy string src to dest returning a pointer to its end.

   Register usage:
   -r0=border-len for switching to vector-instructions
   -r1=tmp
   -r2=dest and return value
   -r3=src
   -r4=tmp
   -r5=current_len
   -v16=part of src
   -v17=index of zero
   -v18=part of src
*/
ENTRY(WCPCPY_Z13)
	.machine "z13"
	.machinemode "zarch_nohighgprs"

	vlbb	%v16,0(%r3),6	/* Load s until next 4k-byte boundary.  */
	lcbb	%r1,0(%r3),6	/* Get bytes to 4k-byte boundary or 16.  */

	tmll	%r3,3		/* Test if s is 4-byte aligned?  */
	jne	.Lfallback	/* And use common-code variant if not.  */

	vfenezf	%v17,%v16,%v16	/* Find element not equal with zero search.  */
	vlgvb	%r5,%v17,7	/* Load zero index or 16 if not found.  */
	clrjl	%r5,%r1,.Lfound_align /* If found zero within loaded bytes,
					 copy bytes before and return.  */

	/* Align s to 16 byte.  */
	risbgn	%r4,%r3,60,128+63,0 /* %r3 = bits 60-63 of %r2 'and' 15.  */
	lghi	%r5,15		/* current_len = 15.  */
	slr	%r5,%r4		/* Compute highest index to 16byte boundary.  */

	vstl	%v16,%r5,0(%r2)	/* Copy loaded characters - no zero.  */
	ahi	%r5,1		/* Start loop at next character.  */

	/* Find zero in 16byte aligned loop.  */
.Lloop:
	vl	%v16,0(%r5,%r3)	/* Load s.  */
	vfenezfs %v17,%v16,%v16	/* Find element not equal with zero search.  */
	je	.Lfound_v16_0	/* Jump away if zero was found.  */
	vl	%v18,16(%r5,%r3) /* Load next part of s.  */
	vst	%v16,0(%r5,%r2)	/* Store previous part without zero to dst.  */
	vfenezfs %v17,%v18,%v18
	je	.Lfound_v18_16
	vl	%v16,32(%r5,%r3)
	vst	%v18,16(%r5,%r2)
	vfenezfs %v17,%v16,%v16
	je	.Lfound_v16_32
	vl	%v18,48(%r5,%r3)
	vst	%v16,32(%r5,%r2)
	vfenezfs %v17,%v18,%v18
	je	.Lfound_v18_48
	vst	%v18,48(%r5,%r2)

	aghi	%r5,64
	j	.Lloop		/* No zero found -> loop.  */

.Lfound_v16_32:
	aghi	%r5,32
.Lfound_v16_0:
	la	%r3,0(%r5,%r2)
	vlgvb	%r1,%v17,7	/* Load byte index of zero.  */
	aghi	%r1,3		/* Also copy remaining bytes of zero.  */
	vstl	%v16,%r1,0(%r3)	/* Copy characters including zero.  */
	lay	%r2,-3(%r1,%r3)	/* Return pointer to zero.  */
	br	%r14

.Lfound_v18_48:
	aghi	%r5,32
.Lfound_v18_16:
	la	%r3,16(%r5,%r2)
	vlgvb	%r1,%v17,7	/* Load byte index of zero.  */
	aghi	%r1,3		/* Also copy remaining bytes of zero.  */
	vstl	%v18,%r1,0(%r3)	/* Copy characters including zero.  */
	lay	%r2,-3(%r1,%r3)	/* Return pointer to zero.  */
	br	%r14

.Lfound_align:
	aghi	%r5,3		/* Also copy remaining bytes of zero.  */
	vstl	%v16,%r5,0(%r2)	/* Copy characters including zero.  */
	lay	%r2,-3(%r5,%r2)	/* Return pointer to zero.  */
	br	%r14

.Lfallback:
	jg	WCPCPY_C
END(WCPCPY_Z13)

# if ! HAVE_WCPCPY_IFUNC
strong_alias (WCPCPY_Z13, __wcpcpy)
weak_alias (__wcpcpy, wcpcpy)
# endif
#endif
