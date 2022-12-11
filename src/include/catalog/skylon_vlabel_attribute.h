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
/*-------------------------------------------------------------------------
 *
 * pg_elabel.h
 *    record sql execution information
 *
 *-------------------------------------------------------------------------
 */

#ifndef SRC_INCLUDE_CATALOG_SKYLON_VLABEL_ATTRIBUTE_H_
#define SRC_INCLUDE_CATALOG_SKYLON_VLABEL_ATTRIBUTE_H_

#include "catalog/genbki.h"
#include "c.h"

/* TIDYCAT_BEGINDEF

   CREATE TABLE pg_elabel
   with (camelcase=Elabel, shared=true, oid=false, relid=4851, reltype_oid=4871, content=MASTER_ONLY)
   (
   vlabelName                name,
   schemaName              name
   );

   TIDYCAT_ENDDEF
*/

/* TIDYCAT_BEGIN_CODEGEN

   WARNING: DO NOT MODIFY THE FOLLOWING SECTION:
   Generated by tidycat.pl version 34
   on Fri Feb 26 10:43:15 2016
*/


/*
 TidyCat Comments for gp_configuration_history:
  Table is shared, so catalog.c:IsSharedRelation is updated.
  Table does not have an Oid column.
  Table has static type (see pg_types.h).
  Table has TOASTable columns, but NO TOAST table.
  Table contents are only maintained on MASTER.
  Table has weird hack for timestamp column.

*/

/*
 * The CATALOG definition has to refer to the type of "time" as
 * "timestamptz" (lower case) so that bootstrap mode recognizes it.  But
 * the C header files define this type as TimestampTz.  Since the field is
 * potentially-null and therefore cannot be accessed directly from C code,
 * there is no particular need for the C struct definition to show the
 * field type as TimestampTz --- instead we just make it Datum.
 */


/* ----------------
 *    pg_elabel definition.  cpp turns this into
 *    typedef struct FormData_pg_elabel
 * ----------------
 */

#define VlabelAttrRelationId 4852

CATALOG(skylon_vlabel_attribute,4852) BKI_WITHOUT_OIDS
{
  NameData schemaname;
  NameData vlabelname;
  NameData attrname;
  Oid attrtypid;
  int4 primaryrank;
  int4 rank;
} FormData_skylon_vlabel_attribute;


/* ----------------
 *    Form_pg_elabel corresponds to a pointer to a tuple with
 *    the format of pg_elabel relation.
 * ----------------
 */

typedef FormData_skylon_vlabel_attribute *Form_skylon_vlabel_attribute;

/* ----------------
 *    compiler constants for pg_database
 * ----------------
 */
#define Natts_skylon_vlabel_attribute 6
#define Anum_skylon_vlabel_attribute_schemaname 1
#define Anum_skylon_vlabel_attribute_vlabelname 2
#define Anum_skylon_vlabel_attribute_attrname 3
#define Anum_skylon_vlabel_attribute_attrtypid 4
#define Anum_skylon_vlabel_attribute_primaryrank 5
#define Anum_skylon_vlabel_attribute_rank 6
/* TIDYCAT_END_CODEGEN */

extern void InsertVlabelAttrEntry(const char* schemaname, const char* vlabelname, const char* attrname,
                                    Oid attrtypid, int4 primaryrank, int4 rank);
#endif /* SRC_INCLUDE_CATALOG_SKYLON_VLABEL_ATTRIBUTE_H_ */
