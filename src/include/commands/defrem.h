/*-------------------------------------------------------------------------
 *
 * defrem.h
 *	  POSTGRES define and remove utility definitions.
 *
 *
 * Portions Copyright (c) 1996-2009, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * $PostgreSQL: pgsql/src/include/commands/defrem.h,v 1.77 2006/10/04 00:30:08 momjian Exp $
 *
 *-------------------------------------------------------------------------
 */
#ifndef DEFREM_H
#define DEFREM_H

#include "catalog/index.h"
#include "nodes/parsenodes.h"

struct HTAB;  /* utils/hsearch.h */

/* commands/indexcmds.c */
extern void DefineIndex(Oid relationId,
			char *indexRelationName,
			Oid indexRelationId,
			char *accessMethodName,
			char *tableSpaceName,
			List *attributeList,
			Expr *predicate,
			List *rangetable,
			List *options,
			bool unique,
			bool primary,
			bool isconstraint,
			bool is_alter_table,
			bool check_rights,
			bool skip_build,
			bool quiet,
			bool concurrent,
			bool part_expanded, /* MPP */
			IndexStmt *stmt /* MPP */);
extern void CDBDefineIndex(IndexStmt *stmt);
extern void RemoveIndex(RangeVar *relation, DropBehavior behavior);
extern void ReindexIndex(ReindexStmt *stmt);
extern void ReindexTable(ReindexStmt *stmt);
extern void ReindexDatabase(ReindexStmt *stmt);
extern char *makeObjectName(const char *name1, const char *name2,
			   const char *label);
extern char *ChooseRelationName(const char *name1, const char *name2,
								const char *label, Oid namespaceName, 
								struct HTAB *cache);
extern Oid	GetDefaultOpClass(Oid type_id, Oid am_id);
extern void ComputeConstraintAttrs(struct IndexInfo *indexInfo,
                                   List *attList,   /* list of IndexElem's */
                                   Oid relId);

/* commands/functioncmds.c */
extern void CreateFunction(CreateFunctionStmt *stmt);
extern void RemoveFunction(RemoveFuncStmt *stmt);
extern void RemoveFunctionById(Oid funcOid);
extern void SetFunctionReturnType(Oid funcOid, Oid newRetType);
extern void SetFunctionArgType(Oid funcOid, int argIndex, Oid newArgType);
extern void RenameFunction(List *name, List *argtypes, const char *newname);
extern void AlterFunctionOwner(List *name, List *argtypes, Oid newOwnerId);
extern void AlterFunctionOwner_oid(Oid procOid, Oid newOwnerId);
extern void AlterFunction(AlterFunctionStmt *stmt);
extern void CreateCast(CreateCastStmt *stmt);
extern void DropCast(DropCastStmt *stmt);
extern void DropCastById(Oid castOid);
extern void AlterFunctionNamespace(List *name, List *argtypes, bool isagg,
					   const char *newschema);

/* commands/operatorcmds.c */
extern void DefineOperator(List *names, List *parameters, Oid newOid);
extern void RemoveOperator(RemoveFuncStmt *stmt);
extern void RemoveOperatorById(Oid operOid);
extern void AlterOperatorOwner(List *name, TypeName *typeName1,
				   TypeName *typename2, Oid newOwnerId);
extern void AlterOperatorOwner_oid(Oid operOid, Oid newOwnerId);

/* commands/aggregatecmds.c */
extern void DefineAggregate(List *name, List *args, bool oldstyle,
							List *parameters, Oid newOid, bool ordered);
extern void RemoveAggregate(RemoveFuncStmt *stmt);
extern void RenameAggregate(List *name, List *args, const char *newname);
extern void AlterAggregateOwner(List *name, List *args, Oid newOwnerId);
/* commands/opclasscmds.c */
extern void DefineOpClass(CreateOpClassStmt *stmt);
extern void RemoveOpClass(RemoveOpClassStmt *stmt);
extern void RemoveOpClassById(Oid opclassOid);
extern void RenameOpClass(List *name, const char *access_method, const char *newname);
extern void AlterOpClassOwner(List *name, const char *access_method, Oid newOwnerId);

/* commands/foreigncmds.c */
extern void AlterForeignServerOwner(const char *name, Oid newOwnerId);
extern void AlterForeignDataWrapperOwner(const char *name, Oid newOwnerId);
extern void CreateForeignDataWrapper(CreateFdwStmt *stmt);
extern void AlterForeignDataWrapper(AlterFdwStmt *stmt);
extern void RemoveForeignDataWrapper(DropFdwStmt *stmt);
extern void RemoveForeignDataWrapperById(Oid fdwId);
extern void CreateForeignServer(CreateForeignServerStmt *stmt);
extern void AlterForeignServer(AlterForeignServerStmt *stmt);
extern void RemoveForeignServer(DropForeignServerStmt *stmt);
extern void RemoveForeignServerById(Oid srvId);
extern void CreateUserMapping(CreateUserMappingStmt *stmt);
extern void AlterUserMapping(AlterUserMappingStmt *stmt);
extern void RemoveUserMapping(DropUserMappingStmt *stmt);
extern void RemoveUserMappingById(Oid umId);
//extern void InsertForeignTableEntry(Oid relid, Oid serverid, Oid fdwid, List *options);
//extern void RemoveForeignTableEntry(Oid relid);

/* support routines in commands/define.c */

extern char *case_translate_language_name(const char *input);

extern char *defGetString(DefElem *def, bool *need_free);
extern double defGetNumeric(DefElem *def);
extern bool defGetBoolean(DefElem *def);
extern int64 defGetInt64(DefElem *def);
extern List *defGetQualifiedName(DefElem *def);
extern TypeName *defGetTypeName(DefElem *def);
extern int	defGetTypeLength(DefElem *def);
extern DefElem *defWithOids(bool value);

#endif   /* DEFREM_H */
