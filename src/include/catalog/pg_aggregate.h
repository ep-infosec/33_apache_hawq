/*-------------------------------------------------------------------------
 *
 * pg_aggregate.h
 *	  definition of the system "aggregate" relation (pg_aggregate)
 *	  along with the relation's initial contents.
 *
 *
 * Portions Copyright (c) 1996-2008, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $PostgreSQL: pgsql/src/include/catalog/pg_aggregate.h,v 1.58 2006/10/04 00:30:07 momjian Exp $
 *
 * NOTES
 *	  the genbki.sh script reads this file and generates .bki
 *	  information from the DATA() statements.
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_AGGREGATE_H
#define PG_AGGREGATE_H

#include "catalog/genbki.h"
#include "nodes/pg_list.h"

/* ----------------------------------------------------------------
 *		pg_aggregate definition.
 *
 *		cpp turns this into typedef struct FormData_pg_aggregate
 *
 *	aggfnoid			pg_proc OID of the aggregate itself
 *	aggtransfn			transition function
 *	agginvtransfn		inverse transition function
 *  aggprelimfn			preliminary aggregation function
 *  agginvprelimfn		inverse preliminary aggregation function
 *	aggfinalfn			final function (0 if none)
 *	aggsortop			associated sort operator (0 if none)
 *	aggtranstype		type of aggregate's transition (state) data
 *	agginitval			initial value for transition state (can be NULL)
 *  aggorder            array of ordering columns (can be NULL)
 * ----------------------------------------------------------------
 */

/* TIDYCAT_BEGINDEF

   CREATE TABLE pg_aggregate
   with (oid=false, relid=2600)
   (
   aggfnoid        regproc, 
   aggtransfn      regproc, 
   agginvtransfn   regproc, -- MPP windowing
   aggprelimfn     regproc, -- MPP 2-phase agg
   agginvprelimfn  regproc, -- MPP windowing
   aggfinalfn      regproc, 
   aggsortop       oid, 
   aggtranstype    oid, 
   agginitval      text,    -- VARIABLE LENGTH FIELD
   aggordered      bool     -- MPP ordered aggregates
   );

   create unique index on pg_aggregate(aggfnoid) with (indexid=2650, indexname=pg_aggregate_fnoid_index, syscacheid=AGGFNOID, syscache_nbuckets=32);

   alter table pg_aggregate add fk aggfnoid on pg_proc(oid);
   alter table pg_aggregate add fk aggtransfn on pg_proc(oid);
   alter table pg_aggregate add fk agginvtransfn on pg_proc(oid);
   alter table pg_aggregate add fk aggprelimfn on pg_proc(oid);
   alter table pg_aggregate add fk agginvprelimfn on pg_proc(oid);
   alter table pg_aggregate add fk aggfinalfn on pg_proc(oid);
   alter table pg_aggregate add fk aggsortop on pg_operator(oid);
   alter table pg_aggregate add fk aggtranstype on pg_type(oid);

   TIDYCAT_ENDDEF

*/

/* TIDYCAT_BEGIN_CODEGEN 

   WARNING: DO NOT MODIFY THE FOLLOWING SECTION: 
   Generated by tidycat.pl version 33
   on Wed Aug 15 14:23:37 2012
*/


/*
 TidyCat Comments for pg_aggregate:
  Table does not have an Oid column.
  Table does not have static type (only legal for pre-3.3 tables). 
  Table has TOASTable columns, but NO TOAST table.

*/

/* ----------------
 *		pg_aggregate definition.  cpp turns this into
 *		typedef struct FormData_pg_aggregate
 * ----------------
 */
#define AggregateRelationId	2600

CATALOG(pg_aggregate,2600) BKI_WITHOUT_OIDS
{
	regproc	aggfnoid;		
	regproc	aggtransfn;		
	regproc	agginvtransfn;	/* MPP windowing */
	regproc	aggprelimfn;	/* MPP 2-phase agg */
	regproc	agginvprelimfn;	/* MPP windowing */
	regproc	aggfinalfn;		
	Oid		aggsortop;		
	Oid		aggtranstype;	
	text	agginitval;		/* VARIABLE LENGTH FIELD */
	bool	aggordered;		/* MPP ordered aggregates */
} FormData_pg_aggregate;


/* ----------------
 *		Form_pg_aggregate corresponds to a pointer to a tuple with
 *		the format of pg_aggregate relation.
 * ----------------
 */
typedef FormData_pg_aggregate *Form_pg_aggregate;


/* ----------------
 *		compiler constants for pg_aggregate
 * ----------------
 */
#define Natts_pg_aggregate					10
#define Anum_pg_aggregate_aggfnoid			1
#define Anum_pg_aggregate_aggtransfn		2
#define Anum_pg_aggregate_agginvtransfn		3
#define Anum_pg_aggregate_aggprelimfn		4
#define Anum_pg_aggregate_agginvprelimfn	5
#define Anum_pg_aggregate_aggfinalfn		6
#define Anum_pg_aggregate_aggsortop			7
#define Anum_pg_aggregate_aggtranstype		8
#define Anum_pg_aggregate_agginitval		9
#define Anum_pg_aggregate_aggordered		10


/* TIDYCAT_END_CODEGEN */

/*
 * Constant definitions of pg_proc.oid for aggregate functions.
 */
#define AGGFNOID_COUNT_ANY 2147 /* returns INT8OID */
#define AGGFNOID_SUM_BIGINT 2107 /* returns NUMERICOID */

/* ----------------
 * initial contents of pg_aggregate
 * ---------------
 */

/* avg */
DATA(insert ( 2100	int8_avg_accum	int8_avg_decum int8_avg_amalg int8_avg_demalg int8_avg	0 17 "" f)); 
DATA(insert ( 2101	int4_avg_accum	int4_avg_decum int8_avg_amalg int8_avg_demalg int8_avg	0 17 "" f)); 
DATA(insert ( 2102	int2_avg_accum	int2_avg_decum int8_avg_amalg int8_avg_demalg int8_avg	0 17 "" f)); 
DATA(insert ( 2103	numeric_avg_accum numeric_avg_decum	 numeric_avg_amalg numeric_avg_demalg numeric_avg 0	17 "" f));
DATA(insert ( 2104	float4_avg_accum float4_avg_decum float8_avg_amalg float8_avg_demalg float8_avg	0 17 "" f)); 
DATA(insert ( 2105	float8_avg_accum float8_avg_decum float8_avg_amalg float8_avg_demalg float8_avg	0 17 "" f));	
DATA(insert ( 2106	interval_accum	interval_decum interval_amalg	interval_demalg interval_avg	0	1187	"{0 second,0 second}"  f));

/* sum */
DATA(insert ( 2107	int8_sum		int8_invsum 	numeric_add		numeric_sub -				0	1700	_null_  f));
DATA(insert ( 2108	int4_sum		int4_invsum 	int8pl			int8mi -				0	20		_null_  f));
DATA(insert ( 2109	int2_sum		int2_invsum		int8pl			int8mi -				0	20		_null_  f));
DATA(insert ( 2110	float4pl		float4mi float4pl			float4mi -				0	700		_null_  f));
DATA(insert ( 2111	float8pl		float8mi float8pl			float8mi -				0	701		_null_  f));
DATA(insert ( 2112	cash_pl			cash_mi cash_pl			cash_mi -				0	790		_null_  f));
DATA(insert ( 2113	interval_pl		interval_mi interval_pl		interval_mi -				0	1186	_null_  f));
DATA(insert ( 2114	numeric_add		numeric_sub numeric_add		numeric_sub -				0	1700	_null_  f));

/* max */
DATA(insert ( 2115	int8larger		- int8larger		- -				413		20		_null_  f));
DATA(insert ( 2116	int4larger		- int4larger		- -				521		23		_null_  f));
DATA(insert ( 2117	int2larger		- int2larger		- -				520		21		_null_  f));
DATA(insert ( 2118	oidlarger		- oidlarger			- -				610		26		_null_  f));
DATA(insert ( 2119	float4larger	- float4larger		- -				623		700		_null_  f));
DATA(insert ( 2120	float8larger	- float8larger		- -				674		701		_null_  f));
DATA(insert ( 2121	int4larger		- int4larger		- -				563		702		_null_  f));
DATA(insert ( 2122	date_larger		- date_larger		- -				1097	1082	_null_  f));
DATA(insert ( 2123	time_larger		- time_larger		- -				1112	1083	_null_  f));
DATA(insert ( 2124	timetz_larger	- timetz_larger		- -				1554	1266	_null_  f));
DATA(insert ( 2125	cashlarger		- cashlarger		- -				903		790		_null_  f));
DATA(insert ( 2126	timestamp_larger - timestamp_larger - -				2064	1114	_null_  f));
DATA(insert ( 2127	timestamptz_larger - timestamptz_larger - -			1324	1184	_null_  f));
DATA(insert ( 2128	interval_larger - interval_larger	- -				1334	1186	_null_  f));
DATA(insert ( 2129	text_larger		- text_larger		- -				666		25		_null_  f));
DATA(insert ( 2130	numeric_larger	- numeric_larger	- -				1756	1700	_null_  f));
DATA(insert ( 2050	array_larger	- array_larger		- -				1073	2277	_null_  f));
DATA(insert ( 2244	bpchar_larger	- bpchar_larger		- -				1060	1042	_null_  f));
DATA(insert ( 2797	tidlarger		- tidlarger			- -				2800	27		_null_  f));
DATA(insert ( 3332	gpxlogloclarger	- gpxlogloclarger	- -				3328	3310	_null_  f));

/* min */
DATA(insert ( 2131	int8smaller		- int8smaller		- -				412		20		_null_  f));
DATA(insert ( 2132	int4smaller		- int4smaller		- -				97		23		_null_  f));
DATA(insert ( 2133	int2smaller		- int2smaller		- -				95		21		_null_  f));
DATA(insert ( 2134	oidsmaller		- oidsmaller		- -				609		26		_null_  f));
DATA(insert ( 2135	float4smaller	- float4smaller		- -				622		700		_null_  f));
DATA(insert ( 2136	float8smaller	- float8smaller		- -				672		701		_null_  f));
DATA(insert ( 2137	int4smaller		- int4smaller		- -				562		702		_null_  f));
DATA(insert ( 2138	date_smaller	- date_smaller		- -				1095	1082	_null_  f));
DATA(insert ( 2139	time_smaller	- time_smaller		- -				1110	1083	_null_  f));
DATA(insert ( 2140	timetz_smaller	- timetz_smaller	- -				1552	1266	_null_  f));
DATA(insert ( 2141	cashsmaller		- cashsmaller		- -				902		790		_null_  f));
DATA(insert ( 2142	timestamp_smaller	- timestamp_smaller	- -			2062	1114	_null_  f));
DATA(insert ( 2143	timestamptz_smaller - timestamptz_smaller - -		1322	1184	_null_  f));
DATA(insert ( 2144	interval_smaller	- interval_smaller	- -			1332	1186	_null_  f));
DATA(insert ( 2145	text_smaller	- text_smaller		- -				664		25		_null_  f));
DATA(insert ( 2146	numeric_smaller - numeric_smaller	- -				1754	1700	_null_  f));
DATA(insert ( 2051	array_smaller	- array_smaller		- -				1072	2277	_null_  f));
DATA(insert ( 2245	bpchar_smaller	- bpchar_smaller	- -				1058	1042	_null_  f));
DATA(insert ( 2798	tidsmaller		- tidsmaller		- -				2799	27		_null_  f));
DATA(insert ( 3333	gpxloglocsmaller - gpxloglocsmaller		- -			3327	3310	_null_  f));

/* count */
DATA(insert ( 2147	int8inc_any		- int8pl	int8mi -				0		20		"0"  f));
DATA(insert ( 2803	int8inc			int8dec	 int8pl	int8mi -				0		20		"0"  f));

/* var_pop */
DATA(insert ( 2718	int8_accum		int8_decum	 numeric_amalg	numeric_demalg numeric_var_pop	0	1231	"{0,0,0}"  f));
DATA(insert ( 2719	int4_accum		int4_decum	 numeric_amalg	numeric_demalg numeric_var_pop	0	1231	"{0,0,0}"  f));
DATA(insert ( 2720	int2_accum		int2_decum	 numeric_amalg	numeric_demalg numeric_var_pop	0	1231	"{0,0,0}"  f));
DATA(insert ( 2721	float4_accum	float4_decum float8_amalg	float8_demalg float8_var_pop	0	1022	"{0,0,0}"  f));
DATA(insert ( 2722	float8_accum	float8_decum float8_amalg	float8_demalg float8_var_pop	0	1022	"{0,0,0}"  f));
DATA(insert ( 2723	numeric_accum	numeric_decum	 numeric_amalg	numeric_demalg numeric_var_pop	0	1231	"{0,0,0}"  f));

/* var_samp */
DATA(insert ( 2641	int8_accum		int8_decum	 numeric_amalg	numeric_demalg numeric_var_samp	0	1231	"{0,0,0}"  f));
DATA(insert ( 2642	int4_accum		int4_decum	 numeric_amalg	numeric_demalg numeric_var_samp	0	1231	"{0,0,0}"  f));
DATA(insert ( 2643	int2_accum		int2_decum	 numeric_amalg	numeric_demalg numeric_var_samp	0	1231	"{0,0,0}"  f));
DATA(insert ( 2644	float4_accum	float4_decum float8_amalg	float8_demalg float8_var_samp	0	1022	"{0,0,0}"  f));
DATA(insert ( 2645	float8_accum	float8_decum float8_amalg	float8_demalg float8_var_samp	0	1022	"{0,0,0}"  f));
DATA(insert ( 2646	numeric_accum 	numeric_decum	 numeric_amalg	numeric_demalg numeric_var_samp	0	1231	"{0,0,0}"  f));

/* variance: historical Postgres syntax for var_samp */
DATA(insert ( 2148	int8_accum		int8_decum	 numeric_amalg	numeric_demalg numeric_var_samp	0	1231	"{0,0,0}"  f));
DATA(insert ( 2149	int4_accum		int4_decum	 numeric_amalg	numeric_demalg numeric_var_samp	0	1231	"{0,0,0}"  f));
DATA(insert ( 2150	int2_accum		int2_decum	 numeric_amalg	numeric_demalg numeric_var_samp	0	1231	"{0,0,0}"  f));
DATA(insert ( 2151	float4_accum	float4_decum float8_amalg	float8_demalg float8_var_samp	0	1022	"{0,0,0}"  f));
DATA(insert ( 2152	float8_accum	float8_decum float8_amalg	float8_demalg float8_var_samp	0	1022	"{0,0,0}"  f));
DATA(insert ( 2153	numeric_accum	numeric_decum	 numeric_amalg	numeric_demalg numeric_var_samp	0	1231	"{0,0,0}"  f));

/* stddev_pop */
DATA(insert ( 2724	int8_accum		int8_decum	 numeric_amalg	numeric_demalg numeric_stddev_pop	0	1231	"{0,0,0}"  f));
DATA(insert ( 2725	int4_accum		int4_decum	 numeric_amalg	numeric_demalg numeric_stddev_pop	0	1231	"{0,0,0}"  f));
DATA(insert ( 2726	int2_accum		int2_decum	 numeric_amalg	numeric_demalg numeric_stddev_pop	0	1231	"{0,0,0}"  f));
DATA(insert ( 2727	float4_accum	float4_decum float8_amalg	float8_demalg float8_stddev_pop		0	1022	"{0,0,0}"  f));
DATA(insert ( 2728	float8_accum	float8_decum float8_amalg	float8_demalg float8_stddev_pop		0	1022	"{0,0,0}"  f));
DATA(insert ( 2729	numeric_accum	numeric_decum	 numeric_amalg	numeric_demalg numeric_stddev_pop	0	1231	"{0,0,0}"  f));

/* stddev_samp */
DATA(insert ( 2712	int8_accum		int8_decum	 numeric_amalg	numeric_demalg numeric_stddev_samp	0	1231	"{0,0,0}"  f));
DATA(insert ( 2713	int4_accum		int4_decum	 numeric_amalg	numeric_demalg numeric_stddev_samp	0	1231	"{0,0,0}"  f));
DATA(insert ( 2714	int2_accum		int2_decum	 numeric_amalg	numeric_demalg numeric_stddev_samp	0	1231	"{0,0,0}"  f));
DATA(insert ( 2715	float4_accum	float4_decum float8_amalg	float8_demalg float8_stddev_samp	0	1022	"{0,0,0}"  f));
DATA(insert ( 2716	float8_accum	float8_decum float8_amalg	float8_demalg float8_stddev_samp	0	1022	"{0,0,0}"  f));
DATA(insert ( 2717	numeric_accum	numeric_decum	 numeric_amalg	numeric_demalg numeric_stddev_samp	0	1231	"{0,0,0}"  f));

/* stddev: historical Postgres syntax for stddev_samp */
DATA(insert ( 2154	int8_accum		int8_decum	 numeric_amalg	numeric_demalg numeric_stddev_samp	0	1231	"{0,0,0}"  f));
DATA(insert ( 2155	int4_accum		int4_decum	 numeric_amalg	numeric_demalg numeric_stddev_samp	0	1231	"{0,0,0}"  f));
DATA(insert ( 2156	int2_accum		int2_decum numeric_amalg	numeric_demalg numeric_stddev_samp	0	1231	"{0,0,0}"  f));
DATA(insert ( 2157	float4_accum	float4_decum float8_amalg	float8_demalg float8_stddev_samp	0	1022	"{0,0,0}"  f));
DATA(insert ( 2158	float8_accum	float8_decum float8_amalg	float8_demalg float8_stddev_samp	0	1022	"{0,0,0}"  f));
DATA(insert ( 2159	numeric_accum	numeric_decum numeric_amalg	numeric_demalg numeric_stddev_samp	0	1231	"{0,0,0}"  f));

/* SQL2003 binary regression aggregates */
DATA(insert ( 2818	int8inc_float8_float8	- int8pl					- -							0	20		"0"  f));
DATA(insert ( 2819	float8_regr_accum		- float8_regr_amalg	- float8_regr_sxx			0	1022	"{0,0,0,0,0,0}"  f));
DATA(insert ( 2820	float8_regr_accum		- float8_regr_amalg	- float8_regr_syy			0	1022	"{0,0,0,0,0,0}"  f));
DATA(insert ( 2821	float8_regr_accum		- float8_regr_amalg	- float8_regr_sxy			0	1022	"{0,0,0,0,0,0}"  f));
DATA(insert ( 2822	float8_regr_accum		- float8_regr_amalg	- float8_regr_avgx			0	1022	"{0,0,0,0,0,0}"  f));
DATA(insert ( 2823	float8_regr_accum		- float8_regr_amalg	- float8_regr_avgy			0	1022	"{0,0,0,0,0,0}"  f));
DATA(insert ( 2824	float8_regr_accum		- float8_regr_amalg	- float8_regr_r2			0	1022	"{0,0,0,0,0,0}"  f));
DATA(insert ( 2825	float8_regr_accum		- float8_regr_amalg	- float8_regr_slope			0	1022	"{0,0,0,0,0,0}"  f));
DATA(insert ( 2826	float8_regr_accum		- float8_regr_amalg	- float8_regr_intercept		0	1022	"{0,0,0,0,0,0}"  f));
DATA(insert ( 2827	float8_regr_accum		- float8_regr_amalg	- float8_covar_pop			0	1022	"{0,0,0,0,0,0}"  f));
DATA(insert ( 2828	float8_regr_accum		- float8_regr_amalg	- float8_covar_samp			0	1022	"{0,0,0,0,0,0}"  f));
DATA(insert ( 2829	float8_regr_accum		- float8_regr_amalg	- float8_corr				0	1022	"{0,0,0,0,0,0}"  f));

/* boolean-and and boolean-or */
DATA(insert ( 2517	booland_statefunc	- booland_statefunc	- -			0	16		_null_  f));
DATA(insert ( 2518	boolor_statefunc	- boolor_statefunc	- -			0	16		_null_  f));
DATA(insert ( 2519	booland_statefunc	- booland_statefunc	- -			0	16		_null_  f));

/* bitwise integer */
DATA(insert ( 2236 int2and		  - int2and		  - -					0	21		_null_  f));
DATA(insert ( 2237 int2or		  - int2or		  - -					0	21		_null_  f));
DATA(insert ( 2238 int4and		  - int4and		  - -					0	23		_null_  f));
DATA(insert ( 2239 int4or		  - int4or		  - -					0	23		_null_  f));
DATA(insert ( 2240 int8and		  - int8and		  - -					0	20		_null_  f));
DATA(insert ( 2241 int8or		  - int8or		  - -					0	20		_null_  f));
DATA(insert ( 2242 bitand		  - bitand		  - -					0	1560	_null_  f));
DATA(insert ( 2243 bitor		  - bitor		  - -					0	1560	_null_  f));

/* MPP Aggregate -- array_sum -- special for prospective customer. */
DATA(insert ( 6013	array_add     - array_add	  - -					0	1007	"{}"  f));

/* sum(array[]) */
DATA(insert ( 3216  int2_matrix_accum     - int8_matrix_accum     - -   0   1016    _null_  f));
DATA(insert ( 3217  int4_matrix_accum     - int8_matrix_accum     - -   0   1016    _null_  f));
DATA(insert ( 3218  int8_matrix_accum     - int8_matrix_accum     - -   0   1016    _null_  f));
DATA(insert ( 3219  float8_matrix_accum   - float8_matrix_accum   - -   0   1022    _null_  f));

/* pivot_sum(...) */
DATA(insert ( 3226  int4_pivot_accum      - int8_matrix_accum     - -   0   1007    _null_  f));
DATA(insert ( 3228  int8_pivot_accum      - int8_matrix_accum     - -   0   1016    _null_  f));
DATA(insert ( 3230  float8_pivot_accum    - float8_matrix_accum   - -   0   1022    _null_  f));

/* xml */
DATA(insert ( 2981  xmlconcat2	             - - - - 				  0	142  _null_ t));

/* array */
DATA(insert ( 2910	array_agg_transfn        - - - array_agg_finalfn  0 2281 _null_ t));

/* text */
DATA(insert ( 3537	string_agg_transfn       - - - string_agg_finalfn 0 2281 _null_ t));
DATA(insert ( 3538	string_agg_delim_transfn - - - string_agg_finalfn 0 2281 _null_ t));

DATA(insert ( 2913	pg_partition_oid_transfn      - - - pg_partition_oid_finalfn 0 2281 _null_ f));

/* json */
DATA(insert ( 3175 json_agg_transfn - - - json_agg_finalfn 0 2281 _null_ f));
DATA(insert ( 3197 json_object_agg_transfn - - - json_object_agg_finalfn 0 2281 _null_ f));

/* jsonb */
DATA(insert ( 5521 jsonb_agg_transfn - - - jsonb_agg_finalfn 0 2281 _null_ f));
DATA(insert ( 5524 jsonb_object_agg_transfn - - - jsonb_object_agg_finalfn 0 2281 _null_ f));

/*
 * prototypes for functions in pg_aggregate.c
 */
extern Oid
AggregateCreateWithOid(const char	*aggName,
					   Oid			 aggNamespace,
					   Oid			*aggArgTypes,
					   int			 numArgs,
					   List			*aggtransfnName,
					   List			*aggprelimfnName,
					   List			*aggfinalfnName,
					   List			*aggsortopName,
					   Oid			 aggTransType,
					   const char	*agginitval,
					   bool          aggordered,
					   Oid			 procOid);

#endif   /* PG_AGGREGATE_H */
