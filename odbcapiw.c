/*-------
 * Module:			odbcapiw.c
 *
 * Description:		This module contains UNICODE routines
 *
 * Classes:			n/a
 *
 * API functions:	SQLColumnPrivilegesW, SQLColumnsW,
 			SQLConnectW, SQLDataSourcesW, SQLDescribeColW,
			SQLDriverConnectW, SQLExecDirectW,
			SQLForeignKeysW,
			SQLGetCursorNameW, SQLGetInfoW, SQLNativeSqlW,
			SQLPrepareW, SQLPrimaryKeysW, SQLProcedureColumnsW,
			SQLProceduresW, SQLSetCursorNameW,
			SQLSpecialColumnsW, SQLStatisticsW, SQLTablesW,
			SQLTablePrivilegesW, SQLGetTypeInfoW
 *-------
 */

#include "psqlodbc.h"
#include <stdio.h>
#include <string.h>

#include "pgapifunc.h"
#include "connection.h"
#include "statement.h"

RETCODE  SQL_API SQLColumnsW(HSTMT StatementHandle,
           SQLWCHAR *CatalogName, SQLSMALLINT NameLength1,
           SQLWCHAR *SchemaName, SQLSMALLINT NameLength2,
           SQLWCHAR *TableName, SQLSMALLINT NameLength3,
           SQLWCHAR *ColumnName, SQLSMALLINT NameLength4)
{
	CSTR func = "SQLColumnsW";
	RETCODE	ret;
	char	*ctName, *scName, *tbName, *clName;
	SQLLEN	nmlen1, nmlen2, nmlen3, nmlen4;
	StatementClass *stmt = (StatementClass *) StatementHandle;
	ConnectionClass *conn;
	BOOL	lower_id;
	UWORD	flag = PODBC_SEARCH_PUBLIC_SCHEMA; 

	mylog("[%s]", func);
	conn = SC_get_conn(stmt);
	lower_id = SC_is_lower_case(stmt, conn);
	ctName = ucs2_to_utf8(CatalogName, NameLength1, &nmlen1, lower_id);
	scName = ucs2_to_utf8(SchemaName, NameLength2, &nmlen2, lower_id);
	tbName = ucs2_to_utf8(TableName, NameLength3, &nmlen3, lower_id);
	clName = ucs2_to_utf8(ColumnName, NameLength4, &nmlen4, lower_id);
	ENTER_STMT_CS(stmt);
	SC_clear_error(stmt);
	StartRollbackState(stmt);
#if (ODBCVER >= 0x0300)
	if (stmt->options.metadata_id)
		flag |= PODBC_NOT_SEARCH_PATTERN;
#endif
	if (SC_opencheck(stmt, func))
		ret = SQL_ERROR;
	else
		ret = PGAPI_Columns(StatementHandle, ctName, (SQLSMALLINT) nmlen1,
           		scName, (SQLSMALLINT) nmlen2, tbName, (SQLSMALLINT) nmlen3,
           		clName, (SQLSMALLINT) nmlen4, flag, 0, 0);
	ret = DiscardStatementSvp(stmt, ret, FALSE);
	LEAVE_STMT_CS(stmt);
	if (ctName)
		free(ctName);
	if (scName)
		free(scName);
	if (tbName)
		free(tbName);
	if (clName)
		free(clName);
	return ret;
}


RETCODE  SQL_API SQLConnectW(HDBC ConnectionHandle,
           SQLWCHAR *ServerName, SQLSMALLINT NameLength1,
           SQLWCHAR *UserName, SQLSMALLINT NameLength2,
           SQLWCHAR *Authentication, SQLSMALLINT NameLength3)
{
	CSTR func = "SQLConnectW";
	char	*svName, *usName, *auth;
	SQLLEN	nmlen1, nmlen2, nmlen3;
	RETCODE	ret;
	ConnectionClass *conn = (ConnectionClass *) ConnectionHandle;
	
	mylog("[%s]", func);
	ENTER_CONN_CS(conn);
	CC_clear_error(conn);
	CC_set_in_unicode_driver(conn);
	svName = ucs2_to_utf8(ServerName, NameLength1, &nmlen1, FALSE);
	usName = ucs2_to_utf8(UserName, NameLength2, &nmlen2, FALSE);
	auth = ucs2_to_utf8(Authentication, NameLength3, &nmlen3, FALSE);
	ret = PGAPI_Connect(ConnectionHandle, svName, (SQLSMALLINT) nmlen1,
           	usName, (SQLSMALLINT) nmlen2, auth, (SQLSMALLINT) nmlen3);
	LEAVE_CONN_CS(conn);
	if (svName)
		free(svName);
	if (usName)
		free(usName);
	if (auth)
		free(auth);
	return ret;
}

RETCODE SQL_API SQLDriverConnectW(HDBC hdbc,
                                 HWND hwnd,
                                 SQLWCHAR *szConnStrIn,
                                 SQLSMALLINT cbConnStrIn,
                                 SQLWCHAR *szConnStrOut,
                                 SQLSMALLINT cbConnStrOutMax,
                                 SQLSMALLINT FAR *pcbConnStrOut,
                                 SQLUSMALLINT fDriverCompletion)
{
	CSTR func = "SQLDriverConnectW";
	char	*szIn, *szOut = NULL;
	SQLSMALLINT	maxlen, obuflen = 0;
	SQLLEN		inlen;
	SQLSMALLINT	olen, *pCSO;
	RETCODE	ret;
	ConnectionClass *conn = (ConnectionClass *) hdbc;

	mylog("[%s]", func);
	ENTER_CONN_CS(conn);
	CC_clear_error(conn);
	CC_set_in_unicode_driver(conn);
	szIn = ucs2_to_utf8(szConnStrIn, cbConnStrIn, &inlen, FALSE);
	maxlen = cbConnStrOutMax;
	pCSO = NULL;
	olen = 0;
	if (maxlen > 0)
	{
		obuflen = maxlen + 1;
		szOut = malloc(obuflen);
		pCSO = &olen;
	}
	else if (pcbConnStrOut)
		pCSO = &olen;
	ret = PGAPI_DriverConnect(hdbc, hwnd, szIn, (SQLSMALLINT) inlen,
		szOut, maxlen, pCSO, fDriverCompletion);
	if (ret != SQL_ERROR && NULL != pCSO)
	{
		SQLLEN outlen = olen;

		if (olen < obuflen)
			outlen = utf8_to_ucs2(szOut, olen, szConnStrOut, cbConnStrOutMax);
		else
			utf8_to_ucs2(szOut, maxlen, szConnStrOut, cbConnStrOutMax);
		if (outlen >= cbConnStrOutMax)
		{
inolog("cbConnstrOutMax=%d pcb=%p\n", cbConnStrOutMax, pcbConnStrOut);
			if (SQL_SUCCESS == ret)
			{
				CC_set_error(conn, CONN_TRUNCATED, "the ConnStrOut is too small", func);
				ret = SQL_SUCCESS_WITH_INFO;
			}
		}
		if (pcbConnStrOut)
			*pcbConnStrOut = (SQLSMALLINT) outlen;
	}
	LEAVE_CONN_CS(conn);
	if (szOut)
		free(szOut);
	if (szIn)
		free(szIn);
	return ret;
}
RETCODE SQL_API SQLBrowseConnectW(
    HDBC            hdbc,
    SQLWCHAR 		  *szConnStrIn,
    SQLSMALLINT        cbConnStrIn,
    SQLWCHAR 		  *szConnStrOut,
    SQLSMALLINT        cbConnStrOutMax,
    SQLSMALLINT       *pcbConnStrOut)
{
	CSTR func = "SQLBrowseConnectW";
	char	*szIn, *szOut;
	SQLLEN		inlen;
	SQLUSMALLINT	obuflen;
	SQLSMALLINT	olen;
	RETCODE	ret;
	ConnectionClass *conn = (ConnectionClass *) hdbc;

	mylog("[%s]", func);
	ENTER_CONN_CS(conn);
	CC_clear_error(conn);
	CC_set_in_unicode_driver(conn);
	szIn = ucs2_to_utf8(szConnStrIn, cbConnStrIn, &inlen, FALSE);
	obuflen = cbConnStrOutMax + 1;
	szOut = malloc(obuflen);
	ret = PGAPI_BrowseConnect(hdbc, szIn, (SQLSMALLINT) inlen,
		szOut, cbConnStrOutMax, &olen);
	LEAVE_CONN_CS(conn);
	if (ret != SQL_ERROR)
	{
		SQLLEN	outlen = utf8_to_ucs2(szOut, olen, szConnStrOut, cbConnStrOutMax);
		if (pcbConnStrOut)
			*pcbConnStrOut = (SQLSMALLINT) outlen;
	}
	free(szOut);
	if (szIn)
		free(szIn);
	return ret;
}

RETCODE  SQL_API SQLDataSourcesW(HENV EnvironmentHandle,
           SQLUSMALLINT Direction, SQLWCHAR *ServerName,
           SQLSMALLINT BufferLength1, SQLSMALLINT *NameLength1,
           SQLWCHAR *Description, SQLSMALLINT BufferLength2,
           SQLSMALLINT *NameLength2)
{
	CSTR func = "SQLDataSourcesW";
	mylog("[%s]", func);
	/*
	return PGAPI_DataSources(EnvironmentHandle, Direction, ServerName,
		 BufferLength1, NameLength1, Description, BufferLength2,
           	NameLength2);
	*/
	return SQL_ERROR;
}

RETCODE  SQL_API SQLDescribeColW(HSTMT StatementHandle,
           SQLUSMALLINT ColumnNumber, SQLWCHAR *ColumnName,
           SQLSMALLINT BufferLength, SQLSMALLINT *NameLength,
           SQLSMALLINT *DataType, SQLULEN *ColumnSize,
           SQLSMALLINT *DecimalDigits, SQLSMALLINT *Nullable)
{
	CSTR func = "SQLDescribeColW";
	RETCODE	ret;
	StatementClass	*stmt = (StatementClass *) StatementHandle;
	SQLSMALLINT	buflen, nmlen;
	char	*clName = NULL;

	mylog("[%s]", func);
	buflen = 0;
	if (BufferLength > 0)
		buflen = BufferLength * 3;
	else if (NameLength)
		buflen = 32;
	if (buflen > 0)
		clName = malloc(buflen);
	ENTER_STMT_CS(stmt);
	SC_clear_error(stmt);
	StartRollbackState(stmt);
	for (;; buflen = nmlen + 1, clName = realloc(clName, buflen))
	{
		ret = PGAPI_DescribeCol(StatementHandle, ColumnNumber,
			clName, buflen, &nmlen, DataType, ColumnSize,
			DecimalDigits, Nullable);
		if (SQL_SUCCESS_WITH_INFO != ret || nmlen < buflen)
			break;
	}
	if (SQL_SUCCEEDED(ret))
	{
		SQLLEN	nmcount = nmlen;

		if (nmlen < buflen)
			nmcount = utf8_to_ucs2(clName, nmlen, ColumnName, BufferLength);
		if (SQL_SUCCESS == ret && BufferLength > 0 && nmcount > BufferLength)
		{
			ret = SQL_SUCCESS_WITH_INFO;
			SC_set_error(stmt, STMT_TRUNCATED, "Column name too large", func);
		}
		if (NameLength)
			*NameLength = (SQLSMALLINT) nmcount;
	}
	ret = DiscardStatementSvp(stmt, ret, FALSE);
	LEAVE_STMT_CS(stmt);
	if (clName)
		free(clName); 
	return ret;
}

RETCODE  SQL_API SQLExecDirectW(HSTMT StatementHandle,
           SQLWCHAR *StatementText, SQLINTEGER TextLength)
{
	CSTR	func = "SQLExecDirectW";
	RETCODE	ret;
	char	*stxt;
	SQLLEN	slen;
	StatementClass	*stmt = (StatementClass *) StatementHandle;
	UWORD	flag = 0;

	mylog("[%s]", func);
	stxt = ucs2_to_utf8(StatementText, TextLength, &slen, FALSE);
	ENTER_STMT_CS(stmt);
	SC_clear_error(stmt);
	if (PG_VERSION_GE(SC_get_conn(stmt), 7.4))
		flag |= PODBC_WITH_HOLD;
	StartRollbackState(stmt);
	if (SC_opencheck(stmt, func))
		ret = SQL_ERROR;
	else
		ret = PGAPI_ExecDirect(StatementHandle, stxt, (SQLINTEGER) slen, flag);
	ret = DiscardStatementSvp(stmt, ret, FALSE);
	LEAVE_STMT_CS(stmt);
	if (stxt)
		free(stxt);
	return ret;
}

RETCODE  SQL_API SQLGetCursorNameW(HSTMT StatementHandle,
           SQLWCHAR *CursorName, SQLSMALLINT BufferLength,
           SQLSMALLINT *NameLength)
{
	CSTR func = "SQLGetCursorNameW";
	RETCODE	ret;
	StatementClass * stmt = (StatementClass *) StatementHandle;
	char	*crName;
	SQLSMALLINT	clen, buflen;

	mylog("[%s]", func);
	if (BufferLength > 0)
		buflen = BufferLength * 3;
	else
		buflen = 32;
	crName = malloc(buflen);
	ENTER_STMT_CS(stmt);
	SC_clear_error(stmt);
	StartRollbackState(stmt);
	for (;; buflen = clen + 1, crName = realloc(crName, buflen))
	{
		ret = PGAPI_GetCursorName(StatementHandle, crName, buflen, &clen);
		if (SQL_SUCCESS_WITH_INFO != ret || clen < buflen)
			break;
	}
	if (SQL_SUCCEEDED(ret))
	{
		SQLLEN	nmcount = clen;

		if (clen < buflen)
			nmcount = utf8_to_ucs2(crName, clen, CursorName, BufferLength);
		if (SQL_SUCCESS == ret && nmcount > BufferLength)
		{
			ret = SQL_SUCCESS_WITH_INFO;
			SC_set_error(stmt, STMT_TRUNCATED, "Cursor name too large", func);
		}
		if (NameLength)
			*NameLength = (SQLSMALLINT) nmcount;
	}
	ret = DiscardStatementSvp(stmt, ret, FALSE);
	LEAVE_STMT_CS(stmt);
	free(crName);
	return ret;
}

RETCODE  SQL_API SQLGetInfoW(HDBC ConnectionHandle,
           SQLUSMALLINT InfoType, PTR InfoValue,
           SQLSMALLINT BufferLength, SQLSMALLINT *StringLength)
{
	CSTR func = "SQLGetInfoW";
	ConnectionClass	*conn = (ConnectionClass *) ConnectionHandle;
	RETCODE	ret;

	ENTER_CONN_CS(conn);
	CC_set_in_unicode_driver(conn);
	CC_clear_error(conn);
#if (ODBCVER >= 0x0300)
	mylog("[%s(30)]", func);
	if ((ret = PGAPI_GetInfo(ConnectionHandle, InfoType, InfoValue,
           	BufferLength, StringLength)) == SQL_ERROR)
	{
		if (conn->driver_version >= 0x0300)
		{
			CC_clear_error(conn);
			ret = PGAPI_GetInfo30(ConnectionHandle, InfoType, InfoValue,
           			BufferLength, StringLength);
		}
	}
	if (SQL_ERROR == ret)
		CC_log_error("SQLGetInfoW(30)", "", conn);
#else
	mylog("[%s]", func);
	ret = PGAPI_GetInfo(ConnectionHandle, InfoType, InfoValue,
           	BufferLength, StringLength);
	if (SQL_ERROR == ret)
		CC_log_error("SQLGetInfoW", "", conn);
#endif
	LEAVE_CONN_CS(conn);
	return ret;
}

RETCODE  SQL_API SQLPrepareW(HSTMT StatementHandle,
           SQLWCHAR *StatementText, SQLINTEGER TextLength)
{
	CSTR func = "SQLPrepareW";
	StatementClass *stmt = (StatementClass *) StatementHandle;
	RETCODE	ret;
	char	*stxt;
	SQLLEN	slen;

	mylog("[%s]", func);
	stxt = ucs2_to_utf8(StatementText, TextLength, &slen, FALSE);
	ENTER_STMT_CS(stmt);
	SC_clear_error(stmt);
	StartRollbackState(stmt);
	ret = PGAPI_Prepare(StatementHandle, stxt, (SQLINTEGER) slen);
	ret = DiscardStatementSvp(stmt, ret, FALSE);
	LEAVE_STMT_CS(stmt);
	if (stxt)
		free(stxt);
	return ret;
}

RETCODE  SQL_API SQLSetCursorNameW(HSTMT StatementHandle,
           SQLWCHAR *CursorName, SQLSMALLINT NameLength)
{
	CSTR func = "SQLSetCursorNameW";
	RETCODE	ret;
	StatementClass *stmt = (StatementClass *) StatementHandle;
	char	*crName;
	SQLLEN	nlen;

	mylog("[%s]", func);
	crName = ucs2_to_utf8(CursorName, NameLength, &nlen, FALSE);
	ENTER_STMT_CS(stmt);
	SC_clear_error(stmt);
	StartRollbackState(stmt);
	ret = PGAPI_SetCursorName(StatementHandle, crName, (SQLSMALLINT) nlen);
	ret = DiscardStatementSvp(stmt, ret, FALSE);
	LEAVE_STMT_CS(stmt);
	if (crName)
		free(crName);
	return ret;
}

RETCODE  SQL_API SQLSpecialColumnsW(HSTMT StatementHandle,
           SQLUSMALLINT IdentifierType, SQLWCHAR *CatalogName,
           SQLSMALLINT NameLength1, SQLWCHAR *SchemaName,
           SQLSMALLINT NameLength2, SQLWCHAR *TableName,
           SQLSMALLINT NameLength3, SQLUSMALLINT Scope,
           SQLUSMALLINT Nullable)
{
	CSTR func = "SQLSpecialColumnsW";
	RETCODE	ret;
	char	*ctName, *scName, *tbName;
	SQLLEN	nmlen1, nmlen2, nmlen3;
	StatementClass *stmt = (StatementClass *) StatementHandle;
	ConnectionClass *conn;
	BOOL lower_id;
	
	mylog("[%s]", func);
	conn = SC_get_conn(stmt);
	lower_id = SC_is_lower_case(stmt, conn);
	ctName = ucs2_to_utf8(CatalogName, NameLength1, &nmlen1, lower_id);
	scName = ucs2_to_utf8(SchemaName, NameLength2, &nmlen2, lower_id);
	tbName = ucs2_to_utf8(TableName, NameLength3, &nmlen3, lower_id);
	ENTER_STMT_CS(stmt);
	SC_clear_error(stmt);
	StartRollbackState(stmt);
	if (SC_opencheck(stmt, func))
		ret = SQL_ERROR;
	else
		ret = PGAPI_SpecialColumns(StatementHandle, IdentifierType, ctName,
           	(SQLSMALLINT) nmlen1, scName, (SQLSMALLINT) nmlen2, tbName, (SQLSMALLINT) nmlen3,
		Scope, Nullable);
	ret = DiscardStatementSvp(stmt, ret, FALSE);
	LEAVE_STMT_CS(stmt);
	if (ctName)
		free(ctName);
	if (scName)
		free(scName);
	if (tbName)
		free(tbName);
	return ret;
}

RETCODE  SQL_API SQLStatisticsW(HSTMT StatementHandle,
           SQLWCHAR *CatalogName, SQLSMALLINT NameLength1,
           SQLWCHAR *SchemaName, SQLSMALLINT NameLength2,
           SQLWCHAR *TableName, SQLSMALLINT NameLength3,
           SQLUSMALLINT Unique, SQLUSMALLINT Reserved)
{
	CSTR func = "SQLStatisticsW";
	RETCODE	ret;
	char	*ctName, *scName, *tbName;
	SQLLEN	nmlen1, nmlen2, nmlen3;
	StatementClass *stmt = (StatementClass *) StatementHandle;
	ConnectionClass *conn;
	BOOL lower_id;

	mylog("[%s]", func);
	conn = SC_get_conn(stmt);
	lower_id = SC_is_lower_case(stmt, conn);
	ctName = ucs2_to_utf8(CatalogName, NameLength1, &nmlen1, lower_id);
	scName = ucs2_to_utf8(SchemaName, NameLength2, &nmlen2, lower_id);
	tbName = ucs2_to_utf8(TableName, NameLength3, &nmlen3, lower_id);
	ENTER_STMT_CS(stmt);
	SC_clear_error(stmt);
	StartRollbackState(stmt);
	if (SC_opencheck(stmt, func))
		ret = SQL_ERROR;
	else
		ret = PGAPI_Statistics(StatementHandle, ctName, (SQLSMALLINT) nmlen1,
           scName, (SQLSMALLINT) nmlen2, tbName, (SQLSMALLINT) nmlen3, Unique,
		Reserved);
	ret = DiscardStatementSvp(stmt, ret, FALSE);
	LEAVE_STMT_CS(stmt);
	if (ctName)
		free(ctName);
	if (scName)
		free(scName);
	if (tbName)
		free(tbName);
	return ret;
}

RETCODE  SQL_API SQLTablesW(HSTMT StatementHandle,
           SQLWCHAR *CatalogName, SQLSMALLINT NameLength1,
           SQLWCHAR *SchemaName, SQLSMALLINT NameLength2,
           SQLWCHAR *TableName, SQLSMALLINT NameLength3,
           SQLWCHAR *TableType, SQLSMALLINT NameLength4)
{
	CSTR func = "SQLTablesW";
	RETCODE	ret;
	char	*ctName, *scName, *tbName, *tbType;
	SQLLEN	nmlen1, nmlen2, nmlen3, nmlen4;
	StatementClass *stmt = (StatementClass *) StatementHandle;
	ConnectionClass *conn;
	BOOL lower_id;
	UWORD	flag = 0;

	mylog("[%s]", func);
	conn = SC_get_conn(stmt);
	lower_id = SC_is_lower_case(stmt, conn);
	ctName = ucs2_to_utf8(CatalogName, NameLength1, &nmlen1, lower_id);
	scName = ucs2_to_utf8(SchemaName, NameLength2, &nmlen2, lower_id);
	tbName = ucs2_to_utf8(TableName, NameLength3, &nmlen3, lower_id);
	tbType = ucs2_to_utf8(TableType, NameLength4, &nmlen4, FALSE);
	ENTER_STMT_CS(stmt);
	SC_clear_error(stmt);
	StartRollbackState(stmt);
#if (ODBCVER >= 0x0300)
	if (stmt->options.metadata_id)
		flag |= PODBC_NOT_SEARCH_PATTERN;
#endif
	if (SC_opencheck(stmt, func))
		ret = SQL_ERROR;
	else
		ret = PGAPI_Tables(StatementHandle, ctName, (SQLSMALLINT) nmlen1,
           		scName, (SQLSMALLINT) nmlen2, tbName, (SQLSMALLINT) nmlen3,
           		tbType, (SQLSMALLINT) nmlen4, flag);
	ret = DiscardStatementSvp(stmt, ret, FALSE);
	LEAVE_STMT_CS(stmt);
	if (ctName)
		free(ctName);
	if (scName)
		free(scName);
	if (tbName)
		free(tbName);
	if (tbType)
		free(tbType);
	return ret;
}

RETCODE SQL_API SQLColumnPrivilegesW(
    HSTMT           hstmt,
    SQLWCHAR 		  *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLWCHAR 		  *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLWCHAR 		  *szTableName,
    SQLSMALLINT        cbTableName,
    SQLWCHAR 		  *szColumnName,
    SQLSMALLINT        cbColumnName)
{
	CSTR func = "SQLColumnPrivilegesW";
	RETCODE	ret;
	char	*ctName, *scName, *tbName, *clName;
	SQLLEN	nmlen1, nmlen2, nmlen3, nmlen4;
	StatementClass *stmt = (StatementClass *) hstmt;
	ConnectionClass *conn;
	BOOL	lower_id;
	UWORD	flag = 0; 

	mylog("[%s]", func);
	conn = SC_get_conn(stmt);
	lower_id = SC_is_lower_case(stmt, conn);
	ctName = ucs2_to_utf8(szCatalogName, cbCatalogName, &nmlen1, lower_id);
	scName = ucs2_to_utf8(szSchemaName, cbSchemaName, &nmlen2, lower_id);
	tbName = ucs2_to_utf8(szTableName, cbTableName, &nmlen3, lower_id);
	clName = ucs2_to_utf8(szColumnName, cbColumnName, &nmlen4, lower_id);
	ENTER_STMT_CS(stmt);
	SC_clear_error(stmt);
	StartRollbackState(stmt);
#if (ODBCVER >= 0x0300)
	if (stmt->options.metadata_id)
		flag |= PODBC_NOT_SEARCH_PATTERN;
#endif
	if (SC_opencheck(stmt, func))
		ret = SQL_ERROR;
	else
		ret = PGAPI_ColumnPrivileges(hstmt, ctName, (SQLSMALLINT) nmlen1,
			scName, (SQLSMALLINT) nmlen2, tbName, (SQLSMALLINT) nmlen3,
			clName, (SQLSMALLINT) nmlen4, flag);
	ret = DiscardStatementSvp(stmt, ret, FALSE);
	LEAVE_STMT_CS(stmt);
	if (ctName)
		free(ctName);
	if (scName)
		free(scName);
	if (tbName)
		free(tbName);
	if (clName)
		free(clName);
	return ret;
}

RETCODE SQL_API SQLForeignKeysW(
    HSTMT           hstmt,
    SQLWCHAR 		  *szPkCatalogName,
    SQLSMALLINT        cbPkCatalogName,
    SQLWCHAR 		  *szPkSchemaName,
    SQLSMALLINT        cbPkSchemaName,
    SQLWCHAR 		  *szPkTableName,
    SQLSMALLINT        cbPkTableName,
    SQLWCHAR 		  *szFkCatalogName,
    SQLSMALLINT        cbFkCatalogName,
    SQLWCHAR 		  *szFkSchemaName,
    SQLSMALLINT        cbFkSchemaName,
    SQLWCHAR 		  *szFkTableName,
    SQLSMALLINT        cbFkTableName)
{
	CSTR func = "SQLForeignKeysW";
	RETCODE	ret;
	char	*ctName, *scName, *tbName, *fkctName, *fkscName, *fktbName;
	SQLLEN	nmlen1, nmlen2, nmlen3, nmlen4, nmlen5, nmlen6;
	StatementClass *stmt = (StatementClass *) hstmt;
	ConnectionClass *conn;
	BOOL	lower_id; 

	mylog("[%s]", func);
	conn = SC_get_conn(stmt);
	lower_id = SC_is_lower_case(stmt, conn);
	ctName = ucs2_to_utf8(szPkCatalogName, cbPkCatalogName, &nmlen1, lower_id);
	scName = ucs2_to_utf8(szPkSchemaName, cbPkSchemaName, &nmlen2, lower_id);
	tbName = ucs2_to_utf8(szPkTableName, cbPkTableName, &nmlen3, lower_id);
	fkctName = ucs2_to_utf8(szFkCatalogName, cbFkCatalogName, &nmlen4, lower_id);
	fkscName = ucs2_to_utf8(szFkSchemaName, cbFkSchemaName, &nmlen5, lower_id);
	fktbName = ucs2_to_utf8(szFkTableName, cbFkTableName, &nmlen6, lower_id);
	ENTER_STMT_CS(stmt);
	SC_clear_error(stmt);
	StartRollbackState(stmt);
	if (SC_opencheck(stmt, func))
		ret = SQL_ERROR;
	else
		ret = PGAPI_ForeignKeys(hstmt, ctName, (SQLSMALLINT) nmlen1,
			scName, (SQLSMALLINT) nmlen2, tbName, (SQLSMALLINT) nmlen3,
			fkctName, (SQLSMALLINT) nmlen4, fkscName, (SQLSMALLINT) nmlen5,
			fktbName, (SQLSMALLINT) nmlen6);
	ret = DiscardStatementSvp(stmt, ret, FALSE);
	LEAVE_STMT_CS(stmt);
	if (ctName)
		free(ctName);
	if (scName)
		free(scName);
	if (tbName)
		free(tbName);
	if (fkctName)
		free(fkctName);
	if (fkscName)
		free(fkscName);
	if (fktbName)
		free(fktbName);
	return ret;
}

RETCODE SQL_API SQLNativeSqlW(
    HDBC            hdbc,
    SQLWCHAR 		  *szSqlStrIn,
    SQLINTEGER         cbSqlStrIn,
    SQLWCHAR 		  *szSqlStr,
    SQLINTEGER         cbSqlStrMax,
    SQLINTEGER 		  *pcbSqlStr)
{
	CSTR func = "SQLNativeSqlW";
	RETCODE		ret;
	char		*szIn, *szOut = NULL;
	SQLLEN		slen;
	SQLINTEGER	buflen, olen;
	ConnectionClass *conn = (ConnectionClass *) hdbc;

	mylog("[%s}", func);
	ENTER_CONN_CS(conn);
	CC_clear_error(conn);
	CC_set_in_unicode_driver(conn);
	szIn = ucs2_to_utf8(szSqlStrIn, cbSqlStrIn, &slen, FALSE);
	buflen = 3 * cbSqlStrMax;
	if (buflen > 0)
		szOut = malloc(buflen);
	for (;; buflen = olen + 1, szOut = realloc(szOut, buflen))
	{
		ret = PGAPI_NativeSql(hdbc, szIn, (SQLINTEGER) slen,
			szOut, buflen, &olen);
		if (SQL_SUCCESS_WITH_INFO != ret || olen < buflen)
			break;
	}
	if (szIn)
		free(szIn);
	if (SQL_SUCCEEDED(ret))
	{
		SQLLEN	szcount = olen;

		if (olen < buflen)
			szcount = utf8_to_ucs2(szOut, olen, szSqlStr, cbSqlStrMax);
		if (SQL_SUCCESS == ret && szcount > cbSqlStrMax)
		{
			ConnectionClass	*conn = (ConnectionClass *) hdbc;

			ret = SQL_SUCCESS_WITH_INFO;
			CC_set_error(conn, CONN_TRUNCATED, "Sql string too large", func);
		}
		if (pcbSqlStr)
			*pcbSqlStr = (SQLINTEGER) szcount;
	}
	LEAVE_CONN_CS(conn);
	free(szOut);
	return ret;
}

RETCODE SQL_API SQLPrimaryKeysW(
    HSTMT           hstmt,
    SQLWCHAR 		  *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLWCHAR 		  *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLWCHAR 		  *szTableName,
    SQLSMALLINT        cbTableName)
{
	CSTR func = "SQLPrimaryKeysW";
	RETCODE	ret;
	char	*ctName, *scName, *tbName;
	SQLLEN	nmlen1, nmlen2, nmlen3;
	StatementClass *stmt = (StatementClass *) hstmt;
	ConnectionClass *conn;
	BOOL	lower_id; 

	mylog("[%s]", func);
	conn = SC_get_conn(stmt);
	lower_id = SC_is_lower_case(stmt, conn);
	ctName = ucs2_to_utf8(szCatalogName, cbCatalogName, &nmlen1, lower_id);
	scName = ucs2_to_utf8(szSchemaName, cbSchemaName, &nmlen2, lower_id);
	tbName = ucs2_to_utf8(szTableName, cbTableName, &nmlen3, lower_id);
	ENTER_STMT_CS(stmt);
	SC_clear_error(stmt);
	StartRollbackState(stmt);
	if (SC_opencheck(stmt, func))
		ret = SQL_ERROR;
	else
		ret = PGAPI_PrimaryKeys(hstmt, ctName, (SQLSMALLINT) nmlen1,
			scName, (SQLSMALLINT) nmlen2, tbName, (SQLSMALLINT) nmlen3);
	ret = DiscardStatementSvp(stmt, ret, FALSE);
	LEAVE_STMT_CS(stmt);
	if (ctName)
		free(ctName);
	if (scName)
		free(scName);
	if (tbName)
		free(tbName);
	return ret;
}

RETCODE SQL_API SQLProcedureColumnsW(
    HSTMT           hstmt,
    SQLWCHAR 		  *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLWCHAR 		  *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLWCHAR 		  *szProcName,
    SQLSMALLINT        cbProcName,
    SQLWCHAR 		  *szColumnName,
    SQLSMALLINT        cbColumnName)
{
	CSTR func = "SQLProcedureColumnsW";
	RETCODE	ret;
	char	*ctName, *scName, *prName, *clName;
	SQLLEN	nmlen1, nmlen2, nmlen3, nmlen4;
	StatementClass *stmt = (StatementClass *) hstmt;
	ConnectionClass *conn;
	BOOL	lower_id;
	UWORD	flag = 0; 

	mylog("[%s]", func);
	conn = SC_get_conn(stmt);
	lower_id = SC_is_lower_case(stmt, conn);
	ctName = ucs2_to_utf8(szCatalogName, cbCatalogName, &nmlen1, lower_id);
	scName = ucs2_to_utf8(szSchemaName, cbSchemaName, &nmlen2, lower_id);
	prName = ucs2_to_utf8(szProcName, cbProcName, &nmlen3, lower_id);
	clName = ucs2_to_utf8(szColumnName, cbColumnName, &nmlen4, lower_id);
	ENTER_STMT_CS(stmt);
	SC_clear_error(stmt);
	StartRollbackState(stmt);
#if (ODBCVER >= 0x0300)
	if (stmt->options.metadata_id)
		flag |= PODBC_NOT_SEARCH_PATTERN;
#endif
	if (SC_opencheck(stmt, func))
		ret = SQL_ERROR;
	else
		ret = PGAPI_ProcedureColumns(hstmt, ctName, (SQLSMALLINT) nmlen1,
			scName, (SQLSMALLINT) nmlen2, prName, (SQLSMALLINT) nmlen3,
			clName, (SQLSMALLINT) nmlen4, flag);
	ret = DiscardStatementSvp(stmt, ret, FALSE);
	LEAVE_STMT_CS(stmt);
	if (ctName)
		free(ctName);
	if (scName)
		free(scName);
	if (prName)
		free(prName);
	if (clName)
		free(clName);
	return ret;
}

RETCODE SQL_API SQLProceduresW(
    HSTMT           hstmt,
    SQLWCHAR 		  *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLWCHAR 		  *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLWCHAR 		  *szProcName,
    SQLSMALLINT        cbProcName)
{
	CSTR func = "SQLProceduresW";
	RETCODE	ret;
	char	*ctName, *scName, *prName;
	SQLLEN	nmlen1, nmlen2, nmlen3;
	StatementClass *stmt = (StatementClass *) hstmt;
	ConnectionClass *conn;
	BOOL	lower_id;
	UWORD	flag = 0; 

	mylog("[%s]", func);
	conn = SC_get_conn(stmt);
	lower_id = SC_is_lower_case(stmt, conn);
	ctName = ucs2_to_utf8(szCatalogName, cbCatalogName, &nmlen1, lower_id);
	scName = ucs2_to_utf8(szSchemaName, cbSchemaName, &nmlen2, lower_id);
	prName = ucs2_to_utf8(szProcName, cbProcName, &nmlen3, lower_id);
	ENTER_STMT_CS(stmt);
	SC_clear_error(stmt);
	StartRollbackState(stmt);
#if (ODBCVER >= 0x0300)
	if (stmt->options.metadata_id)
		flag |= PODBC_NOT_SEARCH_PATTERN;
#endif
	if (SC_opencheck(stmt, func))
		ret = SQL_ERROR;
	else
		ret = PGAPI_Procedures(hstmt, ctName, (SQLSMALLINT) nmlen1,
					scName, (SQLSMALLINT) nmlen2, prName,
					 (SQLSMALLINT) nmlen3, flag);
	ret = DiscardStatementSvp(stmt, ret, FALSE);
	LEAVE_STMT_CS(stmt);
	if (ctName)
		free(ctName);
	if (scName)
		free(scName);
	if (prName)
		free(prName);
	return ret;
}

RETCODE SQL_API SQLTablePrivilegesW(
    HSTMT           hstmt,
    SQLWCHAR 		  *szCatalogName,
    SQLSMALLINT        cbCatalogName,
    SQLWCHAR 		  *szSchemaName,
    SQLSMALLINT        cbSchemaName,
    SQLWCHAR 		  *szTableName,
    SQLSMALLINT        cbTableName)
{
	CSTR func = "SQLTablePrivilegesW";
	RETCODE	ret;
	char	*ctName, *scName, *tbName;
	SQLLEN	nmlen1, nmlen2, nmlen3;
	StatementClass *stmt = (StatementClass *) hstmt;
	ConnectionClass *conn;
	BOOL	lower_id; 
	UWORD	flag = 0; 

	mylog("[%s]", func);
	conn = SC_get_conn(stmt);
	lower_id = SC_is_lower_case(stmt, conn);
	ctName = ucs2_to_utf8(szCatalogName, cbCatalogName, &nmlen1, lower_id);
	scName = ucs2_to_utf8(szSchemaName, cbSchemaName, &nmlen2, lower_id);
	tbName = ucs2_to_utf8(szTableName, cbTableName, &nmlen3, lower_id);
	ENTER_STMT_CS((StatementClass *) hstmt);
	SC_clear_error(stmt);
	StartRollbackState(stmt);
#if (ODBCVER >= 0x0300)
	if (stmt->options.metadata_id)
		flag |= PODBC_NOT_SEARCH_PATTERN;
#endif
	if (SC_opencheck(stmt, func))
		ret = SQL_ERROR;
	else
		ret = PGAPI_TablePrivileges(hstmt, ctName, (SQLSMALLINT) nmlen1,
			scName, (SQLSMALLINT) nmlen2, tbName, (SQLSMALLINT) nmlen3, flag);
	ret = DiscardStatementSvp(stmt, ret, FALSE);
	LEAVE_STMT_CS((StatementClass *) hstmt);
	if (ctName)
		free(ctName);
	if (scName)
		free(scName);
	if (tbName)
		free(tbName);
	return ret;
}

RETCODE SQL_API	SQLGetTypeInfoW(
		SQLHSTMT	StatementHandle,
		SQLSMALLINT	DataType)
{
	CSTR func = "SQLGetTypeInfoW";
	RETCODE	ret;
	StatementClass * stmt = (StatementClass *) StatementHandle;

	mylog("[%s]", func);
	ENTER_STMT_CS(stmt);
	SC_clear_error(stmt);
	StartRollbackState(stmt);
	if (SC_opencheck(stmt, func))
		ret = SQL_ERROR;
	else
		ret = PGAPI_GetTypeInfo(StatementHandle, DataType);
	ret = DiscardStatementSvp(stmt, ret, FALSE);
	LEAVE_STMT_CS(stmt);
	return ret;
}
