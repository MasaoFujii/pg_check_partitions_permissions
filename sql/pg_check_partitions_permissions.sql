-- Create partitioned table and partitions.
CREATE TABLE test_tbl (id INT PRIMARY KEY, val TEXT) PARTITION BY RANGE(id);
CREATE TABLE test_tbl_1 PARTITION OF test_tbl FOR VALUES FROM (minvalue) TO (11);
CREATE TABLE test_tbl_2 PARTITION OF test_tbl FOR VALUES FROM (11) TO (21);
CREATE TABLE test_tbl_3 PARTITION OF test_tbl FOR VALUES FROM (21) TO (31);
CREATE TABLE test_tbl_4 PARTITION OF test_tbl FOR VALUES FROM (31) TO (maxvalue);
INSERT INTO test_tbl VALUES (generate_series(1, 40), 'test');

-- Create role with non-superuser privilege.
CREATE ROLE test_user WITH NOSUPERUSER LOGIN;

-- Confirm that non-superuser is not allowed to access to the tables
-- that superuser created, by default.
\connect - test_user
SELECT * FROM test_tbl WHERE id = 3;

-- Allow non-superuser to access to only partitioned table.
\connect - postgres
GRANT ALL ON test_tbl TO test_user;

-- Confirm that non-superuser is allowed to access to the tables
-- via partitioned table, but not allowed to access to the partition directly.
\connect - test_user
SELECT * FROM test_tbl WHERE id = 3;
SELECT * FROM test_tbl_1 WHERE id = 3;

-- Configure so that this extension is loaded when connecting
-- as created non-suerpuser.
\connect - postgres
ALTER ROLE test_user SET session_preload_libraries TO 'pg_check_partitions_permissions';

-- Confirm that non-superuser is not allowed to access to the tables
-- via partitioned table.
\connect - test_user
SELECT * FROM test_tbl WHERE id = 3;
SELECT * FROM test_tbl_1 WHERE id = 3;

-- Drop global objects used for regression test.
\connect - postgres
DROP TABLE test_tbl;
DROP ROLE test_user;
