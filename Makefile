MODULE_big = pg_check_partitions_permissions
OBJS = pg_check_partitions_permissions.o

PGFILEDESC = "pg_check_partitions_permissions - check access permissions for partitions"

REGRESS = pg_check_partitions_permissions

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/pg_check_partitions_permissions
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
