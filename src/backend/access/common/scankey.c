/*-------------------------------------------------------------------------
 *
 * scankey.c
 *	  scan key support code
 *
 * Portions Copyright (c) 1996-2009, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  $PostgreSQL: pgsql/src/backend/access/common/scankey.c,v 1.32 2009/01/01 17:23:34 momjian Exp $
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/skey.h"


/*
 * ScanKeyEntryInitialize
 *		Initializes a scan key entry given all the field values.
 *		The target procedure is specified by OID (but can be invalid
 *		if SK_SEARCHNULL is set).
 *
 * Note: CurrentMemoryContext at call should be as long-lived as the ScanKey
 * itself, because that's what will be used for any subsidiary info attached
 * to the ScanKey's FmgrInfo record.
 */
void
ScanKeyEntryInitialize(ScanKey entry,
					   int flags,
					   AttrNumber attributeNumber,
					   StrategyNumber strategy,
					   Oid subtype,
					   RegProcedure opProcedure,
					   Datum argument,
					   AttrNumber attributeNumberOld,
					   RegProcedure outputProcedure)
{
	entry->sk_flags = flags;
	entry->sk_attno = attributeNumber;
	entry->sk_strategy = strategy;
	entry->sk_subtype = subtype;
	entry->sk_argument = argument;
	if (RegProcedureIsValid(opProcedure))
		fmgr_info(opProcedure, &entry->sk_func);
	else
	{
		Assert(flags & SK_SEARCHNULL);
		MemSet(&entry->sk_func, 0, sizeof(entry->sk_func));
	}
	entry->sk_attnoold = attributeNumberOld;
	if (RegProcedureIsValid(outputProcedure))
		fmgr_info(outputProcedure, &entry->sk_out_func);
	else
	        MemSet(&entry->sk_out_func, 0, sizeof(entry->sk_out_func));
}

/*
 * ScanKeyInit
 *		Shorthand version of ScanKeyEntryInitialize: flags and subtype
 *		are assumed to be zero (the usual value).
 *
 * This is the recommended version for hardwired lookups in system catalogs.
 * It cannot handle NULL arguments, unary operators, or nondefault operators,
 * but we need none of those features for most hardwired lookups.
 *
 * Note: CurrentMemoryContext at call should be as long-lived as the ScanKey
 * itself, because that's what will be used for any subsidiary info attached
 * to the ScanKey's FmgrInfo record.
 */
void
ScanKeyInit(ScanKey entry,
			AttrNumber attributeNumber,
			StrategyNumber strategy,
			RegProcedure procedure,
			Datum argument)
{
	entry->sk_flags = 0;
	entry->sk_attno = attributeNumber;
	entry->sk_strategy = strategy;
	entry->sk_subtype = InvalidOid;
	entry->sk_argument = argument;
	fmgr_info(procedure, &entry->sk_func);
}

/*
 * ScanKeyEntryInitializeWithInfo
 *		Initializes a scan key entry using an already-completed FmgrInfo
 *		function lookup record.
 *
 * Note: CurrentMemoryContext at call should be as long-lived as the ScanKey
 * itself, because that's what will be used for any subsidiary info attached
 * to the ScanKey's FmgrInfo record.
 */
void
ScanKeyEntryInitializeWithInfo(ScanKey entry,
							   int flags,
							   AttrNumber attributeNumber,
							   StrategyNumber strategy,
							   Oid subtype,
							   FmgrInfo *finfo,
							   Datum argument)
{
	entry->sk_flags = flags;
	entry->sk_attno = attributeNumber;
	entry->sk_strategy = strategy;
	entry->sk_subtype = subtype;
	entry->sk_argument = argument;
	fmgr_info_copy(&entry->sk_func, finfo, CurrentMemoryContext);
}
