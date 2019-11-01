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

/* Current nesting depth of check_partitions_permissions calls */
static int	nested_level = 0;

void		_PG_init(void);
void		_PG_fini(void);

static bool check_partitions_permissions(List *rangeTable,
										 bool ereport_on_violation);

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

	if (nested_level > 0)
		return true;

	nested_level++;
	PG_TRY();
	{
		if (prev_ExecutorCheckPerms)
		{
			result = prev_ExecutorCheckPerms(rangeTable, ereport_on_violation);
			if (!result)
			{
				nested_level--;
				return result;
			}
		}

		foreach(l, rangeTable)
		{
			RangeTblEntry *rte = (RangeTblEntry *) lfirst(l);

			/*
			 * Don't check access permissions for objects except partitions
			 * with empty requiredPerms.
			 */
			if (rte->requiredPerms != 0 ||
				rte->relkind != RELKIND_RELATION ||
				!get_rel_relispartition(rte->relid))
				continue;

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
			rte = copyObject(rte);
			rte->requiredPerms |= ACL_SELECT;
			result = ExecCheckRTPerms(list_make1(rte), ereport_on_violation);
			if (!result)
			{
				nested_level--;
				return false;
			}
		}
	}
	PG_CATCH();
	{
		nested_level--;
		PG_RE_THROW();
	}
	PG_END_TRY();

	nested_level--;
	return result;
}
