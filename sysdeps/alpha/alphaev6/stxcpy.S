/* Copyright (C) 2000-2025 Free Software Foundation, Inc.
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

/* Copy a null-terminated string from SRC to DST.

   This is an internal routine used by strcpy, stpcpy, and strcat.
   As such, it uses special linkage conventions to make implementation
   of these public functions more efficient.

   On input:
	t9 = return address
	a0 = DST
	a1 = SRC

   On output:
	t8  = bitmask (with one bit set) indicating the last byte written
	a0  = unaligned address of the last *word* written

   Furthermore, v0, a3-a5, t11, and t12 are untouched.
*/


#include <sysdep.h>

	.arch ev6
	.set noat
	.set noreorder

	.text
	.type	__stxcpy, @function
	.globl	__stxcpy
	.usepv	__stxcpy, no

	cfi_startproc
	cfi_return_column (t9)

	/* On entry to this basic block:
	   t0 == the first destination word for masking back in
	   t1 == the first source word.  */
	.align 4
stxcpy_aligned:
	/* Create the 1st output word and detect 0's in the 1st input word.  */
	lda	t2, -1		# E : build a mask against false zero
	mskqh	t2, a1, t2	# U :   detection in the src word (stall)
	mskqh	t1, a1, t3	# U :
	ornot	t1, t2, t2	# E : (stall)

	mskql	t0, a1, t0	# U : assemble the first output word
	cmpbge	zero, t2, t10	# E : bits set iff null found
	or	t0, t3, t1	# E : (stall)
	bne	t10, $a_eos	# U : (stall)

	/* On entry to this basic block:
	   t0 == the first destination word for masking back in
	   t1 == a source word not containing a null.  */
	/* Nops here to separate store quads from load quads */

$a_loop:
	stq_u	t1, 0(a0)	# L :
	addq	a0, 8, a0	# E :
	nop
	nop

	ldq_u	t1, 0(a1)	# L : Latency=3
	addq	a1, 8, a1	# E :
	cmpbge	zero, t1, t10	# E : (3 cycle stall)
	beq	t10, $a_loop	# U : (stall for t10)

	/* Take care of the final (partial) word store.
	   On entry to this basic block we have:
	   t1 == the source word containing the null
	   t10 == the cmpbge mask that found it.  */
$a_eos:
	negq	t10, t6		# E : find low bit set
	and	t10, t6, t8	# E : (stall)
	/* For the sake of the cache, don't read a destination word
	   if we're not going to need it.  */
	and	t8, 0x80, t6	# E : (stall)
	bne	t6, 1f		# U : (stall)

	/* We're doing a partial word store and so need to combine
	   our source and original destination words.  */
	ldq_u	t0, 0(a0)	# L : Latency=3
	subq	t8, 1, t6	# E :
	zapnot	t1, t6, t1	# U : clear src bytes >= null (stall)
	or	t8, t6, t10	# E : (stall)

	zap	t0, t10, t0	# E : clear dst bytes <= null
	or	t0, t1, t1	# E : (stall)
	nop
	nop

1:	stq_u	t1, 0(a0)	# L :
	ret	(t9)		# L0 : Latency=3
	nop
	nop

	.align 4
__stxcpy:
	/* Are source and destination co-aligned?  */
	xor	a0, a1, t0	# E :
	unop			# E :
	and	t0, 7, t0	# E : (stall)
	bne	t0, $unaligned	# U : (stall)

	/* We are co-aligned; take care of a partial first word.  */
	ldq_u	t1, 0(a1)		# L : load first src word
	and	a0, 7, t0		# E : take care not to load a word ...
	addq	a1, 8, a1		# E :
	beq	t0, stxcpy_aligned	# U : ... if we wont need it (stall)

	ldq_u	t0, 0(a0)	# L :
	br	stxcpy_aligned	# L0 : Latency=3
	nop
	nop


/* The source and destination are not co-aligned.  Align the destination
   and cope.  We have to be very careful about not reading too much and
   causing a SEGV.  */

	.align 4
$u_head:
	/* We know just enough now to be able to assemble the first
	   full source word.  We can still find a zero at the end of it
	   that prevents us from outputting the whole thing.

	   On entry to this basic block:
	   t0 == the first dest word, for masking back in, if needed else 0
	   t1 == the low bits of the first source word
	   t6 == bytemask that is -1 in dest word bytes */

	ldq_u	t2, 8(a1)	# L :
	addq	a1, 8, a1	# E :
	extql	t1, a1, t1	# U : (stall on a1)
	extqh	t2, a1, t4	# U : (stall on a1)

	mskql	t0, a0, t0	# U :
	or	t1, t4, t1	# E :
	mskqh	t1, a0, t1	# U : (stall on t1)
	or	t0, t1, t1	# E : (stall on t1)

	or	t1, t6, t6	# E :
	cmpbge	zero, t6, t10	# E : (stall)
	lda	t6, -1		# E : for masking just below
	bne	t10, $u_final	# U : (stall)

	mskql	t6, a1, t6		# U : mask out the bits we have
	or	t6, t2, t2		# E :   already extracted before (stall)
	cmpbge	zero, t2, t10		# E :   testing eos (stall)
	bne	t10, $u_late_head_exit	# U : (stall)

	/* Finally, we've got all the stupid leading edge cases taken care
	   of and we can set up to enter the main loop.  */

	stq_u	t1, 0(a0)	# L : store first output word
	addq	a0, 8, a0	# E :
	extql	t2, a1, t0	# U : position ho-bits of lo word
	ldq_u	t2, 8(a1)	# U : read next high-order source word

	addq	a1, 8, a1	# E :
	cmpbge	zero, t2, t10	# E : (stall for t2)
	nop			# E :
	bne	t10, $u_eos	# U : (stall)

	/* Unaligned copy main loop.  In order to avoid reading too much,
	   the loop is structured to detect zeros in aligned source words.
	   This has, unfortunately, effectively pulled half of a loop
	   iteration out into the head and half into the tail, but it does
	   prevent nastiness from accumulating in the very thing we want
	   to run as fast as possible.

	   On entry to this basic block:
	   t0 == the shifted high-order bits from the previous source word
	   t2 == the unshifted current source word

	   We further know that t2 does not contain a null terminator.  */

	.align 3
$u_loop:
	extqh	t2, a1, t1	# U : extract high bits for current word
	addq	a1, 8, a1	# E : (stall)
	extql	t2, a1, t3	# U : extract low bits for next time (stall)
	addq	a0, 8, a0	# E :

	or	t0, t1, t1	# E : current dst word now complete
	ldq_u	t2, 0(a1)	# L : Latency=3 load high word for next time
	stq_u	t1, -8(a0)	# L : save the current word (stall)
	mov	t3, t0		# E :

	cmpbge	zero, t2, t10	# E : test new word for eos
	beq	t10, $u_loop	# U : (stall)
	nop
	nop

	/* We've found a zero somewhere in the source word we just read.
	   If it resides in the lower half, we have one (probably partial)
	   word to write out, and if it resides in the upper half, we
	   have one full and one partial word left to write out.

	   On entry to this basic block:
	   t0 == the shifted high-order bits from the previous source word
	   t2 == the unshifted current source word.  */
$u_eos:
	extqh	t2, a1, t1	# U :
	or	t0, t1, t1	# E : first (partial) source word complete (stall)
	cmpbge	zero, t1, t10	# E : is the null in this first bit? (stall)
	bne	t10, $u_final	# U : (stall)

$u_late_head_exit:
	stq_u	t1, 0(a0)	# L : the null was in the high-order bits
	addq	a0, 8, a0	# E :
	extql	t2, a1, t1	# U :
	cmpbge	zero, t1, t10	# E : (stall)

	/* Take care of a final (probably partial) result word.
	   On entry to this basic block:
	   t1 == assembled source word
	   t10 == cmpbge mask that found the null.  */
$u_final:
	negq	t10, t6		# E : isolate low bit set
	and	t6, t10, t8	# E : (stall)
	and	t8, 0x80, t6	# E : avoid dest word load if we can (stall)
	bne	t6, 1f		# U : (stall)

	ldq_u	t0, 0(a0)	# E :
	subq	t8, 1, t6	# E :
	or	t6, t8, t10	# E : (stall)
	zapnot	t1, t6, t1	# U : kill source bytes >= null (stall)

	zap	t0, t10, t0	# U : kill dest bytes <= null (2 cycle data stall)
	or	t0, t1, t1	# E : (stall)
	nop
	nop

1:	stq_u	t1, 0(a0)	# L :
	ret	(t9)		# L0 : Latency=3
	nop
	nop

	/* Unaligned copy entry point.  */
	.align 4
$unaligned:

	ldq_u	t1, 0(a1)	# L : load first source word
	and	a0, 7, t4	# E : find dest misalignment
	and	a1, 7, t5	# E : find src misalignment
	/* Conditionally load the first destination word and a bytemask
	   with 0xff indicating that the destination byte is sacrosanct.  */
	mov	zero, t0	# E :

	mov	zero, t6	# E :
	beq	t4, 1f		# U :
	ldq_u	t0, 0(a0)	# L :
	lda	t6, -1		# E :

	mskql	t6, a0, t6	# U :
	nop
	nop
	nop
1:
	subq	a1, t4, a1	# E : sub dest misalignment from src addr
	/* If source misalignment is larger than dest misalignment, we need
	   extra startup checks to avoid SEGV.  */
	cmplt	t4, t5, t8	# E :
	beq	t8, $u_head	# U :
	lda	t2, -1		# E : mask out leading garbage in source

	mskqh	t2, t5, t2	# U :
	ornot	t1, t2, t3	# E : (stall)
	cmpbge	zero, t3, t10	# E : is there a zero? (stall)
	beq	t10, $u_head	# U : (stall)

	/* At this point we've found a zero in the first partial word of
	   the source.  We need to isolate the valid source data and mask
	   it into the original destination data.  (Incidentally, we know
	   that we'll need at least one byte of that original dest word.) */

	ldq_u	t0, 0(a0)	# L :
	negq	t10, t6		# E : build bitmask of bytes <= zero
	and	t6, t10, t8	# E : (stall)
	and	a1, 7, t5	# E :

	subq	t8, 1, t6	# E :
	or	t6, t8, t10	# E : (stall)
	srl	t8, t5, t8	# U : adjust final null return value
	zapnot	t2, t10, t2	# U : prepare source word; mirror changes (stall)

	and	t1, t2, t1	# E : to source validity mask
	extql	t2, a1, t2	# U :
	extql	t1, a1, t1	# U : (stall)
	andnot	t0, t2, t0	# .. e1 : zero place for source to reside (stall)

	or	t0, t1, t1	# e1    : and put it there
	stq_u	t1, 0(a0)	# .. e0 : (stall)
	ret	(t9)		# e1    :

	cfi_endproc
