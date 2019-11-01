/*-------------------------------------------------------------------------
 *
 * pg_check_partitions_permissions.c
 *	    PostgreSQL extension to check access permissions for partitions.
 *
 *  Copyright (c) 2019, Fujii Masao
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "catalog/objectaddress.h"
#include "catalog/pg_class.h"
#include "executor/executor.h"
#include "miscadmin.h"
#include "utils/acl.h"
#include "utils/lsyscache.h"

PG_MODULE_MAGIC;

/* Saved hook values in case of unload */
static ExecutorCheckPerms_hook_type prev_ExecutorCheckPerms = NULL;

void		_PG_init(void);
void		_PG_fini(void);

static bool check_partitions_permissions(List *rangeTable,
										 bool ereport_on_violation);
static bool do_ExecCheckRTEPerms(RangeTblEntry *rte);
static bool do_ExecCheckRTEPermsModified(Oid relOid, Oid userid,
										 Bitmapset *modifiedCols,
										 AclMode requiredPerms);

/*
 * Module load callback
 */
void
_PG_init(void)
{
	/* Install hooks. */
	prev_ExecutorCheckPerms = ExecutorCheckPerms_hook;
	ExecutorCheckPerms_hook = check_partitions_permissions;
}

/*
 * Module unload callback
 */
void
_PG_fini(void)
{
	/* Uninstall hooks. */
	ExecutorCheckPerms_hook = prev_ExecutorCheckPerms;
}

/*
 * ExecutorCheckPerms hook
 */
static bool
check_partitions_permissions(List *rangeTable, bool ereport_on_violation)
{
	ListCell   *l;
	bool		result = true;

	if (prev_ExecutorCheckPerms)
	{
		result = prev_ExecutorCheckPerms(rangeTable, ereport_on_violation);
		if (!result)
			return result;
	}

	foreach(l, rangeTable)
	{
		RangeTblEntry *rte = (RangeTblEntry *) lfirst(l);
		AclMode	save_requiredPerms = 0;

		/*
		 * Don't check access permissions for objects except partitions
		 * with empty requiredPerms.
		 */
		if (rte->requiredPerms != 0 ||
			rte->relkind != RELKIND_RELATION ||
			!get_rel_relispartition(rte->relid))
			continue;

		PG_TRY();
		{
			/*
			 * The permissions on partitions are ignored because
			 * their requiredPerms is not set. To check their permissions,
			 * their requiredPerms must be set to the same as that of
			 * their partitionned table before calling ExecCheckRTEPerms().
			 * But it's a bit complicated and difficult to get requiredPerms
			 * of the partitioned table, so currently we forcibly append
			 * ACL_SELECT to their requiredPerms and check SELECT
			 * permissions on all tables here. This is OK because this
			 * extension is still in prototype phase. But this will be changed
			 * in the future.
			 */
			save_requiredPerms = rte->requiredPerms;
			rte->requiredPerms |= ACL_SELECT;
			result = do_ExecCheckRTEPerms(rte);
			rte->requiredPerms = save_requiredPerms;
		}
		PG_CATCH();
		{
			rte->requiredPerms = save_requiredPerms;
			PG_RE_THROW();
		}
		PG_END_TRY();

		if (!result)
		{
			Assert(rte->rtekind == RTE_RELATION);
			if (ereport_on_violation)
				aclcheck_error(ACLCHECK_NO_PRIV, get_relkind_objtype(get_rel_relkind(rte->relid)),
							   get_rel_name(rte->relid));
			return false;
		}
	}

	return result;
}

/*
 * Copy-and-paste from ExecCheckRTPerms() in PostgreSQL 13dev
 * source code.
 *
 * ExecCheckRTEPerms
 *		Check access permissions for a single RTE.
 */
static bool
do_ExecCheckRTEPerms(RangeTblEntry *rte)
{
	AclMode		requiredPerms;
	AclMode		relPerms;
	AclMode		remainingPerms;
	Oid			relOid;
	Oid			userid;

	/*
	 * Only plain-relation RTEs need to be checked here.  Function RTEs are
	 * checked when the function is prepared for execution.  Join, subquery,
	 * and special RTEs need no checks.
	 */
	if (rte->rtekind != RTE_RELATION)
		return true;

	/*
	 * No work if requiredPerms is empty.
	 */
	requiredPerms = rte->requiredPerms;
	if (requiredPerms == 0)
		return true;

	relOid = rte->relid;

	/*
	 * userid to check as: current user unless we have a setuid indication.
	 *
	 * Note: GetUserId() is presently fast enough that there's no harm in
	 * calling it separately for each RTE.  If that stops being true, we could
	 * call it once in ExecCheckRTPerms and pass the userid down from there.
	 * But for now, no need for the extra clutter.
	 */
	userid = rte->checkAsUser ? rte->checkAsUser : GetUserId();

	/*
	 * We must have *all* the requiredPerms bits, but some of the bits can be
	 * satisfied from column-level rather than relation-level permissions.
	 * First, remove any bits that are satisfied by relation permissions.
	 */
	relPerms = pg_class_aclmask(relOid, userid, requiredPerms, ACLMASK_ALL);
	remainingPerms = requiredPerms & ~relPerms;
	if (remainingPerms != 0)
	{
		int			col = -1;

		/*
		 * If we lack any permissions that exist only as relation permissions,
		 * we can fail straight away.
		 */
		if (remainingPerms & ~(ACL_SELECT | ACL_INSERT | ACL_UPDATE))
			return false;

		/*
		 * Check to see if we have the needed privileges at column level.
		 *
		 * Note: failures just report a table-level error; it would be nicer
		 * to report a column-level error if we have some but not all of the
		 * column privileges.
		 */
		if (remainingPerms & ACL_SELECT)
		{
			/*
			 * When the query doesn't explicitly reference any columns (for
			 * example, SELECT COUNT(*) FROM table), allow the query if we
			 * have SELECT on any column of the rel, as per SQL spec.
			 */
			if (bms_is_empty(rte->selectedCols))
			{
				if (pg_attribute_aclcheck_all(relOid, userid, ACL_SELECT,
											  ACLMASK_ANY) != ACLCHECK_OK)
					return false;
			}

			while ((col = bms_next_member(rte->selectedCols, col)) >= 0)
			{
				/* bit #s are offset by FirstLowInvalidHeapAttributeNumber */
				AttrNumber	attno = col + FirstLowInvalidHeapAttributeNumber;

				if (attno == InvalidAttrNumber)
				{
					/* Whole-row reference, must have priv on all cols */
					if (pg_attribute_aclcheck_all(relOid, userid, ACL_SELECT,
												  ACLMASK_ALL) != ACLCHECK_OK)
						return false;
				}
				else
				{
					if (pg_attribute_aclcheck(relOid, attno, userid,
											  ACL_SELECT) != ACLCHECK_OK)
						return false;
				}
			}
		}

		/*
		 * Basically the same for the mod columns, for both INSERT and UPDATE
		 * privilege as specified by remainingPerms.
		 */
		if (remainingPerms & ACL_INSERT && !do_ExecCheckRTEPermsModified(relOid,
																	  userid,
																	  rte->insertedCols,
																	  ACL_INSERT))
			return false;

		if (remainingPerms & ACL_UPDATE && !do_ExecCheckRTEPermsModified(relOid,
																	  userid,
																	  rte->updatedCols,
																	  ACL_UPDATE))
			return false;
	}
	return true;
}

/*
 * Copy-and-paste from ExecCheckRTPermsModified() in
 * PostgreSQL 13dev source code.
 *
 * ExecCheckRTEPermsModified
 *		Check INSERT or UPDATE access permissions for a single RTE (these
 *		are processed uniformly).
 */
static bool
do_ExecCheckRTEPermsModified(Oid relOid, Oid userid, Bitmapset *modifiedCols,
						  AclMode requiredPerms)
{
	int			col = -1;

	/*
	 * When the query doesn't explicitly update any columns, allow the query
	 * if we have permission on any column of the rel.  This is to handle
	 * SELECT FOR UPDATE as well as possible corner cases in UPDATE.
	 */
	if (bms_is_empty(modifiedCols))
	{
		if (pg_attribute_aclcheck_all(relOid, userid, requiredPerms,
									  ACLMASK_ANY) != ACLCHECK_OK)
			return false;
	}

	while ((col = bms_next_member(modifiedCols, col)) >= 0)
	{
		/* bit #s are offset by FirstLowInvalidHeapAttributeNumber */
		AttrNumber	attno = col + FirstLowInvalidHeapAttributeNumber;

		if (attno == InvalidAttrNumber)
		{
			/* whole-row reference can't happen here */
			elog(ERROR, "whole-row update is not implemented");
		}
		else
		{
			if (pg_attribute_aclcheck(relOid, attno, userid,
									  requiredPerms) != ACLCHECK_OK)
				return false;
		}
	}
	return true;
}
