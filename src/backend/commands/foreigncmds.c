/*-------------------------------------------------------------------------
 *
 * foreigncmds.c
 *	  foreign-data wrapper/server creation/manipulation commands
 *
 * Portions Copyright (c) 1996-2009, PostgreSQL Global Development Group
 *
 *
 * IDENTIFICATION
 *	  $PostgreSQL: pgsql/src/backend/commands/foreigncmds.c,v 1.8 2009/06/11 14:48:55 momjian Exp $
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/heapam.h"
#include "access/reloptions.h"
#include "catalog/catalog.h"
#include "catalog/catquery.h"
#include "catalog/dependency.h"
#include "catalog/indexing.h"
#include "catalog/pg_foreign_data_wrapper.h"
#include "catalog/pg_foreign_table.h"
#include "catalog/pg_foreign_server.h"
#include "catalog/pg_foreign_table.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_type.h"
#include "catalog/pg_user_mapping.h"
#include "cdb/cdbdisp.h"
#include "cdb/cdbvars.h"
#include "commands/defrem.h"
#include "foreign/foreign.h"
#include "miscadmin.h"
#include "parser/parse_func.h"
#include "utils/acl.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/rel.h"
#include "utils/syscache.h"
#include "access/genam.h"
#include "cdb/dispatcher.h"


/*
 * Convert a DefElem list to the text array format that is used in
 * pg_foreign_data_wrapper, pg_foreign_server, and pg_user_mapping.
 * Returns the array in the form of a Datum, or PointerGetDatum(NULL)
 * if the list is empty.
 *
 * Note: The array is usually stored to database without further
 * processing, hence any validation should be done before this
 * conversion.
 */
static Datum
optionListToArray(List *options)
{
	ArrayBuildState *astate = NULL;
	ListCell   *cell;

	foreach(cell, options)
	{
		DefElem    *def = lfirst(cell);
		char *value;
		Size		len;
		text	   *t;

		bool need_free_value = false;
		value = defGetString(def, &need_free_value);
		len = VARHDRSZ + strlen(def->defname) + 1 + strlen(value);
		t = palloc(len + 1);
		SET_VARSIZE(t, len);
		sprintf(VARDATA(t), "%s=%s", def->defname, value);
		if (need_free_value)
		{
			pfree(value);
			value = NULL;
		}

		AssertImply(need_free_value, NULL == value);

		astate = accumArrayResult(astate, PointerGetDatum(t),
								  false, TEXTOID,
								  CurrentMemoryContext);
	}

	if (astate)
		return makeArrayResult(astate, CurrentMemoryContext);

	return PointerGetDatum(NULL);
}


/*
 * Transform a list of DefElem into text array format.	This is substantially
 * the same thing as optionListToArray(), except we recognize SET/ADD/DROP
 * actions for modifying an existing list of options, which is passed in
 * Datum form as oldOptions.  Also, if fdwvalidator isn't InvalidOid
 * it specifies a validator function to call on the result.
 *
 * Returns the array in the form of a Datum, or PointerGetDatum(NULL)
 * if the list is empty.
 *
 * This is used by CREATE/ALTER of FOREIGN DATA WRAPPER/SERVER/USER MAPPING.
 */
static Datum
transformGenericOptions(Datum oldOptions,
						List *options,
						Oid fdwvalidator)
{
	List	   *resultOptions = untransformRelOptions(oldOptions);
	ListCell   *optcell;
	Datum		result;

	foreach(optcell, options)
	{
		DefElem    *od = lfirst(optcell);
		ListCell   *cell;
		ListCell   *prev = NULL;

		/*
		 * Find the element in resultOptions.  We need this for validation in
		 * all cases.  Also identify the previous element.
		 */
		foreach(cell, resultOptions)
		{
			DefElem    *def = lfirst(cell);

			if (strcmp(def->defname, od->defname) == 0)
				break;
			else
				prev = cell;
		}

		/*
		 * It is possible to perform multiple SET/DROP actions on the same
		 * option.	The standard permits this, as long as the options to be
		 * added are unique.  Note that an unspecified action is taken to be
		 * ADD.
		 */
		switch (od->defaction)
		{
			case DEFELEM_DROP:
				if (!cell)
					ereport(ERROR,
							(errcode(ERRCODE_UNDEFINED_OBJECT),
							 errmsg("option \"%s\" not found",
									od->defname),
									errOmitLocation(true)));
				resultOptions = list_delete_cell(resultOptions, cell, prev);
				break;

			case DEFELEM_SET:
				if (!cell)
					ereport(ERROR,
							(errcode(ERRCODE_UNDEFINED_OBJECT),
							 errmsg("option \"%s\" not found",
									od->defname),
											errOmitLocation(true)));
				lfirst(cell) = od;
				break;

			case DEFELEM_ADD:
			case DEFELEM_UNSPEC:
				if (cell)
					ereport(ERROR,
							(errcode(ERRCODE_DUPLICATE_OBJECT),
							 errmsg("option \"%s\" provided more than once",
									od->defname),
											errOmitLocation(true)));
				resultOptions = lappend(resultOptions, od);
				break;

			default:
				elog(ERROR, "unrecognized action %d on option \"%s\"",
					 (int) od->defaction, od->defname);
				break;
		}
	}

	result = optionListToArray(resultOptions);

	if (fdwvalidator)
		OidFunctionCall2(fdwvalidator, result, (Datum) 0);

	return result;
}


/*
 * Convert the user mapping user name to OID
 */
static Oid
GetUserOidFromMapping(const char *username, bool missing_ok)
{
	if (!username)
		/* PUBLIC user mapping */
		return InvalidOid;

	if (strcmp(username, "current_user") == 0)
		/* map to the owner */
		return GetUserId();

	/* map to provided user */
	return missing_ok ? get_roleid(username) : get_roleid_checked(username);
}


/*
 * Change foreign-data wrapper owner.
 *
 * Allow this only for superusers; also the new owner must be a
 * superuser.
 */
void
AlterForeignDataWrapperOwner(const char *name, Oid newOwnerId)
{
	HeapTuple	tup;
	Relation	rel;
	Oid			fdwId;
	Form_pg_foreign_data_wrapper form;
	cqContext	cqc;
	cqContext  *pcqCtx;

	/* Must be a superuser to change a FDW owner */
	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("permission denied to change owner of foreign-data wrapper \"%s\"",
						name),
				 errhint("Must be superuser to change owner of a foreign-data wrapper."),
							errOmitLocation(true)));

	/* New owner must also be a superuser */
	if (!superuser_arg(newOwnerId))
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
				 errmsg("permission denied to change owner of foreign-data wrapper \"%s\"",
						name),
		errhint("The owner of a foreign-data wrapper must be a superuser."),
				errOmitLocation(true)));

	rel = heap_open(ForeignDataWrapperRelationId, RowExclusiveLock);

	pcqCtx = caql_addrel(cqclr(&cqc), rel);

	tup = caql_getfirst(
			pcqCtx,
			cql("SELECT * FROM pg_foreign_data_wrapper "
				" WHERE fdwname = :1 "
				" FOR UPDATE ",
				CStringGetDatum(name)));

	if (!HeapTupleIsValid(tup))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("foreign-data wrapper \"%s\" does not exist", name),
							errOmitLocation(true)));

	fdwId = HeapTupleGetOid(tup);
	form = (Form_pg_foreign_data_wrapper) GETSTRUCT(tup);

	if (form->fdwowner != newOwnerId)
	{
		form->fdwowner = newOwnerId;

		caql_update_current(pcqCtx, tup); /* implicit update of index as well*/

		/* Update owner dependency reference */
		changeDependencyOnOwner(ForeignDataWrapperRelationId,
								fdwId,
								newOwnerId);
	}

	heap_close(rel, NoLock);
	heap_freetuple(tup);
}


/*
 * Change foreign server owner
 */
void
AlterForeignServerOwner(const char *name, Oid newOwnerId)
{
	HeapTuple	tup;
	Relation	rel;
	Oid			srvId;
	AclResult	aclresult;
	Form_pg_foreign_server form;
	cqContext	cqc;
	cqContext  *pcqCtx;

	rel = heap_open(ForeignServerRelationId, RowExclusiveLock);

	pcqCtx = caql_addrel(cqclr(&cqc), rel);

	tup = caql_getfirst(
			pcqCtx,
			cql("SELECT * FROM pg_foreign_server "
				" WHERE srvname = :1 "
				" FOR UPDATE ",
				CStringGetDatum(name)));

	if (!HeapTupleIsValid(tup))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("server \"%s\" does not exist", name),
							errOmitLocation(true)));

	srvId = HeapTupleGetOid(tup);
	form = (Form_pg_foreign_server) GETSTRUCT(tup);

	if (form->srvowner != newOwnerId)
	{
		/* Superusers can always do it */
		if (!superuser())
		{
			/* Must be owner */
			if (!pg_foreign_server_ownercheck(srvId, GetUserId()))
				aclcheck_error(ACLCHECK_NOT_OWNER, ACL_KIND_FOREIGN_SERVER,
							   name);

			/* Must be able to become new owner */
			check_is_member_of_role(GetUserId(), newOwnerId);

			/* New owner must have USAGE privilege on foreign-data wrapper */
			aclresult = pg_foreign_data_wrapper_aclcheck(form->srvfdw, newOwnerId, ACL_USAGE);
			if (aclresult != ACLCHECK_OK)
			{
				ForeignDataWrapper *fdw = GetForeignDataWrapper(form->srvfdw);

				aclcheck_error(aclresult, ACL_KIND_FDW, fdw->fdwname);
			}
		}

		form->srvowner = newOwnerId;

		caql_update_current(pcqCtx, tup); /* implicit update of index as well*/

		/* Update owner dependency reference */
		changeDependencyOnOwner(ForeignServerRelationId, HeapTupleGetOid(tup),
								newOwnerId);
	}

	heap_close(rel, NoLock);
	heap_freetuple(tup);
}


/*
 * Convert a validator function name passed from the parser to an Oid.
 */
static Oid
lookup_fdw_validator_func(List *validator)
{
	Oid			funcargtypes[2];

	funcargtypes[0] = TEXTARRAYOID;
	funcargtypes[1] = OIDOID;
	return LookupFuncName(validator, 2, funcargtypes, false);
	/* return value is ignored, so we don't check the type */
}


/*
 * Create a foreign-data wrapper
 */
void
CreateForeignDataWrapper(CreateFdwStmt *stmt)
{
	Relation	rel;
	Datum		values[Natts_pg_foreign_data_wrapper];
	bool		nulls[Natts_pg_foreign_data_wrapper];
	HeapTuple	tuple;
	Oid			fdwId;
	Oid			fdwvalidator;
	Datum		fdwoptions;
	Oid			ownerId;
	cqContext	cqc;
	cqContext  *pcqCtx;

	/* Must be super user */
	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
			errmsg("permission denied to create foreign-data wrapper \"%s\"",
				   stmt->fdwname),
			errhint("Must be superuser to create a foreign-data wrapper."),
					errOmitLocation(true)));

	/* For now the owner cannot be specified on create. Use effective user ID. */
	ownerId = GetUserId();

	/*
	 * Check that there is no other foreign-data wrapper by this name.
	 */
	if (GetForeignDataWrapperByName(stmt->fdwname, true) != NULL)
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_OBJECT),
				 errmsg("foreign-data wrapper \"%s\" already exists",
						stmt->fdwname),
								errOmitLocation(true)));

	/*
	 * Insert tuple into pg_foreign_data_wrapper.
	 */
	rel = heap_open(ForeignDataWrapperRelationId, RowExclusiveLock);

	pcqCtx = caql_beginscan(
			caql_addrel(cqclr(&cqc), rel),
			cql("INSERT INTO pg_foreign_data_wrapper", 
				NULL));

	memset(values, 0, sizeof(values));
	memset(nulls, false, sizeof(nulls));

	values[Anum_pg_foreign_data_wrapper_fdwname - 1] =
		DirectFunctionCall1(namein, CStringGetDatum(stmt->fdwname));
	values[Anum_pg_foreign_data_wrapper_fdwowner - 1] = ObjectIdGetDatum(ownerId);

	if (stmt->validator)
		fdwvalidator = lookup_fdw_validator_func(stmt->validator);
	else
		fdwvalidator = InvalidOid;

	values[Anum_pg_foreign_data_wrapper_fdwvalidator - 1] = fdwvalidator;

	nulls[Anum_pg_foreign_data_wrapper_fdwacl - 1] = true;

	fdwoptions = transformGenericOptions(PointerGetDatum(NULL), stmt->options,
										 fdwvalidator);

	if (PointerIsValid(DatumGetPointer(fdwoptions)))
		values[Anum_pg_foreign_data_wrapper_fdwoptions - 1] = fdwoptions;
	else
		nulls[Anum_pg_foreign_data_wrapper_fdwoptions - 1] = true;

	tuple = caql_form_tuple(pcqCtx, values, nulls);

	fdwId = caql_insert(pcqCtx, tuple); /* implicit update of index as well */

	heap_freetuple(tuple);

	if (fdwvalidator)
	{
		ObjectAddress myself;
		ObjectAddress referenced;

		myself.classId = ForeignDataWrapperRelationId;
		myself.objectId = fdwId;
		myself.objectSubId = 0;

		referenced.classId = ProcedureRelationId;
		referenced.objectId = fdwvalidator;
		referenced.objectSubId = 0;
		recordDependencyOn(&myself, &referenced, DEPENDENCY_NORMAL);
	}

	recordDependencyOnOwner(ForeignDataWrapperRelationId, fdwId, ownerId);

	caql_endscan(pcqCtx);
	heap_close(rel, NoLock);

	/* dispatch to QEs */
	if (Gp_role == GP_ROLE_DISPATCH)
	  ereport(ERROR, (errcode(ERRCODE_CDB_FEATURE_NOT_YET),
	          errmsg("Cannot support CreateForeignDataWrapper")));
}


/*
 * Alter foreign-data wrapper
 */
void
AlterForeignDataWrapper(AlterFdwStmt *stmt)
{
	Relation	rel;
	HeapTuple	tp;
	Datum		repl_val[Natts_pg_foreign_data_wrapper];
	bool		repl_null[Natts_pg_foreign_data_wrapper];
	bool		repl_repl[Natts_pg_foreign_data_wrapper];
	Oid			fdwId;
	bool		isnull;
	Datum		datum;
	Oid			fdwvalidator;
	cqContext	cqc;
	cqContext  *pcqCtx;

	/* Must be super user */
	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
			 errmsg("permission denied to alter foreign-data wrapper \"%s\"",
					stmt->fdwname),
			 errhint("Must be superuser to alter a foreign-data wrapper."),
						errOmitLocation(true)));

	rel = heap_open(ForeignDataWrapperRelationId, RowExclusiveLock);

	pcqCtx = caql_addrel(cqclr(&cqc), rel);

	tp = caql_getfirst(
			pcqCtx,
			cql("SELECT * FROM pg_foreign_data_wrapper "
				" WHERE fdwname = :1 "
				" FOR UPDATE ",
				CStringGetDatum(stmt->fdwname)));

	if (!HeapTupleIsValid(tp))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
		errmsg("foreign-data wrapper \"%s\" does not exist", stmt->fdwname),
				errOmitLocation(true)));

	fdwId = HeapTupleGetOid(tp);

	memset(repl_val, 0, sizeof(repl_val));
	memset(repl_null, false, sizeof(repl_null));
	memset(repl_repl, false, sizeof(repl_repl));

	if (stmt->change_validator)
	{
		fdwvalidator = stmt->validator ? lookup_fdw_validator_func(stmt->validator) : InvalidOid;
		repl_val[Anum_pg_foreign_data_wrapper_fdwvalidator - 1] = ObjectIdGetDatum(fdwvalidator);
		repl_repl[Anum_pg_foreign_data_wrapper_fdwvalidator - 1] = true;

		/*
		 * It could be that the options for the FDW, SERVER and USER MAPPING
		 * are no longer valid with the new validator.	Warn about this.
		 */
		if (stmt->validator)
			ereport(WARNING,
			 (errmsg("changing the foreign-data wrapper validator can cause "
					 "the options for dependent objects to become invalid"),
								errOmitLocation(true)));
	}
	else
	{
		/*
		 * Validator is not changed, but we need it for validating options.
		 */
		datum = caql_getattr(pcqCtx,
							 Anum_pg_foreign_data_wrapper_fdwvalidator,
							 &isnull);
		Assert(!isnull);
		fdwvalidator = DatumGetObjectId(datum);
	}

	/*
	 * Options specified, validate and update.
	 */
	if (stmt->options)
	{
		/* Extract the current options */
		datum = caql_getattr(pcqCtx,
							 Anum_pg_foreign_data_wrapper_fdwoptions,
							 &isnull);
		if (isnull)
			datum = PointerGetDatum(NULL);

		/* Transform the options */
		datum = transformGenericOptions(datum, stmt->options, fdwvalidator);

		if (PointerIsValid(DatumGetPointer(datum)))
			repl_val[Anum_pg_foreign_data_wrapper_fdwoptions - 1] = datum;
		else
			repl_null[Anum_pg_foreign_data_wrapper_fdwoptions - 1] = true;

		repl_repl[Anum_pg_foreign_data_wrapper_fdwoptions - 1] = true;
	}

	/* Everything looks good - update the tuple */

	tp = caql_modify_current(pcqCtx,
							 repl_val, repl_null, repl_repl);

	caql_update_current(pcqCtx, tp);
	/* and Update indexes (implicit) */

	heap_close(rel, RowExclusiveLock);
	heap_freetuple(tp);
	
	/* dispatch to QEs */
	if (Gp_role == GP_ROLE_DISPATCH)
	  ereport(ERROR, (errcode(ERRCODE_CDB_FEATURE_NOT_YET),
	      errmsg("Cannot support AlterForeignDataWrapper")));

}


/*
 * Drop foreign-data wrapper
 */
void
RemoveForeignDataWrapper(DropFdwStmt *stmt)
{
	Oid			fdwId;
	ObjectAddress object;

	fdwId = GetForeignDataWrapperOidByName(stmt->fdwname, true);

	if (!superuser())
		ereport(ERROR,
				(errcode(ERRCODE_INSUFFICIENT_PRIVILEGE),
			  errmsg("permission denied to drop foreign-data wrapper \"%s\"",
					 stmt->fdwname),
			  errhint("Must be superuser to drop a foreign-data wrapper."),
						errOmitLocation(true)));

	if (!OidIsValid(fdwId))
	{
		if (!stmt->missing_ok)
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("foreign-data wrapper \"%s\" does not exist",
							stmt->fdwname),
									errOmitLocation(true)));

		/* IF EXISTS specified, just note it */
		ereport(NOTICE,
			  (errmsg("foreign-data wrapper \"%s\" does not exist, skipping",
					  stmt->fdwname),
								errOmitLocation(true)));
		return;
	}

	/*
	 * Do the deletion
	 */
	object.classId = ForeignDataWrapperRelationId;
	object.objectId = fdwId;
	object.objectSubId = 0;

	performDeletion(&object, stmt->behavior);
	
	/* dispatch to QEs */
	if (Gp_role == GP_ROLE_DISPATCH)
	  ereport(ERROR, (errcode(ERRCODE_CDB_FEATURE_NOT_YET),
	                errmsg("Cannot support RemoveForeignDataWrapper")));
}


/*
 * Drop foreign-data wrapper by OID
 */
void
RemoveForeignDataWrapperById(Oid fdwId)
{
	if (0 == caql_getcount(
				NULL,
				cql("DELETE FROM pg_foreign_data_wrapper "
					" WHERE oid = :1 ",
					ObjectIdGetDatum(fdwId))))
	{
		elog(ERROR, "cache lookup failed for foreign-data wrapper %u", fdwId);
	}
}


/*
 * Create a foreign server
 */
void
CreateForeignServer(CreateForeignServerStmt *stmt)
{
	Relation	rel;
	Datum		srvoptions;
	Datum		values[Natts_pg_foreign_server];
	bool		nulls[Natts_pg_foreign_server];
	HeapTuple	tuple;
	Oid			srvId;
	Oid			ownerId;
	AclResult	aclresult;
	ObjectAddress myself;
	ObjectAddress referenced;
	ForeignDataWrapper *fdw;
	cqContext	cqc;
	cqContext  *pcqCtx;

	/* For now the owner cannot be specified on create. Use effective user ID. */
	ownerId = GetUserId();

	/*
	 * Check that there is no other foreign server by this name.
	 */
	if (GetForeignServerByName(stmt->servername, true) != NULL)
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_OBJECT),
				 errmsg("server \"%s\" already exists",
						stmt->servername),
								errOmitLocation(true)));

	/*
	 * Check that the FDW exists and that we have USAGE on it. Also get the
	 * actual FDW for option validation etc.
	 */
	fdw = GetForeignDataWrapperByName(stmt->fdwname, false);

	aclresult = pg_foreign_data_wrapper_aclcheck(fdw->fdwid, ownerId, ACL_USAGE);
	if (aclresult != ACLCHECK_OK)
		aclcheck_error(aclresult, ACL_KIND_FDW, fdw->fdwname);

	/*
	 * Insert tuple into pg_foreign_server.
	 */
	rel = heap_open(ForeignServerRelationId, RowExclusiveLock);
	pcqCtx = caql_beginscan(
			caql_addrel(cqclr(&cqc), rel),
			cql("INSERT INTO pg_foreign_server", 
				NULL));

	memset(values, 0, sizeof(values));
	memset(nulls, false, sizeof(nulls));

	values[Anum_pg_foreign_server_srvname - 1] =
		DirectFunctionCall1(namein, CStringGetDatum(stmt->servername));
	values[Anum_pg_foreign_server_srvowner - 1] = ObjectIdGetDatum(ownerId);
	values[Anum_pg_foreign_server_srvfdw - 1] = ObjectIdGetDatum(fdw->fdwid);

	/* Add server type if supplied */
	if (stmt->servertype)
		values[Anum_pg_foreign_server_srvtype - 1] =
			CStringGetTextDatum(stmt->servertype);
	else
		nulls[Anum_pg_foreign_server_srvtype - 1] = true;

	/* Add server version if supplied */
	if (stmt->version)
		values[Anum_pg_foreign_server_srvversion - 1] =
			CStringGetTextDatum(stmt->version);
	else
		nulls[Anum_pg_foreign_server_srvversion - 1] = true;

	/* Start with a blank acl */
	nulls[Anum_pg_foreign_server_srvacl - 1] = true;

	/* Add server options */
	srvoptions = transformGenericOptions(PointerGetDatum(NULL), stmt->options,
										 fdw->fdwvalidator);

	if (PointerIsValid(DatumGetPointer(srvoptions)))
		values[Anum_pg_foreign_server_srvoptions - 1] = srvoptions;
	else
		nulls[Anum_pg_foreign_server_srvoptions - 1] = true;

	tuple = caql_form_tuple(pcqCtx, values, nulls);

	srvId = caql_insert(pcqCtx, tuple); /* implicit update of index as well */

	heap_freetuple(tuple);

	/* Add dependency on FDW and owner */
	myself.classId = ForeignServerRelationId;
	myself.objectId = srvId;
	myself.objectSubId = 0;

	referenced.classId = ForeignDataWrapperRelationId;
	referenced.objectId = fdw->fdwid;
	referenced.objectSubId = 0;
	recordDependencyOn(&myself, &referenced, DEPENDENCY_NORMAL);

	recordDependencyOnOwner(ForeignServerRelationId, srvId, ownerId);

	caql_endscan(pcqCtx);
	heap_close(rel, NoLock);
	
	/* dispatch to QEs */
	if (Gp_role == GP_ROLE_DISPATCH)
	  ereport(ERROR, (errcode(ERRCODE_CDB_FEATURE_NOT_YET),
	      errmsg("Cannot support CreateForeignServer")));
}


/*
 * Alter foreign server
 */
void
AlterForeignServer(AlterForeignServerStmt *stmt)
{
	Relation	rel;
	HeapTuple	tp;
	Datum		repl_val[Natts_pg_foreign_server];
	bool		repl_null[Natts_pg_foreign_server];
	bool		repl_repl[Natts_pg_foreign_server];
	Oid			srvId;
	Form_pg_foreign_server srvForm;
	cqContext	cqc;
	cqContext  *pcqCtx;

	rel = heap_open(ForeignServerRelationId, RowExclusiveLock);

	pcqCtx = caql_addrel(cqclr(&cqc), rel);

	tp = caql_getfirst(
			pcqCtx,
			cql("SELECT * FROM pg_foreign_server "
				" WHERE srvname = :1 "
				" FOR UPDATE ",
				CStringGetDatum(stmt->servername)));

	if (!HeapTupleIsValid(tp))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("server \"%s\" does not exist", stmt->servername),
							errOmitLocation(true)));

	srvId = HeapTupleGetOid(tp);
	srvForm = (Form_pg_foreign_server) GETSTRUCT(tp);

	/*
	 * Only owner or a superuser can ALTER a SERVER.
	 */
	if (!pg_foreign_server_ownercheck(srvId, GetUserId()))
		aclcheck_error(ACLCHECK_NOT_OWNER, ACL_KIND_FOREIGN_SERVER,
					   stmt->servername);

	memset(repl_val, 0, sizeof(repl_val));
	memset(repl_null, false, sizeof(repl_null));
	memset(repl_repl, false, sizeof(repl_repl));

	if (stmt->has_version)
	{
		/*
		 * Change the server VERSION string.
		 */
		if (stmt->version)
			repl_val[Anum_pg_foreign_server_srvversion - 1] =
				CStringGetTextDatum(stmt->version);
		else
			repl_null[Anum_pg_foreign_server_srvversion - 1] = true;

		repl_repl[Anum_pg_foreign_server_srvversion - 1] = true;
	}

	if (stmt->options)
	{
		ForeignDataWrapper *fdw = GetForeignDataWrapper(srvForm->srvfdw);
		Datum		datum;
		bool		isnull;

		/* Extract the current srvoptions */
		datum = caql_getattr(pcqCtx,
							 Anum_pg_foreign_server_srvoptions,
							 &isnull);
		if (isnull)
			datum = PointerGetDatum(NULL);

		/* Prepare the options array */
		datum = transformGenericOptions(datum, stmt->options,
										fdw->fdwvalidator);

		if (PointerIsValid(DatumGetPointer(datum)))
			repl_val[Anum_pg_foreign_server_srvoptions - 1] = datum;
		else
			repl_null[Anum_pg_foreign_server_srvoptions - 1] = true;

		repl_repl[Anum_pg_foreign_server_srvoptions - 1] = true;
	}

	/* Everything looks good - update the tuple */

	tp = caql_modify_current(pcqCtx,
							 repl_val, repl_null, repl_repl);

	caql_update_current(pcqCtx, tp);
	/* and Update indexes (implicit) */

	heap_close(rel, RowExclusiveLock);
	heap_freetuple(tp);
	
	/* dispatch to QEs */
	if (Gp_role == GP_ROLE_DISPATCH)
	  ereport(ERROR, (errcode(ERRCODE_CDB_FEATURE_NOT_YET),
	          errmsg("Cannot support AlterForeignServer")));

}


/*
 * Drop foreign server
 */
void
RemoveForeignServer(DropForeignServerStmt *stmt)
{
	Oid			srvId;
	ObjectAddress object;

	srvId = GetForeignServerOidByName(stmt->servername, true);

	if (!OidIsValid(srvId))
	{
		/* Server not found, complain or notice */
		if (!stmt->missing_ok)
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
				  errmsg("server \"%s\" does not exist", stmt->servername),
							errOmitLocation(true)));

		/* IF EXISTS specified, just note it */
		ereport(NOTICE,
				(errmsg("server \"%s\" does not exist, skipping",
						stmt->servername),
								errOmitLocation(true)));
		return;
	}

	/* Only allow DROP if the server is owned by the user. */
	if (!pg_foreign_server_ownercheck(srvId, GetUserId()))
		aclcheck_error(ACLCHECK_NOT_OWNER, ACL_KIND_FOREIGN_SERVER,
					   stmt->servername);

	object.classId = ForeignServerRelationId;
	object.objectId = srvId;
	object.objectSubId = 0;

	performDeletion(&object, stmt->behavior);
	
	/* dispatch to QEs */
	if (Gp_role == GP_ROLE_DISPATCH)
	  ereport(ERROR, (errcode(ERRCODE_CDB_FEATURE_NOT_YET),
	      errmsg("Cannot support RemoveForeignServer")));
}


/*
 * Drop foreign server by OID
 */
void
RemoveForeignServerById(Oid srvId)
{
	if (0 == caql_getcount(
				NULL,
				cql("DELETE FROM pg_foreign_server "
					" WHERE oid = :1 ",
					ObjectIdGetDatum(srvId))))
	{
		elog(ERROR, "cache lookup failed for foreign server %u", srvId);
	}
}

/*
 * Common routine to check permission for user-mapping-related DDL
 * commands.  We allow server owners to operate on any mapping, and
 * users to operate on their own mapping.
 */
static void
user_mapping_ddl_aclcheck(Oid umuserid, Oid serverid, const char *servername)
{
	Oid			curuserid = GetUserId();

	if (!pg_foreign_server_ownercheck(serverid, curuserid))
	{
		if (umuserid == curuserid)
		{
			AclResult	aclresult;

			aclresult = pg_foreign_server_aclcheck(serverid, curuserid, ACL_USAGE);
			if (aclresult != ACLCHECK_OK)
				aclcheck_error(aclresult, ACL_KIND_FOREIGN_SERVER, servername);
		}
		else
			aclcheck_error(ACLCHECK_NOT_OWNER, ACL_KIND_FOREIGN_SERVER,
						   servername);
	}
}


/*
 * Create user mapping
 */
void
CreateUserMapping(CreateUserMappingStmt *stmt)
{
	Relation	rel;
	Datum		useoptions;
	Datum		values[Natts_pg_user_mapping];
	bool		nulls[Natts_pg_user_mapping];
	HeapTuple	tuple;
	Oid			useId;
	Oid			umId;
	ObjectAddress myself;
	ObjectAddress referenced;
	ForeignServer *srv;
	ForeignDataWrapper *fdw;
	cqContext	cqc;
	cqContext	cqc2;
	cqContext  *pcqCtx;


	useId = GetUserOidFromMapping(stmt->username, false);

	/* Check that the server exists. */
	srv = GetForeignServerByName(stmt->servername, false);

	user_mapping_ddl_aclcheck(useId, srv->serverid, stmt->servername);

	rel = heap_open(UserMappingRelationId, RowExclusiveLock);

	pcqCtx = caql_beginscan(
			caql_addrel(cqclr(&cqc), rel),
			cql("INSERT INTO pg_user_mapping",
				NULL));

	/*
	 * Check that the user mapping is unique within server.
	 */
	umId = caql_getoid(
			caql_addrel(cqclr(&cqc2), rel),
			cql("SELECT oid FROM pg_user_mapping "
				" WHERE umuser = :1 "
				" AND umserver = :2 ",
				ObjectIdGetDatum(useId),
				ObjectIdGetDatum(srv->serverid)));

	if (OidIsValid(umId))
		ereport(ERROR,
				(errcode(ERRCODE_DUPLICATE_OBJECT),
				 errmsg("user mapping \"%s\" already exists for server %s",
						MappingUserName(useId),
						stmt->servername),
								errOmitLocation(true)));

	fdw = GetForeignDataWrapper(srv->fdwid);

	/*
	 * Insert tuple into pg_user_mapping.
	 */

	memset(values, 0, sizeof(values));
	memset(nulls, false, sizeof(nulls));

	values[Anum_pg_user_mapping_umuser - 1] = ObjectIdGetDatum(useId);
	values[Anum_pg_user_mapping_umserver - 1] = ObjectIdGetDatum(srv->serverid);

	/* Add user options */
	useoptions = transformGenericOptions(PointerGetDatum(NULL), stmt->options,
										 fdw->fdwvalidator);

	if (PointerIsValid(DatumGetPointer(useoptions)))
		values[Anum_pg_user_mapping_umoptions - 1] = useoptions;
	else
		nulls[Anum_pg_user_mapping_umoptions - 1] = true;
	
	tuple = caql_form_tuple(pcqCtx, values, nulls);

	umId = caql_insert(pcqCtx, tuple); /* implicit update of index as well */

	heap_freetuple(tuple);

	/* Add dependency on the server */
	myself.classId = UserMappingRelationId;
	myself.objectId = umId;
	myself.objectSubId = 0;

	referenced.classId = ForeignServerRelationId;
	referenced.objectId = srv->serverid;
	referenced.objectSubId = 0;
	recordDependencyOn(&myself, &referenced, DEPENDENCY_NORMAL);

	if (OidIsValid(useId))
		/* Record the mapped user dependency */
		recordDependencyOnOwner(UserMappingRelationId, umId, useId);

	caql_endscan(pcqCtx);
	heap_close(rel, NoLock);
	
	/* dispatch to QEs */
	if (Gp_role == GP_ROLE_DISPATCH)
	  ereport(ERROR, (errcode(ERRCODE_CDB_FEATURE_NOT_YET),
	          errmsg("Cannot support CreateUserMapping")));
}


/*
 * Alter user mapping
 */
void
AlterUserMapping(AlterUserMappingStmt *stmt)
{
	Relation	rel;
	HeapTuple	tp;
	Datum		repl_val[Natts_pg_user_mapping];
	bool		repl_null[Natts_pg_user_mapping];
	bool		repl_repl[Natts_pg_user_mapping];
	Oid			useId;
	Oid			umId;
	ForeignServer *srv;
	cqContext	cqc;
	cqContext  *pcqCtx;

	useId = GetUserOidFromMapping(stmt->username, false);
	srv = GetForeignServerByName(stmt->servername, false);

	umId = caql_getoid(
			NULL,
			cql("SELECT oid FROM pg_user_mapping "
				" WHERE umuser = :1 "
				" AND umserver = :2 ",
				ObjectIdGetDatum(useId),
				ObjectIdGetDatum(srv->serverid)));

	if (!OidIsValid(umId))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("user mapping \"%s\" does not exist for the server",
						MappingUserName(useId)),
								errOmitLocation(true)));

	user_mapping_ddl_aclcheck(useId, srv->serverid, stmt->servername);

	rel = heap_open(UserMappingRelationId, RowExclusiveLock);

	pcqCtx = caql_addrel(cqclr(&cqc), rel);

	tp = caql_getfirst(
			pcqCtx,
			cql("SELECT * FROM pg_user_mapping "
				" WHERE oid = :1 "
				" FOR UPDATE ",
				ObjectIdGetDatum(umId)));

	if (!HeapTupleIsValid(tp))
		elog(ERROR, "cache lookup failed for user mapping %u", umId);

	memset(repl_val, 0, sizeof(repl_val));
	memset(repl_null, false, sizeof(repl_null));
	memset(repl_repl, false, sizeof(repl_repl));

	if (stmt->options)
	{
		ForeignDataWrapper *fdw;
		Datum		datum;
		bool		isnull;

		/*
		 * Process the options.
		 */

		fdw = GetForeignDataWrapper(srv->fdwid);

		datum = caql_getattr(pcqCtx,
							 Anum_pg_user_mapping_umoptions,
							 &isnull);
		if (isnull)
			datum = PointerGetDatum(NULL);

		/* Prepare the options array */
		datum = transformGenericOptions(datum, stmt->options,
										fdw->fdwvalidator);

		if (PointerIsValid(DatumGetPointer(datum)))
			repl_val[Anum_pg_user_mapping_umoptions - 1] = datum;
		else
			repl_null[Anum_pg_user_mapping_umoptions - 1] = true;

		repl_repl[Anum_pg_user_mapping_umoptions - 1] = true;
	}

	/* Everything looks good - update the tuple */

	tp = caql_modify_current(pcqCtx,
							 repl_val, repl_null, repl_repl);

	caql_update_current(pcqCtx, tp);
	/* and Update indexes (implicit) */

	heap_close(rel, RowExclusiveLock);
	heap_freetuple(tp);
	
	/* dispatch to QEs */
	if (Gp_role == GP_ROLE_DISPATCH)
	  ereport(ERROR, (errcode(ERRCODE_CDB_FEATURE_NOT_YET),
	              errmsg("Cannot support AlterUserMapping")));
}


/*
 * Drop user mapping
 */
void
RemoveUserMapping(DropUserMappingStmt *stmt)
{
	ObjectAddress object;
	Oid			useId;
	Oid			umId;
	ForeignServer *srv;

	useId = GetUserOidFromMapping(stmt->username, stmt->missing_ok);
	srv = GetForeignServerByName(stmt->servername, true);

	if (stmt->username && !OidIsValid(useId))
	{
		/*
		 * IF EXISTS specified, role not found and not public. Notice this and
		 * leave.
		 */
		elog(NOTICE, "role \"%s\" does not exist, skipping", stmt->username);
		return;
	}

	if (!srv)
	{
		if (!stmt->missing_ok)
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("server \"%s\" does not exist",
							stmt->servername),
									errOmitLocation(true)));
		/* IF EXISTS, just note it */
		ereport(NOTICE, (errmsg("server does not exist, skipping"),
				errOmitLocation(true)));
		return;
	}

	umId = caql_getoid(
			NULL,
			cql("SELECT oid FROM pg_user_mapping "
				" WHERE umuser = :1 "
				" AND umserver = :2 ",
				ObjectIdGetDatum(useId),
				ObjectIdGetDatum(srv->serverid)));

	if (!OidIsValid(umId))
	{
		if (!stmt->missing_ok)
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
				  errmsg("user mapping \"%s\" does not exist for the server",
						 MappingUserName(useId)),
									errOmitLocation(true)));

		/* IF EXISTS specified, just note it */
		ereport(NOTICE,
		(errmsg("user mapping \"%s\" does not exist for the server, skipping",
				MappingUserName(useId))));
		return;
	}

	user_mapping_ddl_aclcheck(useId, srv->serverid, srv->servername);

	/*
	 * Do the deletion
	 */
	object.classId = UserMappingRelationId;
	object.objectId = umId;
	object.objectSubId = 0;

	performDeletion(&object, DROP_CASCADE);
	
	/* dispatch to QEs */
	if (Gp_role == GP_ROLE_DISPATCH)
	  ereport(ERROR, (errcode(ERRCODE_CDB_FEATURE_NOT_YET),
	          errmsg("Cannot support RemoveUserMapping")));
}


/*
 * Drop user mapping by OID.  This is called to clean up dependencies.
 */
void
RemoveUserMappingById(Oid umId)
{
	if (0 == caql_getcount(
				NULL,
				cql("DELETE FROM pg_user_mapping "
					" WHERE oid = :1 ",
					ObjectIdGetDatum(umId))))
	{
		elog(ERROR, "cache lookup failed for user mapping %u", umId);
	}
}

/*******************************************************************
 * FOREIGN TABLE catalog manipulation
 * 
 * The foreign table API below is slightly different from the above
 * API. FDW, SERVER, and MAPPING are objects by themselves, and
 * creating/dropping/altering them is done directly on their
 * respective catalog tables. FOREIGN TABLE, however, is different
 * because the object is stored in pg_class and only the foreign
 * specifics are stored in pg_foreign_table (much like the design of
 * pg_exttable and pg_appendonly). So, instead of dealing with the
 * statement as a whole, we leave it to tablecmds.c, which here we
 * take care of physically inserting/altering/removing entries from
 * pg_foreign_table upon request.
 *******************************************************************/

void
InsertForeignTableEntry(Oid relid, char	*servername, List *options)
{
	Relation	rel;
	Datum		tbloptions;
	Datum		values[Natts_pg_foreign_table];
	bool		nulls[Natts_pg_foreign_table];
	HeapTuple	tuple;
	Oid			srvId;
	Oid			ownerId;
	AclResult	aclresult;
	ForeignServer *srv;
	ForeignDataWrapper *fdw;
	cqContext	cqc;
	cqContext  *pcqCtx;

	/* For now the owner cannot be specified on create. Use effective user ID. */
	ownerId = GetUserId();

	/* Check that the server exists. */
	srv = GetForeignServerByName(servername, false);

	/* Check we have at least USAGE permissions */
	if (!pg_foreign_server_ownercheck(srv->serverid, ownerId))
	{
		aclresult = pg_foreign_server_aclcheck(srv->serverid, ownerId, ACL_USAGE);
		if (aclresult != ACLCHECK_OK)
			aclcheck_error(aclresult, ACL_KIND_FOREIGN_SERVER, servername);
	}

	fdw = GetForeignDataWrapper(srv->fdwid);
	
	/*
	 * Insert tuple into pg_foreign_table.
	 */
	rel = heap_open(ForeignTableRelationId, RowExclusiveLock);
	pcqCtx = caql_beginscan(
			caql_addrel(cqclr(&cqc), rel),
			cql("INSERT INTO pg_foreign_table", 
				NULL));

	memset(values, 0, sizeof(values));
	memset(nulls, false, sizeof(nulls));

	values[Anum_pg_foreign_table_reloid - 1] = ObjectIdGetDatum(relid);
	values[Anum_pg_foreign_table_server - 1] = ObjectIdGetDatum(srv->serverid);

	/* Add server options */
	tbloptions = transformGenericOptions(PointerGetDatum(NULL), 
										 options,
										 fdw->fdwvalidator);

	if (PointerIsValid(DatumGetPointer(tbloptions)))
		values[Anum_pg_foreign_table_tbloptions - 1] = tbloptions;
	else
		nulls[Anum_pg_foreign_table_tbloptions - 1] = true;

	tuple = caql_form_tuple(pcqCtx, values, nulls);

	srvId = caql_insert(pcqCtx, tuple); /* implicit update of index as well */
	(void) srvId;  /* never used */

	heap_freetuple(tuple);

	caql_endscan(pcqCtx);
	heap_close(rel, NoLock);
}

/*
 * Drop pg_foreign_table entry by OID.  This is called to clean up dependencies.
 */
void
RemoveForeignTableEntry(Oid relid)
{
	Relation	pg_foreigntable_rel;
	int			numDel;
	cqContext	cqc;

	/*
	 * now remove the pg_foreign_table entry
	 */
	pg_foreigntable_rel = heap_open(ForeignTableRelationId, RowExclusiveLock);

	numDel = caql_getcount(
			caql_addrel(cqclr(&cqc), pg_foreigntable_rel),
			cql("DELETE FROM pg_foreign_table "
				" WHERE reloid = :1 ",
				ObjectIdGetDatum(relid)));

	if (!numDel)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_OBJECT),
				 errmsg("foreign table object id \"%d\" does not exist",
						relid)));

	heap_close(pg_foreigntable_rel, NoLock);
}

