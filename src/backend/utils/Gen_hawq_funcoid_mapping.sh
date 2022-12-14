#! /bin/sh
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
#-------------------------------------------------------------------------
#
# Gen_hawq_func_mapping.sh
#	shell script to generate hawq_funcoid_mapping.h
#
# NOTE: if you want to add more patern, pay attention to 
#	1) the space between the macro name and the coressponding value
#	2) the match_regex pattern to filter out the macro
#   3) TEXT and BPCHAR share the same hornet function when the return
#      type is neither text nor bpchar(i.e. cmp-op, char_length, like etc.).
#      In the other hand, MIN/MAX() has specified function for TEXT/BPCHAR
#      respectively.
#      This is due to the internal design of hornet.
#
#-------------------------------------------------------------------------

set -e
HEADER_FILE="dbcommon/function/func-kind.cg.h"
CMDNAME=`basename $0`
TMP_FILE='hawq_funcoid_mapping_tmp'
INPUT_FILE=$1
OUTPUT_FILE=$2

map_type_hawq+=(BOOL INT2 INT4 INT8 FLOAT4 FLOAT8 TEXT DATE TIME BPCHAR BYTEA TIMESTAMP TID NUMERIC)
map_type_hornet+=(BOOLEAN SMALLINT INT BIGINT FLOAT DOUBLE STRING DATE TIME BPCHAR BINARY TIMESTAMP BIGINT DECIMAL)

map_func_hawq+=(LT LE EQ NE GT GE)
map_func_hornet+=(LESS_THAN LESS_EQ EQUAL NOT_EQUAL GREATER_THAN GREATER_EQ)

map_func_hawq+=(PL MI MUL DIV)
map_func_hornet+=(ADD SUB MUL DIV)

#-------------------------------------------------------------------------
#	filter part
#-------------------------------------------------------------------------
# arithmetic and compare
match_regex='(F_(INT|FLOAT)[248][248]*('`echo ${map_func_hawq[@]}|sed 's/ /|/g'`') )'
match_regex=$match_regex'|(F_(DATE)(_*)(MI) )'
match_regex=$match_regex'|(F_(NUMERIC)(_*)(ADD|SUB|MUL|DIV) )'
# only for compare
match_regex=$match_regex'|(F_(TEXT|BOOL|DATE|TIME|BPCHAR|BYTEA|TIMESTAMP|INTERVAL|TID|NUMERIC|CHAR)(_*)(LT|LE|GT|GE|EQ|NE) )'
match_regex=$match_regex'|(F_(DATE|TIMESTAMP)(_*)(LT|LE|GT|GE|EQ|NE)(_*)(DATE|TIMESTAMP) )'
# type cast
match_regex=$match_regex'|(F_(I[248]|F|D)TO(I[248]|F|D) )'
match_regex=$match_regex'|(F_INT[248][248] )'
match_regex=$match_regex'|(F_NUMERIC_(INT2|INT4|INT8|FLOAT4|FLOAT8) )'
match_regex=$match_regex'|(F_(INT2|INT4|INT8|FLOAT4|FLOAT8)_NUMERIC )'
match_regex=$match_regex'|(F_NUMERIC )'
# aggregation function
match_regex=$match_regex'|(F_(INT[248]|FLOAT[48])_AVG(_(ACCUM|AMALG))* )'
match_regex=$match_regex'|(F_(INT[248]|FLOAT[48])_STDDEV_SAMP(_(ACCUM|AMALG))* )'
match_regex=$match_regex'|(F_(INT[248]|FLOAT[48])_STDDEV_POP(_(ACCUM|AMALG))* )'
match_regex=$match_regex'|(F_(INT[248])_SUM )'
match_regex=$match_regex'|(F_INT8INC(_ANY)* )'
# random function
match_regex=$match_regex'|(F_DRANDOM )'
# sign and round function
match_regex=$match_regex'|(F_(DSIGN|NUMERIC_SIGN) )'
match_regex=$match_regex'|(F_NUMERIC_ROUND )'
# date/time function
match_regex=$match_regex'|(F_(DATE|TIME|TIMESTAMP|INTERVAL)_(PL|MI)(_*)(INTERVAL|DATE|TIME|TIMESTAMP)* )'

cat > $OUTPUT_FILE << CATSTAMP
/*
 * hawq_funcoid_mapping.h
 *
 * NOTES
 *	******************************
 *	*** DO NOT EDIT THIS FILE! ***
 *	******************************
 *
 *	It has been GENERATED by $CMDNAME
 */
#ifndef HAWQ_FUNCOID_MAPPING_H
#define HAWQ_FUNCOID_MAPPING_H

#include "$HEADER_FILE"
static const int hawq_funcoid_mapping[] = {
CATSTAMP

grep 'define' $INPUT_FILE | tr -s ' ' | cut -d ' ' -f 2,3 | grep -E "$match_regex" | 
sort -n -k2 > $TMP_FILE
# type cast function
sed -E "s/F_(I[248]|F|D)TO(I[248]|F|D) /\1_TO_\2 /" $TMP_FILE |
sed -E "s/F_INT([248])([248]) /INT\1_TO_INT\2 /" |
sed -E 's/I([248])_TO_/INT\1_TO_/ ; s/_TO_I([248])/_TO_INT\1/' |
sed -E 's/F_TO_/FLOAT4_TO_/ ; s/_TO_F/_TO_FLOAT4/' |
sed -E 's/D_TO_/FLOAT8_TO_/ ; s/_TO_D/_TO_FLOAT8/' |
sed -E "s/F_NUMERIC_INT([248]) /DECIMAL_TO_INT\1 /" |
sed -E "s/F_INT([248])_NUMERIC /INT\1_TO_DECIMAL /" |
sed -E "s/F_FLOAT4_NUMERIC /FLOAT_TO_DECIMAL /" |
sed -E "s/F_FLOAT8_NUMERIC /DOUBLE_TO_DECIMAL /" |
sed -E "s/F_NUMERIC_FLOAT4 /DECIMAL_TO_FLOAT /" |
sed -E "s/F_NUMERIC_FLOAT8 /DECIMAL_TO_DOUBLE /" |
sed -E "s/F_NUMERIC /DECIMAL_TO_DECIMAL /" > $TMP_FILE.tmp
mv $TMP_FILE.tmp $TMP_FILE

# agg function
sed -E "s/F_(INT[248])_SUM /SUM_\1_SUM /" $TMP_FILE |
sed -E "s/F_INT8INC /COUNT_STAR /" |
sed -E "s/F_INT8INC_ANY /COUNT_INC /" |
sed -E "s/F_(INT[248]|FLOAT[48])_STDDEV_SAMP_ACCUM /STDDEV_SAMP_\1_ACCU /" |
sed -E "s/F_(INT[248]|FLOAT[48])_STDDEV_SAMP_AMALG /STDDEV_SAMP_\1_AMALG /" |
sed -E "s/F_(INT[248]|FLOAT[48])_STDDEV_SAMP /STDDEV_SAMP_\1_STDDEV_SAMP /" |
sed -E "s/F_(INT[248]|FLOAT[48])_STDDEV_POP_ACCUM /STDDEV_POP_\1_ACCU /" |
sed -E "s/F_(INT[248]|FLOAT[48])_STDDEV_POP_AMALG /STDDEV_POP_\1_AMALG /" |
sed -E "s/F_(INT[248]|FLOAT[48])_STDDEV_POP /STDDEV_POP_\1_STDDEV_POP /" |
sed -E "s/F_(INT[248]|FLOAT[48])_AVG_ACCUM /AVG_\1_ACCU /" |
sed -E "s/F_(INT[248]|FLOAT[48])_AVG_AMALG /AVG_\1_AMALG /" |
sed -E "s/F_(INT[248]|FLOAT[48])_AVG /AVG_\1_AVG /" > $TMP_FILE.tmp
mv $TMP_FILE.tmp $TMP_FILE

# random function 
sed -E "s/F_DRANDOM /RANDOMF /" $TMP_FILE > $TMP_FILE.tmp
mv $TMP_FILE.tmp $TMP_FILE

# sign and round function
sed -E "s/F_DSIGN /DOUBLE_SIGN /" $TMP_FILE |
sed -E "s/F_NUMERIC_SIGN /DECIMAL_SIGN /" |
sed -E "s/F_NUMERIC_ROUND /DECIMAL_ROUND /" > $TMP_FILE.tmp
mv $TMP_FILE.tmp $TMP_FILE

# arithmetic and compare
map_func_size=${#map_func_hawq[@]}
for val in `seq 0 $(($map_func_size-1))`
do
	src=${map_func_hawq[$val]}
	dst=${map_func_hornet[$val]}
	# expand when the left operand and the right operand are the same
	sed -E "s/F_(INT|FLOAT)([248])$src /F_\1\2\2$src /" $TMP_FILE | 
	# expand the shorthand operand sequence
	sed -E "s/F_(INT|FLOAT)([248])([248])$src /F_\1\2\1\3$src /" > $TMP_FILE.tmp
	mv $TMP_FILE.tmp $TMP_FILE

	# map the operator and modify into infix expression
	sed -E "s/F_(INT|FLOAT)([248])(INT|FLOAT)([248])${src} /\1\2_${dst}_\3\4 /" $TMP_FILE > $TMP_FILE.tmp
	mv $TMP_FILE.tmp $TMP_FILE

	# for "char"
	sed -E "s/F_CHAR(_*)${src} /TINYINT_${dst}_TINYINT /" $TMP_FILE > $TMP_FILE.tmp
	mv $TMP_FILE.tmp $TMP_FILE

	# for text, boolean, char(n), binary
	sed -E "s/F_(TEXT|BOOL|BPCHAR|BYTEA)(_*)${src} /\1_${dst}_\1 /" $TMP_FILE > $TMP_FILE.tmp
	mv $TMP_FILE.tmp $TMP_FILE

	# for date
	sed -E "s/F_DATE(_*)${src} /INT_${dst}_INT /" $TMP_FILE |
	sed -E "s/F_DATE(_*)${src}(_*)TIMESTAMP /DATE_${dst}_TIMESTAMP /" > $TMP_FILE.tmp
	mv $TMP_FILE.tmp $TMP_FILE

	# for time
	sed -E "s/F_TIME(_*)${src} /BIGINT_${dst}_BIGINT /" $TMP_FILE |
	sed -E "s/F_TIME_MI_TIME /TIME_SUB_TIME /" > $TMP_FILE.tmp
	mv $TMP_FILE.tmp $TMP_FILE

	# for timestamp
	sed -E "s/F_TIMESTAMP(_*)${src} /TIMESTAMP_${dst}_TIMESTAMP /" $TMP_FILE |
	sed -E "s/F_TIMESTAMP(_*)${src}(_*)DATE /TIMESTAMP_${dst}_DATE /" > $TMP_FILE.tmp
	mv $TMP_FILE.tmp $TMP_FILE
	
	# for interval
	sed -E "s/F_INTERVAL(_*)${src} /INTERVAL_${dst}_INTERVAL /" $TMP_FILE |
	sed -E "s/F_(DATE|TIME|TIMESTAMP)_${src}_INTERVAL /\1_${dst}_INTERVAL /" > $TMP_FILE.tmp
	mv $TMP_FILE.tmp $TMP_FILE
	
	# for tid
	sed -E "s/F_TID(_*)${src} /BIGINT_${dst}_BIGINT /" $TMP_FILE > $TMP_FILE.tmp
	mv $TMP_FILE.tmp $TMP_FILE
	
	# for decimal
	sed -E "s/F_NUMERIC(_*)${src} /DECIMAL_${dst}_DECIMAL /" $TMP_FILE |
	sed -E "s/F_NUMERIC(_*)${dst} /DECIMAL_${dst}_DECIMAL /" > $TMP_FILE.tmp
	mv $TMP_FILE.tmp $TMP_FILE

done

# map the type name to hornet
map_type_size=${#map_type_hawq[@]}
for val in `seq 0 $(($map_type_size-1))`
do
	src=${map_type_hawq[$val]}
	dst=${map_type_hornet[$val]}
	sed -E "s/${src}/${dst}/g" $TMP_FILE > $TMP_FILE.tmp
	mv $TMP_FILE.tmp $TMP_FILE
done

# expand checking function oid range
echo "FUNCINVALID 6022" >> $TMP_FILE

# hand written mapping for others
awk '
BEGIN { 
	max=0;
	num=0;
	print "FUNCINVALID,"
	last=0;
}
{
	if ($2 > max) max = $2;
	for (oid=last + 1; oid <= $2 - 1; oid++) 
	{
		# here is a workaroud for agg
		if (oid == 2100)
			print "AVG_BIGINT,";
		else if (oid == 2101)
			print "AVG_INT,";
		else if (oid == 2102)
			print "AVG_SMALLINT,";
		else if	(oid == 2103)
			print "AVG_DECIMAL,";
		else if (oid == 2104)
			print "AVG_FLOAT,";
		else if (oid == 2105)
			print "AVG_DOUBLE,";

		else if (oid == 2107)
			print "SUM_BIGINT,";
		else if (oid == 2108)
			print "SUM_INT,";
		else if (oid == 2109)
			print "SUM_SMALLINT,";
		else if (oid == 2110)
			print "SUM_FLOAT,";
		else if (oid == 2111)
			print "SUM_DOUBLE,";
		else if (oid == 2114)
			print "SUM_DECIMAL,";

		else if (oid == 2115)
			print "MAX_BIGINT,";
		else if (oid == 2116)
			print "MAX_INT,";
		else if (oid == 2117)
			print "MAX_SMALLINT,";
		else if (oid == 2119)
			print "MAX_FLOAT,";
		else if (oid == 2120)
			print "MAX_DOUBLE,";
		else if (oid == 2129)
			print "MAX_STRING,";
		else if (oid == 2244)
			print "MAX_BPCHAR,";
		else if (oid == 2123)
			print "MAX_TIME,";
		else if (oid == 2122)
			print "MAX_DATE,";
		else if (oid == 2126)
			print "MAX_TIMESTAMP,";
		else if (oid == 2130)
			print "MAX_DECIMAL,";

		else if (oid == 2131)
			print "MIN_BIGINT,";
		else if (oid == 2132)
			print "MIN_INT,";
		else if (oid == 2133)
			print "MIN_SMALLINT,";
		else if (oid == 2135)
			print "MIN_FLOAT,";
		else if (oid == 2136)
			print "MIN_DOUBLE,";
		else if (oid == 2145)
			print "MIN_STRING,";
		else if (oid == 2245)
			print "MIN_BPCHAR,";
		else if (oid == 2139)
			print "MIN_TIME,";
		else if (oid == 2138)
			print "MIN_DATE,";
		else if (oid == 2142)
			print "MIN_TIMESTAMP,";
		else if (oid == 2146)
			print "MIN_DECIMAL,";

		else if (oid == 2147)
			print "COUNT,";
		else if (oid == 2803)
			print "COUNT_STAR,";
		
		else if (oid == 2712)
			print "STDDEV_SAMP_BIGINT,";
		else if (oid == 2713)
			print "STDDEV_SAMP_INT,";
		else if (oid == 2714)
			print "STDDEV_SAMP_SMALLINT,";
		else if (oid == 2715)
			print "STDDEV_SAMP_FLOAT,";
		else if (oid == 2716)
			print "STDDEV_SAMP_DOUBLE,";
		else if (oid == 2717)
			print "STDDEV_SAMP_DECIMAL,";

		else if (oid == 2724)
			print "STDDEV_POP_BIGINT,";
		else if (oid == 2725)
			print "STDDEV_POP_INT,";
		else if (oid == 2726)
			print "STDDEV_POP_SMALLINT,";
		else if (oid == 2727)
			print "STDDEV_POP_FLOAT,";
		else if (oid == 2728)
			print "STDDEV_POP_DOUBLE,";
		else if (oid == 2729)
			print "STDDEV_POP_DECIMAL,";

		else if (oid == 2154)
			print "STDDEV_BIGINT,";
		else if (oid == 2155)
			print "STDDEV_INT,";
		else if (oid == 2156)
			print "STDDEV_SMALLINT,";
		else if (oid == 2157)
			print "STDDEV_FLOAT,";
		else if (oid == 2158)
			print "STDDEV_DOUBLE,";
		else if (oid == 2159)
			print "STDDEV_DECIMAL,";

		else if (oid == 1193)
			print "INTERVAL_TO_TEXT,";			
		else if (oid == 112)
			print "INT_TO_TEXT,";
		else if (oid == 113)
			print "SMALLINT_TO_TEXT,";
		else if (oid == 1289)
			print "BIGINT_TO_TEXT,";
		else if (oid == 840)
			print "DOUBLE_TO_TEXT,";
		else if (oid == 841)
			print "FLOAT_TO_TEXT,";
		else if (oid == 1688)
			print "DECIMAL_TO_TEXT,";
		else if (oid == 749)
			print "DATE_TO_TEXT,";
		else if (oid == 2971)
			print "BOOL_TO_TEXT,";
		else if (oid == 948)
			print "TIME_TO_TEXT,";

		else if (oid == 1779)
			print "DOUBLE_TO_BIGINT,";
		else if (oid == 1744)
			print "DOUBLE_TO_INT,";
		else if (oid == 1783)
			print "DOUBLE_TO_SMALLINT,";
		else if (oid == 1745)
			print "DOUBLE_TO_FLOAT,";
		else if (oid == 1746)
			print "DONOTHING,";

		# just for temp workaround
		else if (oid == 2052)
			print "TIMESTAMP_EQUAL_TIMESTAMP,";
		else if (oid == 2053)
			print "TIMESTAMP_NOT_EQUAL_TIMESTAMP,";
		else if (oid == 2054)
			print "TIMESTAMP_LESS_THAN_TIMESTAMP,";
		else if (oid == 2055)
			print "TIMESTAMP_LESS_EQ_TIMESTAMP,";
		else if (oid == 2056)
			print "TIMESTAMP_GREATER_EQ_TIMESTAMP,";
		else if (oid == 2057)
			print "TIMESTAMP_GREATER_THAN_TIMESTAMP,";
			
		else if (oid == 401)
			print "CHAR_TO_STRING,";
		else if (oid == 1374)
			print "STRING_OCTET_LENGTH,";
		else if (oid == 1375)
			print "BPCHAR_OCTET_LENGTH,";
		else if (oid == 1381 || oid == 1317)
			print "STRING_CHAR_LENGTH,";
		else if (oid == 1372 || oid == 1318)
			print "BPCHAR_CHAR_LENGTH,";

		else if (oid == 850)
			print "STRING_LIKE,";
		else if (oid == 1631)
			print "BPCHAR_LIKE,";
		else if (oid == 851)
			print "STRING_NOT_LIKE,";
		else if (oid == 1632)
			print "BPCHAR_NOT_LIKE,";
		else if (oid == 1777)
			print "TO_NUMBER,";	
		else if (oid == 936 || oid == 877)
			print "STRING_SUBSTRING,";
		else if (oid == 937 || oid ==883)
			print "STRING_SUBSTRING_NOLEN,";
			
		else if (oid == 870)
			print "STRING_LOWER,";
		else if (oid == 871)
			print "STRING_UPPER,";
		else if (oid == 2435)
			print "CHAR_TO_BYTEA,";
		else if (oid == 2405)
			print "SMALLINT_TO_BYTEA,";
		else if (oid == 2407)
			print "INT_TO_BYTEA,";
		else if (oid == 2409)
			print "BIGINT_TO_BYTEA,";
		else if (oid == 2425)
			print "FLOAT_TO_BYTEA,";
		else if (oid == 2427)
			print "DOUBLE_TO_BYTEA,";
		else if (oid == 2437)
			print "BOOL_TO_BYTEA,";	
		else if (oid == 2415)
			print "TEXT_TO_BYTEA,";
		else if (oid == 2469)
			print "DATE_TO_BYTEA,";
		else if (oid == 2471)
			print "TIME_TO_BYTEA,";
		else if (oid == 2475)
			print "TIMESTAMP_TO_BYTEA,";
		else if (oid == 2477)
			print "TIMESTAMPTZ_TO_BYTEA,";
		else if (oid == 2479)
			print "INTERVAL_TO_BYTEA,";	
		else if (oid == 2461)
			print "DECIMAL_TO_BYTEA,";
		else if (oid == 818)
			print "TEXT_TO_SMALLINT,";
		else if (oid == 819)
			print "TEXT_TO_INT,";
		else if (oid == 1290)
			print "TEXT_TO_BIGINT,";
		else if (oid == 839)
			print "TEXT_TO_FLOAT,";
		else if (oid == 838)
			print "TEXT_TO_DOUBLE,";
		else if (oid == 1686)
			print "TEXT_TO_DECIMAL,";
		else if (oid == 1258)
			print "STRING_CONCAT,";
		else if (oid == 849)
			print "STRING_POSITION,";
		else if (oid == 868)
			print "STRING_STRPOS,";
		else if (oid == 872)
			print "STRING_INITCAP,";
		else if (oid == 1620)
			print "STRING_ASCII,";
		else if (oid == 669)
			print "STRING_VARCHAR,";
		else if (oid == 875)
			print "STRING_LTRIM_CHARS,";
		else if (oid == 876)
			print "STRING_RTRIM_CHARS,";
		else if (oid == 881)
			print "STRING_LTRIM_BLANK,";
		else if (oid == 882)
			print "STRING_RTRIM_BLANK,";
		else if (oid == 884)
			print "STRING_BTRIM_CHARS,";
		else if (oid == 885)
			print "STRING_BTRIM_BLANK,";
		else if (oid == 1622)
			print "STRING_REPEAT,";
		else if (oid == 1621)
			print "STRING_CHR,";
		else if (oid == 668)
			print "STRING_BPCHAR,";
		else if (oid == 873)
			print "STRING_LPAD,";
		else if (oid == 874)
			print "STRING_RPAD,";
		else if (oid == 879)
			print "STRING_LPAD_NOFILL,";
		else if (oid == 880)
			print "STRING_RPAD_NOFILL,";
		else if (oid == 2557)
			print "INT_TO_BOOLEAN,";
		else if (oid == 2558)
			print "BOOLEAN_TO_INT,";
		else if (oid == 878)
			print "STRING_TRANSLATE,";
		
		else if (oid == 221 || oid == 1395)
			print "DOUBLE_ABS,";
		else if (oid == 207 || oid == 1394)
			print "FLOAT_ABS,";
		else if (oid == 1230 || oid == 1396)
			print "INT64_ABS,";
		else if (oid == 1251 || oid == 1397)
			print "INT32_ABS,";
		else if (oid == 1253 || oid == 1398)
			print "INT16_ABS,";
		else if (oid == 1345)
			print "DOUBLE_CBRT,";
		else if (oid == 231)
			print "DOUBLE_CBRT,";
		else if (oid == 230 || oid == 1344)
			print "DOUBLE_SQRT,";
		else if (oid == 1895)
			print "INT16_BINARY_NOT,";
		else if (oid == 1901)
			print "INT32_BINARY_NOT,";
		else if (oid == 1907)
			print "INT64_BINARY_NOT,";
		else if (oid == 1896)
			print "INT16_BINARY_SHIFT_LEFT,";
		else if (oid == 1902)
			print "INT32_BINARY_SHIFT_LEFT,";
		else if (oid == 1908)
			print "INT64_BINARY_SHIFT_LEFT,";
		else if (oid == 1897)
			print "INT16_BINARY_SHIFT_RIGHT,";
		else if (oid == 1903)
			print "INT32_BINARY_SHIFT_RIGHT,";
		else if (oid == 1909)
			print "INT64_BINARY_SHIFT_RIGHT,";
		else if (oid == 1892)
			print "INT16_BINARY_AND,";
		else if (oid == 1898)
			print "INT32_BINARY_AND,";
		else if (oid == 1904)
			print "INT64_BINARY_AND,";
		else if (oid == 1893)
			print "INT16_BINARY_OR,";
		else if (oid == 1899)
			print "INT32_BINARY_OR,";
		else if (oid == 1905)
			print "INT64_BINARY_OR,";
		else if (oid == 1894)
			print "INT16_BINARY_XOR,";
		else if (oid == 1900)
			print "INT32_BINARY_XOR,";
		else if (oid == 1906)
			print "INT64_BINARY_XOR,";
		else if (oid == 155 || oid == 940)
			print "INT16_MOD,";
		else if (oid == 156 || oid == 941)
			print "INT32_MOD,";
		else if (oid == 174 || oid == 942)
			print "INT16_32_MOD,";
		else if (oid == 175 || oid == 943)
			print "INT32_16_MOD,";
		else if (oid == 945 || oid == 947)
			print "INT64_MOD,";
		else if (oid == 232 || oid == 1346 || oid == 1368)
			print "DOUBLE_POW,";
		else if (oid == 2308 || oid == 2320)
			print "DOUBLE_CEIL,";
		else if (oid == 2309)
			print "DOUBLE_FLOOR,";
		else if (oid == 228 || oid == 1342)
			print "DOUBLE_ROUND,";
		else if (oid == 229 || oid == 1343)
			print "DOUBLE_TRUNC,";
		else if (oid == 2310)
			print "DOUBLE_SIGN,"
		else if (oid == 1704 || oid == 1705)
			print "DECIMAL_ABS,"
		else if (oid == 1706)
			print "DECIMAL_SIGN,"
		else if (oid == 1711 || oid == 2167)
			print "DECIMAL_CEIL,"
		else if (oid == 1712)
			print "DECIMAL_FLOOR,"
		else if (oid == 1707)
			print "DECIMAL_ROUND,"
		else if (oid == 1708)
			print "DECIMAL_ROUND_WITHOUT_SCALE,"
		else if (oid == 1709)
			print "DECIMAL_TRUNC,"
		else if (oid == 1710)
			print "DECIMAL_TRUNC_WITHOUT_SCALE,"
		else if (oid == 1728 || oid == 1729)
			print "DECIMAL_MOD,"
		else if (oid == 233 || oid == 1347)
			print "DOUBLE_EXP,"
		else if (oid == 234 || oid == 1341)
			print "DOUBLE_LN,"
		else if (oid == 1339 || oid == 1340)
			print "DOUBLE_LG,"
		else if (oid == 1601)
			print "DOUBLE_ACOS,"
		else if (oid == 1600)
			print "DOUBLE_ASIN,"
		else if (oid == 1602)
			print "DOUBLE_ATAN,"
		else if (oid == 1603)
			print "DOUBLE_ATAN2,"
		else if (oid == 1605)
			print "DOUBLE_COS,"
		else if (oid == 1607)
			print "DOUBLE_COT,"
		else if (oid == 1604)
			print "DOUBLE_SIN,"
		else if (oid == 1606)
			print "DOUBLE_TAN,"
		else if (oid == 1730 || oid == 1731)
			print "DECIMAL_SQRT,"
		else if (oid == 1732 || oid == 1733)
			print "DECIMAL_EXP,"
		else if (oid == 1734 || oid == 1735)
			print "DECIMAL_LN,"
		else if (oid == 1736 || oid == 1737)
			print "DECIMAL_LOG,"
		else if (oid == 111 || oid == 1376)
			print "DECIMAL_FAC,"
		else if (oid == 1738 || oid == 2169 || oid == 1739)
			print "DECIMAL_POW,"
		

		else if (oid == 720)
			print "BINARY_OCTET_LENGTH,";

		else if (oid == 3135)
			print "FLOAT_ARRAY_EUCLIDEAN_METRIC,"

		else if (oid == 3136)
			print "DOUBLE_ARRAY_EUCLIDEAN_METRIC,"

		else if (oid == 3137)
			print "FLOAT_ARRAY_COSINE_DISTANCE,"

		else if (oid == 3138)
			print "DOUBLE_ARRAY_COSINE_DISTANCE,"

		else if (oid == 2747)
			print "BIGINT_ARRAY_OVERLAP,"

		else if (oid == 2748)
			print "BIGINT_ARRAY_CONTAINS,"

		else if (oid == 2749)
			print "BIGINT_ARRAY_CONTAINED,"

		else if (oid == 1158)
			print "DOUBLE_TO_TIMESTAMP,"
		else if (oid == 2024 || oid == 1174)
		    print "DATE_TO_TIMESTAMP,"
		else if (oid == 2029 || oid == 1178)
			print "TIMESTAMP_TO_DATE,"
		else if (oid == 2031)
			print "TIMESTAMP_SUB_TIMESTAMP,"
		else if (oid == 2034)
			print "TIMESTAMP_TO_TEXT,"
		else if (oid == 1192)
			print "TIMESTAMPTZ_TO_TEXT,"
		else if (oid == 1141)
			print "DATE_ADD_INT,";
		else if (oid == 1142)
			print "DATE_SUB_INT,";
			
		else if (oid == 1389 || oid == 2048)
			print "IS_TIMESTAMP_FINITE,"
		else if (oid == 2021)
			print "TIMESTAMP_DATE_PART,"
		else if (oid == 2020)
			print "TIMESTAMP_DATE_TRUNC,"
		else if (oid == 1342)
			print "DOUBLE_ROUND,"
		
		else if (oid == 2160)
			print "STRING_LESS_THAN_STRING,"
		else if (oid == 2161)
			print "STRING_LESS_EQ_STRING,"
		else if (oid == 2162)
			print "STRING_EQUAL_STRING,"
		else if (oid == 2163)
			print "STRING_GREATER_EQ_STRING,"
		else if (oid == 2164)
			print "STRING_GREATER_THAN_STRING,"
		else if (oid == 2165)
			print "STRING_NOT_EQUAL_STRING,"
		else if (oid == 2174)
			print "BPCHAR_LESS_THAN_BPCHAR,"
		else if (oid == 2175)
			print "BPCHAR_LESS_EQ_BPCHAR,"
		else if (oid == 2176)
			print "BPCHAR_EQUAL_BPCHAR,"
		else if (oid == 2177)
			print "BPCHAR_GREATER_EQ_BPCHAR,"
		else if (oid == 2178)
			print "BPCHAR_GREATER_THAN_BPCHAR,"
		else if (oid == 2179)
			print "BPCHAR_NOT_EQUAL_BPCHAR,"
			
		else if (oid == 2284)
			print "REGEX_REPLACE,"
		else if (oid == 1254 )
			print "TEXT_REGEX_EQ,"
		else if (oid == 1238 )
			print "TEXT_IC_REGEX_EQ,"
		else if (oid == 1256)
			print "TEXT_REGEX_NE,"
		else if (oid == 1239)
			print "TEXT_IC_REGEX_NE,"
		else if (oid == 1658)
			print "BPCHAR_REGEX_EQ,"
		else if (oid == 1656)
			print "BPCHAR_IC_REGEX_EQ,"
		else if (oid == 1659)
			print "BPCHAR_REGEX_NE,"
		else if (oid == 1657)
			print "BPCHAR_IC_REGEX_NE,"

		# convert tid to int8, timestamp and timestamptz interconversion
		else if (oid == 6021 || oid == 2027 || oid == 2028 || oid == 946)
			print "DONOTHING,"

		else 
			print "FUNCINVALID + 1,"
	}
	print $1 ","
	last = $2
}
END {
print "};"
print "#define HAWQ_FUNCOID_MAPPING_UPPER_BOUND " last
}
' $TMP_FILE >> $OUTPUT_FILE
rm $TMP_FILE

cat >> $OUTPUT_FILE << CATSTAMP

#define HAWQ_FUNCOID_MAPPING(oid) \\
  (((oid) > HAWQ_FUNCOID_MAPPING_UPPER_BOUND) ? FUNCINVALID + 1 : hawq_funcoid_mapping[(oid)])

#define IS_HAWQ_MAPPING_FUNCID_INVALID(funcid) (funcid > FUNCINVALID)

#define IS_HAWQ_MAPPING_DO_NOTHING(funcid) (funcid == DONOTHING)

#endif /* HAWQ_FUNCOID_MAPPING_H */
CATSTAMP
