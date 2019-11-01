-- Create superuser and non-superuser.
CREATE ROLE test_admin WITH SUPERUSER LOGIN;
CREATE ROLE test_user WITH NOSUPERUSER LOGIN;

-- Create partitioned table and partitions.
\connect - test_admin
CREATE TABLE test_tbl (id INT PRIMARY KEY, val TEXT) PARTITION BY RANGE(id);
CREATE TABLE test_tbl_1 PARTITION OF test_tbl FOR VALUES FROM (minvalue) TO (11);
CREATE TABLE test_tbl_2 PARTITION OF test_tbl FOR VALUES FROM (11) TO (21);
CREATE TABLE test_tbl_3 PARTITION OF test_tbl FOR VALUES FROM (21) TO (31);
CREATE TABLE test_tbl_4 PARTITION OF test_tbl FOR VALUES FROM (31) TO (maxvalue);
INSERT INTO test_tbl VALUES (generate_series(1, 40), 'test');

-- Confirm that non-superuser is not allowed to access to the tables
-- that superuser created, by default.
\connect - test_user
SELECT * FROM test_tbl WHERE id = 3;

-- Allow non-superuser to access to only partitioned table.
\connect - test_admin
GRANT ALL ON test_tbl TO test_user;

-- Confirm that non-superuser is allowed to access to the tables
-- via partitioned table, but not allowed to access to the partition directly.
\connect - test_user
SELECT * FROM test_tbl WHERE id = 3;
SELECT * FROM test_tbl_1 WHERE id = 3;

-- Configure so that this extension is loaded when connecting
-- as created non-suerpuser.
\connect - test_admin
ALTER ROLE test_user SET session_preload_libraries TO 'pg_check_partitions_permissions';

-- Confirm that non-superuser is not allowed to access to the tables
-- via partitioned table.
\connect - test_user
SELECT * FROM test_tbl WHERE id = 3;
SELECT * FROM test_tbl_1 WHERE id = 3;

-- Drop global objects used for regression test.
\connect - test_admin
DROP TABLE test_tbl;
DROP ROLE test_user;
