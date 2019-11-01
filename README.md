# pg_check_partitions_permissions
PostgreSQL extension to check access permissions for partitions.

pg_check_partitions_permissions is released under the [PostgreSQL License](https://opensource.org/licenses/postgresql), a liberal Open Source license, similar to the BSD or MIT licenses.

## Install

Download the source archive of pg_check_partitions_permissions from
[here](https://github.com/MasaoFujii/pg_check_partitions_permissions),
and then build and install it.

    $ cd pg_check_partitions_permissions
    $ make USE_PGXS=1 PG_CONFIG=/opt/pgsql-X.Y.Z/bin/pg_config
    $ su
    # make USE_PGXS=1 PG_CONFIG=/opt/pgsql-X.Y.Z/bin/pg_config install
    # exit

USE_PGXS=1 must be always specified when building this extension.
The path to [pg_config](http://www.postgresql.org/docs/devel/static/app-pgconfig.html)
(which exists in the bin directory of PostgreSQL installation)
needs be specified in PG_CONFIG.
However, if the PATH environment variable contains the path to pg_config,
PG_CONFIG doesn't need to be specified.

## Configure

[shared_preload_libraries](http://www.postgresql.org/docs/devel/static/runtime-config-client.html#GUC-SHARED-PRELOAD-LIBRARIES)
or [session_preload_libraries](http://www.postgresql.org/docs/devel/static/runtime-config-client.html#GUC-SESSION-PRELOAD-LIBRARIES)
(available in PostgreSQL 9.4 or later) must be set to 'pg_check_partitions_permissions'
in postgresql.conf.

## Copyright
Copyright (c) 2019, Fujii Masao
