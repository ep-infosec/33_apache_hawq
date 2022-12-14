/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */
/*
* $Id$
*
* Revision History
* ===================
* $Log: tpcd.h,v $
* Revision 1.3  2006/05/03 00:49:54  cmcdevitt
* Add support for EXPLAIN ANALYZE.  To use it, use the -x option twice.
*
* Revision 1.2  2006/04/20 00:20:21  cmcdevitt
* fix extra delimiter at end of line
*
* Revision 1.1  2006/04/18 23:17:18  cmcdevitt
* Add TPC-H code.. dbgen, qgen, and tpchdriver
*
* Revision 1.2  2005/01/03 20:08:59  jms
* change line terminations
*
* Revision 1.1.1.1  2004/11/24 23:31:47  jms
* re-establish external server
*
* Revision 1.1.1.1  2003/04/03 18:54:21  jms
* recreation after CVS crash
*
* Revision 1.1.1.1  2003/04/03 18:54:21  jms
* initial checkin
*
*
*/
/*****************************************************************
 *  Title: tpcd.h for TPC D
 *****************************************************************
 */
#define DFLT            0x0001
#define OUTPUT          0x0002
#define EXPLAIN         0x0004
#define DBASE           0x0008
#define VERBOSE         0x0010
#define TIMING          0x0020
#define LOG             0x0040
#define QUERY           0x0080
#define REFRESH         0x0100
#define ANSI            0x0200
#define SEED            0x0400
#define COMMENT         0x0800
#define INIT            0x1000
#define TERMINATE       0x2000
#define DFLT_NUM        0x4000
#define EXPLAINANALYZE  0x8000

/*
 * general defines
 */
#define VTAG            ':'          /* flags a variable substitution */
#define ofp             stdout       /* make the routine a filter */
#define QDIR_TAG        "DSS_QUERY"  /* variable to point to queries */
#define QDIR_DFLT       "."          /* and its default */

/*
 * database portability defines
 */
#ifdef POSTGRES
#ifndef EOL_HANDLING
#define EOL_HANDLING
#endif
#define GEN_QUERY_PLAN  "EXPLAIN"
#define START_TRAN      "BEGIN;"
#define END_TRAN        "COMMIT;"
#define SET_OUTPUT      "\\o %s \n"
#define SET_ROWCOUNT    "LIMIT %d;"
#define SET_DBASE       "\\c %s \n"
#endif

#ifdef DB2
#define GEN_QUERY_PLAN  "SET CURRENT EXPLAIN SNAPSHOT ON;"
#define START_TRAN      ""
#define END_TRAN        "COMMIT WORK;"
#define SET_OUTPUT      ""
#define SET_ROWCOUNT    "--#SET ROWS_FETCH %d\n"
#define SET_DBASE       "CONNECT TO %s ;\n"
#endif

#ifdef INFORMIX
#define GEN_QUERY_PLAN  "SET EXPLAIN ON;"
#define START_TRAN      "BEGIN WORK;"
#define END_TRAN        "COMMIT WORK;"
#define SET_OUTPUT      "OUTPUT TO "
#define SET_ROWCOUNT    "FIRST %d"
#define SET_DBASE       "database %s ;\n"
#endif

#ifdef 	SQLSERVER
#define GEN_QUERY_PLAN  "set showplan on\nset noexec on\ngo\n"
#define START_TRAN      "begin transaction\ngo\n"
#define END_TRAN        "commit transaction\ngo\n"
#define SET_OUTPUT      ""
#define SET_ROWCOUNT    "set rowcount %d\ngo\n\n"
#define SET_DBASE       "use %s\ngo\n"
#endif

#ifdef 	SYBASE
#define GEN_QUERY_PLAN  "set showplan on\nset noexec on\ngo\n"
#define START_TRAN      "begin transaction\ngo\n"
#define END_TRAN        "commit transaction\ngo\n"
#define SET_OUTPUT      ""
#define SET_ROWCOUNT    "set rowcount %d\ngo\n\n"
#define SET_DBASE       "use %s\ngo\n"
#endif

#ifdef TDAT
#define GEN_QUERY_PLAN  "EXPLAIN"
#define START_TRAN      "BEGIN TRANSACTION"
#define END_TRAN        "END TRANSACTION"
#define SET_OUTPUT      ".SET FORMAT OFF\n.EXPORT REPORT file="
#define SET_ROWCOUNT    ".SET RETCANCEL ON\n.SET RETLIMIT %d\n"
#define SET_DBASE       ".LOGON %s\n"
#endif

#define MAX_VARS      8 /* max number of host vars in any query */
#define QLEN_MAX   2048 /* max length of any query */
#define QUERIES_PER_SET 22
#define MAX_PIDS 50

EXTERN int flags;
EXTERN int s_cnt;
EXTERN char *osuff;
EXTERN int stream;
EXTERN char *lfile;
EXTERN char *ifile;
EXTERN char *tfile;

#define MAX_PERMUTE     41
#ifdef DECLARER
int rowcnt_dflt[QUERIES_PER_SET + 1] = 
    {-1,-1,100,10,-1,-1,-1,-1,-1,-1,20,-1,-1,-1,-1,-1,-1,-1,100,-1,-1,100,-1};
int rowcnt;
#define SEQUENCE(stream, query) permutation[stream % MAX_PERMUTE][query - 1]
#else
extern int rowcnt_dflt[];
extern int rowcnt;
#endif
