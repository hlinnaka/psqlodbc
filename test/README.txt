This directory contains a regression test suite for the psqlODBC driver.

Prerequisites
-------------

To run the regression tests, you must have a PostgreSQL server running and
accepting connections at port 5432. You must have the PostgreSQL server
binaries, including the regression test driver pg_regress, in your $PATH.

By default in Linux, the regression tests use the driver built from the
parent directory, ../.libs/psqlodbcw.so, for the tests. You can edit
odbcinst.ini in this directory to test a different version.

Running the tests
-----------------

Linux
=====

To run the test suite, type:

  make installcheck

The PostgreSQL username used for the test is determined by the normal ODBC /
libpq rules. You can set the PGUSER environment variable or .pgpass to
override.

Windows
=======

To run the test suite, you need first to build and install the driver, and
create a Data Source with the name "psqlodbc_test_dsn". This DSN is used by
all the regression tests, and it should point to a valid PostgrSQL server,
to a database called "contrib_regression".

During development, it's useful to use a driver specifically registered
to point to the output directory of the build. For example:

  odbcconf INSTALLDRIVER "psqlodbc_test_driver|Driver=C:\psqlodbc\x64_ANSI_Debug\psqlodbc30a.dll"

To create the required data source, using that driver:

  odbcconf CONFIGDSN "psqlodbc_test_driver" "DSN=psqlodbc_test_dsn|Description=psqlodbc regression tests|Database=contrib_regression|Servername=localhost"

NOTE: The above commands must be run as Administrator. Odbcconf will not
give any error message if you don't have sufficient privileges!

The Windows test suite makes use of the property PG_BIN in the
winbuild/configuration-defaults.props or winbuild/configuration-local.props
file, to find the pg_regress binary. Note that psql version 9.4 or above is
required!

If the PostgreSQL server is not running locally, at the default port, you
will also need to pass extra options to specify the hostname and port of the
same server that the DSN points to.

After setting up the psqlodbc_test_dsn, and building the driver itself, you
can run the regression tests with the command:

  msbuild /t:installcheck

or if the server is not running locally:

  msbuild /t:installcheck /p:REGRESSOPTS=--host=myserver.mydomain

Development
-----------

To add a test, add a *-test.c file to src/ directory, using one of the
existing tests as example. Also add the test to the TESTS list in the
"tests" file, and create an expected output file in expected/ directory.

The current test suite only tests a small fraction of the codebase. Whenever
you add a new feature, or fix a non-trivial bug, please add a test case to
cover it.
