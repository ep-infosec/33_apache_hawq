/*-------------------------------------------------------------------------
 *
 * regproc.c
 *	  Functions for the built-in types regproc, regclass, regtype, etc.
 *
 * These types are all binary-compatible with type Oid, and rely on Oid
 * for comparison and so forth.  Their only interesting behavior is in
 * special I/O conversion routines.
 *
 *
 * Portions Copyright (c) 1996-2008, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $PostgreSQL: pgsql/src/backend/utils/adt/regproc.c,v 1.99 2006/07/14 14:52:24 momjian Exp $
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include <ctype.h>

#include "access/genam.h"
#include "access/heapam.h"
#include "catalog/catquery.h"
#include "catalog/indexing.h"
#include "catalog/namespace.h"
#include "catalog/pg_operator.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_type.h"
#include "miscadmin.h"
#include "parser/parse_type.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"

static void parseNameAndArgTypes(const char *string, const char *caller,
					 bool allowNone,
					 List **names, int *nargs, Oid *argtypes);


/*****************************************************************************
 *	 USER I/O ROUTINES														 *
 *****************************************************************************/

/*
 * regprocin		- converts "proname" to proc OID
 *
 * We also accept a numeric OID, for symmetry with the output routine.
 *
 * '-' signifies unknown (OID 0).  In all other cases, the input must
 * match an existing pg_proc entry.
 */
Datum
regprocin(PG_FUNCTION_ARGS)
{
	char	   *pro_name_or_oid = PG_GETARG_CSTRING(0);
	RegProcedure result = InvalidOid;
	List	   *names;
	FuncCandidateList clist;

	/* '-' ? */
	if (strcmp(pro_name_or_oid, "-") == 0)
		PG_RETURN_OID(InvalidOid);

	/* Numeric OID? */
	if (pro_name_or_oid[0] >= '0' &&
		pro_name_or_oid[0] <= '9' &&
		strspn(pro_name_or_oid, "0123456789") == strlen(pro_name_or_oid))
	{
		result = DatumGetObjectId(DirectFunctionCall1(oidin,
										  CStringGetDatum(pro_name_or_oid)));
		PG_RETURN_OID(result);
	}

	/* Else it's a name, possibly schema-qualified */

	/*
	 * In bootstrap mode we assume the given name is not schema-qualified, and
	 * just search pg_proc for a unique match.	This is needed for
	 * initializing other system catalogs (pg_namespace may not exist yet, and
	 * certainly there are no schemas other than pg_catalog).
	 */
	if (IsBootstrapProcessingMode())
	{
		int			matches = 0;

		result = 
				(RegProcedure) caql_getoid_plus(
						NULL,
						&matches,
						NULL,
						cql("SELECT oid FROM pg_proc "
							" WHERE proname = :1 ",
							CStringGetDatum(pro_name_or_oid)));

		if (matches == 0)
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_FUNCTION),
				 errmsg("function \"%s\" does not exist", pro_name_or_oid)));

		else if (matches > 1)
			ereport(ERROR,
					(errcode(ERRCODE_AMBIGUOUS_FUNCTION),
					 errmsg("more than one function named \"%s\"",
							pro_name_or_oid)));

		PG_RETURN_OID(result);
	}

	/*
	 * Normal case: parse the name into components and see if it matches any
	 * pg_proc entries in the current search path.
	 */
	names = stringToQualifiedNameList(pro_name_or_oid, "regprocin");
	clist = FuncnameGetCandidates(names, -1, false);

	if (clist == NULL)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_FUNCTION),
				 errmsg("function \"%s\" does not exist", pro_name_or_oid)));
	else if (clist->next != NULL)
		ereport(ERROR,
				(errcode(ERRCODE_AMBIGUOUS_FUNCTION),
				 errmsg("more than one function named \"%s\"",
						pro_name_or_oid)));

	result = clist->oid;

	PG_RETURN_OID(result);
}

/*
 * regprocout		- converts proc OID to "pro_name"
 */
Datum
regprocout(PG_FUNCTION_ARGS)
{
	RegProcedure proid = PG_GETARG_OID(0);
	char	   *result;
	HeapTuple	proctup;
	cqContext  *pcqCtx;

	if (proid == InvalidOid)
	{
		result = pstrdup("-");
		PG_RETURN_CSTRING(result);
	}

	pcqCtx = caql_beginscan(
			NULL,
			cql("SELECT * FROM pg_proc "
				" WHERE oid = :1 ",
				ObjectIdGetDatum(proid)));

	proctup = caql_getnext(pcqCtx);

	/* XXX XXX select proname, pronamespace from pg_proc */
	if (HeapTupleIsValid(proctup))
	{
		Form_pg_proc procform = (Form_pg_proc) GETSTRUCT(proctup);
		char	   *proname = NameStr(procform->proname);

		/*
		 * In bootstrap mode, skip the fancy namespace stuff and just return
		 * the proc name.  (This path is only needed for debugging output
		 * anyway.)
		 */
		if (IsBootstrapProcessingMode())
			result = pstrdup(proname);
		else
		{
			char	   *nspname;
			FuncCandidateList clist;

			/*
			 * Would this proc be found (uniquely!) by regprocin? If not,
			 * qualify it.
			 */
			clist = FuncnameGetCandidates(list_make1(makeString(proname)), -1, false);
			if (clist != NULL && clist->next == NULL &&
				clist->oid == proid)
				nspname = NULL;
			else
				nspname = get_namespace_name(procform->pronamespace);

			result = quote_qualified_identifier(nspname, proname);
		}
	}
	else
	{
		/* If OID doesn't match any pg_proc entry, return it numerically */
		result = (char *) palloc(NAMEDATALEN);
		snprintf(result, NAMEDATALEN, "%u", proid);
	}

	caql_endscan(pcqCtx);

	PG_RETURN_CSTRING(result);
}

/*
 *		regprocrecv			- converts external binary format to regproc
 */
Datum
regprocrecv(PG_FUNCTION_ARGS)
{
	/* Exactly the same as oidrecv, so share code */
	return oidrecv(fcinfo);
}

/*
 *		regprocsend			- converts regproc to binary format
 */
Datum
regprocsend(PG_FUNCTION_ARGS)
{
	/* Exactly the same as oidsend, so share code */
	return oidsend(fcinfo);
}


/*
 * regprocedurein		- converts "proname(args)" to proc OID
 *
 * We also accept a numeric OID, for symmetry with the output routine.
 *
 * '-' signifies unknown (OID 0).  In all other cases, the input must
 * match an existing pg_proc entry.
 */
Datum
regprocedurein(PG_FUNCTION_ARGS)
{
	char	   *pro_name_or_oid = PG_GETARG_CSTRING(0);
	RegProcedure result = InvalidOid;
	List	   *names;
	int			nargs;
	Oid			argtypes[FUNC_MAX_ARGS];
	FuncCandidateList clist;

	/* '-' ? */
	if (strcmp(pro_name_or_oid, "-") == 0)
		PG_RETURN_OID(InvalidOid);

	/* Numeric OID? */
	if (pro_name_or_oid[0] >= '0' &&
		pro_name_or_oid[0] <= '9' &&
		strspn(pro_name_or_oid, "0123456789") == strlen(pro_name_or_oid))
	{
		result = DatumGetObjectId(DirectFunctionCall1(oidin,
										  CStringGetDatum(pro_name_or_oid)));
		PG_RETURN_OID(result);
	}

	/*
	 * Else it's a name and arguments.  Parse the name and arguments, look up
	 * potential matches in the current namespace search list, and scan to see
	 * which one exactly matches the given argument types.	(There will not be
	 * more than one match.)
	 *
	 * XXX at present, this code will not work in bootstrap mode, hence this
	 * datatype cannot be used for any system column that needs to receive
	 * data during bootstrap.
	 */
	parseNameAndArgTypes(pro_name_or_oid, "regprocedurein", false,
						 &names, &nargs, argtypes);

	clist = FuncnameGetCandidates(names, nargs, false);

	for (; clist; clist = clist->next)
	{
		if (memcmp(clist->args, argtypes, nargs * sizeof(Oid)) == 0)
			break;
	}

	if (clist == NULL)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_FUNCTION),
				 errmsg("function \"%s\" does not exist", pro_name_or_oid)));

	result = clist->oid;

	PG_RETURN_OID(result);
}

/*
 * format_procedure		- converts proc OID to "pro_name(args)"
 *
 * This exports the useful functionality of regprocedureout for use
 * in other backend modules.  The result is a palloc'd string.
 */
char *
format_procedure(Oid procedure_oid)
{
	char	   *result;
	HeapTuple	proctup;
	cqContext  *pcqCtx;

	pcqCtx = caql_beginscan(
			NULL,
			cql("SELECT * FROM pg_proc "
				" WHERE oid = :1 ",
				ObjectIdGetDatum(procedure_oid)));

	proctup = caql_getnext(pcqCtx);

	/* XXX XXX select proname, pronamespace from pg_proc */
	if (HeapTupleIsValid(proctup))
	{
		Form_pg_proc procform = (Form_pg_proc) GETSTRUCT(proctup);
		char	   *proname = NameStr(procform->proname);
		int			nargs = procform->pronargs;
		int			i;
		char	   *nspname;
		StringInfoData buf;

		/* XXX no support here for bootstrap mode */

		initStringInfo(&buf);

		/*
		 * Would this proc be found (given the right args) by regprocedurein?
		 * If not, we need to qualify it.
		 */
		if (FunctionIsVisible(procedure_oid))
			nspname = NULL;
		else
			nspname = get_namespace_name(procform->pronamespace);

		appendStringInfo(&buf, "%s(",
						 quote_qualified_identifier(nspname, proname));
		for (i = 0; i < nargs; i++)
		{
			Oid			thisargtype = procform->proargtypes.values[i];

			if (i > 0)
				appendStringInfoChar(&buf, ',');
			appendStringInfoString(&buf, format_type_be(thisargtype));
		}
		appendStringInfoChar(&buf, ')');

		result = buf.data;

	}
	else
	{
		/* If OID doesn't match any pg_proc entry, return it numerically */
		result = (char *) palloc(NAMEDATALEN);
		snprintf(result, NAMEDATALEN, "%u", procedure_oid);
	}

	caql_endscan(pcqCtx);

	return result;
}

/*
 * regprocedureout		- converts proc OID to "pro_name(args)"
 */
Datum
regprocedureout(PG_FUNCTION_ARGS)
{
	RegProcedure proid = PG_GETARG_OID(0);
	char	   *result;

	if (proid == InvalidOid)
		result = pstrdup("-");
	else
		result = format_procedure(proid);

	PG_RETURN_CSTRING(result);
}

/*
 *		regprocedurerecv			- converts external binary format to regprocedure
 */
Datum
regprocedurerecv(PG_FUNCTION_ARGS)
{
	/* Exactly the same as oidrecv, so share code */
	return oidrecv(fcinfo);
}

/*
 *		regproceduresend			- converts regprocedure to binary format
 */
Datum
regproceduresend(PG_FUNCTION_ARGS)
{
	/* Exactly the same as oidsend, so share code */
	return oidsend(fcinfo);
}


/*
 * regoperin		- converts "oprname" to operator OID
 *
 * We also accept a numeric OID, for symmetry with the output routine.
 *
 * '0' signifies unknown (OID 0).  In all other cases, the input must
 * match an existing pg_operator entry.
 */
Datum
regoperin(PG_FUNCTION_ARGS)
{
	char	   *opr_name_or_oid = PG_GETARG_CSTRING(0);
	Oid			result = InvalidOid;
	List	   *names;
	FuncCandidateList clist;

	/* '0' ? */
	if (strcmp(opr_name_or_oid, "0") == 0)
		PG_RETURN_OID(InvalidOid);

	/* Numeric OID? */
	if (opr_name_or_oid[0] >= '0' &&
		opr_name_or_oid[0] <= '9' &&
		strspn(opr_name_or_oid, "0123456789") == strlen(opr_name_or_oid))
	{
		result = DatumGetObjectId(DirectFunctionCall1(oidin,
										  CStringGetDatum(opr_name_or_oid)));
		PG_RETURN_OID(result);
	}

	/* Else it's a name, possibly schema-qualified */

	/*
	 * In bootstrap mode we assume the given name is not schema-qualified, and
	 * just search pg_operator for a unique match.	This is needed for
	 * initializing other system catalogs (pg_namespace may not exist yet, and
	 * certainly there are no schemas other than pg_catalog).
	 */
	if (IsBootstrapProcessingMode())
	{
		int			matches = 0;

		result = 
				caql_getoid_plus(
						NULL,
						&matches,
						NULL,
						cql("SELECT oid FROM pg_operator "
							" WHERE oprname = :1 ",
							CStringGetDatum(opr_name_or_oid)));

		if (matches == 0)
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_FUNCTION),
					 errmsg("operator does not exist: %s", opr_name_or_oid)));
		else if (matches > 1)
			ereport(ERROR,
					(errcode(ERRCODE_AMBIGUOUS_FUNCTION),
					 errmsg("more than one operator named %s",
							opr_name_or_oid)));

		PG_RETURN_OID(result);
	}

	/*
	 * Normal case: parse the name into components and see if it matches any
	 * pg_operator entries in the current search path.
	 */
	names = stringToQualifiedNameList(opr_name_or_oid, "regoperin");
	clist = OpernameGetCandidates(names, '\0');

	if (clist == NULL)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_FUNCTION),
				 errmsg("operator does not exist: %s", opr_name_or_oid)));
	else if (clist->next != NULL)
		ereport(ERROR,
				(errcode(ERRCODE_AMBIGUOUS_FUNCTION),
				 errmsg("more than one operator named %s",
						opr_name_or_oid)));

	result = clist->oid;

	PG_RETURN_OID(result);
}

/*
 * regoperout		- converts operator OID to "opr_name"
 */
Datum
regoperout(PG_FUNCTION_ARGS)
{
	Oid			oprid = PG_GETARG_OID(0);
	char	   *result;
	HeapTuple	opertup;
	cqContext  *pcqCtx;

	if (oprid == InvalidOid)
	{
		result = pstrdup("0");
		PG_RETURN_CSTRING(result);
	}

	pcqCtx = caql_beginscan(
			NULL,
			cql("SELECT * FROM pg_operator "
				" WHERE oid = :1 ",
				ObjectIdGetDatum(oprid)));

	opertup = caql_getnext(pcqCtx);

	/* XXX XXX select oprname, oprnamespace from pg_operator */
	if (HeapTupleIsValid(opertup))
	{
		Form_pg_operator operform = (Form_pg_operator) GETSTRUCT(opertup);
		char	   *oprname = NameStr(operform->oprname);

		/*
		 * In bootstrap mode, skip the fancy namespace stuff and just return
		 * the oper name.  (This path is only needed for debugging output
		 * anyway.)
		 */
		if (IsBootstrapProcessingMode())
			result = pstrdup(oprname);
		else
		{
			FuncCandidateList clist;

			/*
			 * Would this oper be found (uniquely!) by regoperin? If not,
			 * qualify it.
			 */
			clist = OpernameGetCandidates(list_make1(makeString(oprname)),
										  '\0');
			if (clist != NULL && clist->next == NULL &&
				clist->oid == oprid)
				result = pstrdup(oprname);
			else
			{
				const char *nspname;

				nspname = get_namespace_name(operform->oprnamespace);
				nspname = quote_identifier(nspname);
				result = (char *) palloc(strlen(nspname) + strlen(oprname) + 2);
				sprintf(result, "%s.%s", nspname, oprname);
			}
		}
	}
	else
	{
		/*
		 * If OID doesn't match any pg_operator entry, return it numerically
		 */
		result = (char *) palloc(NAMEDATALEN);
		snprintf(result, NAMEDATALEN, "%u", oprid);
	}

	caql_endscan(pcqCtx);

	PG_RETURN_CSTRING(result);
}

/*
 *		regoperrecv			- converts external binary format to regoper
 */
Datum
regoperrecv(PG_FUNCTION_ARGS)
{
	/* Exactly the same as oidrecv, so share code */
	return oidrecv(fcinfo);
}

/*
 *		regopersend			- converts regoper to binary format
 */
Datum
regopersend(PG_FUNCTION_ARGS)
{
	/* Exactly the same as oidsend, so share code */
	return oidsend(fcinfo);
}


/*
 * regoperatorin		- converts "oprname(args)" to operator OID
 *
 * We also accept a numeric OID, for symmetry with the output routine.
 *
 * '0' signifies unknown (OID 0).  In all other cases, the input must
 * match an existing pg_operator entry.
 */
Datum
regoperatorin(PG_FUNCTION_ARGS)
{
	char	   *opr_name_or_oid = PG_GETARG_CSTRING(0);
	Oid			result;
	List	   *names;
	int			nargs;
	Oid			argtypes[FUNC_MAX_ARGS];

	/* '0' ? */
	if (strcmp(opr_name_or_oid, "0") == 0)
		PG_RETURN_OID(InvalidOid);

	/* Numeric OID? */
	if (opr_name_or_oid[0] >= '0' &&
		opr_name_or_oid[0] <= '9' &&
		strspn(opr_name_or_oid, "0123456789") == strlen(opr_name_or_oid))
	{
		result = DatumGetObjectId(DirectFunctionCall1(oidin,
										  CStringGetDatum(opr_name_or_oid)));
		PG_RETURN_OID(result);
	}

	/*
	 * Else it's a name and arguments.  Parse the name and arguments, look up
	 * potential matches in the current namespace search list, and scan to see
	 * which one exactly matches the given argument types.	(There will not be
	 * more than one match.)
	 *
	 * XXX at present, this code will not work in bootstrap mode, hence this
	 * datatype cannot be used for any system column that needs to receive
	 * data during bootstrap.
	 */
	parseNameAndArgTypes(opr_name_or_oid, "regoperatorin", true,
						 &names, &nargs, argtypes);
	if (nargs == 1)
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_PARAMETER),
				 errmsg("missing argument"),
				 errhint("Use NONE to denote the missing argument of a unary operator.")));
	if (nargs != 2)
		ereport(ERROR,
				(errcode(ERRCODE_TOO_MANY_ARGUMENTS),
				 errmsg("too many arguments"),
				 errhint("Provide two argument types for operator.")));

	result = OpernameGetOprid(names, argtypes[0], argtypes[1]);

	if (!OidIsValid(result))
		ereport(ERROR,
				(errcode(ERRCODE_UNDEFINED_FUNCTION),
				 errmsg("operator does not exist: %s", opr_name_or_oid)));

	PG_RETURN_OID(result);
}

/*
 * format_operator		- converts operator OID to "opr_name(args)"
 *
 * This exports the useful functionality of regoperatorout for use
 * in other backend modules.  The result is a palloc'd string.
 */
char *
format_operator(Oid operator_oid)
{
	char	   *result;
	HeapTuple	opertup;
	cqContext  *pcqCtx;

	pcqCtx = caql_beginscan(
			NULL,
			cql("SELECT * FROM pg_operator "
				" WHERE oid = :1 ",
				ObjectIdGetDatum(operator_oid)));

	opertup = caql_getnext(pcqCtx);

	/* XXX XXX select oprname, oprnamespace from pg_operator */
	if (HeapTupleIsValid(opertup))
	{
		Form_pg_operator operform = (Form_pg_operator) GETSTRUCT(opertup);
		char	   *oprname = NameStr(operform->oprname);
		char	   *nspname;
		StringInfoData buf;

		/* XXX no support here for bootstrap mode */

		initStringInfo(&buf);

		/*
		 * Would this oper be found (given the right args) by regoperatorin?
		 * If not, we need to qualify it.
		 */
		if (!OperatorIsVisible(operator_oid))
		{
			nspname = get_namespace_name(operform->oprnamespace);
			appendStringInfo(&buf, "%s.",
							 quote_identifier(nspname));
		}

		appendStringInfo(&buf, "%s(", oprname);

		if (operform->oprleft)
			appendStringInfo(&buf, "%s,",
							 format_type_be(operform->oprleft));
		else
			appendStringInfo(&buf, "NONE,");

		if (operform->oprright)
			appendStringInfo(&buf, "%s)",
							 format_type_be(operform->oprright));
		else
			appendStringInfo(&buf, "NONE)");

		result = buf.data;
	}
	else
	{
		/*
		 * If OID doesn't match any pg_operator entry, return it numerically
		 */
		result = (char *) palloc(NAMEDATALEN);
		snprintf(result, NAMEDATALEN, "%u", operator_oid);
	}

	caql_endscan(pcqCtx);

	return result;
}

/*
 * regoperatorout		- converts operator OID to "opr_name(args)"
 */
Datum
regoperatorout(PG_FUNCTION_ARGS)
{
	Oid			oprid = PG_GETARG_OID(0);
	char	   *result;

	if (oprid == InvalidOid)
		result = pstrdup("0");
	else
		result = format_operator(oprid);

	PG_RETURN_CSTRING(result);
}

/*
 *		regoperatorrecv			- converts external binary format to regoperator
 */
Datum
regoperatorrecv(PG_FUNCTION_ARGS)
{
	/* Exactly the same as oidrecv, so share code */
	return oidrecv(fcinfo);
}

/*
 *		regoperatorsend			- converts regoperator to binary format
 */
Datum
regoperatorsend(PG_FUNCTION_ARGS)
{
	/* Exactly the same as oidsend, so share code */
	return oidsend(fcinfo);
}


/*
 * regclassin		- converts "classname" to class OID
 *
 * We also accept a numeric OID, for symmetry with the output routine.
 *
 * '-' signifies unknown (OID 0).  In all other cases, the input must
 * match an existing pg_class entry.
 */
Datum
regclassin(PG_FUNCTION_ARGS)
{
	char	   *class_name_or_oid = PG_GETARG_CSTRING(0);
	Oid			result = InvalidOid;
	List	   *names;

	/* '-' ? */
	if (strcmp(class_name_or_oid, "-") == 0)
		PG_RETURN_OID(InvalidOid);

	/* Numeric OID? */
	if (class_name_or_oid[0] >= '0' &&
		class_name_or_oid[0] <= '9' &&
		strspn(class_name_or_oid, "0123456789") == strlen(class_name_or_oid))
	{
		result = DatumGetObjectId(DirectFunctionCall1(oidin,
										CStringGetDatum(class_name_or_oid)));
		PG_RETURN_OID(result);
	}

	/* Else it's a name, possibly schema-qualified */

	/*
	 * In bootstrap mode we assume the given name is not schema-qualified, and
	 * just search pg_class for a match.  This is needed for initializing
	 * other system catalogs (pg_namespace may not exist yet, and certainly
	 * there are no schemas other than pg_catalog).
	 */
	if (IsBootstrapProcessingMode())
	{
		int			matches = 0;

		result = 
				caql_getoid_plus(
						NULL,
						&matches,
						NULL,
						cql("SELECT oid FROM pg_class "
							" WHERE relname = :1 ",
							CStringGetDatum(class_name_or_oid)));
		if (0 == matches)
		{
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_TABLE),
			   errmsg("relation \"%s\" does not exist", class_name_or_oid)));
		}

		/* We assume there can be only one match */

		PG_RETURN_OID(result);
	}

	/*
	 * Normal case: parse the name into components and see if it matches any
	 * pg_class entries in the current search path.
	 */
	names = stringToQualifiedNameList(class_name_or_oid, "regclassin");

	result = RangeVarGetRelid(makeRangeVarFromNameList(names), false, true /*allowHcatalog*/);

	PG_RETURN_OID(result);
}

/*
 * regclassout		- converts class OID to "class_name"
 */
Datum
regclassout(PG_FUNCTION_ARGS)
{
	Oid			classid = PG_GETARG_OID(0);
	char	   *result;
	HeapTuple	classtup;
	cqContext  *pcqCtx;

	if (classid == InvalidOid)
	{
		result = pstrdup("-");
		PG_RETURN_CSTRING(result);
	}

	pcqCtx = caql_beginscan(
			NULL,
			cql("SELECT * FROM pg_class "
				" WHERE oid = :1 ",
				ObjectIdGetDatum(classid)));

	classtup = caql_getnext(pcqCtx);

	/* XXX XXX select relname, relnamespace from pg_class */
	if (HeapTupleIsValid(classtup))
	{
		Form_pg_class classform = (Form_pg_class) GETSTRUCT(classtup);
		char	   *classname = NameStr(classform->relname);

		/*
		 * In bootstrap mode, skip the fancy namespace stuff and just return
		 * the class name.	(This path is only needed for debugging output
		 * anyway.)
		 */
		if (IsBootstrapProcessingMode())
			result = pstrdup(classname);
		else
		{
			char	   *nspname;

			/*
			 * Would this class be found by regclassin? If not, qualify it.
			 */
			if (RelationIsVisible(classid))
				nspname = NULL;
			else
				nspname = get_namespace_name(classform->relnamespace);

			result = quote_qualified_identifier(nspname, classname);
		}
	}
	else
	{
		/* If OID doesn't match any pg_class entry, return it numerically */
		result = (char *) palloc(NAMEDATALEN);
		snprintf(result, NAMEDATALEN, "%u", classid);
	}

	caql_endscan(pcqCtx);

	PG_RETURN_CSTRING(result);
}

/*
 *		regclassrecv			- converts external binary format to regclass
 */
Datum
regclassrecv(PG_FUNCTION_ARGS)
{
	/* Exactly the same as oidrecv, so share code */
	return oidrecv(fcinfo);
}

/*
 *		regclasssend			- converts regclass to binary format
 */
Datum
regclasssend(PG_FUNCTION_ARGS)
{
	/* Exactly the same as oidsend, so share code */
	return oidsend(fcinfo);
}


/*
 * regtypein		- converts "typename" to type OID
 *
 * We also accept a numeric OID, for symmetry with the output routine.
 *
 * '-' signifies unknown (OID 0).  In all other cases, the input must
 * match an existing pg_type entry.
 *
 * In bootstrap mode the name must just equal some existing name in pg_type.
 * In normal mode the type name can be specified using the full type syntax
 * recognized by the parser; for example, DOUBLE PRECISION and INTEGER[] will
 * work and be translated to the correct type names.  (We ignore any typmod
 * info generated by the parser, however.)
 */
Datum
regtypein(PG_FUNCTION_ARGS)
{
	char	   *typ_name_or_oid = PG_GETARG_CSTRING(0);
	Oid			result = InvalidOid;
	int32		typmod;

	/* '-' ? */
	if (strcmp(typ_name_or_oid, "-") == 0)
		PG_RETURN_OID(InvalidOid);

	/* Numeric OID? */
	if (typ_name_or_oid[0] >= '0' &&
		typ_name_or_oid[0] <= '9' &&
		strspn(typ_name_or_oid, "0123456789") == strlen(typ_name_or_oid))
	{
		result = DatumGetObjectId(DirectFunctionCall1(oidin,
										  CStringGetDatum(typ_name_or_oid)));
		PG_RETURN_OID(result);
	}

	/* Else it's a type name, possibly schema-qualified or decorated */

	/*
	 * In bootstrap mode we assume the given name is not schema-qualified, and
	 * just search pg_type for a match.  This is needed for initializing other
	 * system catalogs (pg_namespace may not exist yet, and certainly there
	 * are no schemas other than pg_catalog).
	 */
	if (IsBootstrapProcessingMode())
	{
		int			matches = 0;

		result = 
				caql_getoid_plus(
						NULL,
						&matches,
						NULL,
						cql("SELECT oid FROM pg_type "
							" WHERE typname = :1 ",
							CStringGetDatum(typ_name_or_oid)));
		if (0 == matches)
		{
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_OBJECT),
					 errmsg("type \"%s\" does not exist", typ_name_or_oid)));
		}

		/* We assume there can be only one match */

		PG_RETURN_OID(result);
	}

	/*
	 * Normal case: invoke the full parser to deal with special cases such as
	 * array syntax.
	 */
	parseTypeString(typ_name_or_oid, &result, &typmod);

	PG_RETURN_OID(result);
}

/*
 * regtypeout		- converts type OID to "typ_name"
 */
Datum
regtypeout(PG_FUNCTION_ARGS)
{
	Oid			typid = PG_GETARG_OID(0);
	char	   *result = NULL;
	int			fetchCount;

	if (typid == InvalidOid)
	{
		result = pstrdup("-");
		PG_RETURN_CSTRING(result);
	}

	result = caql_getcstring_plus(
			NULL,
			&fetchCount,
			NULL,
			cql("SELECT typname FROM pg_type "
				" WHERE oid = :1 ",
				ObjectIdGetDatum(typid)));

	if (fetchCount)
	{
		/*
		 * In bootstrap mode, skip the fancy namespace stuff and just return
		 * the type name.  (This path is only needed for debugging output
		 * anyway.)
		 */
		if (!IsBootstrapProcessingMode())
		{
			if (result) pfree(result);
			result = format_type_be(typid);
		}
	}
	else
	{
		/* If OID doesn't match any pg_type entry, return it numerically */
		result = (char *) palloc(NAMEDATALEN);
		snprintf(result, NAMEDATALEN, "%u", typid);
	}

	PG_RETURN_CSTRING(result);
}

/*
 *		regtyperecv			- converts external binary format to regtype
 */
Datum
regtyperecv(PG_FUNCTION_ARGS)
{
	/* Exactly the same as oidrecv, so share code */
	return oidrecv(fcinfo);
}

/*
 *		regtypesend			- converts regtype to binary format
 */
Datum
regtypesend(PG_FUNCTION_ARGS)
{
	/* Exactly the same as oidsend, so share code */
	return oidsend(fcinfo);
}


/*
 * text_regclass: convert text to regclass
 */
Datum
text_regclass(PG_FUNCTION_ARGS)
{
	text	   *relname = PG_GETARG_TEXT_P(0);
	Oid			result;
	RangeVar   *rv;

	rv = makeRangeVarFromNameList(textToQualifiedNameList(relname));
	result = RangeVarGetRelid(rv, false, true /*allowHcatalog*/);

	PG_RETURN_OID(result);
}


/*
 * Given a C string, parse it into a qualified-name list.
 */
List *
stringToQualifiedNameList(const char *string, const char *caller)
{
	char	   *rawname;
	List	   *result = NIL;
	List	   *namelist;
	ListCell   *l;

	/* We need a modifiable copy of the input string. */
	rawname = pstrdup(string);

	if (!SplitIdentifierString(rawname, '.', &namelist))
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_NAME),
				 errmsg("invalid name syntax")));

	if (namelist == NIL)
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_NAME),
				 errmsg("invalid name syntax")));

	foreach(l, namelist)
	{
		char	   *curname = (char *) lfirst(l);

		result = lappend(result, makeString(pstrdup(curname)));
	}

	pfree(rawname);
	list_free(namelist);

	return result;
}

/*****************************************************************************
 *	 SUPPORT ROUTINES														 *
 *****************************************************************************/

/*
 * Given a C string, parse it into a qualified function or operator name
 * followed by a parenthesized list of type names.	Reduce the
 * type names to an array of OIDs (returned into *nargs and *argtypes;
 * the argtypes array should be of size FUNC_MAX_ARGS).  The function or
 * operator name is returned to *names as a List of Strings.
 *
 * If allowNone is TRUE, accept "NONE" and return it as InvalidOid (this is
 * for unary operators).
 */
static void
parseNameAndArgTypes(const char *string, const char *caller,
					 bool allowNone,
					 List **names, int *nargs, Oid *argtypes)
{
	char	   *rawname;
	char	   *ptr;
	char	   *ptr2;
	char	   *typename;
	bool		in_quote;
	bool		had_comma;
	int			paren_count;
	Oid			typeid;
	int32		typmod;

	/* We need a modifiable copy of the input string. */
	rawname = pstrdup(string);

	/* Scan to find the expected left paren; mustn't be quoted */
	in_quote = false;
	for (ptr = rawname; *ptr; ptr++)
	{
		if (*ptr == '"')
			in_quote = !in_quote;
		else if (*ptr == '(' && !in_quote)
			break;
	}
	if (*ptr == '\0')
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
				 errmsg("expected a left parenthesis")));

	/* Separate the name and parse it into a list */
	*ptr++ = '\0';
	*names = stringToQualifiedNameList(rawname, caller);

	/* Check for the trailing right parenthesis and remove it */
	ptr2 = ptr + strlen(ptr);
	while (--ptr2 > ptr)
	{
		if (!isspace((unsigned char) *ptr2))
			break;
	}
	if (*ptr2 != ')')
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
				 errmsg("expected a right parenthesis")));

	*ptr2 = '\0';

	/* Separate the remaining string into comma-separated type names */
	*nargs = 0;
	had_comma = false;

	for (;;)
	{
		/* allow leading whitespace */
		while (isspace((unsigned char) *ptr))
			ptr++;
		if (*ptr == '\0')
		{
			/* End of string.  Okay unless we had a comma before. */
			if (had_comma)
				ereport(ERROR,
						(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
						 errmsg("expected a type name")));
			break;
		}
		typename = ptr;
		/* Find end of type name --- end of string or comma */
		/* ... but not a quoted or parenthesized comma */
		in_quote = false;
		paren_count = 0;
		for (; *ptr; ptr++)
		{
			if (*ptr == '"')
				in_quote = !in_quote;
			else if (*ptr == ',' && !in_quote && paren_count == 0)
				break;
			else if (!in_quote)
			{
				switch (*ptr)
				{
					case '(':
					case '[':
						paren_count++;
						break;
					case ')':
					case ']':
						paren_count--;
						break;
				}
			}
		}
		if (in_quote || paren_count != 0)
			ereport(ERROR,
					(errcode(ERRCODE_INVALID_TEXT_REPRESENTATION),
					 errmsg("improper type name")));

		ptr2 = ptr;
		if (*ptr == ',')
		{
			had_comma = true;
			*ptr++ = '\0';
		}
		else
		{
			had_comma = false;
			Assert(*ptr == '\0');
		}
		/* Lop off trailing whitespace */
		while (--ptr2 >= typename)
		{
			if (!isspace((unsigned char) *ptr2))
				break;
			*ptr2 = '\0';
		}

		if (allowNone && pg_strcasecmp(typename, "none") == 0)
		{
			/* Special case for NONE */
			typeid = InvalidOid;
			typmod = -1;
		}
		else
		{
			/* Use full parser to resolve the type name */
			parseTypeString(typename, &typeid, &typmod);
		}
		if (*nargs >= FUNC_MAX_ARGS)
			ereport(ERROR,
					(errcode(ERRCODE_TOO_MANY_ARGUMENTS),
					 errmsg("too many arguments")));

		argtypes[*nargs] = typeid;
		(*nargs)++;
	}

	pfree(rawname);
}
