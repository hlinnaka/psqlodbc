/*
 * Test two-phase commit using Microsoft DTS. (Windows-only).
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern "C" {
#include "common.h"
}

#include <transact.h>
#include <xolehlp.h>

SQLHDBC
test_connect_with_env(char *extraparams)
{
	SQLRETURN ret;
	SQLCHAR str[1024];
	SQLSMALLINT strl;
	SQLCHAR dsn[1024];
	SQLHDBC result;

	snprintf((char *) dsn, sizeof(dsn), "DSN=psqlodbc_test_dsn;%s",
			 extraparams ? extraparams : (char *) "");

	SQLAllocHandle(SQL_HANDLE_DBC, env, &result);
	ret = SQLDriverConnect(result, NULL, dsn, SQL_NTS,
						   str, sizeof(str), &strl,
						   SQL_DRIVER_COMPLETE);
	if (SQL_SUCCEEDED(ret)) {
		printf("connected\n");
	} else {
		print_diag((char *) "SQLDriverConnect failed.", SQL_HANDLE_DBC, result);
		exit(1);
	}

	return result;
}

int main(int argc, char **argv)
{
	int rc;
	SQLHDBC conn1;
	SQLHDBC conn2;
	HSTMT hstmt1 = SQL_NULL_HSTMT;
	HSTMT hstmt2 = SQL_NULL_HSTMT;
	ITransactionDispenser *xactDispenser;
	ITransaction *xact;
	HRESULT hr;

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	/* Get a pointer to the Transaction dispenser */
	hr = DtcGetTransactionManager(
		NULL,              // [in] char * pszHost,
		NULL,              // [in] char * pszTmName,
		IID_ITransactionDispenser,    // [in] REFIID riid,
		0,                // [in] DWORD dwReserved1,
		0,                // [in] WORD wcbVarLenReserved2,
		(void *)NULL,          // [in] void * pvVarDataReserved2,
		(void **)&xactDispenser // [out] void ** ppv
		);
	if (FAILED (hr)) {
		printf("DtcGetTransactionManager call failed: Error # %#x\n", hr);
		exit(1); // Replace with specific error handling.
	}

	test_connect();
	conn1 = conn;

	conn2 = test_connect_with_env(NULL);

	/* Begin a DTC transaction */
	hr = xactDispenser->BeginTransaction(
		NULL,           // [in] IUnknown * punkOuter,
		ISOLATIONLEVEL_ISOLATED, // [in] ISOLEVEL isoLevel,
		ISOFLAG_RETAIN_DONTCARE, // [in] ULONG isoFlags,
		NULL,           // [in] ITransactionOptions * pOptions,
		&xact       // [out] ITransaction * ppTransaction
		);
	if (FAILED (hr)) {
		printf("BeginTransaction failed: Error # %#x\n", hr);
		exit(1); // Replace with specific error handling.
	}


	/* Enlist the transaction with both connections */

	rc = SQLSetConnectAttr(conn1, SQL_ATTR_ENLIST_IN_DTC, (SQLPOINTER) xact, NULL);
	if (!((SQL_SUCCESS == rc) || (SQL_SUCCESS_WITH_INFO == rc)))
    {
		print_diag((char *) "SQLSetConnectAttr failed", SQL_HANDLE_DBC, conn1);
		exit(1);
	}

	rc = SQLSetConnectAttr (conn2, SQL_ATTR_ENLIST_IN_DTC, (SQLPOINTER) xact, NULL);
	if (!((SQL_SUCCESS == rc) || (SQL_SUCCESS_WITH_INFO == rc)))
	{
		print_diag((char *) "SQLSetConnectAttr failed", SQL_HANDLE_DBC, conn2);
		exit(1);
	}

	/* Ok, run queries in both databases */
	rc = SQLAllocHandle(SQL_HANDLE_STMT, conn1, &hstmt1);
	if (!SQL_SUCCEEDED(rc))
	{
		print_diag((char *) "failed to allocate stmt handle", SQL_HANDLE_DBC, conn1);
		exit(1);
	}
	rc = SQLExecDirect(hstmt1, (SQLCHAR *) "SELECT 1 UNION ALL SELECT 2", SQL_NTS);
	CHECK_STMT_RESULT(rc, (char *) "SQLExecDirect failed", hstmt1);
	print_result(hstmt1);

	rc = SQLFreeStmt(hstmt1, SQL_CLOSE);
	CHECK_STMT_RESULT(rc, (char *) "SQLFreeStmt failed", hstmt1);

	rc = SQLAllocHandle(SQL_HANDLE_STMT, conn2, &hstmt2);
	if (!SQL_SUCCEEDED(rc))
	{
		print_diag((char *) "failed to allocate stmt handle", SQL_HANDLE_DBC, conn2);
		exit(1);
	}
	rc = SQLExecDirect(hstmt2, (SQLCHAR *) "SELECT 1 UNION ALL SELECT 2", SQL_NTS);
	CHECK_STMT_RESULT(rc, (char *) "SQLExecDirect failed", hstmt2);
	print_result(hstmt2);

	rc = SQLFreeStmt(hstmt2, SQL_CLOSE);
	CHECK_STMT_RESULT(rc, (char *) "SQLFreeStmt failed", hstmt2);

	/* Commit the distributed transaction */
	hr = xact->Commit(FALSE, XACTTC_SYNC_PHASEONE, 0);
	if (FAILED(hr))
	{
		fprintf(stderr, "pTransaction->Commit() failed: Error # %#x\n", hr);
		exit(1);
	}

	/* Release the transaction object. */
	xact->Release();

	/* Clean up */
	xactDispenser->Release();

	printf("disconnecting\n");
	rc = SQLDisconnect(conn1);
	if (!SQL_SUCCEEDED(rc))
	{
		print_diag((char *) "SQLDisconnect failed", SQL_HANDLE_DBC, conn1);
		exit(1);
	}
	rc = SQLDisconnect(conn2);
	if (!SQL_SUCCEEDED(rc))
	{
		print_diag((char *) "SQLDisconnect failed", SQL_HANDLE_DBC, conn2);
		exit(1);
	}

	printf("finished!\n");
	return 0;
}
