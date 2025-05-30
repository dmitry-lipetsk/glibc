/* Floating point output for `printf'.
   Copyright (C) 1995-2025 Free Software Foundation, Inc.

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

/* The gmp headers need some configuration frobs.  */
#define HAVE_ALLOCA 1

#include <array_length.h>
#include <libioP.h>
#include <alloca.h>
#include <ctype.h>
#include <float.h>
#include <gmp-mparam.h>
#include <gmp.h>
#include <ieee754.h>
#include <stdlib/gmp-impl.h>
#include <stdlib/fpioconst.h>
#include <locale/localeinfo.h>
#include <limits.h>
#include <math.h>
#include <printf.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdbool.h>
#include <stdbit.h>
#include <rounding-mode.h>
#include <printf_buffer.h>
#include <printf_buffer_to_file.h>
#include <grouping_iterator.h>

#include <assert.h>

/* We use the GNU MP library to handle large numbers.

   An MP variable occupies a varying number of entries in its array.  We keep
   track of this number for efficiency reasons.  Otherwise we would always
   have to process the whole array.  */
#define MPN_VAR(name) mp_limb_t *name; mp_size_t name##size

#define MPN_ASSIGN(dst,src)						      \
  memcpy (dst, src, (dst##size = src##size) * sizeof (mp_limb_t))
#define MPN_GE(u,v) \
  (u##size > v##size || (u##size == v##size && __mpn_cmp (u, v, u##size) >= 0))

extern mp_size_t __mpn_extract_double (mp_ptr res_ptr, mp_size_t size,
				       int *expt, int *is_neg,
				       double value);
extern mp_size_t __mpn_extract_long_double (mp_ptr res_ptr, mp_size_t size,
					    int *expt, int *is_neg,
					    long double value);


struct hack_digit_param
{
  /* Sign of the exponent.  */
  int expsign;
  /* The type of output format that will be used: 'e'/'E' or 'f'.  */
  int type;
  /* and the exponent.	*/
  int exponent;
  /* The fraction of the floting-point value in question  */
  MPN_VAR(frac);
  /* Scaling factor.  */
  MPN_VAR(scale);
  /* Temporary bignum value.  */
  MPN_VAR(tmp);
};

static char
hack_digit (struct hack_digit_param *p)
{
  mp_limb_t hi;

  if (p->expsign != 0 && p->type == 'f' && p->exponent-- > 0)
    hi = 0;
  else if (p->scalesize == 0)
    {
      hi = p->frac[p->fracsize - 1];
      p->frac[p->fracsize - 1] = __mpn_mul_1 (p->frac, p->frac,
	p->fracsize - 1, 10);
    }
  else
    {
      if (p->fracsize < p->scalesize)
	hi = 0;
      else
	{
	  hi = mpn_divmod (p->tmp, p->frac, p->fracsize,
	    p->scale, p->scalesize);
	  p->tmp[p->fracsize - p->scalesize] = hi;
	  hi = p->tmp[0];

	  p->fracsize = p->scalesize;
	  while (p->fracsize != 0 && p->frac[p->fracsize - 1] == 0)
	    --p->fracsize;
	  if (p->fracsize == 0)
	    {
	      /* We're not prepared for an mpn variable with zero
		 limbs.  */
	      p->fracsize = 1;
	      return '0' + hi;
	    }
	}

      mp_limb_t _cy = __mpn_mul_1 (p->frac, p->frac, p->fracsize, 10);
      if (_cy != 0)
	p->frac[p->fracsize++] = _cy;
    }

  return '0' + hi;
}

/* Version that performs grouping (if INFO->group && THOUSANDS_SEP != 0),
   but not i18n digit translation.

   The output buffer is always multibyte (not wide) at this stage.
   Wide conversion and i18n digit translation happen later, with a
   temporary buffer.  To prepare for that, THOUSANDS_SEP_LENGTH is the
   final length of the thousands separator.  */
static void
__printf_fp_buffer_1 (struct __printf_buffer *buf, locale_t loc,
		      char thousands_sep, char decimal,
		      unsigned int thousands_sep_length,
		      const struct printf_info *info,
		      const void *const *args)
{
  /* The floating-point value to output.  */
  union
    {
      double dbl;
      long double ldbl;
#if __HAVE_DISTINCT_FLOAT128
      _Float128 f128;
#endif
    }
  fpnum;

  /* "NaN" or "Inf" for the special cases.  */
  const char *special = NULL;

  /* Used to determine grouping rules.  */
  int lc_category = info->extra ? LC_MONETARY : LC_NUMERIC;

  /* When _Float128 is enabled in the library and ABI-distinct from long
     double, we need mp_limbs enough for any of them.  */
#if __HAVE_DISTINCT_FLOAT128
# define GREATER_MANT_DIG FLT128_MANT_DIG
#else
# define GREATER_MANT_DIG LDBL_MANT_DIG
#endif
  /* We need just a few limbs for the input before shifting to the right
     position.	*/
  mp_limb_t fp_input[(GREATER_MANT_DIG + BITS_PER_MP_LIMB - 1)
		     / BITS_PER_MP_LIMB];
  /* We need to shift the contents of fp_input by this amount of bits.	*/
  int to_shift = 0;

  struct hack_digit_param p;
  /* Sign of float number.  */
  int is_neg = 0;

  /* General helper (carry limb).  */
  mp_limb_t cy;

  /* Buffer in which we produce the output.  */
  char *wbuffer = NULL;
  /* Flag whether wbuffer and buffer are malloc'ed or not.  */
  int buffer_malloced = 0;

  p.expsign = 0;

#define PRINTF_FP_FETCH(FLOAT, VAR, SUFFIX, MANT_DIG)			\
  {									\
    (VAR) = *(const FLOAT *) args[0];					\
									\
    /* Check for special values: not a number or infinity.  */		\
    if (isnan (VAR))							\
      {									\
	is_neg = signbit (VAR);						\
	if (isupper (info->spec))					\
	  special = "NAN";						\
	else								\
	  special = "nan";						\
      }									\
    else if (isinf (VAR))						\
      {									\
	is_neg = signbit (VAR);						\
	if (isupper (info->spec))					\
	  special = "INF";						\
	else								\
	  special = "inf";						\
      }									\
    else								\
      {									\
	p.fracsize = __mpn_extract_##SUFFIX				\
		     (fp_input, array_length (fp_input),		\
		      &p.exponent, &is_neg, VAR);			\
	to_shift = 1 + p.fracsize * BITS_PER_MP_LIMB - MANT_DIG;	\
      }									\
  }

  /* Fetch the argument value.	*/
#if __HAVE_DISTINCT_FLOAT128
  if (info->is_binary128)
    PRINTF_FP_FETCH (_Float128, fpnum.f128, float128, FLT128_MANT_DIG)
  else
#endif
#ifndef __NO_LONG_DOUBLE_MATH
  if (info->is_long_double && sizeof (long double) > sizeof (double))
    PRINTF_FP_FETCH (long double, fpnum.ldbl, long_double, LDBL_MANT_DIG)
  else
#endif
    PRINTF_FP_FETCH (double, fpnum.dbl, double, DBL_MANT_DIG)

#undef PRINTF_FP_FETCH

  if (special)
    {
      int width = info->width;

      if (is_neg || info->showsign || info->space)
	--width;
      width -= 3;

      if (!info->left)
	__printf_buffer_pad (buf, ' ', width);

      if (is_neg)
	__printf_buffer_putc (buf, '-');
      else if (info->showsign)
	__printf_buffer_putc (buf, '+');
      else if (info->space)
	__printf_buffer_putc (buf, ' ');

      __printf_buffer_puts (buf, special);

      if (info->left)
	__printf_buffer_pad (buf, ' ', width);

      return;
    }


  /* We need three multiprecision variables.  Now that we have the p.exponent
     of the number we can allocate the needed memory.  It would be more
     efficient to use variables of the fixed maximum size but because this
     would be really big it could lead to memory problems.  */
  {
    mp_size_t bignum_size = ((abs (p.exponent) + BITS_PER_MP_LIMB - 1)
			     / BITS_PER_MP_LIMB
			     + (GREATER_MANT_DIG / BITS_PER_MP_LIMB > 2
				? 8 : 4))
			    * sizeof (mp_limb_t);
    p.frac = (mp_limb_t *) alloca (bignum_size);
    p.tmp = (mp_limb_t *) alloca (bignum_size);
    p.scale = (mp_limb_t *) alloca (bignum_size);
  }

  /* We now have to distinguish between numbers with positive and negative
     exponents because the method used for the one is not applicable/efficient
     for the other.  */
  p.scalesize = 0;
  if (p.exponent > 2)
    {
      /* |FP| >= 8.0.  */
      int scaleexpo = 0;
      int explog;
#if __HAVE_DISTINCT_FLOAT128
      if (info->is_binary128)
	explog = FLT128_MAX_10_EXP_LOG;
      else
	explog = LDBL_MAX_10_EXP_LOG;
#else
      explog = LDBL_MAX_10_EXP_LOG;
#endif
      int exp10 = 0;
      const struct mp_power *powers = &_fpioconst_pow10[explog + 1];
      int cnt_h, cnt_l, i;

      if ((p.exponent + to_shift) % BITS_PER_MP_LIMB == 0)
	{
	  MPN_COPY_DECR (p.frac + (p.exponent + to_shift) / BITS_PER_MP_LIMB,
			 fp_input, p.fracsize);
	  p.fracsize += (p.exponent + to_shift) / BITS_PER_MP_LIMB;
	}
      else
	{
	  cy = __mpn_lshift (p.frac
			     + (p.exponent + to_shift) / BITS_PER_MP_LIMB,
			     fp_input, p.fracsize,
			     (p.exponent + to_shift) % BITS_PER_MP_LIMB);
	  p.fracsize += (p.exponent + to_shift) / BITS_PER_MP_LIMB;
	  if (cy)
	    p.frac[p.fracsize++] = cy;
	}
      MPN_ZERO (p.frac, (p.exponent + to_shift) / BITS_PER_MP_LIMB);

      assert (powers > &_fpioconst_pow10[0]);
      do
	{
	  --powers;

	  /* The number of the product of two binary numbers with n and m
	     bits respectively has m+n or m+n-1 bits.	*/
	  if (p.exponent >= scaleexpo + powers->p_expo - 1)
	    {
	      if (p.scalesize == 0)
		{
#if __HAVE_DISTINCT_FLOAT128
		  if ((FLT128_MANT_DIG
			    > _FPIO_CONST_OFFSET * BITS_PER_MP_LIMB)
			   && info->is_binary128)
		    {
#define _FLT128_FPIO_CONST_SHIFT \
  (((FLT128_MANT_DIG + BITS_PER_MP_LIMB - 1) / BITS_PER_MP_LIMB) \
   - _FPIO_CONST_OFFSET)
		      /* 64bit const offset is not enough for
			 IEEE 854 quad long double (_Float128).  */
		      p.tmpsize = powers->arraysize + _FLT128_FPIO_CONST_SHIFT;
		      memcpy (p.tmp + _FLT128_FPIO_CONST_SHIFT,
			      &__tens[powers->arrayoff],
			      p.tmpsize * sizeof (mp_limb_t));
		      MPN_ZERO (p.tmp, _FLT128_FPIO_CONST_SHIFT);
		      /* Adjust p.exponent, as scaleexpo will be this much
			 bigger too.  */
		      p.exponent += _FLT128_FPIO_CONST_SHIFT * BITS_PER_MP_LIMB;
		    }
		  else
#endif /* __HAVE_DISTINCT_FLOAT128 */
#ifndef __NO_LONG_DOUBLE_MATH
		  if (LDBL_MANT_DIG > _FPIO_CONST_OFFSET * BITS_PER_MP_LIMB
		      && info->is_long_double)
		    {
#define _FPIO_CONST_SHIFT \
  (((LDBL_MANT_DIG + BITS_PER_MP_LIMB - 1) / BITS_PER_MP_LIMB) \
   - _FPIO_CONST_OFFSET)
		      /* 64bit const offset is not enough for
			 IEEE quad long double.  */
		      p.tmpsize = powers->arraysize + _FPIO_CONST_SHIFT;
		      memcpy (p.tmp + _FPIO_CONST_SHIFT,
			      &__tens[powers->arrayoff],
			      p.tmpsize * sizeof (mp_limb_t));
		      MPN_ZERO (p.tmp, _FPIO_CONST_SHIFT);
		      /* Adjust p.exponent, as scaleexpo will be this much
			 bigger too.  */
		      p.exponent += _FPIO_CONST_SHIFT * BITS_PER_MP_LIMB;
		    }
		  else
#endif
		    {
		      p.tmpsize = powers->arraysize;
		      memcpy (p.tmp, &__tens[powers->arrayoff],
			      p.tmpsize * sizeof (mp_limb_t));
		    }
		}
	      else
		{
		  cy = __mpn_mul (p.tmp, p.scale, p.scalesize,
				  &__tens[powers->arrayoff
					 + _FPIO_CONST_OFFSET],
				  powers->arraysize - _FPIO_CONST_OFFSET);
		  p.tmpsize = p.scalesize
		    + powers->arraysize - _FPIO_CONST_OFFSET;
		  if (cy == 0)
		    --p.tmpsize;
		}

	      if (MPN_GE (p.frac, p.tmp))
		{
		  int cnt;
		  MPN_ASSIGN (p.scale, p.tmp);
		  cnt = stdc_leading_zeros (p.scale[p.scalesize - 1]);
		  scaleexpo = (p.scalesize - 2) * BITS_PER_MP_LIMB - cnt - 1;
		  exp10 |= 1 << explog;
		}
	    }
	  --explog;
	}
      while (powers > &_fpioconst_pow10[0]);
      p.exponent = exp10;

      /* Optimize number representations.  We want to represent the numbers
	 with the lowest number of bytes possible without losing any
	 bytes. Also the highest bit in the scaling factor has to be set
	 (this is a requirement of the MPN division routines).  */
      if (p.scalesize > 0)
	{
	  /* Determine minimum number of zero bits at the end of
	     both numbers.  */
	  for (i = 0; p.scale[i] == 0 && p.frac[i] == 0; i++)
	    ;

	  /* Determine number of bits the scaling factor is misplaced.	*/
	  cnt_h = stdc_leading_zeros (p.scale[p.scalesize - 1]);

	  if (cnt_h == 0)
	    {
	      /* The highest bit of the scaling factor is already set.	So
		 we only have to remove the trailing empty limbs.  */
	      if (i > 0)
		{
		  MPN_COPY_INCR (p.scale, p.scale + i, p.scalesize - i);
		  p.scalesize -= i;
		  MPN_COPY_INCR (p.frac, p.frac + i, p.fracsize - i);
		  p.fracsize -= i;
		}
	    }
	  else
	    {
	      if (p.scale[i] != 0)
		{
		  cnt_l = stdc_trailing_zeros (p.scale[i]);
		  if (p.frac[i] != 0)
		    {
		      int cnt_l2;
		      cnt_l2 = stdc_trailing_zeros (p.frac[i]);
		      if (cnt_l2 < cnt_l)
			cnt_l = cnt_l2;
		    }
		}
	      else
		cnt_l = stdc_trailing_zeros (p.frac[i]);

	      /* Now shift the numbers to their optimal position.  */
	      if (i == 0 && BITS_PER_MP_LIMB - cnt_h > cnt_l)
		{
		  /* We cannot save any memory.	 So just roll both numbers
		     so that the scaling factor has its highest bit set.  */

		  (void) __mpn_lshift (p.scale, p.scale, p.scalesize, cnt_h);
		  cy = __mpn_lshift (p.frac, p.frac, p.fracsize, cnt_h);
		  if (cy != 0)
		    p.frac[p.fracsize++] = cy;
		}
	      else if (BITS_PER_MP_LIMB - cnt_h <= cnt_l)
		{
		  /* We can save memory by removing the trailing zero limbs
		     and by packing the non-zero limbs which gain another
		     free one. */

		  (void) __mpn_rshift (p.scale, p.scale + i, p.scalesize - i,
				       BITS_PER_MP_LIMB - cnt_h);
		  p.scalesize -= i + 1;
		  (void) __mpn_rshift (p.frac, p.frac + i, p.fracsize - i,
				       BITS_PER_MP_LIMB - cnt_h);
		  p.fracsize -= p.frac[p.fracsize - i - 1] == 0 ? i + 1 : i;
		}
	      else
		{
		  /* We can only save the memory of the limbs which are zero.
		     The non-zero parts occupy the same number of limbs.  */

		  (void) __mpn_rshift (p.scale, p.scale + (i - 1),
				       p.scalesize - (i - 1),
				       BITS_PER_MP_LIMB - cnt_h);
		  p.scalesize -= i;
		  (void) __mpn_rshift (p.frac, p.frac + (i - 1),
				       p.fracsize - (i - 1),
				       BITS_PER_MP_LIMB - cnt_h);
		  p.fracsize -=
		    p.frac[p.fracsize - (i - 1) - 1] == 0 ? i : i - 1;
		}
	    }
	}
    }
  else if (p.exponent < 0)
    {
      /* |FP| < 1.0.  */
      int exp10 = 0;
      int explog;
#if __HAVE_DISTINCT_FLOAT128
      if (info->is_binary128)
	explog = FLT128_MAX_10_EXP_LOG;
      else
	explog = LDBL_MAX_10_EXP_LOG;
#else
      explog = LDBL_MAX_10_EXP_LOG;
#endif
      const struct mp_power *powers = &_fpioconst_pow10[explog + 1];

      /* Now shift the input value to its right place.	*/
      cy = __mpn_lshift (p.frac, fp_input, p.fracsize, to_shift);
      p.frac[p.fracsize++] = cy;
      assert (cy == 1 || (p.frac[p.fracsize - 2] == 0 && p.frac[0] == 0));

      p.expsign = 1;
      p.exponent = -p.exponent;

      assert (powers != &_fpioconst_pow10[0]);
      do
	{
	  --powers;

	  if (p.exponent >= powers->m_expo)
	    {
	      int i, incr, cnt_h, cnt_l;
	      mp_limb_t topval[2];

	      /* The __mpn_mul function expects the first argument to be
		 bigger than the second.  */
	      if (p.fracsize < powers->arraysize - _FPIO_CONST_OFFSET)
		cy = __mpn_mul (p.tmp, &__tens[powers->arrayoff
					    + _FPIO_CONST_OFFSET],
				powers->arraysize - _FPIO_CONST_OFFSET,
				p.frac, p.fracsize);
	      else
		cy = __mpn_mul (p.tmp, p.frac, p.fracsize,
				&__tens[powers->arrayoff + _FPIO_CONST_OFFSET],
				powers->arraysize - _FPIO_CONST_OFFSET);
	      p.tmpsize = p.fracsize + powers->arraysize - _FPIO_CONST_OFFSET;
	      if (cy == 0)
		--p.tmpsize;

	      cnt_h = stdc_leading_zeros (p.tmp[p.tmpsize - 1]);
	      incr = (p.tmpsize - p.fracsize) * BITS_PER_MP_LIMB
		     + BITS_PER_MP_LIMB - 1 - cnt_h;

	      assert (incr <= powers->p_expo);

	      /* If we increased the p.exponent by exactly 3 we have to test
		 for overflow.	This is done by comparing with 10 shifted
		 to the right position.	 */
	      if (incr == p.exponent + 3)
		{
		  if (cnt_h <= BITS_PER_MP_LIMB - 4)
		    {
		      topval[0] = 0;
		      topval[1]
			= ((mp_limb_t) 10) << (BITS_PER_MP_LIMB - 4 - cnt_h);
		    }
		  else
		    {
		      topval[0] = ((mp_limb_t) 10) << (BITS_PER_MP_LIMB - 4);
		      topval[1] = 0;
		      (void) __mpn_lshift (topval, topval, 2,
					   BITS_PER_MP_LIMB - cnt_h);
		    }
		}

	      /* We have to be careful when multiplying the last factor.
		 If the result is greater than 1.0 be have to test it
		 against 10.0.  If it is greater or equal to 10.0 the
		 multiplication was not valid.  This is because we cannot
		 determine the number of bits in the result in advance.  */
	      if (incr < p.exponent + 3
		  || (incr == p.exponent + 3
		      && (p.tmp[p.tmpsize - 1] < topval[1]
			  || (p.tmp[p.tmpsize - 1] == topval[1]
			      && p.tmp[p.tmpsize - 2] < topval[0]))))
		{
		  /* The factor is right.  Adapt binary and decimal
		     exponents.	 */
		  p.exponent -= incr;
		  exp10 |= 1 << explog;

		  /* If this factor yields a number greater or equal to
		     1.0, we must not shift the non-fractional digits down. */
		  if (p.exponent < 0)
		    cnt_h += -p.exponent;

		  /* Now we optimize the number representation.	 */
		  for (i = 0; p.tmp[i] == 0; ++i);
		  if (cnt_h == BITS_PER_MP_LIMB - 1)
		    {
		      MPN_COPY (p.frac, p.tmp + i, p.tmpsize - i);
		      p.fracsize = p.tmpsize - i;
		    }
		  else
		    {
		      cnt_l = stdc_trailing_zeros (p.tmp[i]);

		      /* Now shift the numbers to their optimal position.  */
		      if (i == 0 && BITS_PER_MP_LIMB - 1 - cnt_h > cnt_l)
			{
			  /* We cannot save any memory.	 Just roll the
			     number so that the leading digit is in a
			     separate limb.  */

			  cy = __mpn_lshift (p.frac, p.tmp, p.tmpsize,
			    cnt_h + 1);
			  p.fracsize = p.tmpsize + 1;
			  p.frac[p.fracsize - 1] = cy;
			}
		      else if (BITS_PER_MP_LIMB - 1 - cnt_h <= cnt_l)
			{
			  (void) __mpn_rshift (p.frac, p.tmp + i, p.tmpsize - i,
					       BITS_PER_MP_LIMB - 1 - cnt_h);
			  p.fracsize = p.tmpsize - i;
			}
		      else
			{
			  /* We can only save the memory of the limbs which
			     are zero.	The non-zero parts occupy the same
			     number of limbs.  */

			  (void) __mpn_rshift (p.frac, p.tmp + (i - 1),
					       p.tmpsize - (i - 1),
					       BITS_PER_MP_LIMB - 1 - cnt_h);
			  p.fracsize = p.tmpsize - (i - 1);
			}
		    }
		}
	    }
	  --explog;
	}
      while (powers != &_fpioconst_pow10[1] && p.exponent > 0);
      /* All factors but 10^-1 are tested now.	*/
      if (p.exponent > 0)
	{
	  int cnt_l;

	  cy = __mpn_mul_1 (p.tmp, p.frac, p.fracsize, 10);
	  p.tmpsize = p.fracsize;
	  assert (cy == 0 || p.tmp[p.tmpsize - 1] < 20);

	  cnt_l = stdc_trailing_zeros (p.tmp[0]);
	  if (cnt_l < MIN (4, p.exponent))
	    {
	      cy = __mpn_lshift (p.frac, p.tmp, p.tmpsize,
				 BITS_PER_MP_LIMB - MIN (4, p.exponent));
	      if (cy != 0)
		p.frac[p.tmpsize++] = cy;
	    }
	  else
	    (void) __mpn_rshift (p.frac, p.tmp, p.tmpsize, MIN (4, p.exponent));
	  p.fracsize = p.tmpsize;
	  exp10 |= 1;
	  assert (p.frac[p.fracsize - 1] < 10);
	}
      p.exponent = exp10;
    }
  else
    {
      /* This is a special case.  We don't need a factor because the
	 numbers are in the range of 1.0 <= |fp| < 8.0.  We simply
	 shift it to the right place and divide it by 1.0 to get the
	 leading digit.	 (Of course this division is not really made.)	*/
      assert (0 <= p.exponent && p.exponent < 3
	      && p.exponent + to_shift < BITS_PER_MP_LIMB);

      /* Now shift the input value to its right place.	*/
      cy = __mpn_lshift (p.frac, fp_input, p.fracsize, (p.exponent + to_shift));
      p.frac[p.fracsize++] = cy;
      p.exponent = 0;
    }

  {
    int width = info->width;
    char *wstartp, *wcp;
    size_t chars_needed;
    int expscale;
    int intdig_max, intdig_no = 0;
    int fracdig_min;
    int fracdig_max;
    int dig_max;
    int significant;
    char spec = _tolower (info->spec);

    if (spec == 'e')
      {
	p.type = info->spec;
	intdig_max = 1;
	fracdig_min = fracdig_max = info->prec < 0 ? 6 : info->prec;
	chars_needed = 1 + 1 + (size_t) fracdig_max + 1 + 1 + 4;
	/*	       d   .	 ddd	     e	 +-  ddd  */
	dig_max = INT_MAX;		/* Unlimited.  */
	significant = 1;		/* Does not matter here.  */
      }
    else if (spec == 'f')
      {
	p.type = 'f';
	fracdig_min = fracdig_max = info->prec < 0 ? 6 : info->prec;
	dig_max = INT_MAX;		/* Unlimited.  */
	significant = 1;		/* Does not matter here.  */
	if (p.expsign == 0)
	  {
	    intdig_max = p.exponent + 1;
	    /* This can be really big!	*/  /* XXX Maybe malloc if too big? */
	    chars_needed = (size_t) p.exponent + 1 + 1 + (size_t) fracdig_max;
	  }
	else
	  {
	    intdig_max = 1;
	    chars_needed = 1 + 1 + (size_t) fracdig_max;
	  }
      }
    else
      {
	dig_max = info->prec < 0 ? 6 : (info->prec == 0 ? 1 : info->prec);
	if ((p.expsign == 0 && p.exponent >= dig_max)
	    || (p.expsign != 0 && p.exponent > 4))
	  {
	    if ('g' - 'G' == 'e' - 'E')
	      p.type = 'E' + (info->spec - 'G');
	    else
	      p.type = isupper (info->spec) ? 'E' : 'e';
	    fracdig_max = dig_max - 1;
	    intdig_max = 1;
	    chars_needed = 1 + 1 + (size_t) fracdig_max + 1 + 1 + 4;
	  }
	else
	  {
	    p.type = 'f';
	    intdig_max = p.expsign == 0 ? p.exponent + 1 : 0;
	    fracdig_max = dig_max - intdig_max;
	    /* We need space for the significant digits and perhaps
	       for leading zeros when < 1.0.  The number of leading
	       zeros can be as many as would be required for
	       exponential notation with a negative two-digit
	       p.exponent, which is 4.  */
	    chars_needed = (size_t) dig_max + 1 + 4;
	  }
	fracdig_min = info->alt ? fracdig_max : 0;
	significant = 0;		/* We count significant digits.	 */
      }

    /* Allocate buffer for output.  We need two more because while rounding
       it is possible that we need two more characters in front of all the
       other output.  If the amount of memory we have to allocate is too
       large use `malloc' instead of `alloca'.  */
    if (__glibc_unlikely (chars_needed >= (size_t) -1 - 2
			  || chars_needed < fracdig_max))
      {
	/* Some overflow occurred.  */
	__set_errno (ERANGE);
	__printf_buffer_mark_failed (buf);
	return;
      }
    size_t wbuffer_to_alloc = 2 + chars_needed;
    buffer_malloced = ! __libc_use_alloca (wbuffer_to_alloc);
    if (__builtin_expect (buffer_malloced, 0))
      {
	wbuffer = malloc (wbuffer_to_alloc);
	if (wbuffer == NULL)
	  {
	    /* Signal an error to the caller.  */
	    __printf_buffer_mark_failed (buf);
	    return;
	  }
      }
    else
      wbuffer = alloca (wbuffer_to_alloc);
    wcp = wstartp = wbuffer + 2;	/* Let room for rounding.  */

    /* Do the real work: put digits in allocated buffer.  */
    if (p.expsign == 0 || p.type != 'f')
      {
	assert (p.expsign == 0 || intdig_max == 1);
	while (intdig_no < intdig_max)
	  {
	    ++intdig_no;
	    *wcp++ = hack_digit (&p);
	  }
	significant = 1;
	if (info->alt
	    || fracdig_min > 0
	    || (fracdig_max > 0 && (p.fracsize > 1 || p.frac[0] != 0)))
	  *wcp++ = decimal;
      }
    else
      {
	/* |fp| < 1.0 and the selected p.type is 'f', so put "0."
	   in the buffer.  */
	*wcp++ = '0';
	--p.exponent;
	*wcp++ = decimal;
      }

    /* Generate the needed number of fractional digits.	 */
    int fracdig_no = 0;
    int added_zeros = 0;
    while (fracdig_no < fracdig_min + added_zeros
	   || (fracdig_no < fracdig_max && (p.fracsize > 1 || p.frac[0] != 0)))
      {
	++fracdig_no;
	*wcp = hack_digit (&p);
	if (*wcp++ != '0')
	  significant = 1;
	else if (significant == 0)
	  {
	    ++fracdig_max;
	    if (fracdig_min > 0)
	      ++added_zeros;
	  }
      }

    /* Do rounding.  */
    char last_digit = wcp[-1] != decimal ? wcp[-1] : wcp[-2];
    char next_digit = hack_digit (&p);
    bool more_bits;
    if (next_digit != '0' && next_digit != '5')
      more_bits = true;
    else if (p.fracsize == 1 && p.frac[0] == 0)
      /* Rest of the number is zero.  */
      more_bits = false;
    else if (p.scalesize == 0)
      {
	/* Here we have to see whether all limbs are zero since no
	   normalization happened.  */
	size_t lcnt = p.fracsize;
	while (lcnt >= 1 && p.frac[lcnt - 1] == 0)
	  --lcnt;
	more_bits = lcnt > 0;
      }
    else
      more_bits = true;
    int rounding_mode = get_rounding_mode ();
    if (round_away (is_neg, (last_digit - '0') & 1, next_digit >= '5',
		    more_bits, rounding_mode))
      {
	char *wtp = wcp;

	if (fracdig_no > 0)
	  {
	    /* Process fractional digits.  Terminate if not rounded or
	       radix character is reached.  */
	    int removed = 0;
	    while (*--wtp != decimal && *wtp == '9')
	      {
		*wtp = '0';
		++removed;
	      }
	    if (removed == fracdig_min && added_zeros > 0)
	      --added_zeros;
	    if (*wtp != decimal)
	      /* Round up.  */
	      (*wtp)++;
	    else if (__builtin_expect (spec == 'g' && p.type == 'f' && info->alt
				       && wtp == wstartp + 1
				       && wstartp[0] == '0',
				       0))
	      /* This is a special case: the rounded number is 1.0,
		 the format is 'g' or 'G', and the alternative format
		 is selected.  This means the result must be "1.".  */
	      --added_zeros;
	  }

	if (fracdig_no == 0 || *wtp == decimal)
	  {
	    /* Round the integer digits.  */
	    if (*(wtp - 1) == decimal)
	      --wtp;

	    while (--wtp >= wstartp && *wtp == '9')
	      *wtp = '0';

	    if (wtp >= wstartp)
	      /* Round up.  */
	      (*wtp)++;
	    else
	      /* It is more critical.  All digits were 9's.  */
	      {
		if (p.type != 'f')
		  {
		    *wstartp = '1';
		    p.exponent += p.expsign == 0 ? 1 : -1;

		    /* The above p.exponent adjustment could lead to 1.0e-00,
		       e.g. for 0.999999999.  Make sure p.exponent 0 always
		       uses + sign.  */
		    if (p.exponent == 0)
		      p.expsign = 0;
		  }
		else if (intdig_no == dig_max)
		  {
		    /* This is the case where for p.type %g the number fits
		       really in the range for %f output but after rounding
		       the number of digits is too big.	 */
		    *--wstartp = decimal;
		    *--wstartp = '1';

		    if (info->alt || fracdig_no > 0)
		      {
			/* Overwrite the old radix character.  */
			wstartp[intdig_no + 2] = '0';
			++fracdig_no;
		      }

		    fracdig_no += intdig_no;
		    intdig_no = 1;
		    fracdig_max = intdig_max - intdig_no;
		    ++p.exponent;
		    /* Now we must print the p.exponent.	*/
		    p.type = isupper (info->spec) ? 'E' : 'e';
		  }
		else
		  {
		    /* We can simply add another another digit before the
		       radix.  */
		    *--wstartp = '1';
		    ++intdig_no;
		  }

		/* While rounding the number of digits can change.
		   If the number now exceeds the limits remove some
		   fractional digits.  */
		if (intdig_no + fracdig_no > dig_max)
		  {
		    wcp -= intdig_no + fracdig_no - dig_max;
		    fracdig_no -= intdig_no + fracdig_no - dig_max;
		  }
	      }
	  }
      }

    /* Now remove unnecessary '0' at the end of the string.  */
    while (fracdig_no > fracdig_min + added_zeros && *(wcp - 1) == '0')
      {
	--wcp;
	--fracdig_no;
      }
    /* If we eliminate all fractional digits we perhaps also can remove
       the radix character.  */
    if (fracdig_no == 0 && !info->alt && *(wcp - 1) == decimal)
      --wcp;

    /* Write the p.exponent if it is needed.  */
    if (p.type != 'f')
      {
	if (__glibc_unlikely (p.expsign != 0 && p.exponent == 4 && spec == 'g'))
	  {
	    /* This is another special case.  The p.exponent of the number is
	       really smaller than -4, which requires the 'e'/'E' format.
	       But after rounding the number has an p.exponent of -4.  */
	    assert (wcp >= wstartp + 1);
	    assert (wstartp[0] == '1');
	    memcpy (wstartp, "0.0001", 6);
	    wstartp[1] = decimal;
	    if (wcp >= wstartp + 2)
	      {
		memset (wstartp + 6, '0', wcp - (wstartp + 2));
		wcp += 4;
	      }
	    else
	      wcp += 5;
	  }
	else
	  {
	    *wcp++ = p.type;
	    *wcp++ = p.expsign ? '-' : '+';

	    /* Find the magnitude of the p.exponent.	*/
	    expscale = 10;
	    while (expscale <= p.exponent)
	      expscale *= 10;

	    if (p.exponent < 10)
	      /* Exponent always has at least two digits.  */
	      *wcp++ = '0';
	    else
	      do
		{
		  expscale /= 10;
		  *wcp++ = '0' + (p.exponent / expscale);
		  p.exponent %= expscale;
		}
	      while (expscale > 10);
	    *wcp++ = '0' + p.exponent;
	  }
      }

    struct grouping_iterator iter;
    if (thousands_sep != '\0' && info->group)
      __grouping_iterator_init (&iter, lc_category, loc, intdig_no);
    else
      iter.separators = 0;

    /* Compute number of characters which must be filled with the padding
       character.  */
    if (is_neg || info->showsign || info->space)
      --width;
    /* To count bytes, we would have to use __translated_number_width
       for info->i18n && !info->wide.  See bug 28943.  */
    width -= wcp - wstartp;
    /* For counting bytes, we would have to multiply by
       thousands_sep_length.  */
    width -= iter.separators;

    if (!info->left && info->pad != '0')
      __printf_buffer_pad (buf, info->pad, width);

    if (is_neg)
      __printf_buffer_putc (buf, '-');
    else if (info->showsign)
      __printf_buffer_putc (buf, '+');
    else if (info->space)
      __printf_buffer_putc (buf, ' ');

    if (!info->left && info->pad == '0')
      __printf_buffer_pad (buf, '0', width);

    if (iter.separators > 0)
      {
	char *cp = wstartp;
	for (int i = 0; i < intdig_no; ++i)
	  {
	    if (__grouping_iterator_next (&iter))
	      __printf_buffer_putc (buf, thousands_sep);
	    __printf_buffer_putc (buf, *cp);
	    ++cp;
	  }
	__printf_buffer_write (buf, cp, wcp - cp);
      }
    else
      __printf_buffer_write (buf, wstartp, wcp - wstartp);

    if (info->left)
      __printf_buffer_pad (buf, info->pad, width);
  }

  if (buffer_malloced)
    free (wbuffer);
}

/* ASCII to localization translation.  Multibyte version.  */
struct __printf_buffer_fp
{
  struct __printf_buffer base;

  /* Replacement for ',' and '.'.  */
  const char *thousands_sep;
  const char *decimal;
  unsigned char decimal_point_bytes;
  unsigned char thousands_sep_length;

  /* Buffer to write to.   */
  struct __printf_buffer *next;

  /* Activates outdigit translation if not NULL.  */
  struct __locale_data *ctype;

  /* Buffer to which the untranslated ASCII digits are written.  */
  char untranslated[PRINTF_BUFFER_SIZE_DIGITS];
};

/*  Multibyte buffer-to-buffer flush function with full translation.  */
void
__printf_buffer_flush_fp (struct __printf_buffer_fp *buf)
{
  /* No need to update buf->base.written; the actual count is
     maintained in buf->next->written.  */
  for (char *p = buf->untranslated; p < buf->base.write_ptr; ++p)
    {
      char ch = *p;
      const char *replacement = NULL;
      unsigned int replacement_bytes;
      if (ch == ',')
	{
	  replacement = buf->thousands_sep;
	  replacement_bytes = buf->thousands_sep_length;
	}
      else if (ch == '.')
	{
	  replacement = buf->decimal;
	  replacement_bytes = buf->decimal_point_bytes;
	}
      else if (buf->ctype != NULL && '0' <= ch && ch <= '9')
	{
	  int digit = ch - '0';
	  replacement
	    = buf->ctype->values[_NL_ITEM_INDEX (_NL_CTYPE_OUTDIGIT0_MB)
				 + digit].string;
	  struct lc_ctype_data *ctype = buf->ctype->private;
	  replacement_bytes = ctype->outdigit_bytes[digit];
	}
      if (replacement == NULL)
	__printf_buffer_putc (buf->next, ch);
      else
	__printf_buffer_write (buf->next, replacement, replacement_bytes);
    }

  if (!__printf_buffer_has_failed (buf->next))
    buf->base.write_ptr = buf->untranslated;
  else
    __printf_buffer_mark_failed (&buf->base);
}

void
__printf_fp_l_buffer (struct __printf_buffer *buf, locale_t loc,
		      const struct printf_info *info,
		      const void *const *args)
{
  struct __printf_buffer_fp tmp;

  if (info->extra)
    {
      tmp.thousands_sep = _nl_lookup (loc, LC_MONETARY, MON_THOUSANDS_SEP);
      tmp.decimal = _nl_lookup (loc, LC_MONETARY, MON_DECIMAL_POINT);
      if (tmp.decimal[0] == '\0')
	tmp.decimal = _nl_lookup (loc, LC_NUMERIC, DECIMAL_POINT);
    }
  else
    {
      tmp.thousands_sep = _nl_lookup (loc, LC_NUMERIC, THOUSANDS_SEP);
      tmp.decimal = _nl_lookup (loc, LC_NUMERIC, DECIMAL_POINT);
    }

  tmp.thousands_sep_length = strlen (tmp.thousands_sep);
  if (tmp.decimal[1] == '\0' && tmp.thousands_sep_length <= 1
      && !info->i18n)
    {
      /* Emit the the characters directly.  This is only possible if the
	 separators have length 1 (or 0 in case of thousands_sep).  i18n
	 digit translation still needs the full conversion.  */
      __printf_fp_buffer_1 (buf, loc,
			    tmp.thousands_sep[0], tmp.decimal[0],
			    tmp.thousands_sep_length,
			    info, args);
      return;
    }

  tmp.decimal_point_bytes = strlen (tmp.decimal);

  if (info->i18n)
    tmp.ctype = loc->__locales[LC_CTYPE];
  else
    tmp.ctype = NULL;
  tmp.next = buf;

  __printf_buffer_init (&tmp.base, tmp.untranslated, sizeof (tmp.untranslated),
			__printf_buffer_mode_fp);
  __printf_fp_buffer_1 (&tmp.base, loc, ',', '.',
			tmp.thousands_sep_length, info, args);
  if (__printf_buffer_has_failed (&tmp.base))
    {
      __printf_buffer_mark_failed (tmp.next);
      return;
    }
  __printf_buffer_flush_fp (&tmp);
}

/* The wide version is implemented on top of the multibyte version using
   translation.  */

struct __printf_buffer_fp_to_wide
{
  struct __printf_buffer base;
  wchar_t thousands_sep_wc;
  wchar_t decimalwc;
  struct __wprintf_buffer *next;

  /* Activates outdigit translation if not NULL.  */
  struct __locale_data *ctype;

  char untranslated[PRINTF_BUFFER_SIZE_DIGITS];
};

void
__printf_buffer_flush_fp_to_wide (struct __printf_buffer_fp_to_wide *buf)
{
  /* No need to update buf->base.written; the actual count is
     maintained in buf->next->written.  */
  for (char *p = buf->untranslated; p < buf->base.write_ptr; ++p)
    {
      /* wchar_t overlaps with char in the ASCII range.  */
      wchar_t ch = *p;
      if (ch == L',')
	{
	  ch = buf->thousands_sep_wc;
	  if (ch == 0)
	    continue;
	}
      else if (ch == L'.')
	ch = buf->decimalwc;
      else if (buf->ctype != NULL && L'0' <= ch && ch <= L'9')
	ch = buf->ctype->values[_NL_ITEM_INDEX (_NL_CTYPE_OUTDIGIT0_WC)
				+ ch - L'0'].word;
      __wprintf_buffer_putc (buf->next, ch);
    }

  if (!__wprintf_buffer_has_failed (buf->next))
    buf->base.write_ptr = buf->untranslated;
  else
    __printf_buffer_mark_failed (&buf->base);
}

void
__wprintf_fp_l_buffer (struct __wprintf_buffer *buf, locale_t loc,
		       const struct printf_info *info,
		       const void *const *args)
{
  struct __printf_buffer_fp_to_wide tmp;
  if (info->extra)
    {
      tmp.decimalwc = _nl_lookup_word (loc, LC_MONETARY,
				       _NL_MONETARY_DECIMAL_POINT_WC);
      tmp.thousands_sep_wc = _nl_lookup_word (loc, LC_MONETARY,
					      _NL_MONETARY_THOUSANDS_SEP_WC);
      if (tmp.decimalwc == 0)
	tmp.decimalwc = _nl_lookup_word (loc, LC_NUMERIC,
					 _NL_NUMERIC_DECIMAL_POINT_WC);
    }
  else
    {
      tmp.decimalwc = _nl_lookup_word (loc, LC_NUMERIC,
				       _NL_NUMERIC_DECIMAL_POINT_WC);
      tmp.thousands_sep_wc = _nl_lookup_word (loc, LC_NUMERIC,
					      _NL_NUMERIC_THOUSANDS_SEP_WC);
    }

  if (info->i18n)
    tmp.ctype = loc->__locales[LC_CTYPE];
  else
    tmp.ctype = NULL;
  tmp.next = buf;

  __printf_buffer_init (&tmp.base, tmp.untranslated, sizeof (tmp.untranslated),
			__printf_buffer_mode_fp_to_wide);
  __printf_fp_buffer_1 (&tmp.base, loc, ',', '.', 1, info, args);
  if (__printf_buffer_has_failed (&tmp.base))
    {
      __wprintf_buffer_mark_failed (tmp.next);
      return;
    }
  __printf_buffer_flush (&tmp.base);
}

int
___printf_fp (FILE *fp, const struct printf_info *info,
	      const void *const *args)
{
  if (info->wide)
    {
      struct __wprintf_buffer_to_file buf;
      __wprintf_buffer_to_file_init (&buf, fp);
      __wprintf_fp_l_buffer (&buf.base, _NL_CURRENT_LOCALE, info, args);
      return __wprintf_buffer_to_file_done (&buf);
    }
  else
    {
      struct __printf_buffer_to_file buf;
      __printf_buffer_to_file_init (&buf, fp);
      __printf_fp_l_buffer (&buf.base, _NL_CURRENT_LOCALE, info, args);
      return __printf_buffer_to_file_done (&buf);
    }
}
ldbl_hidden_def (___printf_fp, __printf_fp)
ldbl_strong_alias (___printf_fp, __printf_fp)
