/*------
 * Module:			connection.c
 *
 * Description:		This module contains routines related to
 *					connecting to and disconnecting from the Postgres DBMS.
 *
 * Classes:			ConnectionClass (Functions prefix: "CC_")
 *
 * API functions:	SQLAllocConnect, SQLConnect, SQLDisconnect, SQLFreeConnect,
 *					SQLBrowseConnect(NI)
 *
 * Comments:		See "readme.txt" for copyright and license information.
 *-------
 */
/* Multibyte support	Eiji Tokuya 2001-03-15 */

/*	TryEnterCritiaclSection needs the following #define */
#ifndef	_WIN32_WINNT
#define	_WIN32_WINNT	0x0400
#endif /* _WIN32_WINNT */

#include "connection.h"
#include <libpq-fe.h>

#ifdef WIN32
#define PG_PRINTF_ATTRIBUTE gnu_printf
#else
#define PG_PRINTF_ATTRIBUTE printf
#endif
#include "internal/pqexpbuffer.h" /* for pqexpbuffer.h */

#include "misc.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <arpa/inet.h> /* for htonl */
#ifdef	WIN32
#ifdef	USE_SSPI
#include "sspisvcs.h"
#endif /* USE_SSPI */
#else
#include <errno.h>
#endif /* WIN32 */

#include "environ.h"
#include "statement.h"
#include "qresult.h"
#include "lobj.h"
#include "dlg_specific.h"
#include "loadlib.h"

#include "multibyte.h"

#include "pgapifunc.h"

#define STMT_INCREMENT 16		/* how many statement holders to allocate
								 * at a time */

#define PRN_NULLCHECK

static void CC_lookup_lo(ConnectionClass *self);
static char *CC_create_errormsg(ConnectionClass *self);
static int  CC_close_eof_cursors(ConnectionClass *self);

static void LIBPQ_update_transaction_status(ConnectionClass *self);

extern GLOBAL_VALUES globals;


RETCODE		SQL_API
PGAPI_AllocConnect(HENV henv,
				   HDBC FAR * phdbc)
{
	EnvironmentClass *env = (EnvironmentClass *) henv;
	ConnectionClass *conn;
	CSTR func = "PGAPI_AllocConnect";

	mylog("%s: entering...\n", func);

	conn = CC_Constructor();
	mylog("**** %s: henv = %p, conn = %p\n", func, henv, conn);

	if (!conn)
	{
		env->errormsg = "Couldn't allocate memory for Connection object.";
		env->errornumber = ENV_ALLOC_ERROR;
		*phdbc = SQL_NULL_HDBC;
		EN_log_error(func, "", env);
		return SQL_ERROR;
	}

	if (!EN_add_connection(env, conn))
	{
		env->errormsg = "Maximum number of connections exceeded.";
		env->errornumber = ENV_ALLOC_ERROR;
		CC_Destructor(conn);
		*phdbc = SQL_NULL_HDBC;
		EN_log_error(func, "", env);
		return SQL_ERROR;
	}

	if (phdbc)
		*phdbc = (HDBC) conn;

	return SQL_SUCCESS;
}


RETCODE		SQL_API
PGAPI_Connect(HDBC hdbc,
			  const SQLCHAR FAR * szDSN,
			  SQLSMALLINT cbDSN,
			  const SQLCHAR FAR * szUID,
			  SQLSMALLINT cbUID,
			  const SQLCHAR FAR * szAuthStr,
			  SQLSMALLINT cbAuthStr)
{
	ConnectionClass *conn = (ConnectionClass *) hdbc;
	ConnInfo   *ci;
	CSTR func = "PGAPI_Connect";
	RETCODE	ret = SQL_SUCCESS;
	char	fchar, *tmpstr;

	mylog("%s: entering..cbDSN=%hi.\n", func, cbDSN);

	if (!conn)
	{
		CC_log_error(func, "", NULL);
		return SQL_INVALID_HANDLE;
	}

	ci = &conn->connInfo;
	CC_conninfo_init(ci, COPY_GLOBALS);

	make_string(szDSN, cbDSN, ci->dsn, sizeof(ci->dsn));

	/* get the values for the DSN from the registry */
	getDSNinfo(ci, CONN_OVERWRITE);
	logs_on_off(1, ci->drivers.debug, ci->drivers.commlog);
	/* initialize pg_version from connInfo.protocol    */
	CC_initialize_pg_version(conn);

	/*
	 * override values from DSN info with UID and authStr(pwd) This only
	 * occurs if the values are actually there.
	 */
	fchar = ci->username[0]; /* save the first byte */
	make_string(szUID, cbUID, ci->username, sizeof(ci->username));
	if ('\0' == ci->username[0]) /* an empty string is specified */
		ci->username[0] = fchar; /* restore the original username */
	tmpstr = make_string(szAuthStr, cbAuthStr, NULL, 0);
	if (tmpstr)
	{
		if (tmpstr[0]) /* non-empty string is specified */
			STR_TO_NAME(ci->password, tmpstr);
		free(tmpstr);
	}

	/* fill in any defaults */
	getDSNdefaults(ci);

	qlog("conn = %p, %s(DSN='%s', UID='%s', PWD='%s')\n", conn, func, ci->dsn, ci->username, NAME_IS_VALID(ci->password) ? "xxxxx" : "");

	if ((fchar = CC_connect(conn, AUTH_REQ_OK, NULL)) <= 0)
	{
		/* Error messages are filled in */
		CC_log_error(func, "Error on CC_connect", conn);
		ret = SQL_ERROR;
	}
	if (SQL_SUCCESS == ret && 2 == fchar)
		ret = SQL_SUCCESS_WITH_INFO;

	mylog("%s: returning..%d.\n", func, ret);

	return ret;
}


RETCODE		SQL_API
PGAPI_BrowseConnect(HDBC hdbc,
					const SQLCHAR FAR * szConnStrIn,
					SQLSMALLINT cbConnStrIn,
					SQLCHAR FAR * szConnStrOut,
					SQLSMALLINT cbConnStrOutMax,
					SQLSMALLINT FAR * pcbConnStrOut)
{
	CSTR func = "PGAPI_BrowseConnect";
	ConnectionClass *conn = (ConnectionClass *) hdbc;

	mylog("%s: entering...\n", func);

	CC_set_error(conn, CONN_NOT_IMPLEMENTED_ERROR, "Function not implemented", func);
	return SQL_ERROR;
}


/* Drop any hstmts open on hdbc and disconnect from database */
RETCODE		SQL_API
PGAPI_Disconnect(HDBC hdbc)
{
	ConnectionClass *conn = (ConnectionClass *) hdbc;
	CSTR func = "PGAPI_Disconnect";


	mylog("%s: entering...\n", func);

	if (!conn)
	{
		CC_log_error(func, "", NULL);
		return SQL_INVALID_HANDLE;
	}

	qlog("conn=%p, %s\n", conn, func);

	if (conn->status == CONN_EXECUTING)
	{
		CC_set_error(conn, CONN_IN_USE, "A transaction is currently being executed", func);
		return SQL_ERROR;
	}

	logs_on_off(-1, conn->connInfo.drivers.debug, conn->connInfo.drivers.commlog);
	mylog("%s: about to CC_cleanup\n", func);

	/* Close the connection and free statements */
	CC_cleanup(conn, FALSE);

	mylog("%s: done CC_cleanup\n", func);
	mylog("%s: returning...\n", func);

	return SQL_SUCCESS;
}


RETCODE		SQL_API
PGAPI_FreeConnect(HDBC hdbc)
{
	ConnectionClass *conn = (ConnectionClass *) hdbc;
	CSTR func = "PGAPI_FreeConnect";
	EnvironmentClass *env;

	mylog("%s: entering...\n", func);
	mylog("**** in %s: hdbc=%p\n", func, hdbc);

	if (!conn)
	{
		CC_log_error(func, "", NULL);
		return SQL_INVALID_HANDLE;
	}

	/* Remove the connection from the environment */
	if (NULL != (env = CC_get_env(conn)) &&
	    !EN_remove_connection(env, conn))
	{
		CC_set_error(conn, CONN_IN_USE, "A transaction is currently being executed", func);
		return SQL_ERROR;
	}

	CC_Destructor(conn);

	mylog("%s: returning...\n", func);

	return SQL_SUCCESS;
}

static void
CC_conninfo_release(ConnInfo *conninfo)
{
	NULL_THE_NAME(conninfo->password);
	NULL_THE_NAME(conninfo->conn_settings);
	finalize_globals(&conninfo->drivers);
}

void
CC_conninfo_init(ConnInfo *conninfo, UInt4 option)
{
	CSTR	func = "CC_conninfo_init";
	mylog("%s opt=%d\n", func, option);

	if (0 != (CLEANUP_FOR_REUSE & option))
		CC_conninfo_release(conninfo);
	memset(conninfo, 0, sizeof(ConnInfo));
	conninfo->disallow_premature = -1;
	conninfo->allow_keyset = -1;
	conninfo->lf_conversion = -1;
	conninfo->true_is_minus1 = -1;
	conninfo->int8_as = -101;
	conninfo->bytea_as_longvarbinary = -1;
	conninfo->use_server_side_prepare = -1;
	conninfo->lower_case_identifier = -1;
	conninfo->rollback_on_error = -1;
	conninfo->force_abbrev_connstr = -1;
	conninfo->bde_environment = -1;
	conninfo->fake_mss = -1;
	conninfo->cvt_null_date_string = -1;
	conninfo->autocommit_public = SQL_AUTOCOMMIT_ON;
	conninfo->accessible_only = -1;
	conninfo->ignore_round_trip_time = -1;
	conninfo->disable_keepalive = -1;
	conninfo->gssauth_use_gssapi = -1;
	conninfo->keepalive_idle = -1;
	conninfo->keepalive_interval = -1;
#ifdef	_HANDLE_ENLIST_IN_DTC_
	conninfo->xa_opt = -1;
#endif /* _HANDLE_ENLIST_IN_DTC_ */
	if (0 != (COPY_GLOBALS & option))
		copy_globals(&(conninfo->drivers), &globals);
}

#define	CORR_STRCPY(item)	strncpy_null(ci->item, sci->item, sizeof(ci->item))
#define	CORR_VALCPY(item)	(ci->item = sci->item)

void
CC_copy_conninfo(ConnInfo *ci, const ConnInfo *sci)
{
	memset(ci, 0,sizeof(ConnInfo));

	CORR_STRCPY(dsn);
	CORR_STRCPY(desc);
	CORR_STRCPY(drivername);
	CORR_STRCPY(server);
	CORR_STRCPY(database);
	CORR_STRCPY(username);
	NAME_TO_NAME(ci->password, sci->password);
	CORR_STRCPY(port);
	CORR_STRCPY(sslmode);
	CORR_STRCPY(onlyread);
	CORR_STRCPY(fake_oid_index);
	CORR_STRCPY(show_oid_column);
	CORR_STRCPY(row_versioning);
	CORR_STRCPY(show_system_tables);
	CORR_STRCPY(translation_dll);
	CORR_STRCPY(translation_option);
	CORR_VALCPY(focus_password);
	NAME_TO_NAME(ci->conn_settings, sci->conn_settings);
	CORR_VALCPY(disallow_premature);
	CORR_VALCPY(allow_keyset);
	CORR_VALCPY(updatable_cursors);
	CORR_VALCPY(lf_conversion);
	CORR_VALCPY(true_is_minus1);
	CORR_VALCPY(int8_as);
	CORR_VALCPY(bytea_as_longvarbinary);
	CORR_VALCPY(use_server_side_prepare);
	CORR_VALCPY(lower_case_identifier);
	CORR_VALCPY(rollback_on_error);
	CORR_VALCPY(force_abbrev_connstr);
	CORR_VALCPY(bde_environment);
	CORR_VALCPY(fake_mss);
	CORR_VALCPY(cvt_null_date_string);
	CORR_VALCPY(autocommit_public);
	CORR_VALCPY(accessible_only);
	CORR_VALCPY(ignore_round_trip_time);
	CORR_VALCPY(disable_keepalive);
	CORR_VALCPY(gssauth_use_gssapi);
	CORR_VALCPY(extra_opts);
	CORR_VALCPY(keepalive_idle);
	CORR_VALCPY(keepalive_interval);
#ifdef	_HANDLE_ENLIST_IN_DTC_
	CORR_VALCPY(xa_opt);
#endif
	copy_globals(&(ci->drivers), &(sci->drivers));	/* moved from driver's option */
}
#undef	CORR_STRCPY
#undef	CORR_VALCPY

/*
 *		IMPLEMENTATION CONNECTION CLASS
 */

static void
reset_current_schema(ConnectionClass *self)
{
	if (self->current_schema)
	{
		free(self->current_schema);
		self->current_schema = NULL;
	}
}

static ConnectionClass *
CC_alloc(void)
{
	return (ConnectionClass *) calloc(sizeof(ConnectionClass), 1);
}

static void
CC_lockinit(ConnectionClass *self)
{
	INIT_CONNLOCK(self);
	INIT_CONN_CS(self);
}

static ConnectionClass *
CC_initialize(ConnectionClass *rv, BOOL lockinit)
{
	ConnectionClass *retrv = NULL;
	size_t		clear_size;

#if defined(WIN_MULTITHREAD_SUPPORT) || defined(POSIX_THREADMUTEX_SUPPORT)
	clear_size = (char *)&(rv->cs) - (char *)rv;
#else
	clear_size = sizeof(ConnectionClass);
#endif /* WIN_MULTITHREAD_SUPPORT */

	memset(rv, 0, clear_size);
	rv->status = CONN_NOT_CONNECTED;
	rv->transact_status = CONN_IN_AUTOCOMMIT;		/* autocommit by default */
	rv->unnamed_prepared_stmt = NULL;

	rv->stmts = (StatementClass **) malloc(sizeof(StatementClass *) * STMT_INCREMENT);
	if (!rv->stmts)
		goto cleanup;
	memset(rv->stmts, 0, sizeof(StatementClass *) * STMT_INCREMENT);

	rv->num_stmts = STMT_INCREMENT;
	rv->descs = (DescriptorClass **) malloc(sizeof(DescriptorClass *) * STMT_INCREMENT);
	if (!rv->descs)
		goto cleanup;
	memset(rv->descs, 0, sizeof(DescriptorClass *) * STMT_INCREMENT);

	rv->num_descs = STMT_INCREMENT;

	rv->lobj_type = PG_TYPE_LO_UNDEFINED;
	if (isMsAccess())
		rv->ms_jet = 1;
	rv->isolation = SQL_TXN_READ_COMMITTED;
	rv->mb_maxbyte_per_char = 1;
	rv->max_identifier_length = -1;
	rv->escape_in_literal = ESCAPE_IN_LITERAL;

	/* Initialize statement options to defaults */
	/* Statements under this conn will inherit these options */

	InitializeStatementOptions(&rv->stmtOptions);
	InitializeARDFields(&rv->ardOptions);
	InitializeAPDFields(&rv->apdOptions);
#ifdef	_HANDLE_ENLIST_IN_DTC_
	rv->asdum = NULL;
	rv->gTranInfo = 0;
#endif /* _HANDLE_ENLIST_IN_DTC_ */
	if (lockinit)
		CC_lockinit(rv);
	retrv = rv;
cleanup:
	if (rv && !retrv)
		CC_Destructor(rv);
	return retrv;
}

ConnectionClass *
CC_Constructor()
{
	ConnectionClass *rv, *retrv = NULL;

	if (rv = CC_alloc(), NULL != rv)
		retrv = CC_initialize(rv, TRUE);
	return retrv;
}

char
CC_Destructor(ConnectionClass *self)
{
	mylog("enter CC_Destructor, self=%p\n", self);

	if (self->status == CONN_EXECUTING)
		return 0;

	CC_cleanup(self, FALSE);			/* cleanup socket and statements */

	mylog("after CC_Cleanup\n");

	/* Free up statement holders */
	if (self->stmts)
	{
		free(self->stmts);
		self->stmts = NULL;
	}
	if (self->descs)
	{
		free(self->descs);
		self->descs = NULL;
	}
	mylog("after free statement holders\n");

	NULL_THE_NAME(self->schemaIns);
	NULL_THE_NAME(self->tableIns);
	CC_conninfo_release(&self->connInfo);
	if (self->__error_message)
		free(self->__error_message);
	DELETE_CONN_CS(self);
	DELETE_CONNLOCK(self);
	free(self);

	mylog("exit CC_Destructor\n");

	return 1;
}


/*	Return how many cursors are opened on this connection */
int
CC_cursor_count(ConnectionClass *self)
{
	StatementClass *stmt;
	int			i,
				count = 0;
	QResultClass		*res;

	mylog("CC_cursor_count: self=%p, num_stmts=%d\n", self, self->num_stmts);

	CONNLOCK_ACQUIRE(self);
	for (i = 0; i < self->num_stmts; i++)
	{
		stmt = self->stmts[i];
		if (stmt && (res = SC_get_Result(stmt)) && QR_get_cursor(res))
			count++;
	}
	CONNLOCK_RELEASE(self);

	mylog("CC_cursor_count: returning %d\n", count);

	return count;
}


void
CC_clear_error(ConnectionClass *self)
{
	if (!self)	return;
	CONNLOCK_ACQUIRE(self);
	self->__error_number = 0;
	if (self->__error_message)
	{
		free(self->__error_message);
		self->__error_message = NULL;
	}
	self->sqlstate[0] = '\0';
	self->errormsg_created = FALSE;
	CONNLOCK_RELEASE(self);
}

void
CC_examine_global_transaction(ConnectionClass *self)
{
	if (!self)	return;
#ifdef	_HANDLE_ENLIST_IN_DTC_
	if (CC_is_in_global_trans(self))
		CALL_IsolateDtcConn(self, TRUE);
#endif /* _HANDLE_ENLIST_IN_DTC_ */
}


static const char *bgncmd = "BEGIN";
static const char *cmtcmd = "COMMIT";
static const char *rbkcmd = "ROLLBACK";
static const char *svpcmd = "SAVEPOINT";
static const char *per_query_svp = "_per_query_svp_";
static const char *rlscmd = "RELEASE";

/*
 *	Used to begin a transaction.
 */
char
CC_begin(ConnectionClass *self)
{
	char	ret = TRUE;
	if (!CC_is_in_trans(self))
	{
		QResultClass *res = CC_send_query(self, bgncmd, NULL, 0, NULL);
		mylog("CC_begin:  sending BEGIN!\n");

		ret = QR_command_maybe_successful(res);
		QR_Destructor(res);
	}

	return ret;
}

/*
 *	Used to commit a transaction.
 *	We are almost always in the middle of a transaction.
 */
char
CC_commit(ConnectionClass *self)
{
	char	ret = TRUE;
	if (CC_is_in_trans(self))
	{
		if (!CC_is_in_error_trans(self))
			CC_close_eof_cursors(self);
		if (CC_is_in_trans(self))
		{
			QResultClass *res = CC_send_query(self, cmtcmd, NULL, 0, NULL);
			mylog("CC_commit:  sending COMMIT!\n");
			ret = QR_command_maybe_successful(res);
			QR_Destructor(res);
		}
	}

	return ret;
}

/*
 *	Used to cancel a transaction.
 *	We are almost always in the middle of a transaction.
 */
char
CC_abort(ConnectionClass *self)
{
	char	ret = TRUE;
	if (CC_is_in_trans(self))
	{
		QResultClass *res = CC_send_query(self, rbkcmd, NULL, 0, NULL);
		mylog("CC_abort:  sending ABORT!\n");
		ret = QR_command_maybe_successful(res);
		QR_Destructor(res);
	}

	return ret;
}

/* This is called by SQLSetConnectOption etc also */
char
CC_set_autocommit(ConnectionClass *self, BOOL on)
{
	CSTR func = "CC_set_autocommit";
	BOOL currsts = CC_is_in_autocommit(self);

	if ((on && currsts) ||
	    (!on && !currsts))
		return on;
	mylog("%s: %d->%d\n", func, currsts, on);
	if (CC_is_in_trans(self))
		CC_commit(self);
	if (on)
		self->transact_status |= CONN_IN_AUTOCOMMIT;
	else
		self->transact_status &= ~CONN_IN_AUTOCOMMIT;

	return on;
}

/* Clear cached table info */
static void
CC_clear_col_info(ConnectionClass *self, BOOL destroy)
{
	if (self->col_info)
	{
		int	i;
		COL_INFO	*coli;

		for (i = 0; i < self->ntables; i++)
		{
			if (coli = self->col_info[i], NULL != coli)
			{
				if (destroy || coli->refcnt == 0)
				{
					free_col_info_contents(coli);
					free(coli);
					self->col_info[i] = NULL;
				}
				else
					coli->acc_time = 0;
			}
		}
		self->ntables = 0;
		if (destroy)
		{
			free(self->col_info);
			self->col_info = NULL;
			self->coli_allocated = 0;
		}
	}
}

/* This is called by SQLDisconnect also */
char
CC_cleanup(ConnectionClass *self, BOOL keepCommunication)
{
	int			i;
	StatementClass *stmt;
	DescriptorClass *desc;

	if (self->status == CONN_EXECUTING)
		return FALSE;

	mylog("in CC_Cleanup, self=%p\n", self);

	ENTER_CONN_CS(self);
	/* Cancel an ongoing transaction */
	/* We are always in the middle of a transaction, */
	/* even if we are in auto commit. */
	if (self->pqconn)
	{
		PQfinish(self->pqconn);
		self->pqconn = NULL;
	}

	mylog("after PQfinish\n");

	/* Free all the stmts on this connection */
	for (i = 0; i < self->num_stmts; i++)
	{
		stmt = self->stmts[i];
		if (stmt)
		{
			stmt->hdbc = NULL;	/* prevent any more dbase interactions */

			SC_Destructor(stmt);

			self->stmts[i] = NULL;
		}
	}
	/* Free all the descs on this connection */
	for (i = 0; i < self->num_descs; i++)
	{
		desc = self->descs[i];
		if (desc)
		{
			DC_get_conn(desc) = NULL;	/* prevent any more dbase interactions */
			DC_Destructor(desc);
			free(desc);
			self->descs[i] = NULL;
		}
	}

	/* Check for translation dll */
#ifdef WIN32
	if (!keepCommunication && self->translation_handle)
	{
		FreeLibrary(self->translation_handle);
		self->translation_handle = NULL;
	}
#endif

	if (!keepCommunication)
	{
		self->status = CONN_NOT_CONNECTED;
		self->transact_status = CONN_IN_AUTOCOMMIT;
		self->unnamed_prepared_stmt = NULL;
	}
	if (!keepCommunication)
	{
		CC_conninfo_init(&(self->connInfo), CLEANUP_FOR_REUSE);
		if (self->original_client_encoding)
		{
			free(self->original_client_encoding);
			self->original_client_encoding = NULL;
		}
		if (self->current_client_encoding)
		{
			free(self->current_client_encoding);
			self->current_client_encoding = NULL;
		}
		if (self->server_encoding)
		{
			free(self->server_encoding);
			self->server_encoding = NULL;
		}
		reset_current_schema(self);
	}
	/* Free cached table info */
	CC_clear_col_info(self, TRUE);
	if (self->num_discardp > 0 && self->discardp)
	{
		for (i = 0; i < self->num_discardp; i++)
			free(self->discardp[i]);
		self->num_discardp = 0;
	}
	if (self->discardp)
	{
		free(self->discardp);
		self->discardp = NULL;
	}

	LEAVE_CONN_CS(self);
	mylog("exit CC_Cleanup\n");
	return TRUE;
}


int
CC_set_translation(ConnectionClass *self)
{

#ifdef WIN32
	CSTR	func = "CC_set_translation";

	if (self->translation_handle != NULL)
	{
		FreeLibrary(self->translation_handle);
		self->translation_handle = NULL;
	}

	if (self->connInfo.translation_dll[0] == 0)
		return TRUE;

	self->translation_option = atoi(self->connInfo.translation_option);
	self->translation_handle = LoadLibrary(self->connInfo.translation_dll);

	if (self->translation_handle == NULL)
	{
		CC_set_error(self, CONN_UNABLE_TO_LOAD_DLL, "Could not load the translation DLL.", func);
		return FALSE;
	}

	self->DataSourceToDriver
		= (DataSourceToDriverProc) GetProcAddress(self->translation_handle,
												"SQLDataSourceToDriver");

	self->DriverToDataSource
		= (DriverToDataSourceProc) GetProcAddress(self->translation_handle,
												"SQLDriverToDataSource");

	if (self->DataSourceToDriver == NULL || self->DriverToDataSource == NULL)
	{
		CC_set_error(self, CONN_UNABLE_TO_LOAD_DLL, "Could not find translation DLL functions.", func);
		return FALSE;
	}
#endif
	return TRUE;
}

void
handle_pgres_error(ConnectionClass *self, const PGresult *pgres,
				   const char *comment,
				   QResultClass *res, BOOL fatal)
{
	UDWORD		abort_opt;
	char	   *errseverity;
	char	   *errprimary;
	char	   *errmsg = NULL;
	size_t		errmsglen;

	inolog("handle_pgres_error");

	if (res)
	{
		char *sqlstate = PQresultErrorField(pgres, PG_DIAG_SQLSTATE);
		if (sqlstate)
			strncpy_null(res->sqlstate, sqlstate, sizeof(res->sqlstate));
	}

	/*
	 * The full message with details and context and everything could
	 * be obtained with PQresultErrorMessage(). I think that would be
	 * more user-friendly, but for now, construct a message with
	 * severity and primary message, which is backwards compatible.
	 */
	errseverity = PQresultErrorField(pgres, PG_DIAG_SEVERITY);
	errprimary = PQresultErrorField(pgres, PG_DIAG_MESSAGE_PRIMARY);
	if (errprimary == NULL)
	{
		/* Hmm. got no primary message. Check if there's a connection error */
		if (self->pqconn)
			errprimary = PQerrorMessage(self->pqconn);

		if (errprimary == NULL)
			errprimary = "no error information";
	}
	if (errseverity && errprimary)
	{
		errmsglen = strlen(errseverity) + 2 + strlen(errprimary) + 1;
		errmsg = malloc(errmsglen);
		if (errmsg)
		{
			snprintf(errmsg, errmsglen, "%s: %s", errseverity, errprimary);
		}
	}
	if (errmsg == NULL)
		errmsg = errprimary;

	abort_opt = 0;

	if (PQstatus(self->pqconn) == CONNECTION_BAD)
	{
		CC_set_errornumber(self, CONNECTION_SERVER_REPORTED_ERROR);
		abort_opt = CONN_DEAD;
	}
	else
	{
		CC_set_errornumber(self, CONNECTION_SERVER_REPORTED_WARNING);
		if (CC_is_in_trans(self))
			CC_set_in_error_trans(self);
	}

	mylog("notice/error message len=%d\n", strlen(errmsg));

	if (0 != abort_opt
#ifdef	_LEGACY_MODE_
		|| TRUE
#endif /* _LEGACY_NODE_ */
		)
		CC_on_abort(self, abort_opt);

	if (fatal)
	{
		if (res)
		{
			QR_set_rstatus(res, PORES_FATAL_ERROR);
			QR_set_message(res, errmsg);
			QR_set_aborted(res, TRUE);
		}
	}
	else
	{
		if (res)
		{
			if (QR_command_successful(res))
				QR_set_rstatus(res, PORES_NONFATAL_ERROR);
			QR_set_notice(res, errmsg);  /* will dup this string */
		}
	}
	if (errmsg != errprimary)
		free(errmsg);
}

typedef struct
{
	ConnectionClass *conn;
	const char *comment;
	QResultClass *res;
} notice_receiver_arg;

/*
 * This is a libpq notice receiver callback.
 */
void
receive_libpq_notice(void *arg, const PGresult *pgres)
{
	if (arg != NULL)
	{
		notice_receiver_arg *nrarg = (notice_receiver_arg *) arg;

		handle_pgres_error(nrarg->conn, pgres, nrarg->comment, nrarg->res, FALSE);
	}
}

CSTR std_cnf_strs = "standard_conforming_strings";

static int	protocol3_opts_array(ConnectionClass *self, const char *opts[], const char *vals[], BOOL libpqopt, int dim_opts)
{
	ConnInfo	*ci = &(self->connInfo);
	const	char	*enc = NULL;
	int	cnt;

	cnt = 0;
	if (libpqopt && ci->server[0])
	{
		opts[cnt] = "host";		vals[cnt++] = ci->server;
	}
	if (libpqopt && ci->port[0])
	{
		opts[cnt] = "port";		vals[cnt++] = ci->port;
	}
	if (ci->database[0])
	{
		if (libpqopt)
		{
			opts[cnt] = "dbname";	vals[cnt++] = ci->database;
		}
		else
		{
			opts[cnt] = "database";	vals[cnt++] = ci->database;
		}
	}
	if (ci->username[0] || !libpqopt)
	{
		char	*usrname = ci->username;
#ifdef	WIN32
		DWORD namesize = sizeof(ci->username) - 2;
#endif /* WIN32 */

		opts[cnt] = "user";
		if (!usrname[0])
		{
#ifdef	WIN32
			if (GetUserName(ci->username + 1, &namesize))
				usrname = ci->username + 1;
#endif /* WIN32 */
		}
mylog("!!! usrname=%s server=%s\n", usrname, ci->server);
		vals[cnt++] = usrname;
	}
	if (libpqopt)
	{
		switch (ci->sslmode[0])
		{
			case '\0':
				break;
			case SSLLBYTE_VERIFY:
				opts[cnt] = "sslmode";
				switch (ci->sslmode[1])
				{
					case 'f':
						vals[cnt++] = SSLMODE_VERIFY_FULL;
							break;
					case 'c':
						vals[cnt++] = SSLMODE_VERIFY_CA;
							break;
					default:
						vals[cnt++] = ci->sslmode;
				}
				break;
			default:
				opts[cnt] = "sslmode";
				vals[cnt++] = ci->sslmode;
		}
		if (NAME_IS_VALID(ci->password))
		{
			opts[cnt] = "password";	vals[cnt++] = SAFE_NAME(ci->password);
		}
		if (ci->gssauth_use_gssapi)
		{
			opts[cnt] = "gsslib";	vals[cnt++] = "gssapi";
		}
		if (ci->disable_keepalive)
		{
			opts[cnt] = "keepalives";	vals[cnt++] = "0";
		}
	}
	else
	{
		/* DateStyle */
		opts[cnt] = "DateStyle"; vals[cnt++] = "ISO";
		/* extra_float_digits */
		opts[cnt] = "extra_float_digits";	vals[cnt++] = "2";
		/* client_encoding */
		enc = get_environment_encoding(self, self->original_client_encoding, NULL, TRUE);
		if (enc)
		{
			mylog("startup client_encoding=%s\n", enc);
			opts[cnt] = "client_encoding"; vals[cnt++] = enc;
		}
	}
	opts[cnt] = vals[cnt] = NULL;

	return cnt;
}


CSTR	l_login_timeout = "connect_timeout";
CSTR	l_keepalives_idle = "keepalives_idle";
CSTR	l_keepalives_interval = "keepalives_interval";

#define        PROTOCOL3_OPTS_MAX      20

static char	*protocol3_opts_build(ConnectionClass *self)
{
	CSTR	func = "protocol3_opts_build";
	size_t	slen;
	char	*conninfo, *ppacket;
	const	char	*opts[PROTOCOL3_OPTS_MAX], *vals[PROTOCOL3_OPTS_MAX];
	int	cnt, i;
	BOOL	blankExist;

	cnt = protocol3_opts_array(self, opts, vals, TRUE, sizeof(opts) / sizeof(opts[0]));
	if (cnt < 0)
		return NULL;

	slen =  0;
	for (i = 0; i < cnt; i++)
	{
		slen += (strlen(opts[i]) + 2 + 2); /* add 2 bytes for safety (literal quotes) */
		slen += strlen(vals[i]);
	}
	if (self->login_timeout > 0)
	{
		char	tmout[16];

		slen += (strlen(l_login_timeout) + 2);
		snprintf(tmout, sizeof(tmout), FORMAT_UINTEGER, self->login_timeout);
		slen += strlen(tmout);
	}
	if (self->connInfo.keepalive_idle > 0)
	{
		char	outwrk[16];

		slen += (strlen(l_keepalives_idle) + 2);
		snprintf(outwrk, sizeof(outwrk), FORMAT_UINTEGER, self->connInfo.keepalive_idle);
		slen += strlen(outwrk);
	}
	if (self->connInfo.keepalive_interval > 0)
	{
		char	outwrk[16];

		slen += (strlen(l_keepalives_interval) + 2);
		snprintf(outwrk, sizeof(outwrk), FORMAT_UINTEGER, self->connInfo.keepalive_interval);
		slen += strlen(outwrk);
	}

	slen++;

	if (conninfo = malloc(slen), !conninfo)
	{
		CC_set_error(self, CONNECTION_SERVER_NOT_REACHED, "Could not allocate a connectdb option", func);
		return 0;
	}

	mylog("sizeof connectdb option = %d\n", slen);

	for (i = 0, ppacket = conninfo; i < cnt; i++)
	{
		sprintf(ppacket, " %s=", opts[i]);
		ppacket += (strlen(opts[i]) + 2);
		blankExist = FALSE;
		if (strchr(vals[i], ' '))
			blankExist = TRUE;
		if (blankExist)
		{
			*ppacket = '\'';
			ppacket++;
		}
		strcpy(ppacket, vals[i]);
		ppacket += strlen(vals[i]);
		if (blankExist)
		{
			*ppacket = '\'';
			ppacket++;
		}
	}
	if (self->login_timeout > 0)
	{
		sprintf(ppacket, " %s=", l_login_timeout);
		ppacket = strchr(ppacket, (int) '\0');
		sprintf(ppacket, FORMAT_UINTEGER, self->login_timeout);
		ppacket = strchr(ppacket, (int) '\0');
	}
	if (self->connInfo.keepalive_idle > 0)
	{
		sprintf(ppacket, " %s=", l_keepalives_idle);
		ppacket = strchr(ppacket, (int) '\0');
		sprintf(ppacket, FORMAT_UINTEGER, self->connInfo.keepalive_idle);
		ppacket = strchr(ppacket, (int) '\0');
	}
	if (self->connInfo.keepalive_interval > 0)
	{
		sprintf(ppacket, " %s=", l_keepalives_interval);
		ppacket = strchr(ppacket, (int) '\0');
		sprintf(ppacket, FORMAT_UINTEGER, self->connInfo.keepalive_interval);
		ppacket = strchr(ppacket, (int) '\0');
	}
	*ppacket = '\0';
inolog("return conninfo=%s(%d)\n", conninfo, strlen(conninfo));
	return conninfo;
}

static char CC_initial_log(ConnectionClass *self, const char *func)
{
	const ConnInfo	*ci = &self->connInfo;
	char	*encoding, vermsg[128];

	snprintf(vermsg, sizeof(vermsg), "Driver Version='%s,%s'"
#ifdef	WIN32
		" linking %d"
#ifdef	_MT
#ifdef	_DLL
		" dynamic"
#else
		" static"
#endif /* _DLL */
		" Multithread"
#else
		" Singlethread"
#endif /* _MT */
#ifdef	_DEBUG
		" Debug"
#endif /* DEBUG */
		" library"
#endif /* WIN32 */
		"\n", POSTGRESDRIVERVERSION, __DATE__
#ifdef	_MSC_VER
		, _MSC_VER
#endif /* _MSC_VER */
		);
	qlog(vermsg);
	mylog(vermsg);
	qlog("Global Options: fetch=%d, unknown_sizes=%d, max_varchar_size=%d, max_longvarchar_size=%d\n",
		 ci->drivers.fetch_max,
		 ci->drivers.unknown_sizes,
		 ci->drivers.max_varchar_size,
		 ci->drivers.max_longvarchar_size);
	qlog("                unique_index=%d, use_declarefetch=%d\n",
		 ci->drivers.unique_index,
		 ci->drivers.use_declarefetch);
	qlog("                text_as_longvarchar=%d, unknowns_as_longvarchar=%d, bools_as_char=%d NAMEDATALEN=%d\n",
		 ci->drivers.text_as_longvarchar,
		 ci->drivers.unknowns_as_longvarchar,
		 ci->drivers.bools_as_char,
		 TABLE_NAME_STORAGE_LEN);

	encoding = check_client_encoding(ci->conn_settings);
	if (encoding)
		self->original_client_encoding = encoding;
	else
	{
		encoding = check_client_encoding(ci->drivers.conn_settings);
		if (encoding)
			self->original_client_encoding = encoding;
	}
	if (self->original_client_encoding)
		self->ccsc = pg_CS_code(self->original_client_encoding);
	qlog("                extra_systable_prefixes='%s', conn_settings='%s' conn_encoding='%s'\n",
		 ci->drivers.extra_systable_prefixes,
		 PRINT_NAME(ci->drivers.conn_settings),
		 encoding ? encoding : "");
	if (self->status != CONN_NOT_CONNECTED)
	{
		CC_set_error(self, CONN_OPENDB_ERROR, "Already connected.", func);
		return 0;
	}

	mylog("%s: DSN = '%s', server = '%s', port = '%s', database = '%s', username = '%s', password='%s'\n", func, ci->dsn, ci->server, ci->port, ci->database, ci->username, NAME_IS_VALID(ci->password) ? "xxxxx" : "");

	if (ci->port[0] == '\0')
	{
		CC_set_error(self, CONN_INIREAD_ERROR, "Missing port name in call to CC_connect.", func);
		return 0;
	}
#ifdef	WIN32
	if (ci->server[0] == '\0')
	{
		CC_set_error(self, CONN_INIREAD_ERROR, "Missing server name in call to CC_connect.", func);
		return 0;
	}
#endif /* WIN32 */
	if (ci->database[0] == '\0')
	{
		CC_set_error(self, CONN_INIREAD_ERROR, "Missing database name in call to CC_connect.", func);
		return 0;
	}

	return 1;
}

static	char	CC_setenv(ConnectionClass *self);
static int LIBPQ_connect(ConnectionClass *self);
static char
LIBPQ_CC_connect(ConnectionClass *self, char password_req, char *salt_para)
{
	int		ret;
	CSTR		func = "LIBPQ_CC_connect";

	mylog("%s: entering...\n", func);

	if (password_req == AUTH_REQ_OK) /* not yet connected */
	{
		if (0 == CC_initial_log(self, func))
			return 0;
	}

	if (ret = LIBPQ_connect(self), ret <= 0)
		return ret;
	CC_setenv(self);

	return 1;
}

char
CC_connect(ConnectionClass *self, char password_req, char *salt_para)
{
	ConnInfo *ci = &(self->connInfo);
	CSTR	func = "CC_connect";
	char		ret, *saverr = NULL, retsend;

	mylog("%s: entering...\n", func);

	mylog("sslmode=%s\n", self->connInfo.sslmode);

	ret = LIBPQ_CC_connect(self, password_req, salt_para);
	if (ret <= 0)
		return ret;

	CC_set_translation(self);

	/*
	 * Send any initial settings
	 */

	/*
	 * Since these functions allocate statements, and since the connection
	 * is not established yet, it would violate odbc state transition
	 * rules.  Therefore, these functions call the corresponding local
	 * function instead.
	 */
inolog("CC_send_settings\n");
	retsend = CC_send_settings(self);

	if (CC_get_errornumber(self) > 0)
		saverr = strdup(CC_get_errormsg(self));
	CC_clear_error(self);			/* clear any error */
	CC_lookup_lo(self);			/* a hack to get the oid of
						   our large object oid type */

	/* Multibyte handling */
	CC_lookup_characterset(self);
	if (CC_get_errornumber(self) > 0)
	{
		ret = 0;
		goto cleanup;
	}
#ifdef UNICODE_SUPPORT
	if (CC_is_in_unicode_driver(self))
	{
		if (!self->original_client_encoding ||
		    UTF8 != self->ccsc)
		{
			QResultClass	*res;
			if (self->original_client_encoding)
				free(self->original_client_encoding);
			self->original_client_encoding = NULL;
			if (res = CC_send_query(self, "set client_encoding to 'UTF8'", NULL, 0, NULL), QR_command_maybe_successful(res))
			{
				self->original_client_encoding = strdup("UNICODE");
				self->ccsc = pg_CS_code(self->original_client_encoding);
			}
			QR_Destructor(res);
		}
	}
#else
	{
	}
#endif /* UNICODE_SUPPORT */

	ci->updatable_cursors = DISALLOW_UPDATABLE_CURSORS;
	if (ci->allow_keyset)
	{
		if (ci->drivers.lie || !ci->drivers.use_declarefetch)
			ci->updatable_cursors |= (ALLOW_STATIC_CURSORS | ALLOW_KEYSET_DRIVEN_CURSORS | ALLOW_BULK_OPERATIONS | SENSE_SELF_OPERATIONS);
		else
			ci->updatable_cursors |= (ALLOW_STATIC_CURSORS | SENSE_SELF_OPERATIONS);
	}

	if (CC_get_errornumber(self) > 0)
		CC_clear_error(self);		/* clear any initial command errors */
	self->status = CONN_CONNECTED;
	if (CC_is_in_unicode_driver(self)
	    && 0 < ci->bde_environment)
		self->unicode |= CONN_DISALLOW_WCHAR;
mylog("conn->unicode=%d\n", self->unicode);
	ret = 1;

cleanup:
	mylog("%s: returning...%d\n", func, ret);
	if (NULL != saverr)
	{
		if (ret > 0 && CC_get_errornumber(self) <= 0)
			CC_set_error(self, -1, saverr, func);
		free(saverr);
	}
	if (1 == ret && 0 == retsend)
		ret = 2;

	return ret;
}


char
CC_add_statement(ConnectionClass *self, StatementClass *stmt)
{
	int	i;
	char	ret = TRUE;

	mylog("CC_add_statement: self=%p, stmt=%p\n", self, stmt);

	CONNLOCK_ACQUIRE(self);
	for (i = 0; i < self->num_stmts; i++)
	{
		if (!self->stmts[i])
		{
			stmt->hdbc = self;
			self->stmts[i] = stmt;
			break;
		}
	}

	if (i >= self->num_stmts) /* no more room -- allocate more memory */
	{
		StatementClass **newstmts;
		Int2 new_num_stmts;

		new_num_stmts = STMT_INCREMENT + self->num_stmts;

		if (new_num_stmts > 0)
			newstmts = (StatementClass **)
				realloc(self->stmts, sizeof(StatementClass *) * new_num_stmts);
		else
			newstmts = NULL; /* num_stmts overflowed */
		if (!newstmts)
			ret = FALSE;
		else
		{
			self->stmts = newstmts;
			memset(&self->stmts[self->num_stmts], 0, sizeof(StatementClass *) * STMT_INCREMENT);

			stmt->hdbc = self;
			self->stmts[self->num_stmts] = stmt;

			self->num_stmts = new_num_stmts;
		}
	}
	CONNLOCK_RELEASE(self);

	return ret;
}

static void
CC_set_error_statements(ConnectionClass *self)
{
	int	i;

	mylog("CC_error_statements: self=%p\n", self);

	for (i = 0; i < self->num_stmts; i++)
	{
		if (NULL != self->stmts[i])
			SC_ref_CC_error(self->stmts[i]);
	}
}


char
CC_remove_statement(ConnectionClass *self, StatementClass *stmt)
{
	int	i;
	char	ret = FALSE;

	CONNLOCK_ACQUIRE(self);
	for (i = 0; i < self->num_stmts; i++)
	{
		if (self->stmts[i] == stmt && stmt->status != STMT_EXECUTING)
		{
			self->stmts[i] = NULL;
			ret = TRUE;
			break;
		}
	}
	CONNLOCK_RELEASE(self);

	return ret;
}

int	CC_get_max_idlen(ConnectionClass *self)
{
	int	len = self->max_identifier_length;

	if  (len < 0)
	{
		QResultClass	*res;

		res = CC_send_query(self, "show max_identifier_length", NULL, ROLLBACK_ON_ERROR | IGNORE_ABORT_ON_CONN, NULL);
		if (QR_command_maybe_successful(res))
			len = self->max_identifier_length = QR_get_value_backend_int(res, 0, 0, FALSE);
		QR_Destructor(res);
	}
mylog("max_identifier_length=%d\n", len);
	return len < 0 ? 0 : len;
}

/*
 *	Create a more informative error message by concatenating the connection
 *	error message with its socket error message.
 *
 * XXX: actually, there is no such thing as socket error message anymore
 */
static char *
CC_create_errormsg(ConnectionClass *self)
{
	char	msg[4096];

	mylog("enter CC_create_errormsg\n");

	msg[0] = '\0';

	if (CC_get_errormsg(self))
		strncpy_null(msg, CC_get_errormsg(self), sizeof(msg));

	mylog("msg = '%s'\n", msg);

	mylog("exit CC_create_errormsg\n");
	return strdup(msg);
}


void
CC_set_error(ConnectionClass *self, int number, const char *message, const char *func)
{
	CONNLOCK_ACQUIRE(self);
	if (self->__error_message)
		free(self->__error_message);
	self->__error_number = number;
	self->__error_message = message ? strdup(message) : NULL;
	if (0 != number)
		CC_set_error_statements(self);
	if (func && number != 0)
		CC_log_error(func, "", self);
	CONNLOCK_RELEASE(self);
}


void
CC_set_errormsg(ConnectionClass *self, const char *message)
{
	CONNLOCK_ACQUIRE(self);
	if (self->__error_message)
		free(self->__error_message);
	self->__error_message = message ? strdup(message) : NULL;
	CONNLOCK_RELEASE(self);
}


char
CC_get_error(ConnectionClass *self, int *number, char **message)
{
	int			rv;
	char *msgcrt;

	mylog("enter CC_get_error\n");

	CONNLOCK_ACQUIRE(self);
	/* Create a very informative errormsg if it hasn't been done yet. */
	if (!self->errormsg_created)
	{
		msgcrt = CC_create_errormsg(self);
		if (self->__error_message)
			free(self->__error_message);
		self->__error_message = msgcrt;
		self->errormsg_created = TRUE;
	}

	if (CC_get_errornumber(self))
	{
		*number = CC_get_errornumber(self);
		*message = CC_get_errormsg(self);
	}
	rv = (CC_get_errornumber(self) != 0);

	CONNLOCK_RELEASE(self);

	mylog("exit CC_get_error\n");

	return rv;
}


static int CC_close_eof_cursors(ConnectionClass *self)
{
	int	i, ccount = 0;
	StatementClass	*stmt;
	QResultClass	*res;

	if (!self->ncursors)
		return ccount;
	CONNLOCK_ACQUIRE(self);
	for (i = 0; i < self->num_stmts; i++)
	{
		if (stmt = self->stmts[i], NULL == stmt)
			continue;
		if (res = SC_get_Result(stmt), NULL == res)
			continue;
		if (NULL != QR_get_cursor(res) &&
		    QR_is_withhold(res) &&
		    QR_once_reached_eof(res))
		{
			if (QR_get_num_cached_tuples(res) >= QR_get_num_total_tuples(res) ||
				SQL_CURSOR_FORWARD_ONLY == stmt->options.cursor_type)
			{
				QR_close(res);
				ccount++;
			}
		}
	}
	CONNLOCK_RELEASE(self);
	return ccount;
}

static void CC_clear_cursors(ConnectionClass *self, BOOL on_abort)
{
	int	i;
	StatementClass	*stmt;
	QResultClass	*res;

	if (!self->ncursors)
		return;
	CONNLOCK_ACQUIRE(self);
	for (i = 0; i < self->num_stmts; i++)
	{
		stmt = self->stmts[i];
		if (stmt && (res = SC_get_Result(stmt)) &&
			 (NULL != QR_get_cursor(res)))
		{
			/*
			 * non-holdable cursors are automatically closed
			 * at commit time.
			 * all non-permanent cursors are automatically closed
			 * at rollback time.
			 */
			if ((on_abort && !QR_is_permanent(res)) ||
				!QR_is_withhold(res))
			{
				QR_on_close_cursor(res);
			}
			else if (!QR_is_permanent(res))
			{
				QResultClass	*wres;
				char	cmd[64];

				if (QR_needs_survival_check(res))
				{
					snprintf(cmd, sizeof(cmd), "MOVE 0 in \"%s\"", QR_get_cursor(res));
					CONNLOCK_RELEASE(self);
					wres = CC_send_query(self, cmd, NULL, ROLLBACK_ON_ERROR | IGNORE_ABORT_ON_CONN, NULL);
					QR_set_no_survival_check(res);
					if (QR_command_maybe_successful(wres))
						QR_set_permanent(res);
					else
						QR_set_cursor(res, NULL);
					QR_Destructor(wres);
					CONNLOCK_ACQUIRE(self);
				}
				else
					QR_set_permanent(res);
			}
		}
	}
	CONNLOCK_RELEASE(self);
}

static void CC_mark_cursors_doubtful(ConnectionClass *self)
{
	int	i;
	StatementClass	*stmt;
	QResultClass	*res;

	if (!self->ncursors)
		return;
	CONNLOCK_ACQUIRE(self);
	for (i = 0; i < self->num_stmts; i++)
	{
		stmt = self->stmts[i];
		if (NULL != stmt &&
		    NULL != (res = SC_get_Result(stmt)) &&
		    NULL != QR_get_cursor(res) &&
		    !QR_is_permanent(res))
			QR_set_survival_check(res);
	}
	CONNLOCK_RELEASE(self);
}

void	CC_on_commit(ConnectionClass *conn)
{
	CONNLOCK_ACQUIRE(conn);
	if (CC_is_in_trans(conn))
	{
		CC_set_no_trans(conn);
		CC_set_no_manual_trans(conn);
	}
	CC_clear_cursors(conn, FALSE);
	CONNLOCK_RELEASE(conn);
	CC_discard_marked_objects(conn);
	CONNLOCK_ACQUIRE(conn);
	if (conn->result_uncommitted)
	{
		CONNLOCK_RELEASE(conn);
		ProcessRollback(conn, FALSE, FALSE);
		CONNLOCK_ACQUIRE(conn);
		conn->result_uncommitted = 0;
	}
	CONNLOCK_RELEASE(conn);
}
void	CC_on_abort(ConnectionClass *conn, UDWORD opt)
{
	BOOL	set_no_trans = FALSE;

mylog("CC_on_abort in\n");
	CONNLOCK_ACQUIRE(conn);
	if (0 != (opt & CONN_DEAD)) /* CONN_DEAD implies NO_TRANS also */
		opt |= NO_TRANS;
	if (CC_is_in_trans(conn))
	{
		if (0 != (opt & NO_TRANS))
		{
			CC_set_no_trans(conn);
			CC_set_no_manual_trans(conn);
			set_no_trans = TRUE;
		}
	}
	CC_clear_cursors(conn, TRUE);
	if (0 != (opt & CONN_DEAD))
	{
		conn->status = CONN_DOWN;
		if (conn->pqconn)
		{
			CONNLOCK_RELEASE(conn);
			PQfinish(conn->pqconn);
			CONNLOCK_ACQUIRE(conn);
			conn->pqconn = NULL;
		}
	}
	else if (set_no_trans)
	{
		CONNLOCK_RELEASE(conn);
		CC_discard_marked_objects(conn);
		CONNLOCK_ACQUIRE(conn);
	}
	if (conn->result_uncommitted)
	{
		CONNLOCK_RELEASE(conn);
		ProcessRollback(conn, TRUE, FALSE);
		CONNLOCK_ACQUIRE(conn);
		conn->result_uncommitted = 0;
	}
	CONNLOCK_RELEASE(conn);
}

void	CC_on_abort_partial(ConnectionClass *conn)
{
mylog("CC_on_abort_partial in\n");
	ProcessRollback(conn, TRUE, TRUE);
	CONNLOCK_ACQUIRE(conn);
	CC_discard_marked_objects(conn);
	CONNLOCK_RELEASE(conn);
}

static BOOL
is_setting_search_path(const char *query)
{
	for (query += 4; *query; query++)
	{
		if (!isspace((unsigned char) *query))
		{
			if (strnicmp(query, "search_path", 11) == 0)
				return TRUE;
			query++;
			while (*query && !isspace((unsigned char) *query))
				query++;
		}
	}
	return FALSE;
}

static BOOL
CC_from_PGresult(QResultClass *res, StatementClass *stmt, ConnectionClass *conn, const char *cursor, PGresult *pgres)
{
	BOOL	success = TRUE;

	if (!QR_from_PGresult(res, stmt, conn, cursor, pgres))
	{
		qlog("getting result from PGresult failed\n");
		success = FALSE;
		if (0 >= CC_get_errornumber(conn))
		{
			switch (QR_get_rstatus(res))
			{
				case PORES_NO_MEMORY_ERROR:
					CC_set_error(conn, CONN_NO_MEMORY_ERROR, NULL, __FUNCTION__);
					break;
				case PORES_BAD_RESPONSE:
					CC_set_error(conn, CONNECTION_COMMUNICATION_ERROR, "communication error occured", __FUNCTION__);
					break;
				default:
					CC_set_error(conn, CONN_EXEC_ERROR, QR_get_message(res), __FUNCTION__);
					break;
			}
		}
	}
	return success;
}

/*
 *	The "result_in" is only used by QR_next_tuple() to fetch another group of rows into
 *	the same existing QResultClass (this occurs when the tuple cache is depleted and
 *	needs to be re-filled).
 *
 *	The "cursor" is used by SQLExecute to associate a statement handle as the cursor name
 *	(i.e., C3326857) for SQL select statements.  This cursor is then used in future
 *	'declare cursor C3326857 for ...' and 'fetch 100 in C3326857' statements.
 *
 * * If issue_begin, send "BEGIN"
 * * if needed, send "SAVEPOINT ..."
 * * Send "query", read result
 * * Send appendq, read result.
 *
 */
QResultClass *
CC_send_query_append(ConnectionClass *self, const char *query, QueryInfo *qi, UDWORD flag, StatementClass *stmt, const char *appendq)
{
	CSTR	func = "CC_send_query";
	QResultClass *cmdres = NULL,
			   *retres = NULL,
			   *res = NULL;
	BOOL	ignore_abort_on_conn = ((flag & IGNORE_ABORT_ON_CONN) != 0),
		create_keyset = ((flag & CREATE_KEYSET) != 0),
		issue_begin = ((flag & GO_INTO_TRANSACTION) != 0 && !CC_is_in_trans(self)),
		rollback_on_error, query_rollback, end_with_commit;

	char		*ptr;
	BOOL		ReadyToReturn = FALSE,
				query_completed = FALSE,
				aborted = FALSE,
				used_passed_result_object = FALSE,
			discard_next_begin = FALSE,
			discard_next_savepoint = FALSE,
			consider_rollback;
	int		func_cs_count = 0;
	PQExpBufferData query_buf;

	/* QR_set_command() dups this string so doesn't need static */
	char	   *cmdbuffer;
	BOOL		reduce_round_trip_time = !(flag & IGNORE_ROUND_TRIP);
	PGresult   *pgres;
	notice_receiver_arg nrarg;

	if (appendq)
	{
		mylog("%s_append: conn=%p, query='%s'+'%s'\n", func, self, query, appendq);
		qlog("conn=%p, query='%s'+'%s'\n", self, query, appendq);
	}
	else
	{
		mylog("%s: conn=%p, query='%s'\n", func, self, query);
		qlog("conn=%p, query='%s'\n", self, query);
	}

	if (!self->pqconn)
	{
		CC_set_error(self, CONNECTION_COULD_NOT_SEND, "Could not send Query(connection dead)", func);
		CC_on_abort(self, CONN_DEAD);
		return NULL;
	}

	ENTER_INNER_CONN_CS(self, func_cs_count);
/* Indicate that we are sending a query to the backend */
	if ((NULL == query) || (query[0] == '\0'))
	{
		CLEANUP_FUNC_CONN_CS(func_cs_count, self);
		return NULL;
	}

	/*
	 *	In case the round trip time can be ignored, the query
	 *	and the appeneded query would be issued separately.
	 *	Otherwise a multiple command query would be issued.
	 */
	if (appendq && !reduce_round_trip_time)
	{
		res = CC_send_query_append(self, query, qi, flag, stmt, NULL);
		if (QR_command_maybe_successful(res))
		{
			cmdres = CC_send_query_append(self, appendq, qi, flag & (~(GO_INTO_TRANSACTION)), stmt, NULL);
			if (QR_command_maybe_successful(cmdres))
				res->next = cmdres;
			else
			{
				QR_Destructor(res);
				res = cmdres;
			}
		}
		CLEANUP_FUNC_CONN_CS(func_cs_count, self);
		return res;
	}

	rollback_on_error = (flag & ROLLBACK_ON_ERROR) != 0;
	end_with_commit = (flag & END_WITH_COMMIT) != 0;
#define	return DONT_CALL_RETURN_FROM_HERE???
	consider_rollback = (issue_begin || (CC_is_in_trans(self) && !CC_is_in_error_trans(self)) || strnicmp(query, "begin", 5) == 0);
	if (rollback_on_error)
		rollback_on_error = consider_rollback;
	query_rollback = (rollback_on_error && !end_with_commit && PG_VERSION_GE(self, 8.0));
	if (!query_rollback && consider_rollback && !end_with_commit)
	{
		if (stmt)
		{
			StatementClass	*astmt = SC_get_ancestor(stmt);
			if (!SC_accessed_db(astmt))
			{
				if (SQL_ERROR == SetStatementSvp(astmt))
				{
					SC_set_error(stmt, STMT_INTERNAL_ERROR, "internal savepoint error", func);
					goto cleanup;
				}
			}
		}
	}

	/* XXX: append all these together, to avoid round-trips */
	initPQExpBuffer(&query_buf);
	if (issue_begin)
	{
		appendPQExpBufferStr(&query_buf, bgncmd);
		appendPQExpBufferChar(&query_buf, ';');
		discard_next_begin = TRUE;
	}
	if (query_rollback)
	{
		appendPQExpBuffer(&query_buf, "%s %s;", svpcmd, per_query_svp);
		discard_next_savepoint = TRUE;
	}
	appendPQExpBufferStr(&query_buf, query);
	if (appendq)
	{
		appendPQExpBufferChar(&query_buf, ';');
		appendPQExpBufferStr(&query_buf, appendq);
	}
	if (query_rollback)
		appendPQExpBuffer(&query_buf, ";%s %s", rlscmd, per_query_svp);

	/* Set up notice receiver */
	nrarg.conn = self;
	nrarg.comment = func;
	nrarg.res = NULL;
	PQsetNoticeReceiver(self->pqconn, receive_libpq_notice, &nrarg);

	if(!PQsendQuery(self->pqconn, query_buf.data))
	{
		char *errmsg = PQerrorMessage(self->pqconn);
		CC_set_error(self, CONNECTION_SERVER_NOT_REACHED, errmsg, func);
		goto cleanup;
	}

	cmdres = qi ? qi->result_in : NULL;
	if (cmdres)
		used_passed_result_object = TRUE;
	else
	{
		cmdres = QR_Constructor();
		if (!cmdres)
		{
			CC_set_error(self, CONNECTION_COULD_NOT_RECEIVE, "Could not create result info in send_query.", func);
			goto cleanup;
		}
	}
	res = cmdres;
	nrarg.res = res;

	while (self->pqconn && (pgres = PQgetResult(self->pqconn)) != NULL)
	{
		int status = PQresultStatus(pgres);
		switch (status)
		{
			case PGRES_COMMAND_OK:
				/* portal query command, no tuples returned */
				/* read in the return message from the backend */
				cmdbuffer = PQcmdStatus(pgres);
				mylog("send_query: ok - 'C' - %s\n", cmdbuffer);

				if (query_completed)	/* allow for "show" style notices */
				{
					res->next = QR_Constructor();
					res = res->next;
					nrarg.res = res;
				}

				mylog("send_query: setting cmdbuffer = '%s'\n", cmdbuffer);

				my_trim(cmdbuffer); /* get rid of trailing space */
				if (strnicmp(cmdbuffer, bgncmd, strlen(bgncmd)) == 0)
				{
					CC_set_in_trans(self);
					if (discard_next_begin) /* discard the automatically issued BEGIN */
					{
						discard_next_begin = FALSE;
						continue; /* discard the result */
					}
				}
				else if (strnicmp(cmdbuffer, svpcmd, strlen(svpcmd)) == 0)
				{
					if (discard_next_savepoint)
					{
inolog("Discarded the first SAVEPOINT\n");
						discard_next_savepoint = FALSE;
						continue; /* discard the result */
					}
				}
				else if (strnicmp(cmdbuffer, rbkcmd, strlen(rbkcmd)) == 0)
				{
					CC_mark_cursors_doubtful(self);
					CC_set_in_error_trans(self); /* mark the transaction error in case of manual rollback */
				}
				/*
				 *	DROP TABLE or ALTER TABLE may change
				 *	the table definition. So clear the
				 *	col_info cache though it may be too simple.
				 */
				else if (strnicmp(cmdbuffer, "DROP TABLE", 10) == 0 ||
						 strnicmp(cmdbuffer, "ALTER TABLE", 11) == 0)
					CC_clear_col_info(self, FALSE);
				else
				{
					ptr = strrchr(cmdbuffer, ' ');
					if (ptr)
						res->recent_processed_row_count = atoi(ptr + 1);
					else
						res->recent_processed_row_count = -1;
					if (NULL != self->current_schema &&
						strnicmp(cmdbuffer, "SET", 3) == 0)
					{
						if (is_setting_search_path(query))
							reset_current_schema(self);
					}
				}

				if (QR_command_successful(res))
					QR_set_rstatus(res, PORES_COMMAND_OK);
				QR_set_command(res, cmdbuffer);
				query_completed = TRUE;
				mylog("send_query: returning res = %p\n", res);
				break;

			case PGRES_EMPTY_QUERY:
				/* We return the empty query */
				QR_set_rstatus(res, PORES_EMPTY_QUERY);
				break;
			case PGRES_NONFATAL_ERROR:
				handle_pgres_error(self, pgres, "send_query", res, FALSE);
				break;

			case PGRES_BAD_RESPONSE:
			case PGRES_FATAL_ERROR:
				handle_pgres_error(self, pgres, "send_query", res, TRUE);

				/* We should report that an error occured. Zoltan */
				aborted = TRUE;

				query_completed = TRUE;
				break;
			case PGRES_TUPLES_OK:
				if (query_completed)
				{
					res->next = QR_Constructor();
					if (!res->next)
					{
						CC_set_error(self, CONNECTION_COULD_NOT_RECEIVE, "Could not create result info in send_query.", func);
						ReadyToReturn = TRUE;
						retres = NULL;
						break;
					}
					if (create_keyset)
					{
						QR_set_haskeyset(res->next);
						if (stmt)
							res->next->num_key_fields = stmt->num_key_fields;
					}
					mylog("send_query: 'T' no result_in: res = %p\n", res->next);
					res = res->next;
					nrarg.res = res;

					if (qi)
						QR_set_cache_size(res, qi->row_size);
				}
				if (!used_passed_result_object)
				{
					const char *cursor = qi ? qi->cursor : NULL;
					if (create_keyset)
					{
						QR_set_haskeyset(res);
						if (stmt)
							res->num_key_fields = stmt->num_key_fields;
						if (cursor && cursor[0])
							QR_set_synchronize_keys(res);
					}
					if (!CC_from_PGresult(res, stmt, self, cursor, pgres))
					{
						if (QR_command_maybe_successful(res))
							retres = NULL;
						else
							retres = cmdres;
						aborted = TRUE;
					}
					query_completed = TRUE;
				}
				else
				{				/* next fetch, so reuse an existing result */
					const char *cursor = res->cursor_name;

					/*
					 * called from QR_next_tuple and must return
					 * immediately.
					 */
					if (!CC_from_PGresult(res, stmt, self, cursor, pgres))
					{
						retres = NULL;
						break;
					}
					retres = cmdres;
				}
				if (res->rstatus == PORES_TUPLES_OK && res->notice)
				{
					QR_set_rstatus(res, PORES_NONFATAL_ERROR);
				}
				break;
			case PGRES_COPY_OUT:
				/* XXX: We used to read from stdin here. Does that make any sense? */
			case PGRES_COPY_IN:
				if (query_completed)
				{
					res->next = QR_Constructor();
					res = res->next;
					nrarg.res = res;
				}
				QR_set_rstatus(res, PORES_COPY_IN);
				ReadyToReturn = TRUE;
				retres = cmdres;
				break;
			case PGRES_COPY_BOTH:
			default:
				/* skip the unexpected response if possible */
				CC_set_error(self, CONNECTION_BACKEND_CRAZY, "Unexpected protocol character from backend (send_query)", func);
				CC_on_abort(self, CONN_DEAD);

				mylog("send_query: error - %s\n", CC_get_errormsg(self));
				ReadyToReturn = TRUE;
				retres = NULL;
				break;
		}
	}

cleanup:
	if (self->pqconn)
		PQsetNoticeReceiver(self->pqconn, receive_libpq_notice, NULL);
	if (rollback_on_error && CC_is_in_trans(self) && !discard_next_savepoint)
	{
		if (query_rollback)
		{
			if (CC_is_in_error_trans(self))
			{
				printfPQExpBuffer(&query_buf, "%s TO %s; %s %s",
								  rbkcmd, per_query_svp,
								  rlscmd, per_query_svp);
				PQexec(self->pqconn, query_buf.data);
			}
		}
		else if (CC_is_in_error_trans(self))
			PQexec(self->pqconn, rbkcmd);
	}

	CLEANUP_FUNC_CONN_CS(func_cs_count, self);
#undef	return
	/*
	 * Break before being ready to return.
	 */
	if (!ReadyToReturn)
		retres = cmdres;

	/*
	 * Cleanup garbage results before returning.
	 */
	if (cmdres && retres != cmdres && !used_passed_result_object)
		QR_Destructor(cmdres);
	/*
	 * Cleanup the aborted result if specified
	 */
	if (retres)
	{
		if (aborted)
		{
			/** if (ignore_abort_on_conn)
			{
				if (!used_passed_result_object)
				{
					QR_Destructor(retres);
					retres = NULL;
				}
			} **/
			if (retres)
			{
				/*
				 *	discard results other than errors.
				 */
				QResultClass	*qres;
				for (qres = retres; qres->next; qres = retres)
				{
					if (QR_get_aborted(qres))
						break;
					retres = qres->next;
					qres->next = NULL;
					QR_Destructor(qres);
				}
				/*
				 *	If error message isn't set
				 */
				if (ignore_abort_on_conn)
					CC_set_errornumber(self, 0);
				else if (retres)
				{
					if (NULL == CC_get_errormsg(self) ||
					    !CC_get_errormsg(self)[0])
						CC_set_errormsg(self, QR_get_message(retres));
					if (!self->sqlstate[0])
						strcpy(self->sqlstate, retres->sqlstate);
				}
			}
		}
	}

	/*
	 * Update our copy of the transaction status.
	 *
	 * XXX: Once we stop using the socket directly, and do everything with
	 * libpq, we can get rid of the transaction_status field altogether
	 * and always ask libpq for it.
	 */
	LIBPQ_update_transaction_status(self);

	return retres;
}

#define MAX_SEND_FUNC_ARGS	3
static const char *func_param_str[MAX_SEND_FUNC_ARGS + 1] =
{
	"()",
	"($1)",
	"($1, $2)",
	"($1, $2, $3)"
};


int
CC_send_function(ConnectionClass *self, const char *fn_name, void *result_buf, int *actual_result_len, int result_is_int, LO_ARG *args, int nargs)
{
	int			i;
	int			ret = FALSE;
	int			func_cs_count = 0;
	char		sqlbuffer[1000];
	PGresult   *pgres = NULL;
	Oid			paramTypes[MAX_SEND_FUNC_ARGS];
	char	   *paramValues[MAX_SEND_FUNC_ARGS];
	int			paramLengths[MAX_SEND_FUNC_ARGS];
	int			paramFormats[MAX_SEND_FUNC_ARGS];
	Int4		intParamBufs[MAX_SEND_FUNC_ARGS];

	mylog("send_function(): conn=%p, fn_name=%s, result_is_int=%d, nargs=%d\n", self, fn_name, result_is_int, nargs);

	/* Finish the pending extended query first */
#define	return DONT_CALL_RETURN_FROM_HERE???
	ENTER_INNER_CONN_CS(self, func_cs_count);

	snprintf(sqlbuffer, sizeof(sqlbuffer), "SELECT pg_catalog.%s%s", fn_name,
			 func_param_str[nargs]);
	for (i = 0; i < nargs; ++i)
	{
		mylog("  arg[%d]: len = %d, isint = %d, integer = %d, ptr = %p\n", i, args[i].len, args[i].isint, args[i].u.integer, args[i].u.ptr);

		/* integers are sent as binary, others as text */
		if (args[i].isint)
		{
			paramTypes[i] = PG_TYPE_INT4;
			intParamBufs[i] = htonl(args[i].u.integer);
			paramValues[i] = (char *) &intParamBufs[i];
			paramLengths[i] = 4;
			paramFormats[i] = 1;
		}
		else
		{
			paramTypes[i] = 0;
			paramValues[i] = args[i].u.ptr;
			paramLengths[i] = args[i].len;
			paramFormats[i] = 1;
		}
	}

	pgres = PQexecParams(self->pqconn, sqlbuffer, nargs,
						 paramTypes, (const char * const *) paramValues,
						 paramLengths, paramFormats, 1);

	mylog("send_function: done sending function\n");

	if (PQresultStatus(pgres) != PGRES_TUPLES_OK)
	{
		handle_pgres_error(self, pgres, "send_query", NULL, TRUE);
		goto cleanup;
	}

	if (PQnfields(pgres) != 1 || PQntuples(pgres) != 1)
	{
		CC_set_errormsg(self, "unexpected result set from large_object function");
		goto cleanup;
	}

	*actual_result_len = PQgetlength(pgres, 0, 0);

	mylog("send_function(): got result with length %d\n", *actual_result_len);

	if (*actual_result_len > 0)
	{
		char *value = PQgetvalue(pgres, 0, 0);
		if (result_is_int)
		{
			Int4 int4val;
			memcpy(&int4val, value, sizeof(Int4));
			int4val = ntohl(int4val);
			memcpy(result_buf, &int4val, sizeof(Int4));
		}
		else
			memcpy(result_buf, value, *actual_result_len);
	}

	ret = TRUE;

cleanup:
#undef	return
	CLEANUP_FUNC_CONN_CS(func_cs_count, self);
	if (pgres)
		PQclear(pgres);
	return ret;
}


static	char
CC_setenv(ConnectionClass *self)
{
	HSTMT		hstmt;
	StatementClass *stmt;
	RETCODE		result;
	char		status = TRUE;
	CSTR func = "CC_setenv";


	mylog("%s: entering...\n", func);

/*
 *	This function must use the local odbc API functions since the odbc state
 *	has not transitioned to "connected" yet.
 */

	result = PGAPI_AllocStmt(self, &hstmt, 0);
	if (!SQL_SUCCEEDED(result))
		return FALSE;
	stmt = (StatementClass *) hstmt;

	stmt->internal = TRUE;		/* ensure no BEGIN/COMMIT/ABORT stuff */

	/* Set the Datestyle to the format the driver expects it to be in */
	result = PGAPI_ExecDirect(hstmt, (SQLCHAR *) "set DateStyle to 'ISO'", SQL_NTS, 0);
	if (!SQL_SUCCEEDED(result))
		status = FALSE;

	mylog("%s: result %d, status %d from set DateStyle\n", func, result, status);

	result = PGAPI_ExecDirect(hstmt, (SQLCHAR *) "set extra_float_digits to 2", SQL_NTS, 0);
	if (!SQL_SUCCEEDED(result))
		status = FALSE;

	mylog("%s: result %d, status %d from set extra_float_digits\n", func, result, status);

	PGAPI_FreeStmt(hstmt, SQL_DROP);

	return status;
}

char
CC_send_settings(ConnectionClass *self)
{
	/* char ini_query[MAX_MESSAGE_LEN]; */
	ConnInfo   *ci = &(self->connInfo);

/* QResultClass *res; */
	HSTMT		hstmt;
	StatementClass *stmt;
	RETCODE		result;
	char		status = TRUE;
	char	   *cs,
			   *ptr;
#ifdef	HAVE_STRTOK_R
	char	*last;
#endif /* HAVE_STRTOK_R */
	CSTR func = "CC_send_settings";


	mylog("%s: entering...\n", func);

/*
 *	This function must use the local odbc API functions since the odbc state
 *	has not transitioned to "connected" yet.
 */

	result = PGAPI_AllocStmt(self, &hstmt, 0);
	if (!SQL_SUCCEEDED(result))
		return FALSE;
	stmt = (StatementClass *) hstmt;

	stmt->internal = TRUE;		/* ensure no BEGIN/COMMIT/ABORT stuff */

	/* Global settings */
	if (NAME_IS_VALID(ci->drivers.conn_settings))
	{
		cs = strdup(GET_NAME(ci->drivers.conn_settings));
		if (cs)
		{
#ifdef	HAVE_STRTOK_R
			ptr = strtok_r(cs, ";", &last);
#else
			ptr = strtok(cs, ";");
#endif /* HAVE_STRTOK_R */
			while (ptr)
			{
				result = PGAPI_ExecDirect(hstmt, (SQLCHAR *) ptr, SQL_NTS, 0);
				if (!SQL_SUCCEEDED(result))
					status = FALSE;

				mylog("%s: result %d, status %d from '%s'\n", func, result, status, ptr);

#ifdef	HAVE_STRTOK_R
				ptr = strtok_r(NULL, ";", &last);
#else
				ptr = strtok(NULL, ";");
#endif /* HAVE_STRTOK_R */
			}
			free(cs);
		}
		else
			status = FALSE;
	}

	/* Per Datasource settings */
	if (NAME_IS_VALID(ci->conn_settings))
	{
		cs = strdup(GET_NAME(ci->conn_settings));
		if (cs)
		{
#ifdef	HAVE_STRTOK_R
			ptr = strtok_r(cs, ";", &last);
#else
			ptr = strtok(cs, ";");
#endif /* HAVE_STRTOK_R */
			while (ptr)
			{
				result = PGAPI_ExecDirect(hstmt, (SQLCHAR *) ptr, SQL_NTS, 0);
				if (!SQL_SUCCEEDED(result))
					status = FALSE;

				mylog("%s: result %d, status %d from '%s'\n", func, result, status, ptr);

#ifdef	HAVE_STRTOK_R
				ptr = strtok_r(NULL, ";", &last);
#else
				ptr = strtok(NULL, ";");
#endif /* HAVE_STRTOK_R */
			}
			free(cs);
		}
		else
			status = FALSE;
	}

	PGAPI_FreeStmt(hstmt, SQL_DROP);

	return status;
}


/*
 *	This function is just a hack to get the oid of our Large Object oid type.
 *	If a real Large Object oid type is made part of Postgres, this function
 *	will go away and the define 'PG_TYPE_LO' will be updated.
 */
static void
CC_lookup_lo(ConnectionClass *self)
{
	QResultClass	*res;
	CSTR func = "CC_lookup_lo";

	mylog("%s: entering...\n", func);

	res = CC_send_query(self, "select oid, typbasetype from pg_type where typname = '"  PG_TYPE_LO_NAME "'",
		NULL, IGNORE_ABORT_ON_CONN | ROLLBACK_ON_ERROR, NULL);
	if (QR_command_maybe_successful(res) && QR_get_num_cached_tuples(res) > 0)
	{
		OID	basetype;

		self->lobj_type = QR_get_value_backend_int(res, 0, 0, NULL);
		basetype = QR_get_value_backend_int(res, 0, 1, NULL);
		if (PG_TYPE_OID == basetype)
			self->lo_is_domain = 1;
		else if (0 != basetype)
			self->lobj_type = 0;
	}
	QR_Destructor(res);
	mylog("Got the large object oid: %d\n", self->lobj_type);
	qlog("    [ Large Object oid = %d ]\n", self->lobj_type);
	return;
}


/*
 *	This function initializes the version of PostgreSQL from
 *	connInfo.protocol that we're connected to.
 *	h-inoue 01-2-2001
 */
void
CC_initialize_pg_version(ConnectionClass *self)
{
	strcpy(self->pg_version, "7.4");
	self->pg_version_number = (float) 7.4;
	self->pg_version_major = 7;
	self->pg_version_minor = 4;
}


void
CC_log_error(const char *func, const char *desc, const ConnectionClass *self)
{
#ifdef PRN_NULLCHECK
#define nullcheck(a) (a ? a : "(NULL)")
#endif

	if (self)
	{
		qlog("CONN ERROR: func=%s, desc='%s', errnum=%d, errmsg='%s'\n", func, desc, self->__error_number, nullcheck(self->__error_message));
		mylog("CONN ERROR: func=%s, desc='%s', errnum=%d, errmsg='%s'\n", func, desc, self->__error_number, nullcheck(self->__error_message));
		qlog("            ------------------------------------------------------------\n");
		qlog("            henv=%p, conn=%p, status=%u, num_stmts=%d\n", self->henv, self, self->status, self->num_stmts);
		qlog("            pqconn=%p, stmts=%p, lobj_type=%d\n", self->pqconn, self->stmts, self->lobj_type);
	}
	else
{
		qlog("INVALID CONNECTION HANDLE ERROR: func=%s, desc='%s'\n", func, desc);
		mylog("INVALID CONNECTION HANDLE ERROR: func=%s, desc='%s'\n", func, desc);
}
#undef PRN_NULLCHECK
}

/*
 *	This doesn't really return the CURRENT SCHEMA
 *	but there's no alternative.
 */
const char *
CC_get_current_schema(ConnectionClass *conn)
{
	if (!conn->current_schema)
	{
		QResultClass	*res;

		if (res = CC_send_query(conn, "select current_schema()", NULL, IGNORE_ABORT_ON_CONN | ROLLBACK_ON_ERROR, NULL), QR_command_maybe_successful(res))
		{
			if (QR_get_num_total_tuples(res) == 1)
				conn->current_schema = strdup(QR_get_value_backend_text(res, 0, 0));
		}
		QR_Destructor(res);
	}
	return (const char *) conn->current_schema;
}

int	CC_mark_a_object_to_discard(ConnectionClass *conn, int type, const char *plan)
{
	int	cnt = conn->num_discardp + 1;
	char	*pname;

	CC_REALLOC_return_with_error(conn->discardp, char *,
		(cnt * sizeof(char *)), conn, "Couldn't alloc discardp.", -1);
	CC_MALLOC_return_with_error(pname, char, (strlen(plan) + 2),
		conn, "Couldn't alloc discardp mem.", -1);
	pname[0] = (char) type;	/* 's':prepared statement 'p':cursor */
	strcpy(pname + 1, plan);
	conn->discardp[conn->num_discardp++] = pname;

	return 1;
}

int	CC_discard_marked_objects(ConnectionClass *conn)
{
	int	i, cnt;
	QResultClass *res;
	char	*pname, cmd[64];

	if ((cnt = conn->num_discardp) <= 0)
		return 0;
	for (i = cnt - 1; i >= 0; i--)
	{
		pname = conn->discardp[i];
		if ('s' == pname[0])
			snprintf(cmd, sizeof(cmd), "DEALLOCATE \"%s\"", pname + 1);
		else
			snprintf(cmd, sizeof(cmd), "CLOSE \"%s\"", pname + 1);
		res = CC_send_query(conn, cmd, NULL, ROLLBACK_ON_ERROR | IGNORE_ABORT_ON_CONN, NULL);
		QR_Destructor(res);
		free(conn->discardp[i]);
		conn->num_discardp--;
	}

	return 1;
}

static void
LIBPQ_update_transaction_status(ConnectionClass *self)
{
	BOOL    was_in_error_trans = CC_is_in_error_trans(self);

	if (!self->pqconn)
		return;

	switch (PQtransactionStatus(self->pqconn))
	{
		case PQTRANS_IDLE:
			if (CC_is_in_trans(self))
			{
				if (CC_is_in_error_trans(self))
					CC_on_abort(self, NO_TRANS);
				else
					CC_on_commit(self);
			}
			break;

		case PQTRANS_INERROR:
			CC_set_in_trans(self);
			CC_set_in_error_trans(self);
			break;

		case PQTRANS_ACTIVE:
			CC_set_in_trans(self);
			CC_set_no_error_trans(self);
			if (was_in_error_trans)
				CC_on_abort_partial(self);
			break;

		default: 			/* unknown status */
			break;
	}
}

static int
LIBPQ_connect(ConnectionClass *self)
{
	CSTR	func = "LIBPQ_connect";
	char	ret = 0;
	char *conninfo = NULL;
	void		*pqconn = NULL;
	int		pqret;
	BOOL	libpqLoaded;
	int		pversion;
	const char *param_val;

	mylog("connecting to the database  using %s as the server\n",self->connInfo.server);

#ifdef	NOT_USED	/* currently not yet used */
	if (FALSE && connect_with_param_available())
	{
		const char *opts[PROTOCOL3_OPTS_MAX], *vals[PROTOCOL3_OPTS_MAX];

		protocol3_opts_array(self, opts, vals, TRUE, sizeof(opts) / sizeof(opts[0]));
		pqconn = CALL_PQconnectdbParams(opts, vals, &libpqLoaded);
	}
	else
#endif /* NOT_USED */
	{
		if (!(conninfo = protocol3_opts_build(self)))
		{
			if (CC_get_errornumber(self) <= 0)
				CC_set_error(self, CONN_OPENDB_ERROR, "Couldn't allcate conninfo", func);
			goto cleanup1;
		}
		pqconn = CALL_PQconnectdb(conninfo, &libpqLoaded);
		free(conninfo);
	}
	if (!libpqLoaded)
	{
		CC_set_error(self, CONN_UNABLE_TO_LOAD_DLL, "Couldn't load libpq library", func);
		goto cleanup1;
	}
	if (!pqconn)
	{
		CC_set_error(self, CONN_OPENDB_ERROR, "PQconnectdb error", func);
		goto cleanup1;
	}
	self->pqconn = pqconn;
	pqret = PQstatus(pqconn);
	if (CONNECTION_OK != pqret)
	{
		const char	*errmsg;
inolog("status=%d\n", pqret);
		errmsg = PQerrorMessage(pqconn);
		CC_set_error(self, CONNECTION_SERVER_NOT_REACHED, errmsg, func);
		if (CONNECTION_BAD == pqret && strstr(errmsg, "no password"))
		{
			mylog("password retry\n");
			PQfinish(pqconn);
			self->pqconn = NULL;
			return -1;
		}
		mylog("Could not establish connection to the database; LIBPQ returned -> %s\n", errmsg);
		goto cleanup1;
	}
	ret = 1;

cleanup1:
	if (!ret)
	{
		if (self->pqconn)
			PQfinish(self->pqconn);
		self->pqconn = NULL;
		return ret;
	}
	mylog("libpq connection to the database succeeded.\n");
	ret = 0;
	pversion = PQprotocolVersion(pqconn);
	if (pversion < 3)
	{
		mylog("Protocol version %d is not supported\n", pversion);
		goto cleanup1;
	}
	mylog("protocol=%d\n", pversion);

	pversion = PQserverVersion(pqconn);
	self->pg_version_major = pversion / 10000;
	self->pg_version_minor = (pversion % 10000) / 100;
	sprintf(self->pg_version, "%d.%d.%d",  self->pg_version_major, self->pg_version_minor, pversion % 100);
	self->pg_version_number = (float) atof(self->pg_version);

	param_val = PQparameterStatus(pqconn, std_cnf_strs);
	if (param_val != NULL)
	{
		if (stricmp(param_val, "on") == 0)
			self->escape_in_literal = '\0';
	}

	param_val = PQparameterStatus(pqconn, "client_encoding");
	if (param_val != NULL)
	{
		self->current_client_encoding = strdup(param_val);
	}

	mylog("Server version=%s\n", self->pg_version);
	ret = 1;
	if (ret)
	{
		if (!CC_get_username(self)[0])
		{
			mylog("PQuser=%s\n", PQuser(pqconn));
			strcpy(self->connInfo.username, PQuser(pqconn));
		}
	}
	else
	{
		if (self->pqconn)
		{
			PQfinish(self->pqconn);
			self->pqconn = NULL;
		}
	}

	mylog("%s: retuning %d\n", func, ret);
	return ret;
}

int
CC_send_cancel_request(const ConnectionClass *conn)
{
	int	ret = 0;
	char	errbuf[256];
	void	*cancel;

	/* Check we have an open connection */
	if (!conn || !conn->pqconn)
		return FALSE;

	cancel = PQgetCancel(conn->pqconn);
	if (!cancel)
		return FALSE;
	ret = PQcancel(cancel, errbuf, sizeof(errbuf));
	PQfreeCancel(cancel);
	if (1 == ret)
		return TRUE;
	else
		return FALSE;
}

const char *CurrCat(const ConnectionClass *conn)
{
	/*
	 * Returning the database name causes problems in MS Query. It
	 * generates query like: "SELECT DISTINCT a FROM byronnbad3
	 * bad3"
	 */
	if (isMsQuery())	/* MS Query */
		return NULL;
	return conn->connInfo.database;
}

const char *CurrCatString(const ConnectionClass *conn)
{
	const char *cat = CurrCat(conn);

	if (!cat)
		cat = NULL_STRING;
	return cat;
}

#ifdef	_HANDLE_ENLIST_IN_DTC_
	/*
	 *	Export the following functions so that the pgenlist dll
	 *	can handle ConnectionClass objects as opaque ones.
	 */

#define	_PGDTC_FUNCS_IMPLEMENT_
#include "connexp.h"

#define	SYNC_AUTOCOMMIT(conn)	(SQL_AUTOCOMMIT_OFF != conn->connInfo.autocommit_public ? (conn->transact_status |= CONN_IN_AUTOCOMMIT) : (conn->transact_status &= ~CONN_IN_AUTOCOMMIT))

DLL_DECLARE void PgDtc_create_connect_string(void *self, char *connstr, int strsize)
{
	ConnectionClass	*conn = (ConnectionClass *) self;
	ConnInfo *ci = &(conn->connInfo);
	const char *drivername = ci->drivername;
	char	xaOptStr[32];

#if defined(_WIN32) && !defined(_WIN64)
	/*
	 * If this is an x86 driver running on an x64 host then the driver name
	 * passed to MSDTC must be the (x64) driver but the client app will be
	 * using the 32-bit driver name. So MSDTC.exe will fail to find the driver
	 * and we'll fail to recover XA transactions.
	 *
	 * IsWow64Process(...) would be the ideal function for this, but is only
	 * available on Windows 6+ (Vista/2k8). We'd use GetNativeSystemInfo, which
	 * is supported on XP and 2k3, instead, but that won't link with older
	 * SDKs.
	 *
	 * It's impler to just test the PROCESSOR_ARCHITEW6432 environment
	 * variable.
	 *
	 * See http://www.postgresql.org/message-id/53A45B59.70303@2ndquadrant.com
	 * for details on this issue.
	 */
	const char * const procenv = getenv("PROCESSOR_ARCHITEW6432");
	if (procenv != NULL && strcmp(procenv, "AMD64") == 0)
	{
		/*
		 * We're a 32-bit binary running under SysWow64 on a 64-bit host and need
		 * to pass a different driver name.
		 *
		 * To avoid playing memory management games, just return a different
		 * string constant depending on the unicode-ness of the driver.
		 *
		 * It probably doesn't matter whether we use the Unicode or ANSI driver
		 * for the XA transaction manager, but pick the same as the client driver
		 * to keep things as similar as possible.
		 */
#ifdef UNICODE_SUPPORT
		drivername = DBMS_NAME_UNICODE"(x64)";
#else
		drivername = DBMS_NAME_ANSI"(x64)";
#endif
	}
#endif // _WIN32 &&  !_WIN64

	if (0 >= ci->xa_opt)	return;
	switch (ci->xa_opt)
	{
		case DTC_CHECK_LINK_ONLY:
		case DTC_CHECK_BEFORE_LINK:
			sprintf(xaOptStr, KEYWORD_DTC_CHECK "=0;");
			break;
		case DTC_CHECK_RM_CONNECTION:
			sprintf(xaOptStr, KEYWORD_DTC_CHECK "=1;");
			break;
		default:
			*xaOptStr = '\0';
			break;
	}
	snprintf(connstr, strsize, "DRIVER={%s};"
				"%s"
				"SERVER=%s;PORT=%s;DATABASE=%s;UID=%s;PWD=%s;" ABBR_SSLMODE "=%s"
		,

		drivername, xaOptStr
		, ci->server, ci->port, ci->database, ci->username, SAFE_NAME(ci->password), ci->sslmode
		);
	return;
}

#define SECURITY_WIN32
#include <security.h>
DLL_DECLARE int PgDtc_is_recovery_available(void *self, char *reason, int rsize)
{
	ConnectionClass	*conn = (ConnectionClass *) self;
	ConnInfo *ci = &(conn->connInfo);
	int	ret = -1;	// inknown
	LONG	nameSize;
	char	loginUser[256];
	BOOL	outReason = FALSE;
	BOOL	doubtRootCert = TRUE, doubtCert = TRUE, doubtSspi = TRUE;
	const char *delim;

	/*
	 *	Root certificate is used?
	 */
	if (NULL != reason &&
	    rsize > 0)
		outReason = TRUE;
	/*
	 *	Root certificate is used?
	 */
	doubtRootCert = FALSE;
	if (0 == stricmp(ci->sslmode, SSLMODE_VERIFY_CA) ||
	    0 == stricmp(ci->sslmode, SSLMODE_VERIFY_FULL))
	{
		if (outReason)
			strncpy_null(reason, "sslmode verify-[ca|full]", rsize);
		return 0;
	}

	/*
	 *	Client certificate is used?
	 *	There seems no way to check it.
	 */
	doubtCert = FALSE;
#ifdef	USE_SSL
	if (NULL != sock->ssl)
		doubtCert = TRUE;
#endif /* USE_SSL */
#ifdef	USE_SSPI
	if (0 != (sock->sspisvcs & SchannelService))
		doubtCert = TRUE;
#endif	/* USE_SSPI */

	/*
	 *	Sspi authentication is used?
	 */
	doubtSspi = FALSE;
#ifdef	USE_SSPI
	if (0 != (sock->sspisvcs & (KerberosService | NegotiateService)))
	{
		if (outReason)
			strncpy_null(reason, "sspi authentication", rsize);
		return 0;
	}
#endif	/* USE_SSPI */
	{
		nameSize = sizeof(loginUser);
		if (GetUserNameEx(NameUserPrincipal, loginUser, &nameSize))
		{
			doubtSspi = TRUE;
			mylog("loginUser=%s\n", loginUser);
		}
		else
		{
			int err = GetLastError();
			switch (err)
			{
				case ERROR_NONE_MAPPED:
					mylog("The user name is unavailable in the specified format\n");
					break;
				case ERROR_NO_SUCH_DOMAIN:
					mylog("The domain controller is unavailable to perform the lookup\n");
					break;
				case ERROR_MORE_DATA:
					doubtSspi = TRUE;
					mylog("The buffer is too small\n");
					break;
				default:
					mylog("GetUserNameEx error=%d\n", err);
					break;
			}
		}
	}

	ret = 1;
	if (outReason)
		*reason = '\0';
	delim = "";
	if (doubtRootCert)
	{
		if (outReason)
			snprintf(reason, rsize, "%s%ssslmode verify-[ca|full]", reason, delim);
		delim = ", ";
		ret = -1;
	}
	if (doubtCert)
	{
		if (outReason)
			snprintf(reason, rsize, "%s%scertificate", reason, delim);
		delim = ", ";
		ret = -1;
	}
	if (doubtCert)
	{
		if (outReason)
			snprintf(reason, rsize, "%s%ssspi", reason, delim);
		delim = ", ";
		ret = -1;
	}
	return ret;
}

DLL_DECLARE void PgDtc_set_async(void *self, void *async)
{
	ConnectionClass	*conn = (ConnectionClass *) self;

	if (!conn)	return;
	CONNLOCK_ACQUIRE(conn);
	if (NULL != async)
		CC_set_autocommit(conn, FALSE);
	else
		SYNC_AUTOCOMMIT(conn);
	conn->asdum = async;
	CONNLOCK_RELEASE(conn);
}

DLL_DECLARE void	*PgDtc_get_async(void *self)
{
	ConnectionClass *conn = (ConnectionClass *) self;

	return conn->asdum;
}

DLL_DECLARE void PgDtc_set_property(void *self, int property, void *value)
{
	ConnectionClass *conn = (ConnectionClass *) self;

	CONNLOCK_ACQUIRE(conn);
	switch (property)
	{
		case inprogress:
			if (NULL != value)
				CC_set_dtc_executing(conn);
			else
				CC_no_dtc_executing(conn);
			break;
		case enlisted:
			if (NULL != value)
				CC_set_dtc_enlisted(conn);
			else
				CC_no_dtc_enlisted(conn);
			break;
		case prepareRequested:
			if (NULL != value)
				CC_set_dtc_prepareRequested(conn);
			else
				CC_no_dtc_prepareRequested(conn);
			break;
	}
	CONNLOCK_RELEASE(conn);
}

DLL_DECLARE void PgDtc_set_error(void *self, const char *message, const char *func)
{
	ConnectionClass *conn = (ConnectionClass *) self;

	CC_set_error(conn, CONN_UNSUPPORTED_OPTION, message, func);
}

DLL_DECLARE int PgDtc_get_property(void *self, int property)
{
	ConnectionClass *conn = (ConnectionClass *) self;
	int	ret;

	CONNLOCK_ACQUIRE(conn);
	switch (property)
	{
		case inprogress:
			ret = CC_is_dtc_executing(conn);
			break;
		case enlisted:
			ret = CC_is_dtc_enlisted(conn);
			break;
		case inTrans:
			ret = CC_is_in_trans(conn);
			break;
		case errorNumber:
			ret = CC_get_errornumber(conn);
			break;
		case idleInGlobalTransaction:
			ret = CC_is_idle_in_global_transaction(conn);
			break;
		case connected:
			ret = (CONN_CONNECTED == conn->status);
			break;
		case prepareRequested:
			ret = CC_is_dtc_prepareRequested(conn);
			break;
	}
	CONNLOCK_RELEASE(conn);
	return ret;
}

DLL_DECLARE BOOL PgDtc_connect(void *self)
{
	CSTR	func = "PgDtc_connect";
	ConnectionClass *conn = (ConnectionClass *) self;

	if (CONN_CONNECTED == conn->status)
		return TRUE;
	if (CC_connect(conn, AUTH_REQ_OK, NULL) <= 0)
	{
		/* Error messages are filled in */
		CC_log_error(func, "Error on CC_connect", conn);
		return FALSE;
	}
	return TRUE;
}

DLL_DECLARE void PgDtc_free_connect(void *self)
{
	ConnectionClass *conn = (ConnectionClass *) self;

	PGAPI_FreeConnect(conn);
}

DLL_DECLARE BOOL PgDtc_one_phase_operation(void *self, int operation)
{
	ConnectionClass *conn = (ConnectionClass *) self;
	BOOL	ret, is_in_progress = CC_is_dtc_executing(conn);

	if (!is_in_progress)
		CC_set_dtc_executing(conn);
	switch (operation)
	{
		case ONE_PHASE_COMMIT:
			ret = CC_commit(conn);
			break;
		default:
			ret = CC_abort(conn);
			break;
	}

	if (!is_in_progress)
		CC_no_dtc_executing(conn);

	return ret;
}

DLL_DECLARE BOOL
PgDtc_two_phase_operation(void *self, int operation, const char *gxid)
{
	ConnectionClass *conn = (ConnectionClass *) self;
	QResultClass	*qres;
	BOOL	ret = TRUE;
	char		cmd[512];

	switch (operation)
	{
		case PREPARE_TRANSACTION:
			snprintf(cmd, sizeof(cmd), "PREPARE TRANSACTION '%s'", gxid);
			break;
		case COMMIT_PREPARED:
			snprintf(cmd, sizeof(cmd), "COMMIT PREPARED '%s'", gxid);
			break;
		case ROLLBACK_PREPARED:
			snprintf(cmd, sizeof(cmd), "ROLLBACK PREPARED '%s'", gxid);
			break;
	}

	qres = CC_send_query(conn, cmd, NULL, 0, NULL);
	if (!QR_command_maybe_successful(qres))
		ret = FALSE;
	QR_Destructor(qres);
	return ret;
}

DLL_DECLARE BOOL
PgDtc_lock_cntrl(void *self, BOOL acquire, BOOL bTrial)
{
	ConnectionClass *conn = (ConnectionClass *) self;
	BOOL	ret = TRUE;

	if (acquire)
		if (bTrial)
			ret = TRY_ENTER_CONN_CS(conn);
		else
			ENTER_CONN_CS(conn);
	else
		LEAVE_CONN_CS(conn);

	return ret;
}

static ConnectionClass *
CC_Copy(const ConnectionClass *conn)
{
	ConnectionClass	*newconn = CC_alloc();

	if (newconn)
	{
		memcpy(newconn, conn, sizeof(ConnectionClass));
		CC_lockinit(newconn);
	}
	return newconn;
}

DLL_DECLARE void *
PgDtc_isolate(void *self, DWORD option)
{
	BOOL	disposingConn = (0 != (disposingConnection & option));
	ConnectionClass *sconn = (ConnectionClass *) self, *newconn = NULL;

	if (0 == (useAnotherRoom & option))
	{
		HENV	henv = sconn->henv;

		CC_cleanup(sconn, TRUE);
		if (newconn = CC_Copy(sconn), NULL == newconn)
			return newconn;
		mylog("%s:newconn=%p from %p\n", __FUNCTION__, newconn, sconn);
		CC_initialize(sconn, FALSE);
		if (!disposingConn)
			CC_copy_conninfo(&sconn->connInfo, &newconn->connInfo);
		CC_initialize_pg_version(sconn);
		sconn->henv = henv;
		newconn->henv = NULL;
		SYNC_AUTOCOMMIT(sconn);
		return newconn;
	}
	newconn = CC_Constructor();
	CC_copy_conninfo(&newconn->connInfo, &sconn->connInfo);
	CC_initialize_pg_version(newconn);
	newconn->asdum = sconn->asdum;
	newconn->gTranInfo = sconn->gTranInfo;
	CC_set_dtc_isolated(newconn);
	sconn->asdum = NULL;
	SYNC_AUTOCOMMIT(sconn);
	CC_set_dtc_clear(sconn);
	mylog("generated connection=%p with %p\n", newconn, newconn->asdum);

	return newconn;
}

#endif /* _HANDLE_ENLIST_IN_DTC_ */
