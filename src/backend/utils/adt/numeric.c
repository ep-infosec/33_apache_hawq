/*-------------------------------------------------------------------------
 *
 * numeric.c
 *	  An exact numeric data type for the Postgres database system
 *
 * Original coding 1998, Jan Wieck.  Heavily revised 2003, Tom Lane.
 *
 * Many of the algorithmic ideas are borrowed from David M. Smith's "FM"
 * multiple-precision math library, most recently published as Algorithm
 * 786: Multiple-Precision Complex Arithmetic and Functions, ACM
 * Transactions on Mathematical Software, Vol. 24, No. 4, December 1998,
 * pages 359-367.
 *
 * Copyright (c) 1998-2008, PostgreSQL Global Development Group
 *
 * IDENTIFICATION
 *	  $PostgreSQL: pgsql/src/backend/utils/adt/numeric.c,v 1.96 2006/10/04 00:29:59 momjian Exp $
 *
 *-------------------------------------------------------------------------
 */

#include "postgres.h"

#include <ctype.h>
#include <float.h>
#include <limits.h>
#include <math.h>

#include "access/hash.h"
#include "catalog/pg_type.h"
#include "libpq/pqformat.h"
#include "miscadmin.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/int8.h"
#include "utils/numeric.h"

/* ----------
 * Uncomment the following to enable compilation of dump_numeric()
 * and dump_var() and to get a dump of any result produced by make_result().
 * ----------
#define NUMERIC_DEBUG
 */


/* ----------
 * Local data types
 *
 * Numeric values are represented in a base-NBASE floating point format.
 * Each "digit" ranges from 0 to NBASE-1.  The type NumericDigit is signed
 * and wide enough to store a digit.  We assume that NBASE*NBASE can fit in
 * an int.	Although the purely calculational routines could handle any even
 * NBASE that's less than sqrt(INT_MAX), in practice we are only interested
 * in NBASE a power of ten, so that I/O conversions and decimal rounding
 * are easy.  Also, it's actually more efficient if NBASE is rather less than
 * sqrt(INT_MAX), so that there is "headroom" for mul_var and div_var to
 * postpone processing carries.
 * ----------
 */


#if 1
#define NBASE		10000
#define HALF_NBASE	5000
#define DEC_DIGITS	4			/* decimal digits per NBASE digit */
#define MUL_GUARD_DIGITS	2	/* these are measured in NBASE digits */
#define DIV_GUARD_DIGITS	4

typedef int16 NumericDigit;
#endif


/* ----------
 * NumericVar is the format we use for arithmetic.	The digit-array part
 * is the same as the NumericData storage format, but the header is more
 * complex.
 *
 * The value represented by a NumericVar is determined by the sign, weight,
 * ndigits, and digits[] array.
 * Note: the first digit of a NumericVar's value is assumed to be multiplied
 * by NBASE ** weight.	Another way to say it is that there are weight+1
 * digits before the decimal point.  It is possible to have weight < 0.
 *
 * buf: points at the physical start of the digit buffer for the NumericVar.
 * This is either the local buffer (ndb) or a palloc'd buffer.
 *
 * digits: points at the first digit in actual use (the one with the
 * specified weight).	We normally leave an unused digit or two
 * (preset to zeroes) between buf and digits, so that there is room to store
 * a carry out of the top digit without special pushups.  We just need to
 * decrement digits (and increment weight) to make room for the carry digit.
 * (There is no such extra space in a numeric value stored in the database,
 * only in a NumericVar in memory.)
 *
 * If buf is set to ndb, then the digit buffer isn't actually palloc'd and
 * should not be freed --- see the constants below for an example.
 *
 * dscale: display scale, is the nominal precision expressed as number
 * of digits after the decimal point (it must always be >= 0 at present).
 * dscale may be more than the number of physically stored fractional digits,
 * implying that we have suppressed storage of significant trailing zeroes.
 * It should never be less than the number of stored digits, since that would
 * imply hiding digits that are present.  NOTE that dscale is always expressed
 * in *decimal* digits, and so it may correspond to a fractional number of
 * base-NBASE digits --- divide by DEC_DIGITS to convert to NBASE digits.
 *
 * rscale, or result scale, is the target precision for a computation.
 * Like dscale it is expressed as number of *decimal* digits after the decimal
 * point, and is always >= 0 at present.
 * Note that rscale is not stored in variables --- it's figured on-the-fly
 * from the dscales of the inputs.
 *
 * NB: All the variable-level functions are written in a style that makes it
 * possible to give one and the same variable as argument and destination.
 *
 * ----------
 */
#define	NUMERIC_LOCAL_NDIG	36		/* number of 'digits' in local digits[] */
#define NUMERIC_LOCAL_NMAX	(NUMERIC_LOCAL_NDIG - 2)
#define	NUMERIC_LOCAL_DTXT	128		/* number of char in local text */
#define NUMERIC_LOCAL_DMAX	(NUMERIC_LOCAL_DTXT - 2)

typedef struct NumericVar
{
	int			ndigits;		/* # of digits in digits[] - can be 0! */
	int			weight;			/* weight of first digit */
	int			sign;			/* NUMERIC_POS, NUMERIC_NEG, or NUMERIC_NAN */
	int			dscale;			/* display scale */
	NumericDigit *buf			/* start of space for digits[] */;
	NumericDigit *digits;		/* base-NBASE digits */
	NumericDigit ndb[NUMERIC_LOCAL_NDIG];	/* local space for digits[] */
} NumericVar;

#define NUMERIC_LOCAL_HSIZ	\
			(sizeof(NumericVar) - (sizeof(NumericDigit) * NUMERIC_LOCAL_NDIG))

/* ----------
 * Some preinitialized constants
 * ----------
 */
static NumericDigit const_zero_data[1] = {0};
static NumericVar const_zero =
{0, 0, NUMERIC_POS, 0, const_zero.ndb, const_zero_data, {0}};

static NumericDigit const_one_data[1] = {1};
static NumericVar const_one =
{1, 0, NUMERIC_POS, 0, const_one.ndb, const_one_data, {0}};

static NumericDigit const_two_data[1] = {2};
static NumericVar const_two =
{1, 0, NUMERIC_POS, 0, const_two.ndb, const_two_data, {0}};

#if DEC_DIGITS == 4
static NumericDigit const_zero_point_five_data[1] = {5000};
#elif DEC_DIGITS == 2
static NumericDigit const_zero_point_five_data[1] = {50};
#elif DEC_DIGITS == 1
static NumericDigit const_zero_point_five_data[1] = {5};
#endif
static NumericVar const_zero_point_five =
{1, -1, NUMERIC_POS, 1, const_zero_point_five.ndb, const_zero_point_five_data, {0}};

#if DEC_DIGITS == 4
static NumericDigit const_zero_point_nine_data[1] = {9000};
#elif DEC_DIGITS == 2
static NumericDigit const_zero_point_nine_data[1] = {90};
#elif DEC_DIGITS == 1
static NumericDigit const_zero_point_nine_data[1] = {9};
#endif
static NumericVar const_zero_point_nine =
{1, -1, NUMERIC_POS, 1, const_zero_point_nine.ndb, const_zero_point_nine_data, {0}};

#if DEC_DIGITS == 4
static NumericDigit const_zero_point_01_data[1] = {100};
static NumericVar const_zero_point_01 =
{1, -1, NUMERIC_POS, 2, const_zero_point_01.ndb, const_zero_point_01_data, {0}};
#elif DEC_DIGITS == 2
static NumericDigit const_zero_point_01_data[1] = {1};
static NumericVar const_zero_point_01 =
{1, -1, NUMERIC_POS, 2, const_zero_point_01.ndb, const_zero_point_01_data, {0}};
#elif DEC_DIGITS == 1
static NumericDigit const_zero_point_01_data[1] = {1};
static NumericVar const_zero_point_01 =
{1, -2, NUMERIC_POS, 2, const_zero_point_01.ndb, const_zero_point_01_data, {0}};
#endif

#if DEC_DIGITS == 4
static NumericDigit const_one_point_one_data[2] = {1, 1000};
#elif DEC_DIGITS == 2
static NumericDigit const_one_point_one_data[2] = {1, 10};
#elif DEC_DIGITS == 1
static NumericDigit const_one_point_one_data[2] = {1, 1};
#endif
static NumericVar const_one_point_one =
{2, 0, NUMERIC_POS, 1, const_one_point_one.ndb, const_one_point_one_data, {0}};

static NumericVar const_nan =
{0, 0, NUMERIC_NAN, 0, const_nan.ndb, NULL, {0}};

#if DEC_DIGITS == 4
static const int round_powers[4] = {0, 1000, 100, 10};
#endif


/* ----------
 * Local functions
 * ----------
 */

#ifdef NUMERIC_DEBUG
static void dump_numeric(const char *str, Numeric num);
static void dump_var(const char *str, NumericVar *var);
#else
#define dump_numeric(s,n)
#define dump_var(s,v)
#endif

/* ----------
 * a few Numeric access things not defined in numeric.h
 */
#define NUMERIC_SIGN_DSCALE(num) ((num)->n_sign_dscale)
#define NUMERIC_WEIGHT(num) ((num)->n_weight)
#define NUMERIC_DIGITS(num) ((NumericDigit *)(num)->n_data)
#define NUMERIC_NDIGITS(num) \
	((VARSIZE(num) - NUMERIC_HDRSZ) / sizeof(NumericDigit))
/*
 *----------
 */

#define quick_init_var(v) \
	do { \
		(v)->buf = (v)->ndb;	\
		(v)->digits = NULL; 	\
	} while (0)


#define init_var(v) \
	do { \
		quick_init_var((v));	\
		(v)->ndigits = (v)->weight = (v)->sign = (v)->dscale = 0; \
	} while (0)


#define digitbuf_alloc(ndigits)  \
	((NumericDigit *) palloc((ndigits) * sizeof(NumericDigit)))

#define digitbuf_free(v)	\
	do { \
		if ((v)->buf != (v)->ndb)	\
		{							\
		 	pfree((v)->buf); 		\
			(v)->buf = (v)->ndb;	\
		}	\
	} while (0)

#define free_var(v)	\
				digitbuf_free((v));

/*
 * init_alloc_var() -
 *
 *	Init a var and allocate digit buffer of ndigits digits (plus a spare
 *  digit for rounding).
 *  Called when first using a var.
 */
#define	init_alloc_var(v, n) \
	do 	{	\
		(v)->buf = (v)->ndb;	\
		(v)->ndigits = (n);	\
		if ((n) > NUMERIC_LOCAL_NMAX)	\
			(v)->buf = digitbuf_alloc((n) + 1);	\
		(v)->buf[0] = 0;	\
		(v)->digits = (v)->buf + 1;	\
	} while (0)

static void alloc_var(NumericVar *var, int ndigits);
static void zero_var(NumericVar *var);

static void init_var_from_str(const char *str, NumericVar *dest);
static void set_var_from_var(NumericVar *value, NumericVar *dest);
static void init_var_from_var(NumericVar *value, NumericVar *dest);
static void init_ro_var_from_var(NumericVar *value, NumericVar *dest);
/*static void set_var_from_num(Numeric value, NumericVar *dest);*/
static void init_var_from_num(Numeric value, NumericVar *dest);
static void init_ro_var_from_num(Numeric value, NumericVar *dest);
static char *get_str_from_var(NumericVar *var, int dscale);

/* ----------
 * CAUTION: These routines perform a  free_var(var)
 */
static Numeric make_result(NumericVar *var);
static int make_result_inplace(NumericVar *var, Numeric result, int in_len);
static bool numericvar_to_int8(NumericVar *var, int64 *result);
static int32 numericvar_to_int4(NumericVar *var);
/*
 * ----------
 */

static void apply_typmod(NumericVar *var, int32 typmod);

static void int8_to_numericvar(int64 val, NumericVar *var);
static double numericvar_to_double_no_overflow(NumericVar *var);

static int	cmp_numerics(Numeric num1, Numeric num2);
static int	cmp_var(NumericVar *var1, NumericVar *var2);
static int	cmp_var_common(const NumericDigit *var1digits, int var1ndigits,
			   int var1weight, int var1sign,
			   const NumericDigit *var2digits, int var2ndigits,
			   int var2weight, int var2sign);
static void add_var(NumericVar *var1, NumericVar *var2, NumericVar *result);
static void sub_var(NumericVar *var1, NumericVar *var2, NumericVar *result);
static void mul_var(NumericVar *var1, NumericVar *var2, NumericVar *result,
		int rscale);
static void div_var(NumericVar *var1, NumericVar *var2, NumericVar *result,
		int rscale, bool round);
static int	select_div_scale(NumericVar *var1, NumericVar *var2);
static void mod_var(NumericVar *var1, NumericVar *var2, NumericVar *result);
static void ceil_var(NumericVar *var, NumericVar *result);
static void floor_var(NumericVar *var, NumericVar *result);

static void sqrt_var(NumericVar *arg, NumericVar *result, int rscale);
static void exp_var(NumericVar *arg, NumericVar *result, int rscale);
static void exp_var_internal(NumericVar *arg, NumericVar *result, int rscale);
static void ln_var(NumericVar *arg, NumericVar *result, int rscale);
static void log_var(NumericVar *base, NumericVar *num, NumericVar *result);
static void power_var(NumericVar *base, NumericVar *exp, NumericVar *result);
static void power_var_int(NumericVar *base, int exp, NumericVar *result,
			  int rscale);

static int	cmp_abs(NumericVar *var1, NumericVar *var2);
static int	cmp_abs_common(const NumericDigit *var1digits, int var1ndigits,
			   int var1weight,
			   const NumericDigit *var2digits, int var2ndigits,
			   int var2weight);
static void add_abs(NumericVar *var1, NumericVar *var2, NumericVar *result);
static void sub_abs(NumericVar *var1, NumericVar *var2, NumericVar *result);
static void round_var(NumericVar *var, int rscale);
static void trunc_var(NumericVar *var, int rscale);
static void strip_var(NumericVar *var);
static void compute_bucket(Numeric operand, Numeric bound1, Numeric bound2,
			   NumericVar *count_var, NumericVar *result_var);

static ArrayType *do_numeric_amalg_demalg(ArrayType *aTransArray, 
										  ArrayType *bTransArray,
										  bool is_amalg); /* MPP */


/* ----------------------------------------------------------------------
 *
 * Input-, output- and rounding-functions
 *
 * ----------------------------------------------------------------------
 */


/*
 * numeric_in() -
 *
 *	Input function for numeric data type
 */
Datum
numeric_in(PG_FUNCTION_ARGS)
{
	char	   *str = PG_GETARG_CSTRING(0);

#ifdef NOT_USED
	Oid			typelem = PG_GETARG_OID(1);
#endif
	int32		typmod = PG_GETARG_INT32(2);
	NumericVar	value;
	Numeric		res;

	/*
	 * Check for NaN
	 */
	if (pg_strcasecmp(str, "NaN") == 0)
		PG_RETURN_NUMERIC(make_result(&const_nan));

	/*
	 * Use init_var_from_str() to parse the input string and return it in the
	 * packed DB storage format
	 */
	init_var_from_str(str, &value);

	apply_typmod(&value, typmod);

	res = make_result(&value);

	PG_RETURN_NUMERIC(res);
}


/*
 * numeric_out() -
 *
 *	Output function for numeric data type
 */
Datum
numeric_out(PG_FUNCTION_ARGS)
{
	Numeric		num = PG_GETARG_NUMERIC(0);
	NumericVar	x;
	char	   *str;

	/*
	 * Handle NaN
	 */
	if (NUMERIC_IS_NAN(num))
		PG_RETURN_CSTRING(pstrdup("NaN"));

	/*
	 * Get the number in the variable format.
	 *
	 * Even if we didn't need to change format, we'd still need to copy the
	 * value to have a modifiable copy for rounding.  init_var_from_num() also
	 * guarantees there is extra digit space in case we produce a carry out
	 * from rounding.
	 */
	init_var_from_num(num, &x);

	str = get_str_from_var(&x, x.dscale);

	free_var(&x);

	PG_RETURN_CSTRING(str);
}

/*
 *		numeric_recv			- converts external binary format to numeric
 *
 * External format is a sequence of int16's:
 * ndigits, weight, sign, dscale, NumericDigits.
 */
Datum
numeric_recv(PG_FUNCTION_ARGS)
{
	StringInfo	buf = (StringInfo) PG_GETARG_POINTER(0);

#ifdef NOT_USED
	Oid			typelem = PG_GETARG_OID(1);
#endif
	int32		typmod = PG_GETARG_INT32(2);
	NumericVar	value;
	Numeric		res;
	int			len,
				i;


	len = (uint16) pq_getmsgint(buf, sizeof(uint16));
	if (len < 0 || len > NUMERIC_MAX_PRECISION + NUMERIC_MAX_RESULT_SCALE)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
				 errmsg("invalid length in external \"numeric\" value"),
						 errOmitLocation(true)));

	init_alloc_var(&value, len);

	value.weight = (int16) pq_getmsgint(buf, sizeof(int16));
	value.sign = (uint16) pq_getmsgint(buf, sizeof(uint16));
	if (!(value.sign == NUMERIC_POS ||
		  value.sign == NUMERIC_NEG ||
		  value.sign == NUMERIC_NAN))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
				 errmsg("invalid sign in external \"numeric\" value"),
						 errOmitLocation(true)));

	value.dscale = (uint16) pq_getmsgint(buf, sizeof(uint16));
	for (i = 0; i < len; i++)
	{
		NumericDigit d = pq_getmsgint(buf, sizeof(NumericDigit));

		if (d < 0 || d >= NBASE)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_BINARY_REPRESENTATION),
					 errmsg("invalid digit in external \"numeric\" value"),
							 errOmitLocation(true)));
		value.digits[i] = d;
	}

	apply_typmod(&value, typmod);

	res = make_result(&value);

	PG_RETURN_NUMERIC(res);
}

/*
 *		numeric_send			- converts numeric to binary format
 */
Datum
numeric_send(PG_FUNCTION_ARGS)
{
	Numeric		num = PG_GETARG_NUMERIC(0);
	NumericVar	x;
	StringInfoData buf;
	int			i;

	init_ro_var_from_num(num, &x);

	pq_begintypsend(&buf);

	pq_sendint(&buf, x.ndigits, sizeof(int16));
	pq_sendint(&buf, x.weight, sizeof(int16));
	pq_sendint(&buf, x.sign, sizeof(int16));
	pq_sendint(&buf, x.dscale, sizeof(int16));
	for (i = 0; i < x.ndigits; i++)
		pq_sendint(&buf, x.digits[i], sizeof(NumericDigit));

	free_var(&x);

	PG_RETURN_BYTEA_P(pq_endtypsend(&buf));
}


/*
 * numeric() -
 *
 *	This is a special function called by the Postgres database system
 *	before a value is stored in a tuple's attribute. The precision and
 *	scale of the attribute have to be applied on the value.
 */
Datum
numeric		(PG_FUNCTION_ARGS)
{
	Numeric		num = PG_GETARG_NUMERIC(0);
	int32		typmod = PG_GETARG_INT32(1);
	Numeric		new;
	int32		tmp_typmod;
	int			precision;
	int			scale;
	int			ddigits;
	int			maxdigits;
	NumericVar	var;

	/*
	 * Handle NaN
	 */
	if (NUMERIC_IS_NAN(num))
		PG_RETURN_NUMERIC(make_result(&const_nan));

	/*
	 * If the value isn't a valid type modifier, simply return a copy of the
	 * input value
	 */
	if (typmod < (int32) (VARHDRSZ))
	{
		new = (Numeric) palloc(VARSIZE(num));
		memcpy(new, num, VARSIZE(num));
		PG_RETURN_NUMERIC(new);
	}

	/*
	 * Get the precision and scale out of the typmod value
	 */
	tmp_typmod = typmod - VARHDRSZ;
	precision = (tmp_typmod >> 16) & 0xffff;
	scale = tmp_typmod & 0xffff;
	maxdigits = precision - scale;

	/*
	 * If the number is certainly in bounds and due to the target scale no
	 * rounding could be necessary, just make a copy of the input and modify
	 * its scale fields.  (Note we assume the existing dscale is honest...)
	 */
	ddigits = (NUMERIC_WEIGHT(num) + 1) * DEC_DIGITS;
	if (ddigits <= maxdigits && scale >= NUMERIC_DSCALE(num))
	{
		new = (Numeric) palloc(VARSIZE(num));
		memcpy(new, num, VARSIZE(num));
		NUMERIC_SIGN_DSCALE(new) = NUMERIC_SIGN(new) |
			((uint16) scale & NUMERIC_DSCALE_MASK);
		PG_RETURN_NUMERIC(new);
	}

	/*
	 * We really need to fiddle with things - unpack the number into a
	 * variable and let apply_typmod() do it.
	 */

	init_var_from_num(num, &var);
	apply_typmod(&var, typmod);
	new = make_result(&var);

	PG_RETURN_NUMERIC(new);
}


/* ----------------------------------------------------------------------
 *
 * Sign manipulation, rounding and the like
 *
 * ----------------------------------------------------------------------
 */

Datum
numeric_abs(PG_FUNCTION_ARGS)
{
	Numeric		num = PG_GETARG_NUMERIC(0);
	Numeric		res;

	/*
	 * Handle NaN
	 */
	if (NUMERIC_IS_NAN(num))
		PG_RETURN_NUMERIC(make_result(&const_nan));

	/*
	 * Do it the easy way directly on the packed format
	 */
	res = (Numeric) palloc(VARSIZE(num));
	memcpy(res, num, VARSIZE(num));

	NUMERIC_SIGN_DSCALE(res) = NUMERIC_POS | NUMERIC_DSCALE(num);

	PG_RETURN_NUMERIC(res);
}


Datum
numeric_uminus(PG_FUNCTION_ARGS)
{
	Numeric		num = PG_GETARG_NUMERIC(0);
	Numeric		res;

	/*
	 * Handle NaN
	 */
	if (NUMERIC_IS_NAN(num))
		PG_RETURN_NUMERIC(make_result(&const_nan));

	/*
	 * Do it the easy way directly on the packed format
	 */
	res = (Numeric) palloc(VARSIZE(num));
	memcpy(res, num, VARSIZE(num));

	/*
	 * The packed format is known to be totally zero digit trimmed always. So
	 * we can identify a ZERO by the fact that there are no digits at all.	Do
	 * nothing to a zero.
	 */
	if (VARSIZE(num) != NUMERIC_HDRSZ)
	{
		/* Else, flip the sign */
		if (NUMERIC_SIGN(num) == NUMERIC_POS)
			NUMERIC_SIGN_DSCALE(res) = NUMERIC_NEG | NUMERIC_DSCALE(num);
		else
			NUMERIC_SIGN_DSCALE(res) = NUMERIC_POS | NUMERIC_DSCALE(num);
	}

	PG_RETURN_NUMERIC(res);
}


Datum
numeric_uplus(PG_FUNCTION_ARGS)
{
	Numeric		num = PG_GETARG_NUMERIC(0);
	Numeric		res;

	res = (Numeric) palloc(VARSIZE(num));
	memcpy(res, num, VARSIZE(num));

	PG_RETURN_NUMERIC(res);
}

/*
 * numeric_sign() -
 *
 * returns -1 if the argument is less than 0, 0 if the argument is equal
 * to 0, and 1 if the argument is greater than zero.
 */
Datum
numeric_sign(PG_FUNCTION_ARGS)
{
	Numeric		num = PG_GETARG_NUMERIC(0);
	Numeric		res;
	NumericVar	result;

	/*
	 * Handle NaN
	 */
	if (NUMERIC_IS_NAN(num))
		PG_RETURN_NUMERIC(make_result(&const_nan));

	/*
	 * The packed format is known to be totally zero digit trimmed always. So
	 * we can identify a ZERO by the fact that there are no digits at all.
	 */
	if (VARSIZE(num) == NUMERIC_HDRSZ)
		init_ro_var_from_var(&const_zero, &result);
	else
	{
		/*
		 * And if there are some, we return a copy of ONE with the sign of our
		 * argument
		 */
		init_ro_var_from_var(&const_one, &result);
		result.sign = NUMERIC_SIGN(num);
	}

	res = make_result(&result);

	PG_RETURN_NUMERIC(res);
}


/*
 * numeric_round() -
 *
 *	Round a value to have 'scale' digits after the decimal point.
 *	We allow negative 'scale', implying rounding before the decimal
 *	point --- Oracle interprets rounding that way.
 */
Datum
numeric_round(PG_FUNCTION_ARGS)
{
	Numeric		num = PG_GETARG_NUMERIC(0);
	int32		scale = PG_GETARG_INT32(1);
	Numeric		res;
	NumericVar	arg;

	/*
	 * Handle NaN
	 */
	if (NUMERIC_IS_NAN(num))
		PG_RETURN_NUMERIC(make_result(&const_nan));

	/*
	 * Limit the scale value to avoid possible overflow in calculations
	 */
	scale = Max(scale, -NUMERIC_MAX_RESULT_SCALE);
	scale = Min(scale, NUMERIC_MAX_RESULT_SCALE);

	/*
	 * Unpack the argument and round it at the proper digit position
	 */
	init_var_from_num(num, &arg);

	round_var(&arg, scale);

	/* We don't allow negative output dscale */
	if (scale < 0)
		arg.dscale = 0;

	/*
	 * Return the rounded result
	 */
	res = make_result(&arg);

	PG_RETURN_NUMERIC(res);
}


/*
 * numeric_trunc() -
 *
 *	Truncate a value to have 'scale' digits after the decimal point.
 *	We allow negative 'scale', implying a truncation before the decimal
 *	point --- Oracle interprets truncation that way.
 */
Datum
numeric_trunc(PG_FUNCTION_ARGS)
{
	Numeric		num = PG_GETARG_NUMERIC(0);
	int32		scale = PG_GETARG_INT32(1);
	Numeric		res;
	NumericVar	arg;

	/*
	 * Handle NaN
	 */
	if (NUMERIC_IS_NAN(num))
		PG_RETURN_NUMERIC(make_result(&const_nan));

	/*
	 * Limit the scale value to avoid possible overflow in calculations
	 */
	scale = Max(scale, -NUMERIC_MAX_RESULT_SCALE);
	scale = Min(scale, NUMERIC_MAX_RESULT_SCALE);

	/*
	 * Unpack the argument and truncate it at the proper digit position
	 */
	init_var_from_num(num, &arg);

	trunc_var(&arg, scale);

	/* We don't allow negative output dscale */
	if (scale < 0)
		arg.dscale = 0;

	/*
	 * Return the truncated result
	 */
	res = make_result(&arg);

	PG_RETURN_NUMERIC(res);
}


/*
 * numeric_ceil() -
 *
 *	Return the smallest integer greater than or equal to the argument
 */
Datum
numeric_ceil(PG_FUNCTION_ARGS)
{
	Numeric		num = PG_GETARG_NUMERIC(0);
	Numeric		res;
	NumericVar	result;

	if (NUMERIC_IS_NAN(num))
		PG_RETURN_NUMERIC(make_result(&const_nan));

	init_ro_var_from_num(num, &result);
	ceil_var(&result, &result);

	res = make_result(&result);

	PG_RETURN_NUMERIC(res);
}


/*
 * numeric_floor() -
 *
 *	Return the largest integer equal to or less than the argument
 */
Datum
numeric_floor(PG_FUNCTION_ARGS)
{
	Numeric		num = PG_GETARG_NUMERIC(0);
	Numeric		res;
	NumericVar	result;

	if (NUMERIC_IS_NAN(num))
		PG_RETURN_NUMERIC(make_result(&const_nan));

	init_ro_var_from_num(num, &result);
	floor_var(&result, &result);

	res = make_result(&result);

	PG_RETURN_NUMERIC(res);
}

/*
 * width_bucket_numeric() -
 *
 * 'bound1' and 'bound2' are the lower and upper bounds of the
 * histogram's range, respectively. 'count' is the number of buckets
 * in the histogram. width_bucket() returns an integer indicating the
 * bucket number that 'operand' belongs in for an equiwidth histogram
 * with the specified characteristics. An operand smaller than the
 * lower bound is assigned to bucket 0. An operand greater than the
 * upper bound is assigned to an additional bucket (with number
 * count+1).
 */
Datum
width_bucket_numeric(PG_FUNCTION_ARGS)
{
	Numeric		operand = PG_GETARG_NUMERIC(0);
	Numeric		bound1 = PG_GETARG_NUMERIC(1);
	Numeric		bound2 = PG_GETARG_NUMERIC(2);
	int32		count = PG_GETARG_INT32(3);
	NumericVar	count_var;
	NumericVar	result_var;
	int32		result;

	if (count <= 0)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_ARGUMENT_FOR_WIDTH_BUCKET_FUNCTION),
				 errmsg("count must be greater than zero"),
						 errOmitLocation(true)));

	quick_init_var(&result_var);
	quick_init_var(&count_var);

	/* Convert 'count' to a numeric, for ease of use later */
	int8_to_numericvar((int64) count, &count_var);

	switch (cmp_numerics(bound1, bound2))
	{
		case 0:
			ereport(ERROR,
				(errcode(ERRCODE_INVALID_ARGUMENT_FOR_WIDTH_BUCKET_FUNCTION),
				 errmsg("lower bound cannot equal upper bound"),
						 errOmitLocation(true)));

			/* bound1 < bound2 */
		case -1:
			if (cmp_numerics(operand, bound1) < 0)
				set_var_from_var(&const_zero, &result_var);
			else if (cmp_numerics(operand, bound2) >= 0)
				add_var(&count_var, &const_one, &result_var);
			else
				compute_bucket(operand, bound1, bound2,
							   &count_var, &result_var);
			break;

			/* bound1 > bound2 */
		case 1:
			if (cmp_numerics(operand, bound1) > 0)
				set_var_from_var(&const_zero, &result_var);
			else if (cmp_numerics(operand, bound2) <= 0)
				add_var(&count_var, &const_one, &result_var);
			else
				compute_bucket(operand, bound1, bound2,
							   &count_var, &result_var);
			break;
	}

	free_var(&count_var);

	result = numericvar_to_int4(&result_var);

	PG_RETURN_INT32(result);
}

/*
 * compute_bucket() -
 *
 * If 'operand' is not outside the bucket range, determine the correct
 * bucket for it to go. The calculations performed by this function
 * are derived directly from the SQL2003 spec.
 */
static void
compute_bucket(Numeric operand, Numeric bound1, Numeric bound2,
			   NumericVar *count_var, NumericVar *result_var)
{
	NumericVar	bound1_var;
	NumericVar	bound2_var;
	NumericVar	operand_var;

	init_ro_var_from_num(bound1, &bound1_var);
	init_ro_var_from_num(bound2, &bound2_var);
	init_ro_var_from_num(operand, &operand_var);

	if (cmp_var(&bound1_var, &bound2_var) < 0)
	{
		sub_var(&operand_var, &bound1_var, &operand_var);
		sub_var(&bound2_var, &bound1_var, &bound2_var);
		div_var(&operand_var, &bound2_var, result_var,
				select_div_scale(&operand_var, &bound2_var), true);
	}
	else
	{
		sub_var(&bound1_var, &operand_var, &operand_var);
		sub_var(&bound1_var, &bound2_var, &bound1_var);
		div_var(&operand_var, &bound1_var, result_var,
				select_div_scale(&operand_var, &bound1_var), true);
	}

	free_var(&bound1_var);
	free_var(&bound2_var);
	free_var(&operand_var);

	mul_var(result_var, count_var, result_var,
			result_var->dscale + count_var->dscale);
	add_var(result_var, &const_one, result_var);
	floor_var(result_var, result_var);
}

/* ----------------------------------------------------------------------
 *
 * Comparison functions
 *
 * Note: btree indexes need these routines not to leak memory; therefore,
 * be careful to free working copies of toasted datums.  Most places don't
 * need to be so careful.
 * ----------------------------------------------------------------------
 */


Datum
numeric_cmp(PG_FUNCTION_ARGS)
{
	Numeric		num1 = PG_GETARG_NUMERIC(0);
	Numeric		num2 = PG_GETARG_NUMERIC(1);
	int			result;

	result = cmp_numerics(num1, num2);

	PG_FREE_IF_COPY(num1, 0);
	PG_FREE_IF_COPY(num2, 1);

	PG_RETURN_INT32(result);
}


Datum
numeric_eq(PG_FUNCTION_ARGS)
{
	Numeric		num1 = PG_GETARG_NUMERIC(0);
	Numeric		num2 = PG_GETARG_NUMERIC(1);
	bool		result;

	result = cmp_numerics(num1, num2) == 0;

	PG_FREE_IF_COPY(num1, 0);
	PG_FREE_IF_COPY(num2, 1);

	PG_RETURN_BOOL(result);
}

Datum
numeric_ne(PG_FUNCTION_ARGS)
{
	Numeric		num1 = PG_GETARG_NUMERIC(0);
	Numeric		num2 = PG_GETARG_NUMERIC(1);
	bool		result;

	result = cmp_numerics(num1, num2) != 0;

	PG_FREE_IF_COPY(num1, 0);
	PG_FREE_IF_COPY(num2, 1);

	PG_RETURN_BOOL(result);
}

Datum
numeric_gt(PG_FUNCTION_ARGS)
{
	Numeric		num1 = PG_GETARG_NUMERIC(0);
	Numeric		num2 = PG_GETARG_NUMERIC(1);
	bool		result;

	result = cmp_numerics(num1, num2) > 0;

	PG_FREE_IF_COPY(num1, 0);
	PG_FREE_IF_COPY(num2, 1);

	PG_RETURN_BOOL(result);
}

Datum
numeric_ge(PG_FUNCTION_ARGS)
{
	Numeric		num1 = PG_GETARG_NUMERIC(0);
	Numeric		num2 = PG_GETARG_NUMERIC(1);
	bool		result;

	result = cmp_numerics(num1, num2) >= 0;

	PG_FREE_IF_COPY(num1, 0);
	PG_FREE_IF_COPY(num2, 1);

	PG_RETURN_BOOL(result);
}

Datum
numeric_lt(PG_FUNCTION_ARGS)
{
	Numeric		num1 = PG_GETARG_NUMERIC(0);
	Numeric		num2 = PG_GETARG_NUMERIC(1);
	bool		result;

	result = cmp_numerics(num1, num2) < 0;

	PG_FREE_IF_COPY(num1, 0);
	PG_FREE_IF_COPY(num2, 1);

	PG_RETURN_BOOL(result);
}

Datum
numeric_le(PG_FUNCTION_ARGS)
{
	Numeric		num1 = PG_GETARG_NUMERIC(0);
	Numeric		num2 = PG_GETARG_NUMERIC(1);
	bool		result;

	result = cmp_numerics(num1, num2) <= 0;

	PG_FREE_IF_COPY(num1, 0);
	PG_FREE_IF_COPY(num2, 1);

	PG_RETURN_BOOL(result);
}

static int
cmp_numerics(Numeric num1, Numeric num2)
{
	int			result;

	/*
	 * We consider all NANs to be equal and larger than any non-NAN. This is
	 * somewhat arbitrary; the important thing is to have a consistent sort
	 * order.
	 */
	if (NUMERIC_IS_NAN(num1))
	{
		if (NUMERIC_IS_NAN(num2))
			result = 0;			/* NAN = NAN */
		else
			result = 1;			/* NAN > non-NAN */
	}
	else if (NUMERIC_IS_NAN(num2))
	{
		result = -1;			/* non-NAN < NAN */
	}
	else
	{
		result = cmp_var_common(NUMERIC_DIGITS(num1), NUMERIC_NDIGITS(num1),
								NUMERIC_WEIGHT(num1), NUMERIC_SIGN(num1),
								NUMERIC_DIGITS(num2), NUMERIC_NDIGITS(num2),
								NUMERIC_WEIGHT(num2), NUMERIC_SIGN(num2));
	}

	return result;
}

Datum
hash_numeric(PG_FUNCTION_ARGS)
{
	Numeric 	key = PG_GETARG_NUMERIC(0);
	Datum 		digit_hash;
	Datum 		result;
	int 		weight;
	int 		start_offset;
	int 		end_offset;
	int 		i;
	int 		hash_len;

	/* If it's NaN, don't try to hash the rest of the fields */
	if (NUMERIC_IS_NAN(key))
		PG_RETURN_UINT32(0);

	weight 		 = key->n_weight;
	start_offset = 0;
	end_offset 	 = 0;

	/*
	 * Omit any leading or trailing zeros from the input to the
	 * hash. The numeric implementation *should* guarantee that
	 * leading and trailing zeros are suppressed, but we're
	 * paranoid. Note that we measure the starting and ending offsets
	 * in units of NumericDigits, not bytes.
	 */
	for (i = 0; i < NUMERIC_NDIGITS(key); i++)
	{
		if (NUMERIC_DIGITS(key)[i] != (NumericDigit) 0)
			break;

		start_offset++;
		/*
		 * The weight is effectively the # of digits before the
		 * decimal point, so decrement it for each leading zero we
		 * skip.
		 */
		weight--;
	}

	/*
	 * If there are no non-zero digits, then the value of the number
	 * is zero, regardless of any other fields.
	 */
	if (NUMERIC_NDIGITS(key) == start_offset)
		PG_RETURN_UINT32(-1);

	for (i = NUMERIC_NDIGITS(key) - 1; i >= 0; i--)
	{
		if (NUMERIC_DIGITS(key)[i] != (NumericDigit) 0)
			break;

		end_offset++;
	}

	/* If we get here, there should be at least one non-zero digit */
	Assert(start_offset + end_offset < NUMERIC_NDIGITS(key));

	/*
	 * Note that we don't hash on the Numeric's scale, since two
	 * numerics can compare equal but have different scales. We also
	 * don't hash on the sign, although we could: since a sign
	 * difference implies inequality, this shouldn't affect correctness.
	 */
	hash_len = NUMERIC_NDIGITS(key) - start_offset - end_offset;
	digit_hash = hash_any((unsigned char *) (NUMERIC_DIGITS(key) + start_offset),
                          hash_len * sizeof(NumericDigit));

	/* Mix in the weight, via XOR */
	result = digit_hash ^ weight;

	PG_RETURN_DATUM(result);
}


/* ----------------------------------------------------------------------
 *
 * Basic arithmetic functions
 *
 * ----------------------------------------------------------------------
 */


/*
 * numeric_add() -
 *
 *	Add two numerics
 */
Datum
numeric_add(PG_FUNCTION_ARGS)
{
	Numeric		num1 = PG_GETARG_NUMERIC(0);
	Numeric		num2 = PG_GETARG_NUMERIC(1);
	NumericVar	arg1;
	NumericVar	arg2;
	NumericVar	result;
	Numeric		res;

	/*
	 * Handle NaN
	 */
	if (NUMERIC_IS_NAN(num1) || NUMERIC_IS_NAN(num2))
		PG_RETURN_NUMERIC(make_result(&const_nan));

	/*
	 * Unpack the values, let add_var() compute the result and return it.
	 */
	quick_init_var(&result);

	init_ro_var_from_num(num1, &arg1);
	init_ro_var_from_num(num2, &arg2);

	add_var(&arg1, &arg2, &result);

	res = make_result(&result);

	PG_RETURN_NUMERIC(res);
}


/*
 * numeric_sub() -
 *
 *	Subtract one numeric from another
 */
Datum
numeric_sub(PG_FUNCTION_ARGS)
{
	Numeric		num1 = PG_GETARG_NUMERIC(0);
	Numeric		num2 = PG_GETARG_NUMERIC(1);
	NumericVar	arg1;
	NumericVar	arg2;
	NumericVar	result;
	Numeric		res;

	/*
	 * Handle NaN
	 */
	if (NUMERIC_IS_NAN(num1) || NUMERIC_IS_NAN(num2))
		PG_RETURN_NUMERIC(make_result(&const_nan));

	/*
	 * Unpack the values, let sub_var() compute the result and return it.
	 */
	quick_init_var(&result);

	init_ro_var_from_num(num1, &arg1);
	init_ro_var_from_num(num2, &arg2);

	sub_var(&arg1, &arg2, &result);

	res = make_result(&result);

	PG_RETURN_NUMERIC(res);
}


/*
 * numeric_mul() -
 *
 *	Calculate the product of two numerics
 */
Datum
numeric_mul(PG_FUNCTION_ARGS)
{
	Numeric		num1 = PG_GETARG_NUMERIC(0);
	Numeric		num2 = PG_GETARG_NUMERIC(1);
	NumericVar	arg1;
	NumericVar	arg2;
	NumericVar	result;
	Numeric		res;

	/*
	 * Handle NaN
	 */
	if (NUMERIC_IS_NAN(num1) || NUMERIC_IS_NAN(num2))
		PG_RETURN_NUMERIC(make_result(&const_nan));

	/*
	 * Unpack the values, let mul_var() compute the result and return it.
	 * Unlike add_var() and sub_var(), mul_var() will round its result. In the
	 * case of numeric_mul(), which is invoked for the * operator on numerics,
	 * we request exact representation for the product (rscale = sum(dscale of
	 * arg1, dscale of arg2)).
	 */
	quick_init_var(&result);

	init_ro_var_from_num(num1, &arg1);
	init_ro_var_from_num(num2, &arg2);

	mul_var(&arg1, &arg2, &result, arg1.dscale + arg2.dscale);

	free_var(&arg1);
	free_var(&arg2);

	res = make_result(&result);

	PG_RETURN_NUMERIC(res);
}


/*
 * numeric_div() -
 *
 *	Divide one numeric into another
 */
Datum
numeric_div(PG_FUNCTION_ARGS)
{
	Numeric		num1 = PG_GETARG_NUMERIC(0);
	Numeric		num2 = PG_GETARG_NUMERIC(1);
	NumericVar	arg1;
	NumericVar	arg2;
	NumericVar	result;
	Numeric		res;
	int			rscale;

	/*
	 * Handle NaN
	 */
	if (NUMERIC_IS_NAN(num1) || NUMERIC_IS_NAN(num2))
		PG_RETURN_NUMERIC(make_result(&const_nan));

	/*
	 * Unpack the arguments
	 */
	quick_init_var(&result);

	init_ro_var_from_num(num1, &arg1);
	init_ro_var_from_num(num2, &arg2);

	/*
	 * Select scale for division result
	 */
	rscale = select_div_scale(&arg1, &arg2);

	/*
	 * Do the divide and return the result
	 */
	div_var(&arg1, &arg2, &result, rscale, true);

	free_var(&arg1);
	free_var(&arg2);

	res = make_result(&result);

	PG_RETURN_NUMERIC(res);
}


/*
 * numeric_mod() -
 *
 *	Calculate the modulo of two numerics
 */
Datum
numeric_mod(PG_FUNCTION_ARGS)
{
	Numeric		num1 = PG_GETARG_NUMERIC(0);
	Numeric		num2 = PG_GETARG_NUMERIC(1);
	Numeric		res;
	NumericVar	arg1;
	NumericVar	arg2;
	NumericVar	result;

	if (NUMERIC_IS_NAN(num1) || NUMERIC_IS_NAN(num2))
		PG_RETURN_NUMERIC(make_result(&const_nan));

	quick_init_var(&result);

	init_ro_var_from_num(num1, &arg1);
	init_ro_var_from_num(num2, &arg2);

	mod_var(&arg1, &arg2, &result);

	free_var(&arg2);
	free_var(&arg1);

	res = make_result(&result);

	PG_RETURN_NUMERIC(res);
}

/*
 * numeric_inc() -
 *
 *	Increment a number by one
 */
Datum
numeric_inc(PG_FUNCTION_ARGS)
{
	Numeric		num = PG_GETARG_NUMERIC(0);
	NumericVar	arg;
	Numeric		res;

	/*
	 * Handle NaN
	 */
	if (NUMERIC_IS_NAN(num))
		PG_RETURN_NUMERIC(make_result(&const_nan));

	/*
	 * Compute the result and return it
	 */

	init_ro_var_from_num(num, &arg);

	add_var(&arg, &const_one, &arg);

	res = make_result(&arg);

	PG_RETURN_NUMERIC(res);
}

/*
 * numeric_dec() -
 *
 *	decrement a number by one
 */
Datum
numeric_dec(PG_FUNCTION_ARGS)
{
	Numeric		num = PG_GETARG_NUMERIC(0);
	NumericVar	arg;
	Numeric		res;

	/*
	 * Handle NaN
	 */
	if (NUMERIC_IS_NAN(num))
		PG_RETURN_NUMERIC(make_result(&const_nan));

	/*
	 * Compute the result and return it
	 */

	init_var_from_num(num, &arg);

	sub_var(&arg, &const_one, &arg);

	res = make_result(&arg);

	PG_RETURN_NUMERIC(res);
}


/*
 * numeric_smaller() -
 *
 *	Return the smaller of two numbers
 */
Datum
numeric_smaller(PG_FUNCTION_ARGS)
{
	Numeric		num1 = PG_GETARG_NUMERIC(0);
	Numeric		num2 = PG_GETARG_NUMERIC(1);

	/*
	 * Use cmp_numerics so that this will agree with the comparison operators,
	 * particularly as regards comparisons involving NaN.
	 */
	if (cmp_numerics(num1, num2) < 0)
		PG_RETURN_NUMERIC(num1);
	else
		PG_RETURN_NUMERIC(num2);
}


/*
 * numeric_larger() -
 *
 *	Return the larger of two numbers
 */
Datum
numeric_larger(PG_FUNCTION_ARGS)
{
	Numeric		num1 = PG_GETARG_NUMERIC(0);
	Numeric		num2 = PG_GETARG_NUMERIC(1);

	/*
	 * Use cmp_numerics so that this will agree with the comparison operators,
	 * particularly as regards comparisons involving NaN.
	 */
	if (cmp_numerics(num1, num2) > 0)
		PG_RETURN_NUMERIC(num1);
	else
		PG_RETURN_NUMERIC(num2);
}


/* ----------------------------------------------------------------------
 *
 * Advanced math functions
 *
 * ----------------------------------------------------------------------
 */

/*
 * numeric_fac()
 *
 * Compute factorial
 */
Datum
numeric_fac(PG_FUNCTION_ARGS)
{
	int64		num = PG_GETARG_INT64(0);
	Numeric		res;
	NumericVar	fact;
	NumericVar	result;

	if (num <= 1)
	{
		res = make_result(&const_one);
		PG_RETURN_NUMERIC(res);
	}

	/* Fail immediately if the result will overflow */
	if (num > 32177)
		ereport(ERROR, 
			(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
			 errmsg("value overflows numeric format"),
					 errOmitLocation(true)));

	quick_init_var(&fact);
	quick_init_var(&result);

	int8_to_numericvar(num, &result);

	for (num = num - 1; num > 1; num--)
	{
		/* This loop can take a while so allow it to be interrupted */
		CHECK_FOR_INTERRUPTS();

		int8_to_numericvar(num, &fact);

		mul_var(&result, &fact, &result, 0);
	}

	free_var(&fact);

	res = make_result(&result);

	PG_RETURN_NUMERIC(res);
}


/*
 * numeric_sqrt() -
 *
 *	Compute the square root of a numeric.
 */
Datum
numeric_sqrt(PG_FUNCTION_ARGS)
{
	Numeric		num = PG_GETARG_NUMERIC(0);
	Numeric		res;
	NumericVar	arg;
	NumericVar	result;
	int			sweight;
	int			rscale;

	/*
	 * Handle NaN
	 */
	if (NUMERIC_IS_NAN(num))
		PG_RETURN_NUMERIC(make_result(&const_nan));

	/*
	 * Unpack the argument and determine the result scale.	We choose a scale
	 * to give at least NUMERIC_MIN_SIG_DIGITS significant digits; but in any
	 * case not less than the input's dscale.
	 */
	quick_init_var(&result);

	init_ro_var_from_num(num, &arg);

	/* Assume the input was normalized, so arg.weight is accurate */
	sweight = (arg.weight + 1) * DEC_DIGITS / 2 - 1;

	rscale = NUMERIC_MIN_SIG_DIGITS - sweight;
	rscale = Max(rscale, arg.dscale);
	rscale = Max(rscale, NUMERIC_MIN_DISPLAY_SCALE);
	rscale = Min(rscale, NUMERIC_MAX_DISPLAY_SCALE);

	/*
	 * Let sqrt_var() do the calculation and return the result.
	 */
	sqrt_var(&arg, &result, rscale);
	free_var(&arg);

	res = make_result(&result);

	PG_RETURN_NUMERIC(res);
}


/*
 * numeric_exp() -
 *
 *	Raise e to the power of x
 */
Datum
numeric_exp(PG_FUNCTION_ARGS)
{
	Numeric		num = PG_GETARG_NUMERIC(0);
	Numeric		res;
	NumericVar	arg;
	NumericVar	result;
	int			rscale;
	double		val;

	/*
	 * Handle NaN
	 */
	if (NUMERIC_IS_NAN(num))
		PG_RETURN_NUMERIC(make_result(&const_nan));

	/*
	 * Unpack the argument and determine the result scale.	We choose a scale
	 * to give at least NUMERIC_MIN_SIG_DIGITS significant digits; but in any
	 * case not less than the input's dscale.
	 */
	quick_init_var(&result);

	init_var_from_num(num, &arg);

	/* convert input to float8, ignoring overflow */
	val = numericvar_to_double_no_overflow(&arg);

	/*
	 * log10(result) = num * log10(e), so this is approximately the decimal
	 * weight of the result:
	 */
	val *= 0.434294481903252;

	/* limit to something that won't cause integer overflow */
	val = Max(val, -NUMERIC_MAX_RESULT_SCALE);
	val = Min(val, NUMERIC_MAX_RESULT_SCALE);

	rscale = NUMERIC_MIN_SIG_DIGITS - (int) val;
	rscale = Max(rscale, arg.dscale);
	rscale = Max(rscale, NUMERIC_MIN_DISPLAY_SCALE);
	rscale = Min(rscale, NUMERIC_MAX_DISPLAY_SCALE);

	/*
	 * Let exp_var() do the calculation and return the result.
	 */
	exp_var(&arg, &result, rscale);

	free_var(&arg);

	res = make_result(&result);

	PG_RETURN_NUMERIC(res);
}


/*
 * numeric_ln() -
 *
 *	Compute the natural logarithm of x
 */
Datum
numeric_ln(PG_FUNCTION_ARGS)
{
	Numeric		num = PG_GETARG_NUMERIC(0);
	Numeric		res;
	NumericVar	arg;
	NumericVar	result;
	int			dec_digits;
	int			rscale;

	/*
	 * Handle NaN
	 */
	if (NUMERIC_IS_NAN(num))
		PG_RETURN_NUMERIC(make_result(&const_nan));

	quick_init_var(&result);

	init_ro_var_from_num(num, &arg);

	/* Approx decimal digits before decimal point */
	dec_digits = (arg.weight + 1) * DEC_DIGITS;

	if (dec_digits > 1)
		rscale = NUMERIC_MIN_SIG_DIGITS - (int) log10(dec_digits - 1);
	else if (dec_digits < 1)
		rscale = NUMERIC_MIN_SIG_DIGITS - (int) log10(1 - dec_digits);
	else
		rscale = NUMERIC_MIN_SIG_DIGITS;

	rscale = Max(rscale, arg.dscale);
	rscale = Max(rscale, NUMERIC_MIN_DISPLAY_SCALE);
	rscale = Min(rscale, NUMERIC_MAX_DISPLAY_SCALE);

	ln_var(&arg, &result, rscale);

	free_var(&arg);

	res = make_result(&result);

	PG_RETURN_NUMERIC(res);
}


/*
 * numeric_log() -
 *
 *	Compute the logarithm of x in a given base
 */
Datum
numeric_log(PG_FUNCTION_ARGS)
{
	Numeric		num1 = PG_GETARG_NUMERIC(0);
	Numeric		num2 = PG_GETARG_NUMERIC(1);
	Numeric		res;
	NumericVar	arg1;
	NumericVar	arg2;
	NumericVar	result;

	/*
	 * Handle NaN
	 */
	if (NUMERIC_IS_NAN(num1) || NUMERIC_IS_NAN(num2))
		PG_RETURN_NUMERIC(make_result(&const_nan));

	/*
	 * Initialize things
	 */
	quick_init_var(&result);

	init_ro_var_from_num(num1, &arg1);
	init_ro_var_from_num(num2, &arg2);

	/*
	 * Call log_var() to compute and return the result; note it handles scale
	 * selection itself.
	 */
	log_var(&arg1, &arg2, &result);

	free_var(&arg2);
	free_var(&arg1);

	res = make_result(&result);

	PG_RETURN_NUMERIC(res);
}


/*
 * numeric_power() -
 *
 *	Raise b to the power of x
 */
Datum
numeric_power(PG_FUNCTION_ARGS)
{
	Numeric		num1 = PG_GETARG_NUMERIC(0);
	Numeric		num2 = PG_GETARG_NUMERIC(1);
	Numeric		res;
	NumericVar	arg1;
	NumericVar	arg2;
	NumericVar	arg2_trunc;
	NumericVar	result;

	/*
	 * Handle NaN
	 */
	if (NUMERIC_IS_NAN(num1) || NUMERIC_IS_NAN(num2))
		PG_RETURN_NUMERIC(make_result(&const_nan));

	/*
	 * Initialize things
	 */
	quick_init_var(&result);

	init_ro_var_from_num(num1, &arg1);
	init_ro_var_from_num(num2, &arg2);
	init_var_from_var(&arg2, &arg2_trunc);

	trunc_var(&arg2_trunc, 0);

	/*
	 * Return special SQLSTATE error codes for a few conditions mandated by
	 * the standard.
	 */
	if ((cmp_var(&arg1, &const_zero) == 0 &&
		 cmp_var(&arg2, &const_zero) < 0) ||
		(cmp_var(&arg1, &const_zero) < 0 &&
		 cmp_var(&arg2, &arg2_trunc) != 0))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_ARGUMENT_FOR_POWER_FUNCTION),
				 errmsg("invalid argument for power function"),
						 errOmitLocation(true)));

	/*
	 * Call power_var() to compute and return the result; note it handles
	 * scale selection itself.
	 */
	power_var(&arg1, &arg2, &result);

	free_var(&arg2);
	free_var(&arg2_trunc);
	free_var(&arg1);

	res = make_result(&result);

	PG_RETURN_NUMERIC(res);
}


/* ----------------------------------------------------------------------
 *
 * Type conversion functions
 *
 * ----------------------------------------------------------------------
 */


Datum
int4_numeric(PG_FUNCTION_ARGS)
{
	int32		val = PG_GETARG_INT32(0);
	Numeric		res;
	NumericVar	result;

	quick_init_var(&result);

	int8_to_numericvar((int64) val, &result);

	res = make_result(&result);

	PG_RETURN_NUMERIC(res);
}


Datum
numeric_int4(PG_FUNCTION_ARGS)
{
	Numeric		num = PG_GETARG_NUMERIC(0);
	NumericVar	x;
	int32		result;

	/* XXX would it be better to return NULL? */
	if (NUMERIC_IS_NAN(num))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot convert NaN to integer"),
						 errOmitLocation(true)));

	/* Convert to variable format, then convert to int4 */
	init_var_from_num(num, &x);
	result = numericvar_to_int4(&x);

	PG_RETURN_INT32(result);
}

/*
 * Given a NumericVar, convert it to an int32. If the NumericVar
 * exceeds the range of an int32, raise the appropriate error via
 * ereport(). The input NumericVar is *not* free'd.
 */
static int32
numericvar_to_int4(NumericVar *var)
{
	int32		result;
	int64		val = 0;

	if (!numericvar_to_int8(var, &val))
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("integer out of range"),
						 errOmitLocation(true)));

	/* Down-convert to int4 */
	result = (int32) val;

	/* Test for overflow by reverse-conversion. */
	if ((int64) result != val)
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("integer out of range"),
						 errOmitLocation(true)));

	return result;
}

Datum
int8_numeric(PG_FUNCTION_ARGS)
{
	int64		val = PG_GETARG_INT64(0);
	Numeric		res;
	NumericVar	result;

	quick_init_var(&result);

	int8_to_numericvar(val, &result);

	res = make_result(&result);

	PG_RETURN_NUMERIC(res);
}


Datum
numeric_int8(PG_FUNCTION_ARGS)
{
	Numeric		num = PG_GETARG_NUMERIC(0);
	NumericVar	x;
	int64		result = 0;

	/* XXX would it be better to return NULL? */
	if (NUMERIC_IS_NAN(num))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot convert NaN to bigint"),
						 errOmitLocation(true)));

	/* Convert to variable format and thence to int8 */
	init_var_from_num(num, &x);

	if (!numericvar_to_int8(&x, &result))
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("bigint out of range"),
						 errOmitLocation(true)));

	PG_RETURN_INT64(result);
}


Datum
int2_numeric(PG_FUNCTION_ARGS)
{
	int16		val = PG_GETARG_INT16(0);
	Numeric		res;
	NumericVar	result;

	quick_init_var(&result);

	int8_to_numericvar((int64) val, &result);

	res = make_result(&result);

	PG_RETURN_NUMERIC(res);
}


Datum
numeric_int2(PG_FUNCTION_ARGS)
{
	Numeric		num = PG_GETARG_NUMERIC(0);
	NumericVar	x;
	int64		val = 0;
	int16		result;

	/* XXX would it be better to return NULL? */
	if (NUMERIC_IS_NAN(num))
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot convert NaN to smallint"),
						 errOmitLocation(true)));

	/* Convert to variable format and thence to int8 */
	init_var_from_num(num, &x);

	if (!numericvar_to_int8(&x, &val))
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("smallint out of range"),
						 errOmitLocation(true)));

	/* Down-convert to int2 */
	result = (int16) val;

	/* Test for overflow by reverse-conversion. */
	if ((int64) result != val)
		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("smallint out of range"),
						 errOmitLocation(true)));

	PG_RETURN_INT16(result);
}


Datum
float8_numeric(PG_FUNCTION_ARGS)
{
	float8		val = PG_GETARG_FLOAT8(0);
	Numeric		res;
	NumericVar	result;
	char		buf[DBL_DIG + 100];

	if (isnan(val))
		PG_RETURN_NUMERIC(make_result(&const_nan));

	sprintf(buf, "%.*g", DBL_DIG, val);

	init_var_from_str(buf, &result);
	res = make_result(&result);

	PG_RETURN_NUMERIC(res);
}


Datum
numeric_float8(PG_FUNCTION_ARGS)
{
	Numeric		num = PG_GETARG_NUMERIC(0);
	char	   *tmp;
	Datum		result;

	if (NUMERIC_IS_NAN(num))
		PG_RETURN_FLOAT8(get_float8_nan());

	tmp = DatumGetCString(DirectFunctionCall1(numeric_out,
											  NumericGetDatum(num)));

	result = DirectFunctionCall1(float8in, CStringGetDatum(tmp));

	pfree(tmp);

	PG_RETURN_DATUM(result);
}


/* Convert numeric to float8; if out of range, return +/- HUGE_VAL */
Datum
numeric_float8_no_overflow(PG_FUNCTION_ARGS)
{
	Numeric		num = PG_GETARG_NUMERIC(0);
	double		val;

	if (NUMERIC_IS_NAN(num))
		PG_RETURN_FLOAT8(get_float8_nan());

	val = numeric_to_double_no_overflow(num);

	PG_RETURN_FLOAT8(val);
}

Datum
float4_numeric(PG_FUNCTION_ARGS)
{
	float4		val = PG_GETARG_FLOAT4(0);
	Numeric		res;
	NumericVar	result;
	char		buf[FLT_DIG + 100];

	if (isnan(val))
		PG_RETURN_NUMERIC(make_result(&const_nan));

	sprintf(buf, "%.*g", FLT_DIG, val);

	init_var_from_str(buf, &result);
	res = make_result(&result);

	PG_RETURN_NUMERIC(res);
}


Datum
numeric_float4(PG_FUNCTION_ARGS)
{
	Numeric		num = PG_GETARG_NUMERIC(0);
	char	   *tmp;
	Datum		result;

	if (NUMERIC_IS_NAN(num))
		PG_RETURN_FLOAT4(get_float4_nan());

	tmp = DatumGetCString(DirectFunctionCall1(numeric_out,
											  NumericGetDatum(num)));

	result = DirectFunctionCall1(float4in, CStringGetDatum(tmp));

	pfree(tmp);

	PG_RETURN_DATUM(result);
}


Datum
text_numeric(PG_FUNCTION_ARGS)
{
	text	   *str = PG_GETARG_TEXT_P(0);
	int			len;
	char	   *s;
	Datum		result;
	char		ts[NUMERIC_LOCAL_DTXT];

	len = (VARSIZE(str) - VARHDRSZ);
	if (len > NUMERIC_LOCAL_DMAX)
		s = palloc(len + 1);
	else
		s = ts;

	memcpy(s, VARDATA(str), len);
	*(s + len) = '\0';

	result = DirectFunctionCall3(numeric_in, CStringGetDatum(s),
								 ObjectIdGetDatum(0), Int32GetDatum(-1));

	if (s != ts)
		pfree(s);

	return result;
}

Datum
numeric_text(PG_FUNCTION_ARGS)
{
	/* val is numeric, but easier to leave it as Datum */
	Datum		val = PG_GETARG_DATUM(0);
	char	   *s;
	int			len;
	text	   *result;

	s = DatumGetCString(DirectFunctionCall1(numeric_out, val));
	len = strlen(s);

	result = (text *) palloc(VARHDRSZ + len);

	SET_VARSIZE(result, VARHDRSZ + len);
	memcpy(VARDATA(result), s, len);

	pfree(s);

	PG_RETURN_TEXT_P(result);
}


/* ----------------------------------------------------------------------
 *
 * Aggregate functions
 *
 * The transition datatype for all these aggregates is a 3-element array
 * of Numeric, holding the values N, sum(X), sum(X*X) in that order.
 *
 * We represent N as a numeric mainly to avoid having to build a special
 * datatype; it's unlikely it'd overflow an int4, but ...
 *
 * ----------------------------------------------------------------------
 */

static inline ArrayType *
do_numeric_accum_decum(ArrayType *transarray, Numeric newval, bool accum)
{
	Datum	   *transdatums;
	int			ndatums;
	Datum		N,
				sumX,
				sumX2;
	ArrayType  *result;

	/* We assume the input is array of numeric */
	deconstruct_array(transarray,
					  NUMERICOID, -1, false, 'i',
					  &transdatums, NULL, &ndatums);
	if (ndatums != 3)
		elog(ERROR, "expected 3-element numeric array");
	N = transdatums[0];
	sumX = transdatums[1];
	sumX2 = transdatums[2];

	if (accum)
	{
		N = DirectFunctionCall1(numeric_inc, N);
		sumX = DirectFunctionCall2(numeric_add, sumX,
								   NumericGetDatum(newval));
		sumX2 = DirectFunctionCall2(numeric_add, sumX2,
									DirectFunctionCall2(numeric_mul,
														NumericGetDatum(newval),
														NumericGetDatum(newval)));
	}
	else
	{
		N = DirectFunctionCall1(numeric_dec, N);
		sumX = DirectFunctionCall2(numeric_sub, sumX,
								   NumericGetDatum(newval));
		sumX2 = DirectFunctionCall2(numeric_sub, sumX2,
									DirectFunctionCall2(numeric_mul,
														NumericGetDatum(newval),
														NumericGetDatum(newval)));


	}
	transdatums[0] = N;
	transdatums[1] = sumX;
	transdatums[2] = sumX2;

	result = construct_array(transdatums, 3,
							 NUMERICOID, -1, false, 'i');

	return result;
}

Datum
numeric_accum(PG_FUNCTION_ARGS)
{
	ArrayType  *transarray = PG_GETARG_ARRAYTYPE_P(0);
	Numeric		newval = PG_GETARG_NUMERIC(1);

	PG_RETURN_ARRAYTYPE_P(do_numeric_accum_decum(transarray, newval, true));
}

Datum
numeric_decum(PG_FUNCTION_ARGS)
{
	ArrayType  *transarray = PG_GETARG_ARRAYTYPE_P(0);
	Numeric		newval = PG_GETARG_NUMERIC(1);

	PG_RETURN_ARRAYTYPE_P(do_numeric_accum_decum(transarray, newval, false));
}

/*
 * Integer data types all use Numeric accumulators to share code and
 * avoid risk of overflow.	For int2 and int4 inputs, Numeric accumulation
 * is overkill for the N and sum(X) values, but definitely not overkill
 * for the sum(X*X) value.	Hence, we use int2_accum and int4_accum only
 * for stddev/variance --- there are faster special-purpose accumulator
 * routines for SUM and AVG of these datatypes.
 */

Datum
int2_accum(PG_FUNCTION_ARGS)
{
	ArrayType  *transarray = PG_GETARG_ARRAYTYPE_P(0);
	Datum		newval2 = PG_GETARG_DATUM(1);
	Numeric		newval;

	newval = DatumGetNumeric(DirectFunctionCall1(int2_numeric, newval2));

	PG_RETURN_ARRAYTYPE_P(do_numeric_accum_decum(transarray, newval, true));
}

Datum
int4_accum(PG_FUNCTION_ARGS)
{
	ArrayType  *transarray = PG_GETARG_ARRAYTYPE_P(0);
	Datum		newval4 = PG_GETARG_DATUM(1);
	Numeric		newval;

	newval = DatumGetNumeric(DirectFunctionCall1(int4_numeric, newval4));

	PG_RETURN_ARRAYTYPE_P(do_numeric_accum_decum(transarray, newval, true));
}

Datum
int8_accum(PG_FUNCTION_ARGS)
{
	ArrayType  *transarray = PG_GETARG_ARRAYTYPE_P(0);
	Datum		newval8 = PG_GETARG_DATUM(1);
	Numeric		newval;

	newval = DatumGetNumeric(DirectFunctionCall1(int8_numeric, newval8));

	PG_RETURN_ARRAYTYPE_P(do_numeric_accum_decum(transarray, newval, true));
}

/*
 * Integer decumulators for windowing.
 */
Datum
int2_decum(PG_FUNCTION_ARGS)
{
	ArrayType  *transarray = PG_GETARG_ARRAYTYPE_P(0);
	Datum		newval2 = PG_GETARG_DATUM(1);
	Numeric		newval;

	newval = DatumGetNumeric(DirectFunctionCall1(int2_numeric, newval2));

	PG_RETURN_ARRAYTYPE_P(do_numeric_accum_decum(transarray, newval, false));
}

Datum
int4_decum(PG_FUNCTION_ARGS)
{
	ArrayType  *transarray = PG_GETARG_ARRAYTYPE_P(0);
	Datum		newval4 = PG_GETARG_DATUM(1);
	Numeric		newval;

	newval = DatumGetNumeric(DirectFunctionCall1(int4_numeric, newval4));

	PG_RETURN_ARRAYTYPE_P(do_numeric_accum_decum(transarray, newval, false));
}

Datum
int8_decum(PG_FUNCTION_ARGS)
{
	ArrayType  *transarray = PG_GETARG_ARRAYTYPE_P(0);
	Datum		newval8 = PG_GETARG_DATUM(1);
	Numeric		newval;

	newval = DatumGetNumeric(DirectFunctionCall1(int8_numeric, newval8));

	PG_RETURN_ARRAYTYPE_P(do_numeric_accum_decum(transarray, newval, false));
}


/*
 * Workhorse routine for the standard deviance and variance
 * aggregates. 'transarray' is the aggregate's transition
 * array. 'variance' specifies whether we should calculate the
 * variance or the standard deviation. 'sample' indicates whether the
 * caller is interested in the sample or the population
 * variance/stddev.
 *
 * If appropriate variance statistic is undefined for the input,
 * *is_null is set to true and NULL is returned.
 */
static Numeric
numeric_stddev_internal(ArrayType *transarray,
						bool variance, bool sample,
						bool *is_null)
{
	Datum	   *transdatums;
	int			ndatums;
	Numeric		N,
				sumX,
				sumX2,
				res;
	NumericVar	vN,
				vsumX,
				vsumX2,
				vNminus1;
	NumericVar *comp;
	int			rscale;

	*is_null = false;

	/* We assume the input is array of numeric */
	deconstruct_array(transarray,
					  NUMERICOID, -1, false, 'i',
					  &transdatums, NULL, &ndatums);
	if (ndatums != 3)
		elog(ERROR, "expected 3-element numeric array");
	N = DatumGetNumeric(transdatums[0]);
	sumX = DatumGetNumeric(transdatums[1]);
	sumX2 = DatumGetNumeric(transdatums[2]);

	if (NUMERIC_IS_NAN(N) || NUMERIC_IS_NAN(sumX) || NUMERIC_IS_NAN(sumX2))
		return make_result(&const_nan);

	init_ro_var_from_num(N, &vN);

	/*
	 * Sample stddev and variance are undefined when N <= 1; population stddev
	 * is undefined when N == 0. Return NULL in either case.
	 */
	if (sample)
		comp = &const_one;
	else
		comp = &const_zero;

	if (cmp_var(&vN, comp) <= 0)
	{
		free_var(&vN);
		*is_null = true;
		return NULL;
	}

	quick_init_var(&vNminus1);
	sub_var(&vN, &const_one, &vNminus1);

	init_var_from_num(sumX, &vsumX);
	init_var_from_num(sumX2, &vsumX2);

	/* compute rscale for mul_var calls */
	rscale = vsumX.dscale * 2;

	mul_var(&vsumX, &vsumX, &vsumX, rscale);	/* vsumX = sumX * sumX */
	mul_var(&vN, &vsumX2, &vsumX2, rscale);		/* vsumX2 = N * sumX2 */
	sub_var(&vsumX2, &vsumX, &vsumX2);	/* N * sumX2 - sumX * sumX */

	if (cmp_var(&vsumX2, &const_zero) <= 0)
	{
		/* Watch out for roundoff error producing a negative numerator */
		res = make_result(&const_zero);
	}
	else
	{
		if (sample)
			mul_var(&vN, &vNminus1, &vNminus1, 0);	/* N * (N - 1) */
		else
			mul_var(&vN, &vN, &vNminus1, 0);		/* N * N */
		rscale = select_div_scale(&vsumX2, &vNminus1);
		div_var(&vsumX2, &vNminus1, &vsumX, rscale, true);		/* variance */
		if (!variance)
			sqrt_var(&vsumX, &vsumX, rscale);	/* stddev */

		res = make_result(&vsumX);
	}

	free_var(&vN);
	free_var(&vNminus1);
	free_var(&vsumX);
	free_var(&vsumX2);

	return res;
}

Datum
numeric_var_samp(PG_FUNCTION_ARGS)
{
	Numeric		res;
	bool		is_null;

	res = numeric_stddev_internal(PG_GETARG_ARRAYTYPE_P(0),
								  true, true, &is_null);

	if (is_null)
		PG_RETURN_NULL();
	else
		PG_RETURN_NUMERIC(res);
}

Datum
numeric_stddev_samp(PG_FUNCTION_ARGS)
{
	Numeric		res;
	bool		is_null;

	res = numeric_stddev_internal(PG_GETARG_ARRAYTYPE_P(0),
								  false, true, &is_null);

	if (is_null)
		PG_RETURN_NULL();
	else
		PG_RETURN_NUMERIC(res);
}

Datum
numeric_var_pop(PG_FUNCTION_ARGS)
{
	Numeric		res;
	bool		is_null;

	res = numeric_stddev_internal(PG_GETARG_ARRAYTYPE_P(0),
								  true, false, &is_null);

	if (is_null)
		PG_RETURN_NULL();
	else
		PG_RETURN_NUMERIC(res);
}

Datum
numeric_stddev_pop(PG_FUNCTION_ARGS)
{
	Numeric		res;
	bool		is_null;

	res = numeric_stddev_internal(PG_GETARG_ARRAYTYPE_P(0),
								  false, false, &is_null);

	if (is_null)
		PG_RETURN_NULL();
	else
		PG_RETURN_NUMERIC(res);
}

/*
 * SUM transition functions for integer datatypes.
 *
 * To avoid overflow, we use accumulators wider than the input datatype.
 * A Numeric accumulator is needed for int8 input; for int4 and int2
 * inputs, we use int8 accumulators which should be sufficient for practical
 * purposes.  (The latter two therefore don't really belong in this file,
 * but we keep them here anyway.)
 *
 * Because SQL92 defines the SUM() of no values to be NULL, not zero,
 * the initial condition of the transition data value needs to be NULL. This
 * means we can't rely on ExecAgg to automatically insert the first non-null
 * data value into the transition data: it doesn't know how to do the type
 * conversion.	The upshot is that these routines have to be marked non-strict
 * and handle substitution of the first non-null input themselves.
 */

Datum
int2_sum(PG_FUNCTION_ARGS)
{
	int64		newval;
	int64 		oldsum;

	if (PG_ARGISNULL(0))
	{
		/* No non-null input seen so far... */
		if (PG_ARGISNULL(1))
			PG_RETURN_NULL();	/* still no non-null */
		/* This is the first non-null input. */
		newval = (int64) PG_GETARG_INT16(1);
		PG_RETURN_INT64(newval);
	}

	oldsum = PG_GETARG_INT64(0);

	/* Leave sum unchanged if new input is null. */
	if (PG_ARGISNULL(1))
		PG_RETURN_INT64(oldsum);

	/* OK to do the addition. */
	newval = oldsum + (int64) PG_GETARG_INT16(1);

	PG_RETURN_INT64(newval);
}

Datum
int4_sum(PG_FUNCTION_ARGS)
{
	int64		val;

	if (PG_ARGISNULL(0))
	{
		/* No non-null input seen so far... */
		if (PG_ARGISNULL(1))
			PG_RETURN_NULL();	/* still no non-null */
		/* This is the first non-null input. */
		val = (int64) PG_GETARG_INT32(1);
		PG_RETURN_INT64(val);
	}

	val = PG_GETARG_INT64(0);

	/* Leave sum unchanged if new input is null. */
	if (PG_ARGISNULL(1))
		PG_RETURN_INT64(val); 

	/* OK to do the addition. */
	val = val + (int64) PG_GETARG_INT32(1);

	PG_RETURN_INT64(val);
}

Datum
int8_sum(PG_FUNCTION_ARGS)
{
	Numeric		oldsum;
	Datum		newval;

	if (PG_ARGISNULL(0))
	{
		/* No non-null input seen so far... */
		if (PG_ARGISNULL(1))
			PG_RETURN_NULL();	/* still no non-null */
		/* This is the first non-null input. */
		newval = DirectFunctionCall1(int8_numeric, PG_GETARG_DATUM(1));
		PG_RETURN_DATUM(newval);
	}

	/*
	 * Note that we cannot special-case the nodeAgg case here, as we do for
	 * int2_sum and int4_sum: numeric is of variable size, so we cannot modify
	 * our first parameter in-place.
	 */

	oldsum = PG_GETARG_NUMERIC(0);

	/* Leave sum unchanged if new input is null. */
	if (PG_ARGISNULL(1))
		PG_RETURN_NUMERIC(oldsum);

	/* OK to do the addition. */
	newval = DirectFunctionCall1(int8_numeric, PG_GETARG_DATUM(1));

	PG_RETURN_DATUM(DirectFunctionCall2(numeric_add,
										NumericGetDatum(oldsum), newval));
}

/*
 * SUM inverse transition functions for integer types. We essentially do the
 * opposite of the above routines.
 */
Datum
int2_invsum(PG_FUNCTION_ARGS)
{
	int64		newval;
	int64 		oldsum;

	if (PG_ARGISNULL(0))
	{
		/* No non-null input seen so far... */
		if (PG_ARGISNULL(1))
			PG_RETURN_NULL();	/* still no non-null */
		/* 
		 * We shouldn't be called with non-null input if the transition value
		 * is NULL
		 */
		elog(ERROR, "internal error: inversion function called with NULL "
			 "transition value but non-NULL argument");
	}

	oldsum = PG_GETARG_INT64(0);

	/* Leave sum unchanged if new input is null. */
	if (PG_ARGISNULL(1))
		PG_RETURN_INT64(oldsum);

	/* OK to do the addition. */
	newval = oldsum - (int64) PG_GETARG_INT16(1);

	PG_RETURN_INT64(newval);
}

Datum
int4_invsum(PG_FUNCTION_ARGS)
{
	int64		newval;
	int64 		oldsum;

	if (PG_ARGISNULL(0))
	{
		/* No non-null input seen so far... */
		if (PG_ARGISNULL(1))
			PG_RETURN_NULL();	/* still no non-null */
		/* 
		 * We shouldn't be called with non-null input if the transition value
		 * is NULL
		 */
		elog(ERROR, "internal error: inversion function called with NULL "
			 "transition value but non-NULL argument");
	}

	oldsum = PG_GETARG_INT64(0);

	/* Leave sum unchanged if new input is null. */
	if (PG_ARGISNULL(1))
		PG_RETURN_INT64(oldsum);

	/* OK to do the addition. */
	newval = oldsum - (int64) PG_GETARG_INT32(1);

	PG_RETURN_INT64(newval);
}

Datum
int8_invsum(PG_FUNCTION_ARGS)
{
	Numeric		oldsum;
	Datum		newval;

	if (PG_ARGISNULL(0))
	{
		/* No non-null input seen so far... */
		if (PG_ARGISNULL(1))
			PG_RETURN_NULL();	/* still no non-null */

		/* 
		 * We shouldn't be called with non-null input if the transition value
		 * is NULL
		 */
		elog(ERROR, "internal error: inversion function called with NULL "
			 "transition value but non-NULL argument");
	}

	/*
	 * Note that we cannot special-case the nodeAgg case here, as we do for
	 * int2_sum and int4_sum: numeric is of variable size, so we cannot modify
	 * our first parameter in-place.
	 */

	oldsum = PG_GETARG_NUMERIC(0);

	/* Leave sum unchanged if new input is null. */
	if (PG_ARGISNULL(1))
		PG_RETURN_NUMERIC(oldsum);

	/* OK to do the addition. */
	newval = DirectFunctionCall1(int8_numeric, PG_GETARG_DATUM(1));

	PG_RETURN_DATUM(DirectFunctionCall2(numeric_sub,
										NumericGetDatum(oldsum), newval));
}

/*
 * Routines for avg int type.  The transition datatype is a int64 for count, and a float8 for sum.
 */

typedef struct IntFloatAvgTransdata
{
	int32   _len; /* len for varattrib, do not touch directly */
#if 1
	int32   pad;  /* pad so int64 and float64 will be 8 bytes aligned */
#endif
	int64 	count;
	float8 sum;
} IntFloatAvgTransdata;

static inline Datum intfloat_avg_accum_decum(IntFloatAvgTransdata *transdata, float8 newval, bool acc)
{
	if(transdata == NULL || VARSIZE(transdata) != sizeof(IntFloatAvgTransdata))
	{
		/* If first time execution, need to allocate memory for this */
		Assert(acc);
		transdata = (IntFloatAvgTransdata *) palloc(sizeof(IntFloatAvgTransdata));
		SET_VARSIZE(transdata, sizeof(IntFloatAvgTransdata));
		transdata->count = 0;
		transdata->sum = 0;
	}
	else
	{
		Assert(VARSIZE(transdata) == sizeof(IntFloatAvgTransdata));
	}

	if(acc)
	{
		++transdata->count;
		transdata->sum += newval;
	}
	else
	{
		--transdata->count;
		transdata->sum -= newval;
	}

	return PointerGetDatum(transdata); 
}

static inline Datum
intfloat_avg_amalg_demalg(IntFloatAvgTransdata* tr0,
						  IntFloatAvgTransdata *tr1, bool amalg)
{
	if(tr0 == NULL || VARSIZE(tr0) != sizeof(IntFloatAvgTransdata))
	{
		tr0 = (IntFloatAvgTransdata *) palloc(sizeof(IntFloatAvgTransdata));
		SET_VARSIZE(tr0, sizeof(IntFloatAvgTransdata));
		tr0->count = 0;
		tr0->sum = 0;
	}

	if(tr1 == NULL || VARSIZE(tr1) != sizeof(IntFloatAvgTransdata))
		return PointerGetDatum(tr0);

	Assert(VARSIZE(tr0) == sizeof(IntFloatAvgTransdata));
	Assert(VARSIZE(tr1) == sizeof(IntFloatAvgTransdata));

	if(amalg)
	{
		tr0->count += tr1->count;
		tr0->sum += tr1->sum;
	}
	else
	{
		tr0->count -= tr1->count;
		tr0->sum -= tr1->sum;
	}

	return PointerGetDatum(tr0);
}

Datum
int2_avg_accum(PG_FUNCTION_ARGS)
{
	IntFloatAvgTransdata *tr = (IntFloatAvgTransdata *) PG_GETARG_BYTEA_P(0);
	int16		newval = PG_GETARG_INT16(1);

	Assert(fcinfo->context && IS_AGG_EXECUTION_NODE(fcinfo->context));
	return intfloat_avg_accum_decum(tr, newval, true);
}
		
Datum
int4_avg_accum(PG_FUNCTION_ARGS)
{
	IntFloatAvgTransdata *tr = (IntFloatAvgTransdata *) PG_GETARG_BYTEA_P(0);
	int32		newval = PG_GETARG_INT32(1);

	Assert(fcinfo->context && IS_AGG_EXECUTION_NODE(fcinfo->context));
	return intfloat_avg_accum_decum(tr, newval, true);
}

Datum
int8_avg_accum(PG_FUNCTION_ARGS)
{
	IntFloatAvgTransdata *tr = (IntFloatAvgTransdata *) PG_GETARG_BYTEA_P(0);
	int64		newval = PG_GETARG_INT64(1);

	Assert(fcinfo->context && IS_AGG_EXECUTION_NODE(fcinfo->context));
	return intfloat_avg_accum_decum(tr, newval, true);
}

Datum
float4_avg_accum(PG_FUNCTION_ARGS)
{
	IntFloatAvgTransdata *tr = (IntFloatAvgTransdata *) PG_GETARG_BYTEA_P(0);
	float4		newval = PG_GETARG_FLOAT4(1);

	Assert(fcinfo->context && IS_AGG_EXECUTION_NODE(fcinfo->context));
	return intfloat_avg_accum_decum(tr, newval, true);
}

Datum
float8_avg_accum(PG_FUNCTION_ARGS)
{
	IntFloatAvgTransdata *tr = (IntFloatAvgTransdata *) PG_GETARG_BYTEA_P(0);
	float8		newval = PG_GETARG_FLOAT8(1);

	Assert(fcinfo->context && IS_AGG_EXECUTION_NODE(fcinfo->context));
	return intfloat_avg_accum_decum(tr, newval, true);
}

Datum
int2_avg_decum(PG_FUNCTION_ARGS)
{
	IntFloatAvgTransdata *tr = (IntFloatAvgTransdata *) PG_GETARG_BYTEA_P(0);
	int16		newval = PG_GETARG_INT16(1);

	Assert(fcinfo->context && IS_AGG_EXECUTION_NODE(fcinfo->context));
	return intfloat_avg_accum_decum(tr, newval, false);
}

Datum
int4_avg_decum(PG_FUNCTION_ARGS)
{
	IntFloatAvgTransdata *tr = (IntFloatAvgTransdata *) PG_GETARG_BYTEA_P(0);
	int32		newval = PG_GETARG_INT32(1);

	Assert(fcinfo->context && IS_AGG_EXECUTION_NODE(fcinfo->context));
	return intfloat_avg_accum_decum(tr, newval, false);
}

Datum
int8_avg_decum(PG_FUNCTION_ARGS)
{
	IntFloatAvgTransdata *tr = (IntFloatAvgTransdata *) PG_GETARG_BYTEA_P(0);
	int64		newval = PG_GETARG_INT64(1);

	Assert(fcinfo->context && IS_AGG_EXECUTION_NODE(fcinfo->context));
	return intfloat_avg_accum_decum(tr, newval, false);
}

Datum
float4_avg_decum(PG_FUNCTION_ARGS)
{
	IntFloatAvgTransdata *tr = (IntFloatAvgTransdata *) PG_GETARG_BYTEA_P(0);
	float4		newval = PG_GETARG_FLOAT4(1);

	Assert(fcinfo->context && IS_AGG_EXECUTION_NODE(fcinfo->context));
	return intfloat_avg_accum_decum(tr, newval, false);
}

Datum
float8_avg_decum(PG_FUNCTION_ARGS)
{
	IntFloatAvgTransdata *tr = (IntFloatAvgTransdata *) PG_GETARG_BYTEA_P(0);
	float8		newval = PG_GETARG_FLOAT8(1);

	Assert(fcinfo->context && IS_AGG_EXECUTION_NODE(fcinfo->context));
	return intfloat_avg_accum_decum(tr, newval, false);
}

Datum 
int8_avg_amalg(PG_FUNCTION_ARGS)
{
	IntFloatAvgTransdata* d0 = (IntFloatAvgTransdata *) PG_GETARG_BYTEA_P(0);
	IntFloatAvgTransdata* d1 = (IntFloatAvgTransdata *) PG_GETARG_BYTEA_P(1);

	Assert(fcinfo->context && IS_AGG_EXECUTION_NODE(fcinfo->context));
	return intfloat_avg_amalg_demalg(d0, d1, true);
}

Datum 
float8_avg_amalg(PG_FUNCTION_ARGS)
{
	IntFloatAvgTransdata* d0 = (IntFloatAvgTransdata *) PG_GETARG_BYTEA_P(0);
	IntFloatAvgTransdata* d1 = (IntFloatAvgTransdata *) PG_GETARG_BYTEA_P(1);

	Assert(fcinfo->context && IS_AGG_EXECUTION_NODE(fcinfo->context));
	return intfloat_avg_amalg_demalg(d0, d1, true);
}

Datum
int8_avg_demalg(PG_FUNCTION_ARGS)
{
	IntFloatAvgTransdata* d0 = (IntFloatAvgTransdata *) PG_GETARG_BYTEA_P(0);
	IntFloatAvgTransdata* d1 = (IntFloatAvgTransdata *) PG_GETARG_BYTEA_P(1);
	Assert(fcinfo->context && IS_AGG_EXECUTION_NODE(fcinfo->context));
	return intfloat_avg_amalg_demalg(d0, d1, false);
}


Datum
float8_avg_demalg(PG_FUNCTION_ARGS)
{
	IntFloatAvgTransdata* d0 = (IntFloatAvgTransdata *) PG_GETARG_BYTEA_P(0);
	IntFloatAvgTransdata* d1 = (IntFloatAvgTransdata *) PG_GETARG_BYTEA_P(1);
	Assert(fcinfo->context && IS_AGG_EXECUTION_NODE(fcinfo->context));
	return intfloat_avg_amalg_demalg(d0, d1, false);
}

Datum
int8_avg(PG_FUNCTION_ARGS)
{
	IntFloatAvgTransdata *tr0 = (IntFloatAvgTransdata *) PG_GETARG_BYTEA_P(0);
	float8 avg = 0;

	/* SQL92 defines AVG of no values to be NULL */
	if(!tr0 || VARSIZE(tr0) < sizeof(IntFloatAvgTransdata) || tr0->count == 0) 
		PG_RETURN_NULL();

	avg = tr0->sum / tr0->count;
	PG_RETURN_DATUM(DirectFunctionCall1(float8_numeric, Float8GetDatum(avg)));
}

Datum
float8_avg(PG_FUNCTION_ARGS)
{
	IntFloatAvgTransdata *tr0 = (IntFloatAvgTransdata *) PG_GETARG_BYTEA_P(0);
	float8 avg = 0;

	/* SQL92 defines AVG of no values to be NULL */
	if(!tr0 || VARSIZE(tr0) < sizeof(IntFloatAvgTransdata) || tr0->count == 0) 
		PG_RETURN_NULL();

	avg = tr0->sum / tr0->count;
	return Float8GetDatum(avg);
}

/* 
 * AVG for numeric types
 */

typedef struct NumericAvgTransData
{
	int32 _len; /* varattrib len, do not touch directly */
#if 1
	int32   pad;  /* pad so int64 and float64 will be 8 bytes alligned */
#endif
	int64 count; 
	NumericData sum;
} NumericAvgTransData;
	
static inline NumericAvgTransData *num_avg_store_sum(NumericAvgTransData* tr, NumericVar *var)
{
	int oldlen = VARSIZE(tr) - offsetof(NumericAvgTransData, sum);
	int newlen = 0;
	Numeric num_sum = (Numeric) (&(tr->sum));

	newlen = make_result_inplace(var, num_sum, oldlen);
	if(newlen != 0)
	{
		NumericAvgTransData* oldtr = tr;
		int newtrlen = newlen + offsetof(NumericAvgTransData, sum) + 10;
		tr = palloc(newtrlen);
		SET_VARSIZE(tr, newtrlen);
		tr->count = oldtr->count;
		num_sum = (Numeric) (&(tr->sum));

		newlen = make_result_inplace(var, num_sum, newlen + 10);
		Assert(newlen == 0);

		/* Per comments in review CR-206 don't free this.  Let it live as
		 * long as its memory context.  Fix the leak iff it is reported 
		 * as a problem.
		 *
		 *   pfree(oldtr);
		 */
	}

	return tr;
}

static Datum 
numeric_avg_accum_decum(NumericAvgTransData *tr, Numeric newval, bool acc) 
{
	if(!tr || VARSIZE(tr) < sizeof(NumericAvgTransData))
	{
		/* Give it 5 extra digits hope that we do not need to palloc immediately */
		int len = offsetof(NumericAvgTransData, sum) + VARSIZE(newval) + 10;
		
		Assert(acc);
		
		/* Per comments in review CR-206 we assume that the newval arguement
		 * is a plain (untoasted) 4-byte-header Datum, so we can copy the
		 * Datum plus contiguous storage and not worry about anything else.
		 */
		tr = (NumericAvgTransData *) palloc(len); 
		SET_VARSIZE(tr, len); 
		tr->count = 1;
		memcpy(&(tr->sum), newval, VARSIZE(newval));

		return PointerGetDatum(tr);
	}
	else
	{
		Numeric num_sum = (Numeric) (&(tr->sum));

		if(acc)
			++tr->count;
		else 
			--tr->count;

		if(NUMERIC_IS_NAN(newval) || NUMERIC_IS_NAN(num_sum))
			tr = num_avg_store_sum(tr, &const_nan);
		else
		{
			NumericVar v1;
			NumericVar v2;
			NumericVar result;

			quick_init_var(&result);
			init_ro_var_from_num(num_sum, &v1);
			init_ro_var_from_num(newval, &v2);

			if(acc)
				add_var(&v1, &v2, &result);
			else
				sub_var(&v1, &v2, &result);

			tr = num_avg_store_sum(tr, &result);
		}
	}
	return PointerGetDatum(tr);
}

Datum
numeric_avg_accum(PG_FUNCTION_ARGS)
{
	NumericAvgTransData *tr = (NumericAvgTransData *) PG_GETARG_BYTEA_P(0); 
	Numeric newval = PG_GETARG_NUMERIC(1);
	Datum result;

	Assert(fcinfo->context && IS_AGG_EXECUTION_NODE(fcinfo->context));
	result = numeric_avg_accum_decum(tr, newval, true);
	/* pfree(newval);
	 *  Removed to fix MPP-6135.  Other (numeric,numeric) --> numeric
	 *  accumulators don't free their value argument (2nd numeric).
	 *  Why should this one?
	 */
	return result;
}

Datum
numeric_avg_decum(PG_FUNCTION_ARGS)
{
	NumericAvgTransData *tr = (NumericAvgTransData *) PG_GETARG_BYTEA_P(0); 
	Numeric newval = PG_GETARG_NUMERIC(1);
	Datum result;

	Assert(fcinfo->context && IS_AGG_EXECUTION_NODE(fcinfo->context));
	result = numeric_avg_accum_decum(tr, newval, false);
	/* pfree(newval);
	 *  See comment in numeric_avg_accum.  This didn't actually occur as
	 *  part of MPP-6135, but symmetry suggests it can cause a problem.
	 */
	return result;
}

static Datum numeric_avg_amalg_demalg(NumericAvgTransData* tr0, NumericAvgTransData* tr1, bool amalg)
{

	if(!tr0 || VARSIZE(tr0) < sizeof(NumericAvgTransData))
	{
		Assert(amalg);
		return PointerGetDatum(tr1);
	}

	if(!tr1 || VARSIZE(tr1) < sizeof(NumericAvgTransData))
		return PointerGetDatum(tr0);
	else
	{
		Numeric n0 = (Numeric) (&(tr0->sum));
		Numeric n1 = (Numeric) (&(tr1->sum));

		if(amalg)
			tr0->count += tr1->count;
		else
			tr0->count -= tr1->count;

		if(NUMERIC_IS_NAN(n0) || NUMERIC_IS_NAN(n1))
			tr0 = num_avg_store_sum(tr0, &const_nan);
		else
		{
			NumericVar v0;
			NumericVar v1;
			NumericVar result;

			quick_init_var(&result);
			init_ro_var_from_num(n0, &v0);
			init_ro_var_from_num(n1, &v1);

			if(amalg)
				add_var(&v0, &v1, &result);
			else
				sub_var(&v0, &v1, &result);

			tr0 = num_avg_store_sum(tr0, &result);
		}
	}

	return PointerGetDatum(tr0);
}

Datum numeric_avg_amalg(PG_FUNCTION_ARGS)
{
	NumericAvgTransData *tr0 = (NumericAvgTransData *) PG_GETARG_BYTEA_P(0); 
	NumericAvgTransData *tr1 = (NumericAvgTransData *) PG_GETARG_BYTEA_P(1); 

	Assert(fcinfo->context && IS_AGG_EXECUTION_NODE(fcinfo->context));
	return numeric_avg_amalg_demalg(tr0, tr1, true);
}

Datum numeric_avg_demalg(PG_FUNCTION_ARGS)
{
	NumericAvgTransData *tr0 = (NumericAvgTransData *) PG_GETARG_BYTEA_P(0); 
	NumericAvgTransData *tr1 = (NumericAvgTransData *) PG_GETARG_BYTEA_P(1); 

	Assert(fcinfo->context && IS_AGG_EXECUTION_NODE(fcinfo->context));
	return numeric_avg_amalg_demalg(tr0, tr1, false);
}

Datum
numeric_avg(PG_FUNCTION_ARGS)
{
	NumericAvgTransData *tr = (NumericAvgTransData *) PG_GETARG_BYTEA_P(0); 
	Datum countX;
	Numeric sumX;
	Datum result;

	/* SQL92 defines AVG of no values to be NULL */
	if(!tr || VARSIZE(tr) < sizeof(NumericAvgTransData) || tr->count == 0)
		PG_RETURN_NULL();

	countX = DirectFunctionCall1(int8_numeric, Int64GetDatum(tr->count));
	sumX = (Numeric) (&(tr->sum));

	result = DirectFunctionCall2(numeric_div, NumericGetDatum(sumX), countX); 
	pfree(DatumGetPointer(countX));

	return result;
}

/* ----------------------------------------------------------------------
 *
 * Debug support
 *
 * ----------------------------------------------------------------------
 */

#ifdef NUMERIC_DEBUG

/*
 * dump_numeric() - Dump a value in the db storage format for debugging
 */
static void
dump_numeric(const char *str, Numeric num)
{
	NumericDigit *digits = NUMERIC_DIGITS(num);
	int			ndigits;
	int			i;

	ndigits = NUMERIC_NDIGITS(num);

	printf("%s: NUMERIC w=%d d=%d ", str, NUMERIC_WEIGHT(num), NUMERIC_DSCALE(num));
	switch (NUMERIC_SIGN(num))
	{
		case NUMERIC_POS:
			printf("POS");
			break;
		case NUMERIC_NEG:
			printf("NEG");
			break;
		case NUMERIC_NAN:
			printf("NaN");
			break;
		default:
			printf("SIGN=0x%x", NUMERIC_SIGN(num));
			break;
	}

	for (i = 0; i < ndigits; i++)
		printf(" %0*d", DEC_DIGITS, digits[i]);
	printf("\n");
}


/*
 * dump_var() - Dump a value in the variable format for debugging
 */
static void
dump_var(const char *str, NumericVar *var)
{
	int			i;

	printf("%s: VAR w=%d d=%d ", str, var->weight, var->dscale);
	switch (var->sign)
	{
		case NUMERIC_POS:
			printf("POS");
			break;
		case NUMERIC_NEG:
			printf("NEG");
			break;
		case NUMERIC_NAN:
			printf("NaN");
			break;
		default:
			printf("SIGN=0x%x", var->sign);
			break;
	}

	for (i = 0; i < var->ndigits; i++)
		printf(" %0*d", DEC_DIGITS, var->digits[i]);

	printf("\n");
}
#endif   /* NUMERIC_DEBUG */


/* ----------------------------------------------------------------------
 *
 * Local functions follow
 *
 * In general, these do not support NaNs --- callers must eliminate
 * the possibility of NaN first.  (make_result() is an exception.)
 *
 * ----------------------------------------------------------------------
 */


/*
 * alloc_var() -
 *
 *	Allocate a digit buffer of ndigits digits (plus a spare digit for rounding)
 *  Called when a var may have been previously used.
 */
static void
alloc_var(NumericVar *var, int ndigits)
{
	digitbuf_free(var);
	init_alloc_var(var, ndigits);
}


/*
 * zero_var() -
 *
 *	Set a variable to ZERO.
 *	Note: its dscale is not touched.
 */
static void
zero_var(NumericVar *var)
{
	digitbuf_free(var);
	quick_init_var(var);
	var->ndigits = 0;
	var->weight = 0;			/* by convention; doesn't really matter */
	var->sign = NUMERIC_POS;	/* anything but NAN... */
}


/*
 * init_var_from_str()
 *
 *	Parse a string and put the number into a variable
 *
 */
static void
init_var_from_str(const char *str, NumericVar *dest)
{
	const char *cp = str;
	bool		have_dp = FALSE;
	int			i;
	unsigned char *decdigits;
	int			sign = NUMERIC_POS;
	int			dweight = -1;
	int			ddigits;
	int			dscale = 0;
	int			weight;
	int			ndigits;
	int			offset;
	NumericDigit *digits;
	unsigned char tdd[NUMERIC_LOCAL_DTXT];

	/*
	 * We first parse the string to extract decimal digits and determine the
	 * correct decimal weight.	Then convert to NBASE representation.
	 */

	/* skip leading spaces */
	while (*cp)
	{
		if (!isspace((unsigned char) *cp))
			break;
		cp++;
	}

	switch (*cp)
	{
		case '+':		/* NUMERIC_POS default set up above */
			cp++;
			break;

		case '-':
			sign = NUMERIC_NEG;
			cp++;
			break;
	}

	if (*cp == '.')
	{
		have_dp = TRUE;
		cp++;
	}

	if (!isdigit((unsigned char) *cp))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
			  errmsg("invalid input syntax for type numeric: \"%s\"", str),
						 errOmitLocation(true)));

	decdigits = tdd;
	i = strlen(cp) + DEC_DIGITS * 2;
	if ( i > NUMERIC_LOCAL_DMAX)
		decdigits = (unsigned char *) palloc(i);

	/* leading padding for digit alignment later */
	memset(decdigits, 0, DEC_DIGITS);
	i = DEC_DIGITS;

	while (*cp)
	{
		if (isdigit((unsigned char) *cp))
		{
			decdigits[i++] = *cp++ - '0';
			if (!have_dp)
				dweight++;
			else
				dscale++;
		}
		else if (*cp == '.')
		{
			if (have_dp)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
					  errmsg("invalid input syntax for type numeric: \"%s\"",
							 str),
									 errOmitLocation(true)));
			have_dp = TRUE;
			cp++;
		}
		else
			break;
	}

	ddigits = i - DEC_DIGITS;
	/* trailing padding for digit alignment later */
	memset(decdigits + i, 0, DEC_DIGITS - 1);

	/* Handle exponent, if any */
	if (*cp == 'e' || *cp == 'E')
	{
		long		exponent;
		char	   *endptr;

		cp++;
		exponent = strtol(cp, &endptr, 10);
		if (endptr == cp)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
					 errmsg("invalid input syntax for type numeric: \"%s\"",
							str),
									 errOmitLocation(true)));
		cp = endptr;
		if (exponent > NUMERIC_MAX_PRECISION ||
			exponent < -NUMERIC_MAX_PRECISION)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
					 errmsg("invalid input syntax for type numeric: \"%s\"",
							str),
									 errOmitLocation(true)));
		dweight += (int) exponent;
		dscale -= (int) exponent;
		if (dscale < 0)
			dscale = 0;
	}

	/* Should be nothing left but spaces */
	while (*cp)
	{
		if (!isspace((unsigned char) *cp))
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
					 errmsg("invalid input syntax for type numeric: \"%s\"",
							str),
									 errOmitLocation(true)));
		cp++;
	}

	/*
	 * Okay, convert pure-decimal representation to base NBASE.  First we need
	 * to determine the converted weight and ndigits.  offset is the number of
	 * decimal zeroes to insert before the first given digit to have a
	 * correctly aligned first NBASE digit.
	 */
	if (dweight >= 0)
		weight = (dweight + 1 + DEC_DIGITS - 1) / DEC_DIGITS - 1;
	else
		weight = -((-dweight - 1) / DEC_DIGITS + 1);
	offset = (weight + 1) * DEC_DIGITS - (dweight + 1);
	ndigits = (ddigits + offset + DEC_DIGITS - 1) / DEC_DIGITS;

	init_alloc_var(dest, ndigits);

	dest->weight = weight;
	dest->sign = sign;
	dest->dscale = dscale;

	i = DEC_DIGITS - offset;
	digits = dest->digits;

	while (ndigits-- > 0)
	{
#if DEC_DIGITS == 4
		*digits++ = ((decdigits[i] * 10 + decdigits[i + 1]) * 10 +
					 decdigits[i + 2]) * 10 + decdigits[i + 3];
#elif DEC_DIGITS == 2
		*digits++ = decdigits[i] * 10 + decdigits[i + 1];
#elif DEC_DIGITS == 1
		*digits++ = decdigits[i];
#else
#error unsupported NBASE
#endif
		i += DEC_DIGITS;
	}

	if (decdigits != tdd)
		pfree(decdigits);
}


/*
 * set_var_from_num() -
 *
 *	Convert the packed db format into a variable
 */
/*
static void
set_var_from_num(Numeric num, NumericVar *dest)
{
	int			ndigits;

	ndigits = NUMERIC_NDIGITS(num);

	alloc_var(dest, ndigits);

	dest->weight = NUMERIC_WEIGHT(num);
	dest->sign = NUMERIC_SIGN(num);
	dest->dscale = NUMERIC_DSCALE(num);

	memcpy(dest->digits, NUMERIC_DIGITS(num), ndigits * sizeof(NumericDigit));
}
*/

/*
 * init_var_from_num() -
 *
 *	Convert the packed db format into a variable
 */
static void
init_var_from_num(Numeric num, NumericVar *dest)
{
	int			ndigits;

	ndigits = NUMERIC_NDIGITS(num);

	init_alloc_var(dest, ndigits);

	dest->weight = NUMERIC_WEIGHT(num);
	dest->sign = NUMERIC_SIGN(num);
	dest->dscale = NUMERIC_DSCALE(num);

	memcpy(dest->digits, NUMERIC_DIGITS(num), ndigits * sizeof(NumericDigit));
}

/*
 * init_ro_var_from_num() -
 *
 *	Convert the packed db format into a variable
 */
static void
init_ro_var_from_num(Numeric num, NumericVar *dest)
{
	dest->ndigits = NUMERIC_NDIGITS(num);
	dest->weight = NUMERIC_WEIGHT(num);
	dest->sign = NUMERIC_SIGN(num);
	dest->dscale = NUMERIC_DSCALE(num);
	dest->digits = NUMERIC_DIGITS(num);
	dest->buf = dest->ndb;
}


/*
 * set_var_from_var() -
 *
 *	Copy one variable into another
 */
static void
set_var_from_var(NumericVar *value, NumericVar *dest)
{
	NumericDigit *newbuf;

	newbuf = dest->ndb;			/* most common case */
	if (value->ndigits > NUMERIC_LOCAL_NMAX)
		newbuf = digitbuf_alloc(value->ndigits + 1);

	memmove(newbuf + 1, value->digits, value->ndigits * sizeof(NumericDigit));

	digitbuf_free(dest);

	memmove(dest, value, NUMERIC_LOCAL_HSIZ);
	dest->buf = newbuf;
	dest->digits = newbuf + 1;
	newbuf[0] = 0;				/* spare digit for rounding */
}


/*
 * init_var_from_var() -
 *
 *	init one variable from another - they must NOT be the same variable
 */
static void
init_var_from_var(NumericVar *value, NumericVar *dest)
{
	init_alloc_var(dest, value->ndigits);

	dest->weight = value->weight;
	dest->sign = value->sign;
	dest->dscale = value->dscale;

	memcpy(dest->digits, value->digits, value->ndigits * sizeof(NumericDigit));
}

/*
 * init_ro_var_from_var() -
 *
 *	init one variable from another - they must NOT be the same variable
 */
static void
init_ro_var_from_var(NumericVar *value, NumericVar *dest)
{
	dest->ndigits = value->ndigits;
	dest->weight = value->weight;
	dest->sign = value->sign;
	dest->dscale = value->dscale;
	dest->digits = value->digits;
	dest->buf = dest->ndb;
}

/*
 * get_str_from_var() -
 *
 *	Convert a var to text representation (guts of numeric_out).
 *	CAUTION: var's contents may be modified by rounding!
 *	Returns a palloc'd string.
 */
static char *
get_str_from_var(NumericVar *var, int dscale)
{
	char	   *str;
	char	   *cp;
	char	   *endcp;
	int			i;
	int			d;
	NumericDigit dig;

#if DEC_DIGITS > 1
	NumericDigit d1;
#endif

	if (dscale < 0)
		dscale = 0;

	/*
	 * Check if we must round up before printing the value and do so.
	 */
	round_var(var, dscale);

	/*
	 * Allocate space for the result.
	 *
	 * i is set to to # of decimal digits before decimal point. dscale is the
	 * # of decimal digits we will print after decimal point. We may generate
	 * as many as DEC_DIGITS-1 excess digits at the end, and in addition we
	 * need room for sign, decimal point, null terminator.
	 */
	i = (var->weight + 1) * DEC_DIGITS;
	if (i <= 0)
		i = 1;

	str = palloc(i + dscale + DEC_DIGITS + 2);
	cp = str;

	/*
	 * Output a dash for negative values
	 */
	if (var->sign == NUMERIC_NEG)
		*cp++ = '-';

	/*
	 * Output all digits before the decimal point
	 */
	if (var->weight < 0)
	{
		d = var->weight + 1;
		*cp++ = '0';
	}
	else
	{
		for (d = 0; d <= var->weight; d++)
		{
			dig = (d < var->ndigits) ? var->digits[d] : 0;
			/* In the first digit, suppress extra leading decimal zeroes */
#if DEC_DIGITS == 4
			{
				bool		putit = (d > 0);

				d1 = dig / 1000;
				dig -= d1 * 1000;
				putit |= (d1 > 0);
				if (putit)
					*cp++ = d1 + '0';
				d1 = dig / 100;
				dig -= d1 * 100;
				putit |= (d1 > 0);
				if (putit)
					*cp++ = d1 + '0';
				d1 = dig / 10;
				dig -= d1 * 10;
				putit |= (d1 > 0);
				if (putit)
					*cp++ = d1 + '0';
				*cp++ = dig + '0';
			}
#elif DEC_DIGITS == 2
			d1 = dig / 10;
			dig -= d1 * 10;
			if (d1 > 0 || d > 0)
				*cp++ = d1 + '0';
			*cp++ = dig + '0';
#elif DEC_DIGITS == 1
			*cp++ = dig + '0';
#else
#error unsupported NBASE
#endif
		}
	}

	/*
	 * If requested, output a decimal point and all the digits that follow it.
	 * We initially put out a multiple of DEC_DIGITS digits, then truncate if
	 * needed.
	 */
	if (dscale > 0)
	{
		*cp++ = '.';
		endcp = cp + dscale;
		for (i = 0; i < dscale; d++, i += DEC_DIGITS)
		{
			dig = (d >= 0 && d < var->ndigits) ? var->digits[d] : 0;
#if DEC_DIGITS == 4
			d1 = dig / 1000;
			dig -= d1 * 1000;
			*cp++ = d1 + '0';
			d1 = dig / 100;
			dig -= d1 * 100;
			*cp++ = d1 + '0';
			d1 = dig / 10;
			dig -= d1 * 10;
			*cp++ = d1 + '0';
			*cp++ = dig + '0';
#elif DEC_DIGITS == 2
			d1 = dig / 10;
			dig -= d1 * 10;
			*cp++ = d1 + '0';
			*cp++ = dig + '0';
#elif DEC_DIGITS == 1
			*cp++ = dig + '0';
#else
#error unsupported NBASE
#endif
		}
		cp = endcp;
	}

	/*
	 * terminate the string and return it
	 */
	*cp = '\0';
	return str;
}


/*
 * make_result() -
 *
 *	Create the packed db numeric format in palloc()'d memory from
 *	a variable.
 *	CAUTION: we free_var(var) here!
 */
static Numeric
make_result(NumericVar *var)
{
	Numeric		result;
	NumericDigit *digits = var->digits;
	int			weight = var->weight;
	int			sign = var->sign;
	int			n;
	Size		len;

	if (sign == NUMERIC_NAN)
	{
		free_var(var);
		result = (Numeric) palloc(NUMERIC_HDRSZ);

		SET_VARSIZE(result, NUMERIC_HDRSZ);
		NUMERIC_WEIGHT(result) = 0;
		NUMERIC_SIGN_DSCALE(result) = NUMERIC_NAN;

		dump_numeric("make_result()", result);
		return result;
	}

	n = var->ndigits;

	/* truncate leading zeroes */
	while (n > 0 && *digits == 0)
	{
		digits++;
		weight--;
		n--;
	}
	/* truncate trailing zeroes */
	while (n > 0 && digits[n - 1] == 0)
		n--;

	/* If zero result, force to weight=0 and positive sign */
	if (n == 0)
	{
		weight = 0;
		sign = NUMERIC_POS;
	}

	/* Build the result */
	len = NUMERIC_HDRSZ + n * sizeof(NumericDigit);
	result = (Numeric) palloc(len);
	SET_VARSIZE(result, len);
	NUMERIC_WEIGHT(result) = weight;
	NUMERIC_SIGN_DSCALE(result) = sign | (var->dscale & NUMERIC_DSCALE_MASK);

	memcpy(NUMERIC_DIGITS(result), digits, n * sizeof(NumericDigit));

	/* Check for overflow of int16 fields */
	if (NUMERIC_WEIGHT(result) != weight ||
		NUMERIC_DSCALE(result) != var->dscale)
	{
		char *ntp = get_str_from_var(var, var->dscale);

		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("value overflows numeric format"),
				 errdetail("Overflowing value: %s", ntp),
						 errOmitLocation(true)
				));
	}

	free_var(var);
	dump_numeric("make_result()", result);
	return result;
}

/* Make a var in place.  
 * return 0 if succeed (len is long enough), otherwisse, return bytes needed.
 */
static int
make_result_inplace(NumericVar *var, Numeric result, int in_len)
{
	NumericDigit *digits = var->digits;
	int weight = var->weight;
	int sign = var->sign;
	int n;
	Size len;

	if(sign == NUMERIC_NAN)
	{
		if(in_len < NUMERIC_HDRSZ)
			return NUMERIC_HDRSZ;

		Assert(result);
		SET_VARSIZE(result, NUMERIC_HDRSZ);
		NUMERIC_WEIGHT(result) = 0;
		NUMERIC_SIGN_DSCALE(result) = NUMERIC_NAN;

		return 0;
	}

	n = var->ndigits;

	/* truncate leading zeroes */
	while (n > 0 && *digits == 0)
	{
		digits++;
		weight--;
		n--;
	}
	/* truncate trailing zeroes */
	while (n > 0 && digits[n - 1] == 0)
		n--;

	/* If zero result, force to weight=0 and positive sign */
	if (n == 0)
	{
		weight = 0;
		sign = NUMERIC_POS;
	}

	/* Build the result */
	len = NUMERIC_HDRSZ + n * sizeof(NumericDigit);

	if(in_len < len)
		return len;

	Assert(result);
	SET_VARSIZE(result, len);
	NUMERIC_WEIGHT(result) = weight;
	NUMERIC_SIGN_DSCALE(result) = sign | (var->dscale & NUMERIC_DSCALE_MASK);

	memcpy(NUMERIC_DIGITS(result), digits, n * sizeof(NumericDigit));

	/* Check for overflow of int16 fields */
	if (NUMERIC_WEIGHT(result) != weight ||
		NUMERIC_DSCALE(result) != var->dscale)
	{
		char *ntp = get_str_from_var(var, var->dscale);

		ereport(ERROR,
				(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
				 errmsg("value overflows numeric format"),
				 errdetail("Overflowing value: %s", ntp),
						 errOmitLocation(true)
				));
	}

	free_var(var);

	return 0;
}


/*
 * apply_typmod() -
 *
 *	Do bounds checking and rounding according to the attributes
 *	typmod field.
 */
static void
apply_typmod(NumericVar *var, int32 typmod)
{
	int			precision;
	int			scale;
	int			maxdigits;
	int			ddigits;
	int			i;

	/* Do nothing if we have a default typmod (-1) */
	if (typmod < (int32) (VARHDRSZ))
		return;

	typmod -= VARHDRSZ;
	precision = (typmod >> 16) & 0xffff;
	scale = typmod & 0xffff;
	maxdigits = precision - scale;

	/* Round to target scale (and set var->dscale) */
	round_var(var, scale);

	/*
	 * Check for overflow - note we can't do this before rounding, because
	 * rounding could raise the weight.  Also note that the var's weight could
	 * be inflated by leading zeroes, which will be stripped before storage
	 * but perhaps might not have been yet. In any case, we must recognize a
	 * true zero, whose weight doesn't mean anything.
	 */
	ddigits = (var->weight + 1) * DEC_DIGITS;
	if (ddigits > maxdigits)
	{
		/* Determine true weight; and check for all-zero result */
		for (i = 0; i < var->ndigits; i++)
		{
			NumericDigit dig = var->digits[i];

			if (dig)
			{
				/* Adjust for any high-order decimal zero digits */
#if DEC_DIGITS == 4
				if (dig < 10)
					ddigits -= 3;
				else if (dig < 100)
					ddigits -= 2;
				else if (dig < 1000)
					ddigits -= 1;
#elif DEC_DIGITS == 2
				if (dig < 10)
					ddigits -= 1;
#elif DEC_DIGITS == 1
				/* no adjustment */
#else
#error unsupported NBASE
#endif
				if (ddigits > maxdigits)
				{
					char *ntp = get_str_from_var(var, scale);

					ereport(ERROR,
							(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
							 errmsg("numeric field overflow"),
							 errdetail("A field with precision %d, scale %d must round to an absolute value less than %s%d. Rounded overflowing value: %s",
									   precision, scale,
					/* Display 10^0 as 1 */
									   maxdigits ? "10^" : "",
									   maxdigits ? maxdigits : 1,
									   ntp
									   ),
												 errOmitLocation(true)));
				}
				break;
			}
			ddigits -= DEC_DIGITS;
		}
	}
}

/*
 * Convert numeric to int8, rounding if needed.
 *
 * If overflow, return FALSE (no error is raised).	Return TRUE if okay.
 *
 *	CAUTION: var's contents may be modified by rounding!
 *	CAUTION: we free_var(var) here!
 */
static bool
numericvar_to_int8(NumericVar *var, int64 *result)
{
	NumericDigit *digits;
	int			ndigits;
	int			weight;
	int			i;
	int64		val,
				oldval;
	bool		neg;

	/* Round to nearest integer */
	round_var(var, 0);

	/* Check for zero input */
	strip_var(var);
	ndigits = var->ndigits;
	if (ndigits == 0)
	{
		free_var(var);
		*result = 0;
		return true;
	}

	/*
	 * For input like 10000000000, we must treat stripped digits as real. So
	 * the loop assumes there are weight+1 digits before the decimal point.
	 */
	weight = var->weight;
	Assert(weight >= 0 && ndigits <= weight + 1);

	/* Construct the result */
	digits = var->digits;
	neg = (var->sign == NUMERIC_NEG);
	val = digits[0];
	for (i = 1; i <= weight; i++)
	{
		oldval = val;
		val *= NBASE;
		if (i < ndigits)
			val += digits[i];

		/*
		 * The overflow check is a bit tricky because we want to accept
		 * INT64_MIN, which will overflow the positive accumulator.  We can
		 * detect this case easily though because INT64_MIN is the only
		 * nonzero value for which -val == val (on a two's complement machine,
		 * anyway).
		 */
		if ((val / NBASE) != oldval)	/* possible overflow? */
		{
			if (!neg || (-val) != val || val == 0 || oldval < 0)
			{
				free_var(var);
				return false;
			}
		}
	}

	*result = neg ? -val : val;
	free_var(var);
	return true;
}

/*
 * Convert int8 value to numeric.
 */
static void
int8_to_numericvar(int64 val, NumericVar *var)
{
	uint64		uval,
				newuval;
	NumericDigit *ptr;
	int			ndigits;

	/* int8 can require at most 19 decimal digits; add one for safety */
	alloc_var(var, 20 / DEC_DIGITS);
	if (val < 0)
	{
		var->sign = NUMERIC_NEG;
		uval = -val;
	}
	else
	{
		var->sign = NUMERIC_POS;
		uval = val;
	}
	var->dscale = 0;
	if (val == 0)
	{
		var->ndigits = 0;
		var->weight = 0;
		return;
	}
	ptr = var->digits + var->ndigits;
	ndigits = 0;
	do
	{
		ptr--;
		ndigits++;
		newuval = uval / NBASE;
		*ptr = uval - newuval * NBASE;
		uval = newuval;
	} while (uval);
	var->digits = ptr;
	var->ndigits = ndigits;
	var->weight = ndigits - 1;
}

/*
 * Convert numeric to float8; if out of range, return +/- HUGE_VAL
 */
double
numeric_to_double_no_overflow(Numeric num)
{
	char	   *tmp;
	double		val;
	char	   *endptr;

	tmp = DatumGetCString(DirectFunctionCall1(numeric_out,
											  NumericGetDatum(num)));

	/* unlike float8in, we ignore ERANGE from strtod */
	val = strtod(tmp, &endptr);
	if (*endptr != '\0')
	{
		/* shouldn't happen ... */
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
			 errmsg("invalid input syntax for type double precision: \"%s\"",
					tmp),
							 errOmitLocation(true)));
	}

	pfree(tmp);

	return val;
}

/* As above, but work from a NumericVar */
static double
numericvar_to_double_no_overflow(NumericVar *var)
{
	char	   *tmp;
	double		val;
	char	   *endptr;

	tmp = get_str_from_var(var, var->dscale);

	/* unlike float8in, we ignore ERANGE from strtod */
	val = strtod(tmp, &endptr);
	if (*endptr != '\0')
	{
		/* shouldn't happen ... */
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
			 errmsg("invalid input syntax for type double precision: \"%s\"",
					tmp),
							 errOmitLocation(true)));
	}

	pfree(tmp);

	return val;
}


/*
 * cmp_var() -
 *
 *	Compare two values on variable level.  We assume zeroes have been
 *	truncated to no digits.
 */
static int
cmp_var(NumericVar *var1, NumericVar *var2)
{
	return cmp_var_common(var1->digits, var1->ndigits,
						  var1->weight, var1->sign,
						  var2->digits, var2->ndigits,
						  var2->weight, var2->sign);
}

/*
 * cmp_var_common() -
 *
 *	Main routine of cmp_var(). This function can be used by both
 *	NumericVar and Numeric.
 */
static int
cmp_var_common(const NumericDigit *var1digits, int var1ndigits,
			   int var1weight, int var1sign,
			   const NumericDigit *var2digits, int var2ndigits,
			   int var2weight, int var2sign)
{
	if (var1ndigits == 0)
	{
		if (var2ndigits == 0)
			return 0;
		if (var2sign == NUMERIC_NEG)
			return 1;
		return -1;
	}
	if (var2ndigits == 0)
	{
		if (var1sign == NUMERIC_POS)
			return 1;
		return -1;
	}

	if (var1sign == NUMERIC_POS)
	{
		if (var2sign == NUMERIC_NEG)
			return 1;
		return cmp_abs_common(var1digits, var1ndigits, var1weight,
							  var2digits, var2ndigits, var2weight);
	}

	if (var2sign == NUMERIC_POS)
		return -1;

	return cmp_abs_common(var2digits, var2ndigits, var2weight,
						  var1digits, var1ndigits, var1weight);
}


/*
 * add_var() -
 *
 *	Full version of add functionality on variable level (handling signs).
 *	result might point to one of the operands too without danger.
 */
static void
add_var(NumericVar *var1, NumericVar *var2, NumericVar *result)
{
	/*
	 * Decide on the signs of the two variables what to do
	 */
	if (var1->sign == NUMERIC_POS)
	{
		if (var2->sign == NUMERIC_POS)
		{
			/*
			 * Both are positive result = +(ABS(var1) + ABS(var2))
			 */
			add_abs(var1, var2, result);
			result->sign = NUMERIC_POS;
		}
		else
		{
			/*
			 * var1 is positive, var2 is negative Must compare absolute values
			 */
			switch (cmp_abs(var1, var2))
			{
				case 0:
					/* ----------
					 * ABS(var1) == ABS(var2)
					 * result = ZERO
					 * ----------
					 */
					zero_var(result);
					result->dscale = Max(var1->dscale, var2->dscale);
					break;

				case 1:
					/* ----------
					 * ABS(var1) > ABS(var2)
					 * result = +(ABS(var1) - ABS(var2))
					 * ----------
					 */
					sub_abs(var1, var2, result);
					result->sign = NUMERIC_POS;
					break;

				case -1:
					/* ----------
					 * ABS(var1) < ABS(var2)
					 * result = -(ABS(var2) - ABS(var1))
					 * ----------
					 */
					sub_abs(var2, var1, result);
					result->sign = NUMERIC_NEG;
					break;
			}
		}
	}
	else
	{
		if (var2->sign == NUMERIC_POS)
		{
			/* ----------
			 * var1 is negative, var2 is positive
			 * Must compare absolute values
			 * ----------
			 */
			switch (cmp_abs(var1, var2))
			{
				case 0:
					/* ----------
					 * ABS(var1) == ABS(var2)
					 * result = ZERO
					 * ----------
					 */
					zero_var(result);
					result->dscale = Max(var1->dscale, var2->dscale);
					break;

				case 1:
					/* ----------
					 * ABS(var1) > ABS(var2)
					 * result = -(ABS(var1) - ABS(var2))
					 * ----------
					 */
					sub_abs(var1, var2, result);
					result->sign = NUMERIC_NEG;
					break;

				case -1:
					/* ----------
					 * ABS(var1) < ABS(var2)
					 * result = +(ABS(var2) - ABS(var1))
					 * ----------
					 */
					sub_abs(var2, var1, result);
					result->sign = NUMERIC_POS;
					break;
			}
		}
		else
		{
			/* ----------
			 * Both are negative
			 * result = -(ABS(var1) + ABS(var2))
			 * ----------
			 */
			add_abs(var1, var2, result);
			result->sign = NUMERIC_NEG;
		}
	}
}


/*
 * sub_var() -
 *
 *	Full version of sub functionality on variable level (handling signs).
 *	result might point to one of the operands too without danger.
 */
static void
sub_var(NumericVar *var1, NumericVar *var2, NumericVar *result)
{
	/*
	 * Decide on the signs of the two variables what to do
	 */
	if (var1->sign == NUMERIC_POS)
	{
		if (var2->sign == NUMERIC_NEG)
		{
			/* ----------
			 * var1 is positive, var2 is negative
			 * result = +(ABS(var1) + ABS(var2))
			 * ----------
			 */
			add_abs(var1, var2, result);
			result->sign = NUMERIC_POS;
		}
		else
		{
			/* ----------
			 * Both are positive
			 * Must compare absolute values
			 * ----------
			 */
			switch (cmp_abs(var1, var2))
			{
				case 0:
					/* ----------
					 * ABS(var1) == ABS(var2)
					 * result = ZERO
					 * ----------
					 */
					zero_var(result);
					result->dscale = Max(var1->dscale, var2->dscale);
					break;

				case 1:
					/* ----------
					 * ABS(var1) > ABS(var2)
					 * result = +(ABS(var1) - ABS(var2))
					 * ----------
					 */
					sub_abs(var1, var2, result);
					result->sign = NUMERIC_POS;
					break;

				case -1:
					/* ----------
					 * ABS(var1) < ABS(var2)
					 * result = -(ABS(var2) - ABS(var1))
					 * ----------
					 */
					sub_abs(var2, var1, result);
					result->sign = NUMERIC_NEG;
					break;
			}
		}
	}
	else
	{
		if (var2->sign == NUMERIC_NEG)
		{
			/* ----------
			 * Both are negative
			 * Must compare absolute values
			 * ----------
			 */
			switch (cmp_abs(var1, var2))
			{
				case 0:
					/* ----------
					 * ABS(var1) == ABS(var2)
					 * result = ZERO
					 * ----------
					 */
					zero_var(result);
					result->dscale = Max(var1->dscale, var2->dscale);
					break;

				case 1:
					/* ----------
					 * ABS(var1) > ABS(var2)
					 * result = -(ABS(var1) - ABS(var2))
					 * ----------
					 */
					sub_abs(var1, var2, result);
					result->sign = NUMERIC_NEG;
					break;

				case -1:
					/* ----------
					 * ABS(var1) < ABS(var2)
					 * result = +(ABS(var2) - ABS(var1))
					 * ----------
					 */
					sub_abs(var2, var1, result);
					result->sign = NUMERIC_POS;
					break;
			}
		}
		else
		{
			/* ----------
			 * var1 is negative, var2 is positive
			 * result = -(ABS(var1) + ABS(var2))
			 * ----------
			 */
			add_abs(var1, var2, result);
			result->sign = NUMERIC_NEG;
		}
	}
}


/*
 * mul_var() -
 *
 *	Multiplication on variable level. Product of var1 * var2 is stored
 *	in result.	Result is rounded to no more than rscale fractional digits.
 */
static void
mul_var(NumericVar *var1, NumericVar *var2, NumericVar *result,
		int rscale)
{
	int			res_ndigits;
	int			res_sign;
	int			res_weight;
	int			maxdigits;
	int		   *dig;
	int			carry;
	int			maxdig;
	int			newdig;
	NumericDigit *res_digits;
	int			i,
				ri,
				i1,
				i2;

	/* copy these values into local vars for speed in inner loop */
	int			var1ndigits = var1->ndigits;
	int			var2ndigits = var2->ndigits;
	NumericDigit *var1digits = var1->digits;
	NumericDigit *var2digits = var2->digits;
	int			 tdig[NUMERIC_LOCAL_NDIG];

	if (var1ndigits == 0 || var2ndigits == 0)
	{
		/* one or both inputs is zero; so is result */
		zero_var(result);
		result->dscale = rscale;
		return;
	}

	/* Determine result sign and (maximum possible) weight */
	if (var1->sign == var2->sign)
		res_sign = NUMERIC_POS;
	else
		res_sign = NUMERIC_NEG;
	res_weight = var1->weight + var2->weight + 2;

	/*
	 * Determine number of result digits to compute.  If the exact result
	 * would have more than rscale fractional digits, truncate the computation
	 * with MUL_GUARD_DIGITS guard digits.	We do that by pretending that one
	 * or both inputs have fewer digits than they really do.
	 */
	res_ndigits = var1ndigits + var2ndigits + 1;
	maxdigits = res_weight + 1 + (rscale * DEC_DIGITS) + MUL_GUARD_DIGITS;
	if (res_ndigits > maxdigits)
	{
		if (maxdigits < 3)
		{
			/* no useful precision at all in the result... */
			zero_var(result);
			result->dscale = rscale;
			return;
		}
		/* force maxdigits odd so that input ndigits can be equal */
		if ((maxdigits & 1) == 0)
			maxdigits++;
		if (var1ndigits > var2ndigits)
		{
			var1ndigits -= res_ndigits - maxdigits;
			if (var1ndigits < var2ndigits)
				var1ndigits = var2ndigits = (var1ndigits + var2ndigits) / 2;
		}
		else
		{
			var2ndigits -= res_ndigits - maxdigits;
			if (var2ndigits < var1ndigits)
				var1ndigits = var2ndigits = (var1ndigits + var2ndigits) / 2;
		}
		res_ndigits = maxdigits;
		Assert(res_ndigits == var1ndigits + var2ndigits + 1);
	}

	/*
	 * We do the arithmetic in an array "dig[]" of signed int's.  Since
	 * INT_MAX is noticeably larger than NBASE*NBASE, this gives us headroom
	 * to avoid normalizing carries immediately.
	 *
	 * maxdig tracks the maximum possible value of any dig[] entry; when this
	 * threatens to exceed INT_MAX, we take the time to propagate carries. To
	 * avoid overflow in maxdig itself, it actually represents the max
	 * possible value divided by NBASE-1.
	 */
	i = res_ndigits * sizeof(int);
	if (res_ndigits > NUMERIC_LOCAL_NMAX)
	{
		dig = (int *) palloc0(i);
	}
	else
	{
		dig = tdig;
		memset(dig, 0, i);
	}
	maxdig = 0;

	ri = res_ndigits - 1;
	for (i1 = var1ndigits - 1; i1 >= 0; ri--, i1--)
	{
		int			var1digit = var1digits[i1];

		if (var1digit == 0)
			continue;

		/* Time to normalize? */
		maxdig += var1digit;
		if (maxdig > INT_MAX / (NBASE - 1))
		{
			/* Yes, do it */
			carry = 0;
			for (i = res_ndigits - 1; i >= 0; i--)
			{
				newdig = dig[i] + carry;
				if (newdig >= NBASE)
				{
					carry = newdig / NBASE;
					newdig -= carry * NBASE;
				}
				else
					carry = 0;
				dig[i] = newdig;
			}
			Assert(carry == 0);
			/* Reset maxdig to indicate new worst-case */
			maxdig = 1 + var1digit;
		}

		/* Add appropriate multiple of var2 into the accumulator */
		i = ri;
		for (i2 = var2ndigits - 1; i2 >= 0; i2--)
			dig[i--] += var1digit * var2digits[i2];
	}

	/*
	 * Now we do a final carry propagation pass to normalize the result, which
	 * we combine with storing the result digits into the output. Note that
	 * this is still done at full precision w/guard digits.
	 */
	alloc_var(result, res_ndigits);
	res_digits = result->digits;
	carry = 0;
	for (i = res_ndigits - 1; i >= 0; i--)
	{
		newdig = dig[i] + carry;
		if (newdig >= NBASE)
		{
			carry = newdig / NBASE;
			newdig -= carry * NBASE;
		}
		else
			carry = 0;
		res_digits[i] = newdig;
	}
	Assert(carry == 0);

	if (dig != tdig)
		pfree(dig);

	/*
	 * Finally, round the result to the requested precision.
	 */
	result->weight = res_weight;
	result->sign = res_sign;

	/* Round to target rscale (and set result->dscale) */
	round_var(result, rscale);

	/* Strip leading and trailing zeroes */
	strip_var(result);
}


/*
 * div_var() -
 *
 *	Division on variable level. Quotient of var1 / var2 is stored
 *	in result.	Result is rounded to no more than rscale fractional digits.
 */
static void
div_var(NumericVar *var1, NumericVar *var2, NumericVar *result,
		int rscale, bool round)
{
	int			div_ndigits;
	int			res_sign;
	int			res_weight;
	int		   *div;
	int			qdigit;
	int			carry;
	int			maxdiv;
	int			newdig;
	NumericDigit *res_digits;
	double		fdividend,
				fdivisor,
				fdivisorinverse,
				fquotient;
	int			qi;
	int			i;

	/* copy these values into local vars for speed in inner loop */
	int			var1ndigits = var1->ndigits;
	int			var2ndigits = var2->ndigits;
	NumericDigit *var1digits = var1->digits;
	NumericDigit *var2digits = var2->digits;
	int			tdiv[NUMERIC_LOCAL_NDIG];

	/*
	 * First of all division by zero check; we must not be handed an
	 * unnormalized divisor.
	 */
	if (var2ndigits == 0 || var2digits[0] == 0)
		ereport(ERROR,
				(errcode(ERRCODE_DIVISION_BY_ZERO),
				 errmsg("division by zero"),
						 errOmitLocation(true)));

	/*
	 * Now result zero check
	 */
	if (var1ndigits == 0)
	{
		zero_var(result);
		result->dscale = rscale;
		return;
	}

	/*
	 * Determine the result sign, weight and number of digits to calculate
	 */
	if (var1->sign == var2->sign)
		res_sign = NUMERIC_POS;
	else
		res_sign = NUMERIC_NEG;
	res_weight = var1->weight - var2->weight + 1;
	/* The number of accurate result digits we need to produce: */
	div_ndigits = res_weight + 1 + (rscale + DEC_DIGITS - 1) / DEC_DIGITS;
	/* Add guard digits for roundoff error */
	div_ndigits += DIV_GUARD_DIGITS;
	if (div_ndigits < DIV_GUARD_DIGITS)
		div_ndigits = DIV_GUARD_DIGITS;
	/* Must be at least var1ndigits, too, to simplify data-loading loop */
	if (div_ndigits < var1ndigits)
		div_ndigits = var1ndigits;

	/*
	 * We do the arithmetic in an array "div[]" of signed int's.  Since
	 * INT_MAX is noticeably larger than NBASE*NBASE, this gives us headroom
	 * to avoid normalizing carries immediately.
	 *
	 * We start with div[] containing one zero digit followed by the
	 * dividend's digits (plus appended zeroes to reach the desired precision
	 * including guard digits).  Each step of the main loop computes an
	 * (approximate) quotient digit and stores it into div[], removing one
	 * position of dividend space.	A final pass of carry propagation takes
	 * care of any mistaken quotient digits.
	 */
	i = (div_ndigits + 1) * sizeof(int);
	if (div_ndigits > NUMERIC_LOCAL_NMAX)
	{
		div = (int *) palloc0(i);
	}
	else
	{
		memset(tdiv, 0, i);
		div = tdiv;
	}
	for (i = 0; i < var1ndigits; i++)
		div[i + 1] = var1digits[i];

	/*
	 * We estimate each quotient digit using floating-point arithmetic, taking
	 * the first four digits of the (current) dividend and divisor. This must
	 * be float to avoid overflow.
	 */
	fdivisor = (double) var2digits[0];
	for (i = 1; i < 4; i++)
	{
		fdivisor *= NBASE;
		if (i < var2ndigits)
			fdivisor += (double) var2digits[i];
	}
	fdivisorinverse = 1.0 / fdivisor;

	/*
	 * maxdiv tracks the maximum possible absolute value of any div[] entry;
	 * when this threatens to exceed INT_MAX, we take the time to propagate
	 * carries.  To avoid overflow in maxdiv itself, it actually represents
	 * the max possible abs. value divided by NBASE-1.
	 */
	maxdiv = 1;

	/*
	 * Outer loop computes next quotient digit, which will go into div[qi]
	 */
	for (qi = 0; qi < div_ndigits; qi++)
	{
		/* Approximate the current dividend value */
		fdividend = (double) div[qi];
		for (i = 1; i < 4; i++)
		{
			fdividend *= NBASE;
			if (qi + i <= div_ndigits)
				fdividend += (double) div[qi + i];
		}
		/* Compute the (approximate) quotient digit */
		fquotient = fdividend * fdivisorinverse;
		qdigit = (fquotient >= 0.0) ? ((int) fquotient) :
			(((int) fquotient) - 1);	/* truncate towards -infinity */

		if (qdigit != 0)
		{
			/* Do we need to normalize now? */
			maxdiv += Abs(qdigit);
			if (maxdiv > INT_MAX / (NBASE - 1))
			{
				/* Yes, do it */
				carry = 0;
				for (i = div_ndigits; i > qi; i--)
				{
					newdig = div[i] + carry;
					if (newdig < 0)
					{
						carry = -((-newdig - 1) / NBASE) - 1;
						newdig -= carry * NBASE;
					}
					else if (newdig >= NBASE)
					{
						carry = newdig / NBASE;
						newdig -= carry * NBASE;
					}
					else
						carry = 0;
					div[i] = newdig;
				}
				newdig = div[qi] + carry;
				div[qi] = newdig;

				/*
				 * All the div[] digits except possibly div[qi] are now in the
				 * range 0..NBASE-1.
				 */
				maxdiv = Abs(newdig) / (NBASE - 1);
				maxdiv = Max(maxdiv, 1);

				/*
				 * Recompute the quotient digit since new info may have
				 * propagated into the top four dividend digits
				 */
				fdividend = (double) div[qi];
				for (i = 1; i < 4; i++)
				{
					fdividend *= NBASE;
					if (qi + i <= div_ndigits)
						fdividend += (double) div[qi + i];
				}
				/* Compute the (approximate) quotient digit */
				fquotient = fdividend * fdivisorinverse;
				qdigit = (fquotient >= 0.0) ? ((int) fquotient) :
					(((int) fquotient) - 1);	/* truncate towards -infinity */
				maxdiv += Abs(qdigit);
			}

			/* Subtract off the appropriate multiple of the divisor */
			if (qdigit != 0)
			{
				int			istop = Min(var2ndigits, div_ndigits - qi + 1);

				for (i = 0; i < istop; i++)
					div[qi + i] -= qdigit * var2digits[i];
			}
		}

		/*
		 * The dividend digit we are about to replace might still be nonzero.
		 * Fold it into the next digit position.  We don't need to worry about
		 * overflow here since this should nearly cancel with the subtraction
		 * of the divisor.
		 */
		div[qi + 1] += div[qi] * NBASE;

		div[qi] = qdigit;
	}

	/*
	 * Approximate and store the last quotient digit (div[div_ndigits])
	 */
	fdividend = (double) div[qi];
	for (i = 1; i < 4; i++)
		fdividend *= NBASE;
	fquotient = fdividend * fdivisorinverse;
	qdigit = (fquotient >= 0.0) ? ((int) fquotient) :
		(((int) fquotient) - 1);	/* truncate towards -infinity */
	div[qi] = qdigit;

	/*
	 * Now we do a final carry propagation pass to normalize the result, which
	 * we combine with storing the result digits into the output. Note that
	 * this is still done at full precision w/guard digits.
	 */
	alloc_var(result, div_ndigits + 1);
	res_digits = result->digits;
	carry = 0;
	for (i = div_ndigits; i >= 0; i--)
	{
		newdig = div[i] + carry;
		if (newdig < 0)
		{
			carry = -((-newdig - 1) / NBASE) - 1;
			newdig -= carry * NBASE;
		}
		else if (newdig >= NBASE)
		{
			carry = newdig / NBASE;
			newdig -= carry * NBASE;
		}
		else
			carry = 0;
		res_digits[i] = newdig;
	}
	Assert(carry == 0);

	if (div != tdiv)
		pfree(div);

	/*
	 * Finally, round the result to the requested precision.
	 */
	result->weight = res_weight;
	result->sign = res_sign;

	/* Round to target rscale (and set result->dscale) */
	if (round)
		round_var(result, rscale);
	else
		trunc_var(result, rscale);

	/* Strip leading and trailing zeroes */
	strip_var(result);
}


/*
 * Default scale selection for division
 *
 * Returns the appropriate result scale for the division result.
 */
static int
select_div_scale(NumericVar *var1, NumericVar *var2)
{
	int			weight1,
				weight2,
				qweight,
				i;
	NumericDigit firstdigit1,
				firstdigit2;
	int			rscale;

	/*
	 * The result scale of a division isn't specified in any SQL standard. For
	 * PostgreSQL we select a result scale that will give at least
	 * NUMERIC_MIN_SIG_DIGITS significant digits, so that numeric gives a
	 * result no less accurate than float8; but use a scale not less than
	 * either input's display scale.
	 */

	/* Get the actual (normalized) weight and first digit of each input */

	weight1 = 0;				/* values to use if var1 is zero */
	firstdigit1 = 0;
	for (i = 0; i < var1->ndigits; i++)
	{
		firstdigit1 = var1->digits[i];
		if (firstdigit1 != 0)
		{
			weight1 = var1->weight - i;
			break;
		}
	}

	weight2 = 0;				/* values to use if var2 is zero */
	firstdigit2 = 0;
	for (i = 0; i < var2->ndigits; i++)
	{
		firstdigit2 = var2->digits[i];
		if (firstdigit2 != 0)
		{
			weight2 = var2->weight - i;
			break;
		}
	}

	/*
	 * Estimate weight of quotient.  If the two first digits are equal, we
	 * can't be sure, but assume that var1 is less than var2.
	 */
	qweight = weight1 - weight2;
	if (firstdigit1 <= firstdigit2)
		qweight--;

	/* Select result scale */
	rscale = NUMERIC_MIN_SIG_DIGITS - qweight * DEC_DIGITS;
	rscale = Max(rscale, var1->dscale);
	rscale = Max(rscale, var2->dscale);
	rscale = Max(rscale, NUMERIC_MIN_DISPLAY_SCALE);
	rscale = Min(rscale, NUMERIC_MAX_DISPLAY_SCALE);

	return rscale;
}


/*
 * mod_var() -
 *
 *	Calculate the modulo of two numerics at variable level
 */
static void
mod_var(NumericVar *var1, NumericVar *var2, NumericVar *result)
{
	NumericVar	tmp;
	int			rscale;

	quick_init_var(&tmp);

	/* ---------
	 * We do this using the equation
	 *		mod(x,y) = x - trunc(x/y)*y
	 * We set rscale the same way numeric_div and numeric_mul do
	 * to get the right answer from the equation.  The final result,
	 * however, need not be displayed to more precision than the inputs.
	 * ----------
	 */
	rscale = select_div_scale(var1, var2);

	div_var(var1, var2, &tmp, rscale, false);

	trunc_var(&tmp, 0);

	mul_var(var2, &tmp, &tmp, var2->dscale + tmp.dscale);

	sub_var(var1, &tmp, result);

	free_var(&tmp);

	round_var(result, Max(var1->dscale, var2->dscale));
}


/*
 * ceil_var() -
 *
 *	Return the smallest integer greater than or equal to the argument
 *	on variable level
 */
static void
ceil_var(NumericVar *var, NumericVar *result)
{
	NumericVar	tmp;

	init_var_from_var(var, &tmp);

	trunc_var(&tmp, 0);

	if (var->sign == NUMERIC_POS && cmp_var(var, &tmp) != 0)
		add_var(&tmp, &const_one, result);
	else
		set_var_from_var(&tmp, result);
	free_var(&tmp);
}


/*
 * floor_var() -
 *
 *	Return the largest integer equal to or less than the argument
 *	on variable level
 */
static void
floor_var(NumericVar *var, NumericVar *result)
{
	NumericVar	tmp;

	init_var_from_var(var, &tmp);

	trunc_var(&tmp, 0);

	if (var->sign == NUMERIC_NEG && cmp_var(var, &tmp) != 0)
		sub_var(&tmp, &const_one, result);
	else
		set_var_from_var(&tmp, result);
	free_var(&tmp);
}


/*
 * sqrt_var() -
 *
 *	Compute the square root of x using Newton's algorithm
 */
static void
sqrt_var(NumericVar *arg, NumericVar *result, int rscale)
{
	NumericVar	tmp_arg;
	NumericVar	tmp_val;
	NumericVar	last_val;
	int			local_rscale;
	int			stat;

	local_rscale = rscale + 8;

	stat = cmp_var(arg, &const_zero);
	if (stat == 0)
	{
		zero_var(result);
		result->dscale = rscale;
		return;
	}

	/*
	 * SQL2003 defines sqrt() in terms of power, so we need to emit the right
	 * SQLSTATE error code if the operand is negative.
	 */
	if (stat < 0)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_ARGUMENT_FOR_POWER_FUNCTION),
				 errmsg("cannot take square root of a negative number"),
						 errOmitLocation(true)));

	/* Copy arg in case it is the same var as result */
	init_var_from_var(arg, &tmp_arg);

	/*
	 * Initialize the result to the first guess
	 */
	alloc_var(result, 1);
	result->digits[0] = tmp_arg.digits[0] / 2;
	if (result->digits[0] == 0)
		result->digits[0] = 1;
	result->weight = tmp_arg.weight / 2;
	result->sign = NUMERIC_POS;

	init_var_from_var(result, &last_val);
	quick_init_var(&tmp_val);

	for (;;)
	{
		div_var(&tmp_arg, result, &tmp_val, local_rscale, true);

		add_var(result, &tmp_val, result);
		mul_var(result, &const_zero_point_five, result, local_rscale);

		if (cmp_var(&last_val, result) == 0)
			break;
		set_var_from_var(result, &last_val);
	}

	free_var(&last_val);
	free_var(&tmp_val);
	free_var(&tmp_arg);

	/* Round to requested precision */
	round_var(result, rscale);
}


/*
 * exp_var() -
 *
 *	Raise e to the power of x
 */
static void
exp_var(NumericVar *arg, NumericVar *result, int rscale)
{
	NumericVar	x;
	int			xintval;
	bool		xneg = FALSE;
	int			local_rscale;

	/*----------
	 * We separate the integral and fraction parts of x, then compute
	 *		e^x = e^xint * e^xfrac
	 * where e = exp(1) and e^xfrac = exp(xfrac) are computed by
	 * exp_var_internal; the limited range of inputs allows that routine
	 * to do a good job with a simple Taylor series.  Raising e^xint is
	 * done by repeated multiplications in power_var_int.
	 *----------
	 */

	init_var_from_var(arg, &x);

	if (x.sign == NUMERIC_NEG)
	{
		xneg = TRUE;
		x.sign = NUMERIC_POS;
	}

	/* Extract the integer part, remove it from x */
	xintval = 0;
	while (x.weight >= 0)
	{
		xintval *= NBASE;
		if (x.ndigits > 0)
		{
			xintval += x.digits[0];
			x.digits++;
			x.ndigits--;
		}
		x.weight--;
		/* Guard against overflow */
		if (xintval >= NUMERIC_MAX_RESULT_SCALE * 3)
			ereport(ERROR,
					(errcode(ERRCODE_NUMERIC_VALUE_OUT_OF_RANGE),
					 errmsg("argument for function \"exp\" too big"),
							 errOmitLocation(true)));
	}

	/* Select an appropriate scale for internal calculation */
	local_rscale = rscale + MUL_GUARD_DIGITS * 2;

	/* Compute e^xfrac */
	exp_var_internal(&x, result, local_rscale);

	/* If there's an integer part, multiply by e^xint */
	if (xintval > 0)
	{
		NumericVar	e;

		quick_init_var(&e);
		exp_var_internal(&const_one, &e, local_rscale);
		power_var_int(&e, xintval, &e, local_rscale);
		mul_var(&e, result, result, local_rscale);
		free_var(&e);
	}

	free_var(&x);

	/* Compensate for input sign, and round to requested rscale */
	if (xneg)
		div_var(&const_one, result, result, rscale, true);
	else
		round_var(result, rscale);
}


/*
 * exp_var_internal() -
 *
 *	Raise e to the power of x, where 0 <= x <= 1
 *
 * NB: the result should be good to at least rscale digits, but it has
 * *not* been rounded off; the caller must do that if wanted.
 */
static void
exp_var_internal(NumericVar *arg, NumericVar *result, int rscale)
{
	NumericVar	x;
	NumericVar	xpow;
	NumericVar	ifac;
	NumericVar	elem;
	NumericVar	ni;
	int			ndiv2 = 0;
	int			local_rscale;

	quick_init_var(&elem);
	init_var_from_var(arg, &x);

	Assert(x.sign == NUMERIC_POS);

	local_rscale = rscale + 8;

	/* Reduce input into range 0 <= x <= 0.01 */
	while (cmp_var(&x, &const_zero_point_01) > 0)
	{
		ndiv2++;
		local_rscale++;
		mul_var(&x, &const_zero_point_five, &x, x.dscale + 1);
	}

	/*
	 * Use the Taylor series
	 *
	 * exp(x) = 1 + x + x^2/2! + x^3/3! + ...
	 *
	 * Given the limited range of x, this should converge reasonably quickly.
	 * We run the series until the terms fall below the local_rscale limit.
	 */
	add_var(&const_one, &x, result);
	init_var_from_var(&x, &xpow);
	init_ro_var_from_var(&const_one, &ifac);
	init_ro_var_from_var(&const_one, &ni);

	for (;;)
	{
		add_var(&ni, &const_one, &ni);
		mul_var(&xpow, &x, &xpow, local_rscale);
		mul_var(&ifac, &ni, &ifac, 0);
		div_var(&xpow, &ifac, &elem, local_rscale, true);

		if (elem.ndigits == 0)
			break;

		add_var(result, &elem, result);
	}

	/* Compensate for argument range reduction */
	while (ndiv2-- > 0)
		mul_var(result, result, result, local_rscale);

	free_var(&x);
	free_var(&xpow);
	free_var(&ifac);
	free_var(&elem);
	free_var(&ni);
}


/*
 * ln_var() -
 *
 *	Compute the natural log of x
 */
static void
ln_var(NumericVar *arg, NumericVar *result, int rscale)
{
	NumericVar	x;
	NumericVar	xx;
	NumericVar	ni;
	NumericVar	elem;
	NumericVar	fact;
	int			local_rscale;
	int			cmp;

	cmp = cmp_var(arg, &const_zero);
	if (cmp == 0)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_ARGUMENT_FOR_LOG),
				 errmsg("cannot take logarithm of zero"),
						 errOmitLocation(true)));
	else if (cmp < 0)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_ARGUMENT_FOR_LOG),
				 errmsg("cannot take logarithm of a negative number"),
						 errOmitLocation(true)));

	local_rscale = rscale + 8;

	quick_init_var(&xx);
	quick_init_var(&ni);
	quick_init_var(&elem);

	init_var_from_var(arg, &x);
	init_ro_var_from_var(&const_two, &fact);

	/* Reduce input into range 0.9 < x < 1.1 */
	while (cmp_var(&x, &const_zero_point_nine) <= 0)
	{
		local_rscale++;
		sqrt_var(&x, &x, local_rscale);
		mul_var(&fact, &const_two, &fact, 0);
	}
	while (cmp_var(&x, &const_one_point_one) >= 0)
	{
		local_rscale++;
		sqrt_var(&x, &x, local_rscale);
		mul_var(&fact, &const_two, &fact, 0);
	}

	/*
	 * We use the Taylor series for 0.5 * ln((1+z)/(1-z)),
	 *
	 * z + z^3/3 + z^5/5 + ...
	 *
	 * where z = (x-1)/(x+1) is in the range (approximately) -0.053 .. 0.048
	 * due to the above range-reduction of x.
	 *
	 * The convergence of this is not as fast as one would like, but is
	 * tolerable given that z is small.
	 */
	sub_var(&x, &const_one, result);
	add_var(&x, &const_one, &elem);
	div_var(result, &elem, result, local_rscale, true);
	set_var_from_var(result, &xx);
	mul_var(result, result, &x, local_rscale);

	set_var_from_var(&const_one, &ni);

	for (;;)
	{
		add_var(&ni, &const_two, &ni);
		mul_var(&xx, &x, &xx, local_rscale);
		div_var(&xx, &ni, &elem, local_rscale, true);

		if (elem.ndigits == 0)
			break;

		add_var(result, &elem, result);

		if (elem.weight < (result->weight - local_rscale * 2 / DEC_DIGITS))
			break;
	}

	/* Compensate for argument range reduction, round to requested rscale */
	mul_var(result, &fact, result, rscale);

	free_var(&x);
	free_var(&xx);
	free_var(&ni);
	free_var(&elem);
	free_var(&fact);
}


/*
 * log_var() -
 *
 *	Compute the logarithm of num in a given base.
 *
 *	Note: this routine chooses dscale of the result.
 */
static void
log_var(NumericVar *base, NumericVar *num, NumericVar *result)
{
	NumericVar	ln_base;
	NumericVar	ln_num;
	int			dec_digits;
	int			rscale;
	int			local_rscale;

	quick_init_var(&ln_base);
	quick_init_var(&ln_num);

	/* Set scale for ln() calculations --- compare numeric_ln() */

	/* Approx decimal digits before decimal point */
	dec_digits = (num->weight + 1) * DEC_DIGITS;

	if (dec_digits > 1)
		rscale = NUMERIC_MIN_SIG_DIGITS - (int) log10(dec_digits - 1);
	else if (dec_digits < 1)
		rscale = NUMERIC_MIN_SIG_DIGITS - (int) log10(1 - dec_digits);
	else
		rscale = NUMERIC_MIN_SIG_DIGITS;

	rscale = Max(rscale, base->dscale);
	rscale = Max(rscale, num->dscale);
	rscale = Max(rscale, NUMERIC_MIN_DISPLAY_SCALE);
	rscale = Min(rscale, NUMERIC_MAX_DISPLAY_SCALE);

	local_rscale = rscale + 8;

	/* Form natural logarithms */
	ln_var(base, &ln_base, local_rscale);
	ln_var(num, &ln_num, local_rscale);

	ln_base.dscale = rscale;
	ln_num.dscale = rscale;

	/* Select scale for division result */
	rscale = select_div_scale(&ln_num, &ln_base);

	div_var(&ln_num, &ln_base, result, rscale, true);

	free_var(&ln_num);
	free_var(&ln_base);
}


/*
 * power_var() -
 *
 *	Raise base to the power of exp
 *
 *	Note: this routine chooses dscale of the result.
 */
static void
power_var(NumericVar *base, NumericVar *exp, NumericVar *result)
{
	NumericVar	ln_base;
	NumericVar	ln_num;
	int			dec_digits;
	int			rscale;
	int			local_rscale;
	double		val;

	/* If exp can be represented as an integer, use power_var_int */
	if (exp->ndigits == 0 || exp->ndigits <= exp->weight + 1)
	{
		/* exact integer, but does it fit in int? */
		NumericVar	x;
		int64		expval64 = 0;

		/* must copy because numericvar_to_int8() scribbles on input */
		init_var_from_var(exp, &x);
		if (numericvar_to_int8(&x, &expval64))
		{
			int			expval = (int) expval64;

			/* Test for overflow by reverse-conversion. */
			if ((int64) expval == expval64)
			{
				/* Okay, select rscale */
				rscale = NUMERIC_MIN_SIG_DIGITS;
				rscale = Max(rscale, base->dscale);
				rscale = Max(rscale, NUMERIC_MIN_DISPLAY_SCALE);
				rscale = Min(rscale, NUMERIC_MAX_DISPLAY_SCALE);

				power_var_int(base, expval, result, rscale);

				return;
			}
		}
	}

	quick_init_var(&ln_base);
	quick_init_var(&ln_num);

	/* Set scale for ln() calculation --- need extra accuracy here */

	/* Approx decimal digits before decimal point */
	dec_digits = (base->weight + 1) * DEC_DIGITS;

	if (dec_digits > 1)
		rscale = NUMERIC_MIN_SIG_DIGITS * 2 - (int) log10(dec_digits - 1);
	else if (dec_digits < 1)
		rscale = NUMERIC_MIN_SIG_DIGITS * 2 - (int) log10(1 - dec_digits);
	else
		rscale = NUMERIC_MIN_SIG_DIGITS * 2;

	rscale = Max(rscale, base->dscale * 2);
	rscale = Max(rscale, exp->dscale * 2);
	rscale = Max(rscale, NUMERIC_MIN_DISPLAY_SCALE * 2);
	rscale = Min(rscale, NUMERIC_MAX_DISPLAY_SCALE * 2);

	local_rscale = rscale + 8;

	ln_var(base, &ln_base, local_rscale);

	mul_var(&ln_base, exp, &ln_num, local_rscale);
	free_var(&ln_base);

	/* Set scale for exp() -- compare numeric_exp() */

	/* convert input to float8, ignoring overflow */
	val = numericvar_to_double_no_overflow(&ln_num);

	/*
	 * log10(result) = num * log10(e), so this is approximately the weight:
	 */
	val *= 0.434294481903252;

	/* limit to something that won't cause integer overflow */
	val = Max(val, -NUMERIC_MAX_RESULT_SCALE);
	val = Min(val, NUMERIC_MAX_RESULT_SCALE);

	rscale = NUMERIC_MIN_SIG_DIGITS - (int) val;
	rscale = Max(rscale, base->dscale);
	rscale = Max(rscale, exp->dscale);
	rscale = Max(rscale, NUMERIC_MIN_DISPLAY_SCALE);
	rscale = Min(rscale, NUMERIC_MAX_DISPLAY_SCALE);

	exp_var(&ln_num, result, rscale);

	free_var(&ln_num);
}

/*
 * power_var_int() -
 *
 *	Raise base to the power of exp, where exp is an integer.
 */
static void
power_var_int(NumericVar *base, int exp, NumericVar *result, int rscale)
{
	bool		neg;
	NumericVar	base_prod;
	int			local_rscale;

	/* Detect some special cases, particularly 0^0. */

	switch (exp)
	{
		case 0:
			if (base->ndigits == 0)
				ereport(ERROR,
						(errcode(ERRCODE_FLOATING_POINT_EXCEPTION),
						 errmsg("zero raised to zero is undefined"),
								 errOmitLocation(true)));
			set_var_from_var(&const_one, result);
			result->dscale = rscale;	/* no need to round */
			return;
		case 1:
			set_var_from_var(base, result);
			round_var(result, rscale);
			return;
		case -1:
			div_var(&const_one, base, result, rscale, true);
			return;
		case 2:
			mul_var(base, base, result, rscale);
			return;
		default:
			break;
	}

	/*
	 * The general case repeatedly multiplies base according to the bit
	 * pattern of exp.	We do the multiplications with some extra precision.
	 */
	neg = (exp < 0);
	exp = Abs(exp);

	local_rscale = rscale + MUL_GUARD_DIGITS * 2;

	init_var_from_var(base, &base_prod);

	if (exp & 1)
		set_var_from_var(base, result);
	else
		set_var_from_var(&const_one, result);

	while ((exp >>= 1) > 0)
	{
		mul_var(&base_prod, &base_prod, &base_prod, local_rscale);
		if (exp & 1)
			mul_var(&base_prod, result, result, local_rscale);
	}

	free_var(&base_prod);

	/* Compensate for input sign, and round to requested rscale */
	if (neg)
		div_var(&const_one, result, result, rscale, true);
	else
		round_var(result, rscale);
}


/* ----------------------------------------------------------------------
 *
 * Following are the lowest level functions that operate unsigned
 * on the variable level
 *
 * ----------------------------------------------------------------------
 */


/* ----------
 * cmp_abs() -
 *
 *	Compare the absolute values of var1 and var2
 *	Returns:	-1 for ABS(var1) < ABS(var2)
 *				0  for ABS(var1) == ABS(var2)
 *				1  for ABS(var1) > ABS(var2)
 * ----------
 */
static int
cmp_abs(NumericVar *var1, NumericVar *var2)
{
	return cmp_abs_common(var1->digits, var1->ndigits, var1->weight,
						  var2->digits, var2->ndigits, var2->weight);
}

/* ----------
 * cmp_abs_common() -
 *
 *	Main routine of cmp_abs(). This function can be used by both
 *	NumericVar and Numeric.
 * ----------
 */
static int
cmp_abs_common(const NumericDigit *var1digits, int var1ndigits, int var1weight,
			 const NumericDigit *var2digits, int var2ndigits, int var2weight)
{
	int			i1 = 0;
	int			i2 = 0;

	/* Check any digits before the first common digit */

	while (var1weight > var2weight && i1 < var1ndigits)
	{
		if (var1digits[i1++] != 0)
			return 1;
		var1weight--;
	}
	while (var2weight > var1weight && i2 < var2ndigits)
	{
		if (var2digits[i2++] != 0)
			return -1;
		var2weight--;
	}

	/* At this point, either w1 == w2 or we've run out of digits */

	if (var1weight == var2weight)
	{
		while (i1 < var1ndigits && i2 < var2ndigits)
		{
			int			stat = var1digits[i1++] - var2digits[i2++];

			if (stat)
			{
				if (stat > 0)
					return 1;
				return -1;
			}
		}
	}

	/*
	 * At this point, we've run out of digits on one side or the other; so any
	 * remaining nonzero digits imply that side is larger
	 */
	while (i1 < var1ndigits)
	{
		if (var1digits[i1++] != 0)
			return 1;
	}
	while (i2 < var2ndigits)
	{
		if (var2digits[i2++] != 0)
			return -1;
	}

	return 0;
}


/*
 * add_abs() -
 *
 *	Add the absolute values of two variables into result.
 *	result might point to one of the operands without danger.
 */
static void
add_abs(NumericVar *var1, NumericVar *var2, NumericVar *result)
{
	NumericDigit *res_buf;
	NumericDigit *res_digits;
	int			res_ndigits;
	int			res_weight;
	int			res_rscale,
				rscale1,
				rscale2;
	int			res_dscale;
	int			i,
				i1,
				i2;
	int			carry = 0;

	/* copy these values into local vars for speed in inner loop */
	int			var1ndigits = var1->ndigits;
	int			var2ndigits = var2->ndigits;
	NumericDigit *var1digits = var1->digits;
	NumericDigit *var2digits = var2->digits;
	NumericDigit tdig[NUMERIC_LOCAL_NDIG];

	res_weight = Max(var1->weight, var2->weight) + 1;

	res_dscale = Max(var1->dscale, var2->dscale);

	/* Note: here we are figuring rscale in base-NBASE digits */
	rscale1 = var1->ndigits - var1->weight - 1;
	rscale2 = var2->ndigits - var2->weight - 1;
	res_rscale = Max(rscale1, rscale2);

	res_ndigits = res_rscale + res_weight + 1;
	if (res_ndigits <= 0)
		res_ndigits = 1;

	res_buf = tdig;
	if (res_ndigits > NUMERIC_LOCAL_NMAX) 
		res_buf = digitbuf_alloc(res_ndigits + 1);
	res_buf[0] = 0;				/* spare digit for later rounding */
	res_digits = res_buf + 1;

	i1 = res_rscale + var1->weight + 1;
	i2 = res_rscale + var2->weight + 1;
	for (i = res_ndigits - 1; i >= 0; i--)
	{
		i1--;
		i2--;
		if (i1 >= 0 && i1 < var1ndigits)
			carry += var1digits[i1];
		if (i2 >= 0 && i2 < var2ndigits)
			carry += var2digits[i2];

		if (carry >= NBASE)
		{
			res_digits[i] = carry - NBASE;
			carry = 1;
		}
		else
		{
			res_digits[i] = carry;
			carry = 0;
		}
	}

	Assert(carry == 0);			/* else we failed to allow for carry out */

	digitbuf_free(result);
	if (res_buf != tdig)
	{
		result->buf = res_buf;
		result->digits = res_digits;
	}
	else
	{
		result->digits = result->buf = result->ndb;
		memcpy(result->buf, res_buf, (sizeof(NumericDigit) * (res_ndigits +1)));
		result->digits ++;
	}
	result->ndigits = res_ndigits;
	result->weight = res_weight;
	result->dscale = res_dscale;

	/* Remove leading/trailing zeroes */
	strip_var(result);
}


/*
 * sub_abs()
 *
 *	Subtract the absolute value of var2 from the absolute value of var1
 *	and store in result. result might point to one of the operands
 *	without danger.
 *
 *	ABS(var1) MUST BE GREATER OR EQUAL ABS(var2) !!!
 */
static void
sub_abs(NumericVar *var1, NumericVar *var2, NumericVar *result)
{
	NumericDigit *res_buf;
	NumericDigit *res_digits;
	int			res_ndigits;
	int			res_weight;
	int			res_rscale,
				rscale1,
				rscale2;
	int			res_dscale;
	int			i,
				i1,
				i2;
	int			borrow = 0;

	/* copy these values into local vars for speed in inner loop */
	int			var1ndigits = var1->ndigits;
	int			var2ndigits = var2->ndigits;
	NumericDigit *var1digits = var1->digits;
	NumericDigit *var2digits = var2->digits;
	NumericDigit tdig[NUMERIC_LOCAL_NDIG];

	res_weight = var1->weight;

	res_dscale = Max(var1->dscale, var2->dscale);

	/* Note: here we are figuring rscale in base-NBASE digits */
	rscale1 = var1->ndigits - var1->weight - 1;
	rscale2 = var2->ndigits - var2->weight - 1;
	res_rscale = Max(rscale1, rscale2);

	res_ndigits = res_rscale + res_weight + 1;
	if (res_ndigits <= 0)
		res_ndigits = 1;

	res_buf = tdig;
	if (res_ndigits > NUMERIC_LOCAL_NMAX) 
		res_buf = digitbuf_alloc(res_ndigits + 1);
	res_buf[0] = 0;				/* spare digit for later rounding */
	res_digits = res_buf + 1;

	i1 = res_rscale + var1->weight + 1;
	i2 = res_rscale + var2->weight + 1;
	for (i = res_ndigits - 1; i >= 0; i--)
	{
		i1--;
		i2--;
		if (i1 >= 0 && i1 < var1ndigits)
			borrow += var1digits[i1];
		if (i2 >= 0 && i2 < var2ndigits)
			borrow -= var2digits[i2];

		if (borrow < 0)
		{
			res_digits[i] = borrow + NBASE;
			borrow = -1;
		}
		else
		{
			res_digits[i] = borrow;
			borrow = 0;
		}
	}

	Assert(borrow == 0);		/* else caller gave us var1 < var2 */

	digitbuf_free(result);
	if (res_buf != tdig)
	{
		result->buf = res_buf;
		result->digits = res_digits;
	}
	else
	{
		result->digits = result->buf = result->ndb;
		memcpy(result->buf, res_buf, (sizeof(NumericDigit) * (res_ndigits +1)));
		result->digits ++;
	}
	result->ndigits = res_ndigits;
	result->weight = res_weight;
	result->dscale = res_dscale;

	/* Remove leading/trailing zeroes */
	strip_var(result);
}

/*
 * round_var
 *
 * Round the value of a variable to no more than rscale decimal digits
 * after the decimal point.  NOTE: we allow rscale < 0 here, implying
 * rounding before the decimal point.
 */
static void
round_var(NumericVar *var, int rscale)
{
	NumericDigit *digits = var->digits;
	int			di;
	int			ndigits;
	int			carry;

	var->dscale = rscale;

	/* decimal digits wanted */
	di = (var->weight + 1) * DEC_DIGITS + rscale;

	/*
	 * If di = 0, the value loses all digits, but could round up to 1 if its
	 * first extra digit is >= 5.  If di < 0 the result must be 0.
	 */
	if (di < 0)
	{
		var->ndigits = 0;
		var->weight = 0;
		var->sign = NUMERIC_POS;
	}
	else
	{
		/* NBASE digits wanted */
		ndigits = (di + DEC_DIGITS - 1) / DEC_DIGITS;

		/* 0, or number of decimal digits to keep in last NBASE digit */
		di %= DEC_DIGITS;

		if (ndigits < var->ndigits ||
			(ndigits == var->ndigits && di > 0))
		{
			var->ndigits = ndigits;

#if DEC_DIGITS == 1
			/* di must be zero */
			carry = (digits[ndigits] >= HALF_NBASE) ? 1 : 0;
#else
			if (di == 0)
				carry = (digits[ndigits] >= HALF_NBASE) ? 1 : 0;
			else
			{
				/* Must round within last NBASE digit */
				int			extra,
							pow10;

#if DEC_DIGITS == 4
				pow10 = round_powers[di];
#elif DEC_DIGITS == 2
				pow10 = 10;
#else
#error unsupported NBASE
#endif
				extra = digits[--ndigits] % pow10;
				digits[ndigits] -= extra;
				carry = 0;
				if (extra >= pow10 / 2)
				{
					pow10 += digits[ndigits];
					if (pow10 >= NBASE)
					{
						pow10 -= NBASE;
						carry = 1;
					}
					digits[ndigits] = pow10;
				}
			}
#endif

			/* Propagate carry if needed */
			while (carry)
			{
				carry += digits[--ndigits];
				if (carry >= NBASE)
				{
					digits[ndigits] = carry - NBASE;
					carry = 1;
				}
				else
				{
					digits[ndigits] = carry;
					carry = 0;
				}
			}

			if (ndigits < 0)
			{
				Assert(ndigits == -1);	/* better not have added > 1 digit */
				Assert(var->digits > var->buf);
				var->digits--;
				var->ndigits++;
				var->weight++;
			}
		}
	}
}

/*
 * trunc_var
 *
 * Truncate the value of a variable at rscale decimal digits after the
 * decimal point.  NOTE: we allow rscale < 0 here, implying
 * truncation before the decimal point.
 */
static void
trunc_var(NumericVar *var, int rscale)
{
	int			di;
	int			ndigits;

	var->dscale = rscale;

	/* decimal digits wanted */
	di = (var->weight + 1) * DEC_DIGITS + rscale;

	/*
	 * If di <= 0, the value loses all digits.
	 */
	if (di <= 0)
	{
		var->ndigits = 0;
		var->weight = 0;
		var->sign = NUMERIC_POS;
	}
	else
	{
		/* NBASE digits wanted */
		ndigits = (di + DEC_DIGITS - 1) / DEC_DIGITS;

		if (ndigits <= var->ndigits)
		{
			var->ndigits = ndigits;

#if DEC_DIGITS == 1
			/* no within-digit stuff to worry about */
#else
			/* 0, or number of decimal digits to keep in last NBASE digit */
			di %= DEC_DIGITS;

			if (di > 0)
			{
				/* Must truncate within last NBASE digit */
				NumericDigit *digits = var->digits;
				int			extra,
							pow10;

#if DEC_DIGITS == 4
				pow10 = round_powers[di];
#elif DEC_DIGITS == 2
				pow10 = 10;
#else
#error unsupported NBASE
#endif
				extra = digits[--ndigits] % pow10;
				digits[ndigits] -= extra;
			}
#endif
		}
	}
}

/*
 * strip_var
 *
 * Strip any leading and trailing zeroes from a numeric variable
 */
static void
strip_var(NumericVar *var)
{
	NumericDigit *digits = var->digits;
	int			ndigits = var->ndigits;

	/* Strip leading zeroes */
	while (ndigits > 0 && *digits == 0)
	{
		digits++;
		var->weight--;
		ndigits--;
	}

	/* Strip trailing zeroes */
	while (ndigits > 0 && digits[ndigits - 1] == 0)
		ndigits--;

	/* If it's zero, normalize the sign and weight */
	if (ndigits == 0)
	{
		var->sign = NUMERIC_POS;
		var->weight = 0;
	}

	var->digits = digits;
	var->ndigits = ndigits;
}


/* ----------------------------------------------------------------------
 *
 * Aggregate functions -- Greenplum Database Extensions
 *
 * Greenplum Database adds some builtin functions to amalgamate transition type
 * instances for two-stage aggregation.
 *
 * ----------------------------------------------------------------------
 */

static ArrayType *
do_numeric_amalg_demalg(ArrayType *aTransArray, ArrayType *bTransArray,
						bool is_amalg)
{
	Datum	   *transdatums;
	int			ndatums;
	Datum		aN, bN,
				aSumX, bSumX,
				aSumX2, bSumX2;
	ArrayType  *result;
	PGFunction	malgfunc;
	
	/* We assume the input is array of numeric */
	deconstruct_array(aTransArray,
					  NUMERICOID, -1, false, 'i',
					  &transdatums, NULL, &ndatums);
	if (ndatums != 3)
		elog(ERROR, "expected 3-element numeric array");
	aN = transdatums[0];
	aSumX = transdatums[1];
	aSumX2 = transdatums[2];

	deconstruct_array(bTransArray,
					  NUMERICOID, -1, false, 'i',
					  &transdatums, NULL, &ndatums);
	if (ndatums != 3)
		elog(ERROR, "expected 3-element numeric array");
	bN = transdatums[0];
	bSumX = transdatums[1];
	bSumX2 = transdatums[2];

	if (is_amalg)
		malgfunc = numeric_add;
	else
		malgfunc = numeric_sub;

	aN = DirectFunctionCall2(malgfunc, aN, bN);
	aSumX = DirectFunctionCall2(malgfunc, aSumX, bSumX);
	aSumX2 = DirectFunctionCall2(malgfunc, aSumX2, bSumX2);

	transdatums[0] = aN;
	transdatums[1] = aSumX;
	transdatums[2] = aSumX2;

	result = construct_array(transdatums, 3,
							 NUMERICOID, -1, false, 'i');

	return result;
}

Datum
numeric_amalg(PG_FUNCTION_ARGS)
{
	ArrayType  *aTransArray = PG_GETARG_ARRAYTYPE_P(0);
	ArrayType  *bTransArray = PG_GETARG_ARRAYTYPE_P(1);

	PG_RETURN_ARRAYTYPE_P(do_numeric_amalg_demalg(aTransArray, bTransArray,
												  true));
}

Datum
numeric_demalg(PG_FUNCTION_ARGS)
{
	ArrayType  *aTransArray = PG_GETARG_ARRAYTYPE_P(0);
	ArrayType  *bTransArray = PG_GETARG_ARRAYTYPE_P(1);

	PG_RETURN_ARRAYTYPE_P(do_numeric_amalg_demalg(aTransArray, bTransArray,
												  false));
}

/* Helper for MPP, declared here and in nodeWindow.c
 *
 * Convert numeric to positive int64 by truncation, if invalid return
 * -1 (negative num) or 0 (out of range num).
 */
extern int64 numeric_to_pos_int8_trunc(Numeric num);

int64
numeric_to_pos_int8_trunc(Numeric num)
{
	NumericVar var;
	int64 result = 0;
	
	/* Truncate ntile_arg to int8, put in result. */
	init_var_from_num(num, &var);
	if ( var.sign == NUMERIC_POS )
	{
		trunc_var(&var, 0);
		if ( !numericvar_to_int8(&var, &result) )
			result = 0; /* out of range */
	}
	else
	{
		result = -1; /* out of range */
	}
	free_var(&var);
	
	return result;
}

/*
 * numeric_normalize() -
 *
 *  Output function for numeric data type without trailing zeroes.
 */
char *
numeric_normalize(Numeric num)
{
  NumericVar  x;
  char     *str;
  int     orig, last;

  /*
   * Handle NaN
   */
  if (NUMERIC_IS_NAN(num))
    return pstrdup("NaN");

  init_var_from_num(num, &x);

  str = get_str_from_var(&x,x.dscale);

  orig = last = strlen(str) - 1;

  for (;;)
  {
    if (last == 0 || str[last] != '0')
      break;

    last--;
  }

  if (last > 0 && last != orig)
    str[last] = '\0';

  return str;
}
