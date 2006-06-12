/*-------
 * Module:			odbcapi25w.c
 *
 * Description:		This module contains UNICODE routines
 *
 * Classes:			n/a
 *
 * API functions:	SQLColAttributesW, SQLErrorW, SQLGetConnectOptionW,
			SQLSetConnectOptionW
 *-------
 */

#include "psqlodbc.h"
#include <stdio.h>
#include <string.h>

#include "pgapifunc.h"
#include "connection.h"
#include "statement.h"

RETCODE  SQL_API SQLErrorW(HENV EnvironmentHandle,
           HDBC ConnectionHandle, HSTMT StatementHandle,
           SQLWCHAR *Sqlstate, SQLINTEGER *NativeError,
           SQLWCHAR *MessageText, SQLSMALLINT BufferLength,
           SQLSMALLINT *TextLength)
{
	RETCODE	ret;
	SWORD	tlen, buflen;
	char	*qst = NULL, *mtxt = NULL;

	mylog("[SQLErrorW]");
	if (Sqlstate)
		qst = malloc(8);
	buflen = 0;
	if (MessageText && BufferLength > 0)
	{
		buflen = BufferLength * 3 + 1;
		mtxt = malloc(buflen);
	}
	ret = PGAPI_Error(EnvironmentHandle, ConnectionHandle, StatementHandle,
           	qst, NativeError, mtxt, buflen, &tlen);
	if (qst)
		utf8_to_ucs2(qst, strlen(qst), Sqlstate, 5);
	if (TextLength)
		*TextLength = utf8_to_ucs2(mtxt, tlen, MessageText, BufferLength);
	free(qst);
	if (mtxt)
		free(mtxt);
	return ret;
}

RETCODE  SQL_API SQLGetConnectOptionW(HDBC ConnectionHandle,
           SQLUSMALLINT Option, PTR Value)
{
	mylog("[SQLGetConnectOptionW]");
	CC_set_in_unicode_driver((ConnectionClass *) ConnectionHandle);
	return PGAPI_GetConnectOption(ConnectionHandle, Option, Value, NULL, 64);
} 

RETCODE  SQL_API SQLSetConnectOptionW(HDBC ConnectionHandle,
           SQLUSMALLINT Option, SQLUINTEGER Value)
{
	mylog("[SQLSetConnectionOptionW]");
if (!ConnectionHandle)	return SQL_ERROR;
	CC_set_in_unicode_driver((ConnectionClass *) ConnectionHandle);
	return PGAPI_SetConnectOption(ConnectionHandle, Option, Value);
}

RETCODE SQL_API SQLColAttributesW(
    HSTMT           hstmt,
    SQLUSMALLINT       icol,
    SQLUSMALLINT       fDescType,
    PTR         rgbDesc,
    SQLSMALLINT        cbDescMax,
    SQLSMALLINT 	  *pcbDesc,
    SQLINTEGER 		  *pfDesc)
{
	mylog("[SQLColAttributesW]");
	return PGAPI_ColAttributes(hstmt, icol, fDescType, rgbDesc,
		cbDescMax, pcbDesc, pfDesc);
}
