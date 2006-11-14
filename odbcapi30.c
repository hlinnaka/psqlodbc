/*-------
 * Module:			odbcapi30.c
 *
 * Description:		This module contains routines related to ODBC 3.0
 *			most of their implementations are temporary
 *			and must be rewritten properly.
 *			2001/07/23	inoue
 *
 * Classes:			n/a
 *
 * API functions:	SQLAllocHandle, SQLBindParam, SQLCloseCursor,
			SQLColAttribute, SQLCopyDesc, SQLEndTran,
			SQLFetchScroll, SQLFreeHandle, SQLGetDescField,
			SQLGetDescRec, SQLGetDiagField, SQLGetDiagRec,
			SQLGetEnvAttr, SQLGetConnectAttr, SQLGetStmtAttr,
			SQLSetConnectAttr, SQLSetDescField, SQLSetDescRec,
			SQLSetEnvAttr, SQLSetStmtAttr, SQLBulkOperations
 *-------
 */

#include "psqlodbc.h"
#include "misc.h"

#if (ODBCVER >= 0x0300)
#include <stdio.h>
#include <string.h>

#include "environ.h"
#include "connection.h"
#include "statement.h"
#include "pgapifunc.h"

/*	SQLAllocConnect/SQLAllocEnv/SQLAllocStmt -> SQLAllocHandle */
RETCODE		SQL_API
SQLAllocHandle(SQLSMALLINT HandleType,
			   SQLHANDLE InputHandle, SQLHANDLE * OutputHandle)
{
	CSTR	func = "SQLAllocHandle";
	RETCODE		ret;
	ConnectionClass	*conn;

	mylog("[[%s]]", func);
	switch (HandleType)
	{
		case SQL_HANDLE_ENV:
			ret = PGAPI_AllocEnv(OutputHandle);
			break;
		case SQL_HANDLE_DBC:
			ENTER_ENV_CS((EnvironmentClass *) InputHandle);
			ret = PGAPI_AllocConnect(InputHandle, OutputHandle);
			LEAVE_ENV_CS((EnvironmentClass *) InputHandle);
			break;
		case SQL_HANDLE_STMT:
			ENTER_CONN_CS((ConnectionClass *) InputHandle);
			ret = PGAPI_AllocStmt(InputHandle, OutputHandle);
			LEAVE_CONN_CS((ConnectionClass *) InputHandle);
			break;
		case SQL_HANDLE_DESC:
			conn = (ConnectionClass *) InputHandle;
			ENTER_CONN_CS(conn);
			ret = PGAPI_AllocDesc(InputHandle, OutputHandle);
			LEAVE_CONN_CS(conn);
inolog("OutputHandle=%p\n", *OutputHandle);
			break;
		default:
			ret = SQL_ERROR;
			break;
	}
	return ret;
}

/*	SQLBindParameter/SQLSetParam -> SQLBindParam */
RETCODE		SQL_API
SQLBindParam(HSTMT StatementHandle,
			 SQLUSMALLINT ParameterNumber, SQLSMALLINT ValueType,
			 SQLSMALLINT ParameterType, SQLULEN LengthPrecision,
			 SQLSMALLINT ParameterScale, PTR ParameterValue,
			 SQLLEN *StrLen_or_Ind)
{
	CSTR	func = "SQLBindParam";
	RETCODE			ret;
	StatementClass	*stmt = (StatementClass *) StatementHandle;
	int			BufferLength = 512;		/* Is it OK ? */

	mylog("[[%s]]", func);
	ENTER_STMT_CS(stmt);
	SC_clear_error(stmt);
	StartRollbackState(stmt);
	ret = PGAPI_BindParameter(StatementHandle, ParameterNumber, SQL_PARAM_INPUT, ValueType, ParameterType, LengthPrecision, ParameterScale, ParameterValue, BufferLength, StrLen_or_Ind);
	ret = DiscardStatementSvp(stmt,ret, FALSE);
	LEAVE_STMT_CS(stmt);
	return ret;
}

/*	New function */
RETCODE		SQL_API
SQLCloseCursor(HSTMT StatementHandle)
{
	CSTR	func = "SQLCloseCursor";
	StatementClass	*stmt = (StatementClass *) StatementHandle;
	RETCODE	ret;

	mylog("[[%s]]", func);
	ENTER_STMT_CS(stmt);
	SC_clear_error(stmt);
	StartRollbackState(stmt);
	ret = PGAPI_FreeStmt(StatementHandle, SQL_CLOSE);
	ret = DiscardStatementSvp(stmt,ret, FALSE);
	LEAVE_STMT_CS(stmt);
	return ret;
}

/*	SQLColAttributes -> SQLColAttribute */
SQLRETURN	SQL_API
SQLColAttribute(SQLHSTMT StatementHandle,
			SQLUSMALLINT ColumnNumber,
			SQLUSMALLINT FieldIdentifier,
			SQLPOINTER CharacterAttribute,
			SQLSMALLINT BufferLength,
			SQLSMALLINT *StringLength,
#if defined(_WIN64)
			SQLLEN *NumericAttribute
#elif defined(WITH_UNIXODBC) || defined(WIN32)
			SQLPOINTER NumericAttribute
#else
			SQLLEN *NumericAttribute
#endif
			)
{
	CSTR	func = "SQLColAttribute";
	RETCODE	ret;
	StatementClass	*stmt = (StatementClass *) StatementHandle;

	mylog("[[%s]]", func);
	ENTER_STMT_CS(stmt);
	SC_clear_error(stmt);
	StartRollbackState(stmt);
	ret = PGAPI_ColAttributes(StatementHandle, ColumnNumber,
					   FieldIdentifier, CharacterAttribute, BufferLength,
							   StringLength, NumericAttribute);
	ret = DiscardStatementSvp(stmt,ret, FALSE);
	LEAVE_STMT_CS(stmt);
	return ret;
}

/*	new function */
RETCODE		SQL_API
SQLCopyDesc(SQLHDESC SourceDescHandle,
			SQLHDESC TargetDescHandle)
{
	CSTR	func = "SQLCopyDesc";
	RETCODE	ret;

	mylog("[[%s]]\n", func);
	ret = PGAPI_CopyDesc(SourceDescHandle, TargetDescHandle);
	return ret;
}

/*	SQLTransact -> SQLEndTran */
RETCODE		SQL_API
SQLEndTran(SQLSMALLINT HandleType, SQLHANDLE Handle,
		   SQLSMALLINT CompletionType)
{
	CSTR	func = "SQLEndTran";
	RETCODE	ret;

	mylog("[[%s]]", func);
	switch (HandleType)
	{
		case SQL_HANDLE_ENV:
			ENTER_ENV_CS((EnvironmentClass *) Handle);
			ret = PGAPI_Transact(Handle, SQL_NULL_HDBC, CompletionType);
			LEAVE_ENV_CS((EnvironmentClass *) Handle);
			break;
		case SQL_HANDLE_DBC:
			ENTER_CONN_CS((ConnectionClass *) Handle);
			CC_clear_error((ConnectionClass *) Handle);
			ret = PGAPI_Transact(SQL_NULL_HENV, Handle, CompletionType);
			LEAVE_CONN_CS((ConnectionClass *) Handle);
			break;
		default:
			ret = SQL_ERROR;
			break;
	}
	return ret;
}

/*	SQLExtendedFetch -> SQLFetchScroll */
RETCODE		SQL_API
SQLFetchScroll(HSTMT StatementHandle,
			   SQLSMALLINT FetchOrientation, SQLLEN FetchOffset)
{
	CSTR func = "SQLFetchScroll";
	StatementClass *stmt = (StatementClass *) StatementHandle;
	RETCODE		ret = SQL_SUCCESS;
	IRDFields	*irdopts = SC_get_IRDF(stmt);
	SQLUSMALLINT *rowStatusArray = irdopts->rowStatusArray;
	SQLLEN *pcRow = irdopts->rowsFetched, bkmarkoff = 0;

	mylog("[[%s]] %d,%d\n", func, FetchOrientation, FetchOffset);
	ENTER_STMT_CS(stmt);
	SC_clear_error(stmt);
	StartRollbackState(stmt);
	if (FetchOrientation == SQL_FETCH_BOOKMARK)
	{
		if (stmt->options.bookmark_ptr)
		{
			bkmarkoff = FetchOffset;
			FetchOffset = *((Int4 *) stmt->options.bookmark_ptr);
mylog("bookmark=%u FetchOffset = %d\n", FetchOffset, bkmarkoff);
		}
		else
		{
			SC_set_error(stmt, STMT_SEQUENCE_ERROR, "Bookmark isn't specifed yet", func);
			ret = SQL_ERROR;
		}
	}
	if (SQL_SUCCESS == ret)
	{
		ARDFields	*opts = SC_get_ARDF(stmt);

		ret = PGAPI_ExtendedFetch(StatementHandle, FetchOrientation, FetchOffset,
				pcRow, rowStatusArray, bkmarkoff, opts->size_of_rowset);
		stmt->transition_status = 6;
	}
	ret = DiscardStatementSvp(stmt,ret, FALSE);
	LEAVE_STMT_CS(stmt);
	if (ret != SQL_SUCCESS)
		mylog("%s return = %d\n", func, ret);
	return ret;
}

/*	SQLFree(Connect/Env/Stmt) -> SQLFreeHandle */
RETCODE		SQL_API
SQLFreeHandle(SQLSMALLINT HandleType, SQLHANDLE Handle)
{
	CSTR	func = "SQLFreeHandle";
	RETCODE		ret;
	mylog("[[%s]]", func);
	switch (HandleType)
	{
		case SQL_HANDLE_ENV:
			ret = PGAPI_FreeEnv(Handle);
			break;
		case SQL_HANDLE_DBC:
			ret = PGAPI_FreeConnect(Handle);
			break;
		case SQL_HANDLE_STMT:
			ret = PGAPI_FreeStmt(Handle, SQL_DROP);
			break;
		case SQL_HANDLE_DESC:
			ret = PGAPI_FreeDesc(Handle);
			break;
		default:
			ret = SQL_ERROR;
			break;
	}
	return ret;
}

/*	new function */
RETCODE		SQL_API
SQLGetDescField(SQLHDESC DescriptorHandle,
				SQLSMALLINT RecNumber, SQLSMALLINT FieldIdentifier,
				PTR Value, SQLINTEGER BufferLength,
				SQLINTEGER *StringLength)
{
	RETCODE	ret;

	mylog("[[SQLGetDescField]]\n");
	ret = PGAPI_GetDescField(DescriptorHandle, RecNumber, FieldIdentifier,
			Value, BufferLength, StringLength);
	return ret;
}

/*	new function */
RETCODE		SQL_API
SQLGetDescRec(SQLHDESC DescriptorHandle,
			  SQLSMALLINT RecNumber, SQLCHAR *Name,
			  SQLSMALLINT BufferLength, SQLSMALLINT *StringLength,
			  SQLSMALLINT *Type, SQLSMALLINT *SubType,
			  SQLLEN *Length, SQLSMALLINT *Precision,
			  SQLSMALLINT *Scale, SQLSMALLINT *Nullable)
{
	mylog("[[SQLGetDescRec]]\n");
	mylog("Error not implemented\n");
	return SQL_ERROR;
}

/*	new function */
RETCODE		SQL_API
SQLGetDiagField(SQLSMALLINT HandleType, SQLHANDLE Handle,
				SQLSMALLINT RecNumber, SQLSMALLINT DiagIdentifier,
				PTR DiagInfo, SQLSMALLINT BufferLength,
				SQLSMALLINT *StringLength)
{
	CSTR func = "SQLGetDiagField";
	RETCODE	ret;

	mylog("[[%s]] Handle=(%u,%p) Rec=%d Id=%d info=(%p,%d)\n", func, HandleType, Handle, RecNumber, DiagIdentifier, DiagInfo, BufferLength);
	ret = PGAPI_GetDiagField(HandleType, Handle, RecNumber, DiagIdentifier,
				DiagInfo, BufferLength, StringLength);
	return ret;
}

/*	SQLError -> SQLDiagRec */
RETCODE		SQL_API
SQLGetDiagRec(SQLSMALLINT HandleType, SQLHANDLE Handle,
			  SQLSMALLINT RecNumber, SQLCHAR *Sqlstate,
			  SQLINTEGER *NativeError, SQLCHAR *MessageText,
			  SQLSMALLINT BufferLength, SQLSMALLINT *TextLength)
{
	RETCODE	ret;

	mylog("[[SQLGetDiagRec]]\n");
	ret = PGAPI_GetDiagRec(HandleType, Handle, RecNumber, Sqlstate,
			NativeError, MessageText, BufferLength, TextLength);
	return ret;
}

/*	new function */
RETCODE		SQL_API
SQLGetEnvAttr(HENV EnvironmentHandle,
			  SQLINTEGER Attribute, PTR Value,
			  SQLINTEGER BufferLength, SQLINTEGER *StringLength)
{
	RETCODE	ret;
	EnvironmentClass *env = (EnvironmentClass *) EnvironmentHandle;

	mylog("[[SQLGetEnvAttr]] %d\n", Attribute);
	ENTER_ENV_CS(env);
	ret = SQL_SUCCESS;
	switch (Attribute)
	{
		case SQL_ATTR_CONNECTION_POOLING:
			*((unsigned int *) Value) = EN_is_pooling(env) ? SQL_CP_ONE_PER_DRIVER : SQL_CP_OFF;
			break;
		case SQL_ATTR_CP_MATCH:
			*((unsigned int *) Value) = SQL_CP_RELAXED_MATCH;
			break;
		case SQL_ATTR_ODBC_VERSION:
			*((unsigned int *) Value) = EN_is_odbc2(env) ? SQL_OV_ODBC2 : SQL_OV_ODBC3;
			break;
		case SQL_ATTR_OUTPUT_NTS:
			*((unsigned int *) Value) = SQL_TRUE;
			break;
		default:
			env->errornumber = CONN_INVALID_ARGUMENT_NO;
			ret = SQL_ERROR;
	}
	LEAVE_ENV_CS(env);
	return ret;
}

/*	SQLGetConnectOption -> SQLGetconnectAttr */
RETCODE		SQL_API
SQLGetConnectAttr(HDBC ConnectionHandle,
				  SQLINTEGER Attribute, PTR Value,
				  SQLINTEGER BufferLength, SQLINTEGER *StringLength)
{
	RETCODE	ret;

	mylog("[[SQLGetConnectAttr]] %d\n", Attribute);
	ENTER_CONN_CS((ConnectionClass *) ConnectionHandle);
	CC_clear_error((ConnectionClass *) ConnectionHandle);
	ret = PGAPI_GetConnectAttr(ConnectionHandle, Attribute,Value,
			BufferLength, StringLength);
	LEAVE_CONN_CS((ConnectionClass *) ConnectionHandle);
	return ret;
}

/*	SQLGetStmtOption -> SQLGetStmtAttr */
RETCODE		SQL_API
SQLGetStmtAttr(HSTMT StatementHandle,
			   SQLINTEGER Attribute, PTR Value,
			   SQLINTEGER BufferLength, SQLINTEGER *StringLength)
{
	RETCODE	ret;
	CSTR func = "SQLGetStmtAttr";
	StatementClass	*stmt = (StatementClass *) StatementHandle;

	mylog("[[%s]] Handle=%u %d\n", func, StatementHandle, Attribute);
	ENTER_STMT_CS(stmt);
	SC_clear_error(stmt);
	StartRollbackState(stmt);
	ret = PGAPI_GetStmtAttr(StatementHandle, Attribute, Value,
			BufferLength, StringLength);
	ret = DiscardStatementSvp(stmt,ret, FALSE);
	LEAVE_STMT_CS(stmt);
	return ret;
}

/*	SQLSetConnectOption -> SQLSetConnectAttr */
RETCODE		SQL_API
SQLSetConnectAttr(HDBC ConnectionHandle,
				  SQLINTEGER Attribute, PTR Value,
				  SQLINTEGER StringLength)
{
	RETCODE	ret;
	ConnectionClass *conn = (ConnectionClass *) ConnectionHandle;

	mylog("[[SQLSetConnectAttr]] %d\n", Attribute);
	ENTER_CONN_CS(conn);
	CC_clear_error(conn);
	ret = PGAPI_SetConnectAttr(ConnectionHandle, Attribute, Value,
				  StringLength);
	LEAVE_CONN_CS(conn);
	return ret;
}

/*	new function */
RETCODE		SQL_API
SQLSetDescField(SQLHDESC DescriptorHandle,
				SQLSMALLINT RecNumber, SQLSMALLINT FieldIdentifier,
				PTR Value, SQLINTEGER BufferLength)
{
	RETCODE		ret;

	mylog("[[SQLSetDescField]] h=%p rec=%d field=%d val=%p\n", DescriptorHandle, RecNumber, FieldIdentifier, Value);
	ret = PGAPI_SetDescField(DescriptorHandle, RecNumber, FieldIdentifier,
				Value, BufferLength);
	return ret;
}

/*	new fucntion */
RETCODE		SQL_API
SQLSetDescRec(SQLHDESC DescriptorHandle,
			  SQLSMALLINT RecNumber, SQLSMALLINT Type,
			  SQLSMALLINT SubType, SQLLEN Length,
			  SQLSMALLINT Precision, SQLSMALLINT Scale,
			  PTR Data, SQLLEN *StringLength,
			  SQLLEN *Indicator)
{
	CSTR func = "SQLSetDescRec";

	mylog("[[SQLSetDescRec]]\n");
	mylog("Error not implemented\n");
	return SQL_ERROR;
}

/*	new function */
RETCODE		SQL_API
SQLSetEnvAttr(HENV EnvironmentHandle,
			  SQLINTEGER Attribute, PTR Value,
			  SQLINTEGER StringLength)
{
	RETCODE	ret;
	EnvironmentClass *env = (EnvironmentClass *) EnvironmentHandle;

	mylog("[[SQLSetEnvAttr]] att=%d,%u\n", Attribute, Value);
	ENTER_ENV_CS(env);
	switch (Attribute)
	{
		case SQL_ATTR_CONNECTION_POOLING:
			switch ((ULONG_PTR) Value)
			{
			 	case SQL_CP_OFF:
					EN_unset_pooling(env);
					ret = SQL_SUCCESS;
					break;
#if defined(WIN_MULTITHREAD_SUPPORT) || defined(POSIX_MULTITHREAD_SUPPORT)
				case SQL_CP_ONE_PER_DRIVER:
					EN_set_pooling(env);
					ret = SQL_SUCCESS;
					break;
#endif /* WIN_MULTITHREAD_SUPPORT */
				default:
					ret = SQL_SUCCESS_WITH_INFO;
			}
			break;
		case SQL_ATTR_CP_MATCH:
			/* *((unsigned int *) Value) = SQL_CP_RELAXED_MATCH; */
			ret = SQL_SUCCESS;
			break;
		case SQL_ATTR_ODBC_VERSION:
			if (SQL_OV_ODBC2 == CAST_UPTR(SQLUINTEGER, Value))
				EN_set_odbc2(env);
			else
				EN_set_odbc3(env);
			ret = SQL_SUCCESS;
			break;
		case SQL_ATTR_OUTPUT_NTS:
			if (SQL_TRUE == CAST_UPTR(SQLUINTEGER, Value))
				ret = SQL_SUCCESS;
			else
				ret = SQL_SUCCESS_WITH_INFO;
	
			break;
		default:
			env->errornumber = CONN_INVALID_ARGUMENT_NO;
			ret = SQL_ERROR;
	}
	if (SQL_SUCCESS_WITH_INFO == ret)
	{
		env->errornumber = CONN_OPTION_VALUE_CHANGED;
		env->errormsg = "SetEnv changed to ";
	}
	LEAVE_ENV_CS(env);
	return ret;
}

/*	SQLSet(Param/Scroll/Stmt)Option -> SQLSetStmtAttr */
RETCODE		SQL_API
SQLSetStmtAttr(HSTMT StatementHandle,
			   SQLINTEGER Attribute, PTR Value,
			   SQLINTEGER StringLength)
{
	CSTR func = "SQLSetStmtAttr";
	StatementClass *stmt = (StatementClass *) StatementHandle;
	RETCODE	ret;

	mylog("[[%s]] Handle=%p %d,%u\n", func, StatementHandle, Attribute, Value);
	ENTER_STMT_CS(stmt);
	SC_clear_error(stmt);
	StartRollbackState(stmt);
	ret = PGAPI_SetStmtAttr(StatementHandle, Attribute, Value, StringLength);
	ret = DiscardStatementSvp(stmt,ret, FALSE);
	LEAVE_STMT_CS(stmt);
	return ret;
}

#define SQL_FUNC_ESET(pfExists, uwAPI) \
		(*(((UWORD*) (pfExists)) + ((uwAPI) >> 4)) \
			|= (1 << ((uwAPI) & 0x000F)) \
				)
RETCODE		SQL_API
PGAPI_GetFunctions30(HDBC hdbc, SQLUSMALLINT fFunction, SQLUSMALLINT FAR * pfExists)
{
	ConnectionClass	*conn = (ConnectionClass *) hdbc;
	ConnInfo	*ci = &(conn->connInfo);

inolog("lie=%d\n", ci->drivers.lie);
	CC_clear_error(conn);
	if (fFunction != SQL_API_ODBC3_ALL_FUNCTIONS)
		return SQL_ERROR;
	memset(pfExists, 0, sizeof(UWORD) * SQL_API_ODBC3_ALL_FUNCTIONS_SIZE);

	/* SQL_FUNC_ESET(pfExists, SQL_API_SQLALLOCCONNECT); 1 deprecated */
	/* SQL_FUNC_ESET(pfExists, SQL_API_SQLALLOCENV); 2 deprecated */
	/* SQL_FUNC_ESET(pfExists, SQL_API_SQLALLOCSTMT); 3 deprecated */

	/*
	 * for (i = SQL_API_SQLBINDCOL; i <= 23; i++) SQL_FUNC_ESET(pfExists,
	 * i);
	 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLBINDCOL);		/* 4 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLCANCEL);		/* 5 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLCOLATTRIBUTE);	/* 6 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLCONNECT);		/* 7 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLDESCRIBECOL);	/* 8 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLDISCONNECT);		/* 9 */
	/* SQL_FUNC_ESET(pfExists, SQL_API_SQLERROR);  10 deprecated */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLEXECDIRECT);		/* 11 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLEXECUTE);		/* 12 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLFETCH);	/* 13 */
	/* SQL_FUNC_ESET(pfExists, SQL_API_SQLFREECONNECT); 14 deprecated */
	/* SQL_FUNC_ESET(pfExists, SQL_API_SQLFREEENV); 15 deprecated */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLFREESTMT);		/* 16 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLGETCURSORNAME);	/* 17 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLNUMRESULTCOLS);	/* 18 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLPREPARE);		/* 19 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLROWCOUNT);		/* 20 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLSETCURSORNAME);	/* 21 */
	/* SQL_FUNC_ESET(pfExists, SQL_API_SQLSETPARAM); 22 deprecated */
	/* SQL_FUNC_ESET(pfExists, SQL_API_SQLTRANSACT); 23 deprecated */

	/*
	 * for (i = 40; i < SQL_API_SQLEXTENDEDFETCH; i++)
	 * SQL_FUNC_ESET(pfExists, i);
	 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLCOLUMNS);		/* 40 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLDRIVERCONNECT);	/* 41 */
	/* SQL_FUNC_ESET(pfExists, SQL_API_SQLGETCONNECTOPTION); 42 deprecated */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLGETDATA);		/* 43 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLGETFUNCTIONS);	/* 44 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLGETINFO);		/* 45 */
	/* SQL_FUNC_ESET(pfExists, SQL_API_SQLGETSTMTOPTION); 46 deprecated */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLGETTYPEINFO);	/* 47 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLPARAMDATA);		/* 48 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLPUTDATA);		/* 49 */

	/* SQL_FUNC_ESET(pfExists, SQL_API_SQLSETCONNECTIONOPTION); 50 deprecated */
	/* SQL_FUNC_ESET(pfExists, SQL_API_SQLSETSTMTOPTION); 51 deprecated */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLSPECIALCOLUMNS);	/* 52 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLSTATISTICS);		/* 53 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLTABLES);		/* 54 */
	if (ci->drivers.lie)
		SQL_FUNC_ESET(pfExists, SQL_API_SQLBROWSECONNECT);	/* 55 */
	if (ci->drivers.lie)
		SQL_FUNC_ESET(pfExists, SQL_API_SQLCOLUMNPRIVILEGES);	/* 56 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLDATASOURCES);	/* 57 */
	if (SUPPORT_DESCRIBE_PARAM(ci) || ci->drivers.lie)
		SQL_FUNC_ESET(pfExists, SQL_API_SQLDESCRIBEPARAM); /* 58 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLEXTENDEDFETCH); /* 59 deprecated ? */

	/*
	 * for (++i; i < SQL_API_SQLBINDPARAMETER; i++)
	 * SQL_FUNC_ESET(pfExists, i);
	 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLFOREIGNKEYS);	/* 60 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLMORERESULTS);	/* 61 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLNATIVESQL);		/* 62 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLNUMPARAMS);		/* 63 */
	/* SQL_FUNC_ESET(pfExists, SQL_API_SQLPARAMOPTIONS); 64 deprecated */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLPRIMARYKEYS);	/* 65 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLPROCEDURECOLUMNS);	/* 66 */ 
	SQL_FUNC_ESET(pfExists, SQL_API_SQLPROCEDURES);		/* 67 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLSETPOS);		/* 68 */
	/* SQL_FUNC_ESET(pfExists, SQL_API_SQLSETSCROLLOPTIONS); 69 deprecated */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLTABLEPRIVILEGES);	/* 70 */
	/* SQL_FUNC_ESET(pfExists, SQL_API_SQLDRIVERS); */	/* 71 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLBINDPARAMETER);	/* 72 */

	SQL_FUNC_ESET(pfExists, SQL_API_SQLALLOCHANDLE);	/* 1001 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLBINDPARAM);		/* 1002 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLCLOSECURSOR);	/* 1003 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLCOPYDESC);		/* 1004 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLENDTRAN);		/* 1005 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLFREEHANDLE);		/* 1006 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLGETCONNECTATTR);	/* 1007 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLGETDESCFIELD); 	/* 1008 */
	if (ci->drivers.lie)
	{
		SQL_FUNC_ESET(pfExists, SQL_API_SQLGETDESCREC); /* 1009 not implemented yet */
	}
	SQL_FUNC_ESET(pfExists, SQL_API_SQLGETDIAGFIELD); /* 1010 minimal implementation */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLGETDIAGREC);		/* 1011 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLGETENVATTR);		/* 1012 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLGETSTMTATTR);	/* 1014 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLSETCONNECTATTR);	/* 1016 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLSETDESCFIELD);	/* 1017 */
	if (ci->drivers.lie)
	{
		SQL_FUNC_ESET(pfExists, SQL_API_SQLSETDESCREC); /* 1018 not implemented yet */
	}
	SQL_FUNC_ESET(pfExists, SQL_API_SQLSETENVATTR);		/* 1019 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLSETSTMTATTR);	/* 1020 */
	SQL_FUNC_ESET(pfExists, SQL_API_SQLFETCHSCROLL);	/* 1021 */
	if (0 != (ALLOW_BULK_OPERATIONS & ci->updatable_cursors))
		SQL_FUNC_ESET(pfExists, SQL_API_SQLBULKOPERATIONS);	/* 24 */

	return SQL_SUCCESS;
}

RETCODE	SQL_API
SQLBulkOperations(HSTMT hstmt, SQLSMALLINT operation)
{
	RETCODE	ret;
	CSTR func = "SQLBulkOperations";
	StatementClass	*stmt = (StatementClass *) hstmt;

	ENTER_STMT_CS(stmt);
	mylog("[[%s]] Handle=%p %d\n", func, hstmt, operation);
	SC_clear_error(stmt);
	StartRollbackState(stmt);
	ret = PGAPI_BulkOperations(hstmt, operation);
	ret = DiscardStatementSvp(stmt,ret, FALSE);
	LEAVE_STMT_CS(stmt);
	return ret;
}	
#endif /* ODBCVER >= 0x0300 */
