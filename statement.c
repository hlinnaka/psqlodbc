/*-------
 * Module:			statement.c
 *
 * Description:		This module contains functions related to creating
 *					and manipulating a statement.
 *
 * Classes:			StatementClass (Functions prefix: "SC_")
 *
 * API functions:	SQLAllocStmt, SQLFreeStmt
 *
 * Comments:		See "notice.txt" for copyright and license information.
 *-------
 */

#include "statement.h"

#include "bind.h"
#include "connection.h"
#include "qresult.h"
#include "convert.h"
#include "environ.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "pgapifunc.h"


#define PRN_NULLCHECK


/*	Map sql commands to statement types */
static struct
{
	int			type;
	char	   *s;
}	Statement_Type[] =

{
	{
		STMT_TYPE_SELECT, "SELECT"
	},
	{
		STMT_TYPE_INSERT, "INSERT"
	},
	{
		STMT_TYPE_UPDATE, "UPDATE"
	},
	{
		STMT_TYPE_DELETE, "DELETE"
	},
	{
		STMT_TYPE_CREATE, "CREATE"
	},
	{
		STMT_TYPE_ALTER, "ALTER"
	},
	{
		STMT_TYPE_DROP, "DROP"
	},
	{
		STMT_TYPE_GRANT, "GRANT"
	},
	{
		STMT_TYPE_REVOKE, "REVOKE"
	},
	{
		STMT_TYPE_PROCCALL, "{"
	},
	{
		0, NULL
	}
};


RETCODE		SQL_API
PGAPI_AllocStmt(HDBC hdbc,
				HSTMT FAR * phstmt)
{
	static char *func = "PGAPI_AllocStmt";
	ConnectionClass *conn = (ConnectionClass *) hdbc;
	StatementClass *stmt;

	mylog("%s: entering...\n", func);

	if (!conn)
	{
		CC_log_error(func, "", NULL);
		return SQL_INVALID_HANDLE;
	}

	stmt = SC_Constructor();

	mylog("**** PGAPI_AllocStmt: hdbc = %u, stmt = %u\n", hdbc, stmt);

	if (!stmt)
	{
		conn->errornumber = CONN_STMT_ALLOC_ERROR;
		conn->errormsg = "No more memory to allocate a further SQL-statement";
		*phstmt = SQL_NULL_HSTMT;
		CC_log_error(func, "", conn);
		return SQL_ERROR;
	}

	if (!CC_add_statement(conn, stmt))
	{
		conn->errormsg = "Maximum number of connections exceeded.";
		conn->errornumber = CONN_STMT_ALLOC_ERROR;
		CC_log_error(func, "", conn);
		SC_Destructor(stmt);
		*phstmt = SQL_NULL_HSTMT;
		return SQL_ERROR;
	}

	*phstmt = (HSTMT) stmt;

	/* Copy default statement options based from Connection options */
	stmt->options = conn->stmtOptions;

	stmt->stmt_size_limit = CC_get_max_query_len(conn);
	/* Save the handle for later */
	stmt->phstmt = phstmt;

	return SQL_SUCCESS;
}


RETCODE		SQL_API
PGAPI_FreeStmt(HSTMT hstmt,
			   UWORD fOption)
{
	static char *func = "PGAPI_FreeStmt";
	StatementClass *stmt = (StatementClass *) hstmt;

	mylog("%s: entering...hstmt=%u, fOption=%d\n", func, hstmt, fOption);

	if (!stmt)
	{
		SC_log_error(func, "", NULL);
		return SQL_INVALID_HANDLE;
	}
	SC_clear_error(stmt);

	if (fOption == SQL_DROP)
	{
		ConnectionClass *conn = stmt->hdbc;

		/* Remove the statement from the connection's statement list */
		if (conn)
		{
			if (!CC_remove_statement(conn, stmt))
			{
				stmt->errornumber = STMT_SEQUENCE_ERROR;
				stmt->errormsg = "Statement is currently executing a transaction.";
				SC_log_error(func, "", stmt);
				return SQL_ERROR;		/* stmt may be executing a
										 * transaction */
			}

			/* Free any cursors and discard any result info */
			if (stmt->result)
			{
				QR_Destructor(stmt->result);
				stmt->result = NULL;
			}
		}

		/* Destroy the statement and free any results, cursors, etc. */
		SC_Destructor(stmt);
	}
	else if (fOption == SQL_UNBIND)
		SC_unbind_cols(stmt);
	else if (fOption == SQL_CLOSE)
	{
		/*
		 * this should discard all the results, but leave the statement
		 * itself in place (it can be executed again)
		 */
		if (!SC_recycle_statement(stmt))
		{
			/* errormsg passed in above */
			SC_log_error(func, "", stmt);
			return SQL_ERROR;
		}
	}
	else if (fOption == SQL_RESET_PARAMS)
		SC_free_params(stmt, STMT_FREE_PARAMS_ALL);
	else
	{
		stmt->errormsg = "Invalid option passed to PGAPI_FreeStmt.";
		stmt->errornumber = STMT_OPTION_OUT_OF_RANGE_ERROR;
		SC_log_error(func, "", stmt);
		return SQL_ERROR;
	}

	return SQL_SUCCESS;
}


/*
 * StatementClass implementation
 */
void
InitializeStatementOptions(StatementOptions *opt)
{
	memset(opt, 0, sizeof(StatementOptions));
	opt->maxRows = 0;			/* driver returns all rows */
	opt->maxLength = 0;			/* driver returns all data for char/binary */
	opt->rowset_size = 1;
	opt->keyset_size = 0;		/* fully keyset driven is the default */
	opt->scroll_concurrency = SQL_CONCUR_READ_ONLY;
	opt->cursor_type = SQL_CURSOR_FORWARD_ONLY;
	opt->bind_size = 0;			/* default is to bind by column */
	opt->retrieve_data = SQL_RD_ON;
	opt->use_bookmarks = SQL_UB_OFF;
	opt->paramset_size = 1;
	opt->param_bind_type = 0;	/* default is column-wise binding */
}


StatementClass *
SC_Constructor(void)
{
	StatementClass *rv;

	rv = (StatementClass *) malloc(sizeof(StatementClass));
	if (rv)
	{
		rv->hdbc = NULL;		/* no connection associated yet */
		rv->phstmt = NULL;
		rv->result = NULL;
		rv->manual_result = FALSE;
		rv->prepare = FALSE;
		rv->status = STMT_ALLOCATED;
		rv->internal = FALSE;

		rv->errormsg = NULL;
		rv->errornumber = 0;
		rv->errormsg_created = FALSE;
		rv->errormsg_malloced = FALSE;

		rv->statement = NULL;
		rv->stmt_with_params = NULL;
		rv->stmt_size_limit = -1;
		rv->statement_type = STMT_TYPE_UNKNOWN;

		rv->bindings = NULL;
		rv->bindings_allocated = 0;

		rv->bookmark.buffer = NULL;
		rv->bookmark.used = NULL;

		rv->parameters_allocated = 0;
		rv->parameters = 0;

		rv->currTuple = -1;
		rv->rowset_start = -1;
		rv->current_col = -1;
		rv->bind_row = 0;
		rv->last_fetch_count = 0;
		rv->save_rowset_size = -1;

		rv->data_at_exec = -1;
		rv->current_exec_param = -1;
		rv->exec_start_row = -1;
		rv->exec_end_row = -1;
		rv->exec_current_row = -1;
		rv->put_data = FALSE;

		rv->lobj_fd = -1;
		rv->cursor_name[0] = '\0';

		/* Parse Stuff */
		rv->ti = NULL;
		rv->fi = NULL;
		rv->ntab = 0;
		rv->nfld = 0;
		rv->parse_status = STMT_PARSE_NONE;

		/* Clear Statement Options -- defaults will be set in AllocStmt */
		memset(&rv->options, 0, sizeof(StatementOptions));

		rv->pre_executing = FALSE;
		rv->inaccurate_result = FALSE;
		rv->miscinfo = 0;
	}
	return rv;
}


char
SC_Destructor(StatementClass *self)
{
	mylog("SC_Destructor: self=%u, self->result=%u, self->hdbc=%u\n", self, self->result, self->hdbc);
	SC_clear_error(self);
	if (STMT_EXECUTING == self->status)
	{
		self->errornumber = STMT_SEQUENCE_ERROR;
		self->errormsg = "Statement is currently executing a transaction.";
		return FALSE;
	}

	if (self->result)
	{
		if (!self->hdbc)
			self->result->conn = NULL;	/* prevent any dbase activity */

		QR_Destructor(self->result);
	}

	if (self->statement)
		free(self->statement);
	if (self->stmt_with_params)
	{
		free(self->stmt_with_params);
		self->stmt_with_params = NULL;
	}

	SC_free_params(self, STMT_FREE_PARAMS_ALL);

	/*
	 * the memory pointed to by the bindings is not deallocated by the
	 * driver but by the application that uses that driver, so we don't
	 * have to care
	 */
	/* about that here. */
	if (self->bindings)
	{
		int			lf;

		for (lf = 0; lf < self->bindings_allocated; lf++)
		{
			if (self->bindings[lf].ttlbuf != NULL)
				free(self->bindings[lf].ttlbuf);
		}
		free(self->bindings);
	}

	/* Free the parsed table information */
	if (self->ti)
	{
		int			i;

		for (i = 0; i < self->ntab; i++)
			free(self->ti[i]);

		free(self->ti);
	}

	/* Free the parsed field information */
	if (self->fi)
	{
		int			i;

		for (i = 0; i < self->nfld; i++)
			free(self->fi[i]);
		free(self->fi);
	}

	free(self);

	mylog("SC_Destructor: EXIT\n");

	return TRUE;
}


/*
 *	Free parameters and free the memory from the
 *	data-at-execution parameters that was allocated in SQLPutData.
 */
void
SC_free_params(StatementClass *self, char option)
{
	int			i;

	mylog("SC_free_params:  ENTER, self=%d\n", self);

	if (!self->parameters)
		return;

	for (i = 0; i < self->parameters_allocated; i++)
	{
		if (self->parameters[i].data_at_exec == TRUE)
		{
			if (self->parameters[i].EXEC_used)
			{
				free(self->parameters[i].EXEC_used);
				self->parameters[i].EXEC_used = NULL;
			}

			if (self->parameters[i].EXEC_buffer)
			{
				if (self->parameters[i].SQLType != SQL_LONGVARBINARY)
					free(self->parameters[i].EXEC_buffer);
				self->parameters[i].EXEC_buffer = NULL;
			}
		}
	}
	self->data_at_exec = -1;
	self->current_exec_param = -1;
	self->put_data = FALSE;

	if (option == STMT_FREE_PARAMS_ALL)
	{
		free(self->parameters);
		self->parameters = NULL;
		self->parameters_allocated = 0;
		self->exec_start_row = -1;
		self->exec_end_row = -1;
		self->exec_current_row = -1;
	}

	mylog("SC_free_params:  EXIT\n");
}


int
statement_type(char *statement)
{
	int			i;

	/* ignore leading whitespace in query string */
	while (*statement && isspace((unsigned char) *statement))
		statement++;

	for (i = 0; Statement_Type[i].s; i++)
		if (!strnicmp(statement, Statement_Type[i].s, strlen(Statement_Type[i].s)))
			return Statement_Type[i].type;

	return STMT_TYPE_OTHER;
}


/*
 *	Called from SQLPrepare if STMT_PREMATURE, or
 *	from SQLExecute if STMT_FINISHED, or
 *	from SQLFreeStmt(SQL_CLOSE)
 */
char
SC_recycle_statement(StatementClass *self)
{
	ConnectionClass *conn;

	mylog("recycle statement: self= %u\n", self);

	SC_clear_error(self);
	/* This would not happen */
	if (self->status == STMT_EXECUTING)
	{
		self->errornumber = STMT_SEQUENCE_ERROR;
		self->errormsg = "Statement is currently executing a transaction.";
		return FALSE;
	}

	switch (self->status)
	{
		case STMT_ALLOCATED:
			/* this statement does not need to be recycled */
			return TRUE;

		case STMT_READY:
			break;

		case STMT_PREMATURE:

			/*
			 * Premature execution of the statement might have caused the
			 * start of a transaction. If so, we have to rollback that
			 * transaction.
			 */
			conn = SC_get_conn(self);
			if (!CC_is_in_autocommit(conn) && CC_is_in_trans(conn))
			{
				if (SC_is_pre_executable(self) && !conn->connInfo.disallow_premature)
					CC_abort(conn);
			}
			break;

		case STMT_FINISHED:
			break;

		default:
			self->errormsg = "An internal error occured while recycling statements";
			self->errornumber = STMT_INTERNAL_ERROR;
			return FALSE;
	}

	/* Free the parsed table information */
	if (self->ti)
	{
		int			i;

		for (i = 0; i < self->ntab; i++)
			free(self->ti[i]);

		free(self->ti);
		self->ti = NULL;
		self->ntab = 0;
	}

	/* Free the parsed field information */
	if (self->fi)
	{
		int			i;

		for (i = 0; i < self->nfld; i++)
			free(self->fi[i]);
		free(self->fi);
		self->fi = NULL;
		self->nfld = 0;
	}
	self->parse_status = STMT_PARSE_NONE;

	/* Free any cursors */
	if (self->result)
	{
		QR_Destructor(self->result);
		self->result = NULL;
	}
	self->inaccurate_result = FALSE;

	/*
	 * Reset only parameters that have anything to do with results
	 */
	self->status = STMT_READY;
	self->manual_result = FALSE;	/* very important */

	self->currTuple = -1;
	self->rowset_start = -1;
	self->current_col = -1;
	self->bind_row = 0;
	self->last_fetch_count = 0;

	if (self->errormsg_malloced && self->errormsg)
		free(self->errormsg);
	self->errormsg = NULL;
	self->errornumber = 0;
	self->errormsg_created = FALSE;
	self->errormsg_malloced = FALSE;

	self->lobj_fd = -1;

	/*
	 * Free any data at exec params before the statement is executed
	 * again.  If not, then there will be a memory leak when the next
	 * SQLParamData/SQLPutData is called.
	 */
	SC_free_params(self, STMT_FREE_PARAMS_DATA_AT_EXEC_ONLY);

	return TRUE;
}


/* Pre-execute a statement (SQLPrepare/SQLDescribeCol) */
void
SC_pre_execute(StatementClass *self)
{
	mylog("SC_pre_execute: status = %d\n", self->status);

	if (self->status == STMT_READY)
	{
		mylog("              preprocess: status = READY\n");

		self->miscinfo = 0;
		if (self->statement_type == STMT_TYPE_SELECT)
		{
			char		old_pre_executing = self->pre_executing;

			self->pre_executing = TRUE;
			self->inaccurate_result = FALSE;

			PGAPI_Execute(self);

			self->pre_executing = old_pre_executing;

			if (self->status == STMT_FINISHED)
			{
				mylog("              preprocess: after status = FINISHED, so set PREMATURE\n");
				self->status = STMT_PREMATURE;
			}
		}
		if (!SC_is_pre_executable(self))
		{
			self->result = QR_Constructor();
			QR_set_status(self->result, PGRES_TUPLES_OK);
			self->inaccurate_result = TRUE;
			self->status = STMT_PREMATURE;
		}
	}
}


/* This is only called from SQLFreeStmt(SQL_UNBIND) */
char
SC_unbind_cols(StatementClass *self)
{
	Int2		lf;

	for (lf = 0; lf < self->bindings_allocated; lf++)
	{
		self->bindings[lf].data_left = -1;
		self->bindings[lf].buflen = 0;
		self->bindings[lf].buffer = NULL;
		self->bindings[lf].used = NULL;
		self->bindings[lf].returntype = SQL_C_CHAR;
	}

	self->bookmark.buffer = NULL;
	self->bookmark.used = NULL;

	return 1;
}


void
SC_clear_error(StatementClass *self)
{
	if (self->errormsg_malloced && self->errormsg)
		free(self->errormsg);
	self->errornumber = 0;
	self->errormsg = NULL;
	self->errormsg_created = FALSE;
	self->errormsg_malloced = FALSE;
}


/*
 *	This function creates an error msg which is the concatenation
 *	of the result, statement, connection, and socket messages.
 */
char *
SC_create_errormsg(StatementClass *self)
{
	QResultClass *res = self->result;
	ConnectionClass *conn = self->hdbc;
	int			pos;
	static char msg[4096];

	msg[0] = '\0';

	if (res && res->message)
		strcpy(msg, res->message);

	else if (self->errormsg)
		strcpy(msg, self->errormsg);

	if (conn)
	{
		SocketClass *sock = conn->sock;

		if (conn->errormsg && conn->errormsg[0] != '\0')
		{
			pos = strlen(msg);
			sprintf(&msg[pos], ";\n%s", conn->errormsg);
		}

		if (sock && sock->errormsg && sock->errormsg[0] != '\0')
		{
			pos = strlen(msg);
			sprintf(&msg[pos], ";\n%s", sock->errormsg);
		}
	}
	if (!msg[0] && res && QR_get_notice(res))
		return QR_get_notice(res);

	return msg;
}


char
SC_get_error(StatementClass *self, int *number, char **message)
{
	char		rv;

	/* Create a very informative errormsg if it hasn't been done yet. */
	if (!self->errormsg_created)
	{
		self->errormsg = SC_create_errormsg(self);
		self->errormsg_created = TRUE;
	}

	if (self->errornumber)
	{
		*number = self->errornumber;
		*message = self->errormsg;
		if (!self->errormsg_malloced)
			self->errormsg = NULL;
	}

	rv = (self->errornumber != 0);
	self->errornumber = 0;

	return rv;
}


/*
 *	Currently, the driver offers very simple bookmark support -- it is
 *	just the current row number.  But it could be more sophisticated
 *	someday, such as mapping a key to a 32 bit value
 */
unsigned long
SC_get_bookmark(StatementClass *self)
{
	return (self->currTuple + 1);
}


RETCODE
SC_fetch(StatementClass *self)
{
	static char *func = "SC_fetch";
	QResultClass *res = self->result;
	int			retval,
				result;

#ifdef	DRIVER_CURSOR_IMPLEMENT
	int			updret;
#endif   /* DRIVER_CURSOR_IMPLEMENT */
	Int2		num_cols,
				lf;
	Oid			type;
	char	   *value;
	ColumnInfoClass *coli;

	/* TupleField *tupleField; */
	ConnInfo   *ci = &(SC_get_conn(self)->connInfo);

	self->last_fetch_count = 0;
	coli = QR_get_fields(res);	/* the column info */

	mylog("manual_result = %d, use_declarefetch = %d\n", self->manual_result, ci->drivers.use_declarefetch);

	if (self->manual_result || !SC_is_fetchcursor(self))
	{
		if (self->currTuple >= QR_get_num_tuples(res) - 1 ||
			(self->options.maxRows > 0 && self->currTuple == self->options.maxRows - 1))
		{
			/*
			 * if at the end of the tuples, return "no data found" and set
			 * the cursor past the end of the result set
			 */
			self->currTuple = QR_get_num_tuples(res);
			return SQL_NO_DATA_FOUND;
		}

		mylog("**** SC_fetch: manual_result\n");
		(self->currTuple)++;
	}
	else
	{
		/* read from the cache or the physical next tuple */
		retval = QR_next_tuple(res);
		if (retval < 0)
		{
			mylog("**** SC_fetch: end_tuples\n");
			return SQL_NO_DATA_FOUND;
		}
		else if (retval > 0)
			(self->currTuple)++;	/* all is well */
		else
		{
			mylog("SC_fetch: error\n");
			self->errornumber = STMT_EXEC_ERROR;
			self->errormsg = "Error fetching next row";
			SC_log_error(func, "", self);
			return SQL_ERROR;
		}
	}

	num_cols = QR_NumResultCols(res);

	result = SQL_SUCCESS;
	self->last_fetch_count = 1;

	/*
	 * If the bookmark column was bound then return a bookmark. Since this
	 * is used with SQLExtendedFetch, and the rowset size may be greater
	 * than 1, and an application can use row or column wise binding, use
	 * the code in copy_and_convert_field() to handle that.
	 */
	if (self->bookmark.buffer)
	{
		char		buf[32];
		UInt4	offset = self->options.row_offset_ptr ? *self->options.row_offset_ptr : 0;

		sprintf(buf, "%ld", SC_get_bookmark(self));
		result = copy_and_convert_field(self, 0, buf,
			 SQL_C_ULONG, self->bookmark.buffer + offset, 0,
			self->bookmark.used ? self->bookmark.used + (offset >> 2) : NULL);
	}

#ifdef	DRIVER_CURSOR_IMPLEMENT
	updret = 0;
	if (self->options.scroll_concurrency != SQL_CONCUR_READ_ONLY)
	{
		if (!QR_get_value_backend_row(res, self->currTuple, num_cols - 1))
			updret = SQL_ROW_DELETED;
		num_cols -= 2;
	}
#endif   /* DRIVER_CURSOR_IMPLEMENT */
	if (self->options.retrieve_data == SQL_RD_OFF)		/* data isn't required */
#ifdef	DRIVER_CURSOR_IMPLEMENT
		return updret ? updret + 10 : SQL_SUCCESS;
#else
		return SQL_SUCCESS;
#endif   /* DRIVER_CURSOR_IMPLEMENT */
	for (lf = 0; lf < num_cols; lf++)
	{
		mylog("fetch: cols=%d, lf=%d, self = %u, self->bindings = %u, buffer[] = %u\n", num_cols, lf, self, self->bindings, self->bindings[lf].buffer);

		/* reset for SQLGetData */
		self->bindings[lf].data_left = -1;

		if (self->bindings[lf].buffer != NULL)
		{
			/* this column has a binding */

			/* type = QR_get_field_type(res, lf); */
			type = CI_get_oid(coli, lf);		/* speed things up */

			mylog("type = %d\n", type);

			if (self->manual_result)
			{
				value = QR_get_value_manual(res, self->currTuple, lf);
				mylog("manual_result\n");
			}
			else if (SC_is_fetchcursor(self))
				value = QR_get_value_backend(res, lf);
			else
				value = QR_get_value_backend_row(res, self->currTuple, lf);

			mylog("value = '%s'\n", (value == NULL) ? "<NULL>" : value);

			retval = copy_and_convert_field_bindinfo(self, type, value, lf);

			mylog("copy_and_convert: retval = %d\n", retval);

			switch (retval)
			{
				case COPY_OK:
					break;		/* OK, do next bound column */

				case COPY_UNSUPPORTED_TYPE:
					self->errormsg = "Received an unsupported type from Postgres.";
					self->errornumber = STMT_RESTRICTED_DATA_TYPE_ERROR;
					SC_log_error(func, "", self);
					result = SQL_ERROR;
					break;

				case COPY_UNSUPPORTED_CONVERSION:
					self->errormsg = "Couldn't handle the necessary data type conversion.";
					self->errornumber = STMT_RESTRICTED_DATA_TYPE_ERROR;
					SC_log_error(func, "", self);
					result = SQL_ERROR;
					break;

				case COPY_RESULT_TRUNCATED:
					self->errornumber = STMT_TRUNCATED;
					self->errormsg = "Fetched item was truncated.";
					qlog("The %dth item was truncated\n", lf + 1);
					qlog("The buffer size = %d", self->bindings[lf].buflen);
					qlog(" and the value is '%s'\n", value);
					result = SQL_SUCCESS_WITH_INFO;
					break;

					/* error msg already filled in */
				case COPY_GENERAL_ERROR:
					SC_log_error(func, "", self);
					result = SQL_ERROR;
					break;

					/* This would not be meaningful in SQLFetch. */
				case COPY_NO_DATA_FOUND:
					break;

				default:
					self->errormsg = "Unrecognized return value from copy_and_convert_field.";
					self->errornumber = STMT_INTERNAL_ERROR;
					SC_log_error(func, "", self);
					result = SQL_ERROR;
					break;
			}
		}
	}

#ifdef	DRIVER_CURSOR_IMPLEMENT
	if (updret)
		result = updret + 10;
#endif   /* DRIVER_CURSOR_IMPLEMENT */
	return result;
}


RETCODE
SC_execute(StatementClass *self)
{
	static char *func = "SC_execute";
	ConnectionClass *conn;
	char		was_ok, was_nonfatal;
	Int2		oldstatus,
				numcols;
	QueryInfo	qi;
	ConnInfo   *ci;


	conn = SC_get_conn(self);
	ci = &(conn->connInfo);

	/* Begin a transaction if one is not already in progress */

	/*
	 * Basically we don't have to begin a transaction in autocommit mode
	 * because Postgres backend runs in autocomit mode. We issue "BEGIN"
	 * in the following cases. 1) we use declare/fetch and the statement
	 * is SELECT (because declare/fetch must be called in a transaction).
	 * 2) we are in autocommit off state and the statement isn't of type
	 * OTHER.
	 */
	if (!self->internal && !CC_is_in_trans(conn) &&
		(SC_is_fetchcursor(self) ||
		 (!CC_is_in_autocommit(conn) && self->statement_type != STMT_TYPE_OTHER)))
	{
		mylog("   about to begin a transaction on statement = %u\n", self);
		if (!CC_begin(conn))
		{
			self->errormsg = "Could not begin a transaction";
			self->errornumber = STMT_EXEC_ERROR;
			SC_log_error(func, "", self);
			return SQL_ERROR;
		}
	}

	oldstatus = conn->status;
	conn->status = CONN_EXECUTING;
	self->status = STMT_EXECUTING;

	/* If it's a SELECT statement, use a cursor. */

	/*
	 * Note that the declare cursor has already been prepended to the
	 * statement
	 */
	/* in copy_statement... */
	if (self->statement_type == STMT_TYPE_SELECT)
	{
		char		fetch[128];

		mylog("       Sending SELECT statement on stmt=%u, cursor_name='%s'\n", self, self->cursor_name);

		/* send the declare/select */
		self->result = CC_send_query(conn, self->stmt_with_params, NULL);

		if (SC_is_fetchcursor(self) && self->result != NULL &&
			QR_command_successful(self->result))
		{
			QR_Destructor(self->result);

			/*
			 * That worked, so now send the fetch to start getting data
			 * back
			 */
			qi.result_in = NULL;
			qi.cursor = self->cursor_name;
			qi.row_size = ci->drivers.fetch_max;

			/*
			 * Most likely the rowset size will not be set by the
			 * application until after the statement is executed, so might
			 * as well use the cache size. The qr_next_tuple() function
			 * will correct for any discrepancies in sizes and adjust the
			 * cache accordingly.
			 */
			sprintf(fetch, "fetch %d in %s", qi.row_size, self->cursor_name);

			self->result = CC_send_query(conn, fetch, &qi);
		}
		mylog("     done sending the query:\n");
	}
	else
	{
		/* not a SELECT statement so don't use a cursor */
		mylog("      it's NOT a select statement: stmt=%u\n", self);
		self->result = CC_send_query(conn, self->stmt_with_params, NULL);

		/*
		 * We shouldn't send COMMIT. Postgres backend does the autocommit
		 * if neccessary. (Zoltan, 04/26/2000)
		 */

		/*
		 * Above seems wrong. Even in case of autocommit, started
		 * transactions must be committed. (Hiroshi, 02/11/2001)
		 */
		if (!self->internal && CC_is_in_autocommit(conn) && CC_is_in_trans(conn))
			CC_commit(conn);
	}

	conn->status = oldstatus;
	self->status = STMT_FINISHED;

	/* Check the status of the result */
	if (self->result)
	{
		was_ok = QR_command_successful(self->result);
		was_nonfatal = QR_command_nonfatal(self->result);

		if (was_ok)
			self->errornumber = STMT_OK;
		else
			self->errornumber = was_nonfatal ? STMT_INFO_ONLY : STMT_ERROR_TAKEN_FROM_BACKEND;

		/* set cursor before the first tuple in the list */
		self->currTuple = -1;
		self->current_col = -1;
		self->rowset_start = -1;

		/* see if the query did return any result columns */
		numcols = QR_NumResultCols(self->result);

		/* now allocate the array to hold the binding info */
		if (numcols > 0)
		{
			extend_bindings(self, numcols);
			if (self->bindings == NULL)
			{
				self->errornumber = STMT_NO_MEMORY_ERROR;
				self->errormsg = "Could not get enough free memory to store the binding information";
				SC_log_error(func, "", self);
				return SQL_ERROR;
			}
		}
		/* issue "ABORT" when query aborted */
		if (QR_get_aborted(self->result) && !self->internal)
			CC_abort(conn);
	}
	else
	{
		/* Bad Error -- The error message will be in the Connection */
		if (self->statement_type == STMT_TYPE_CREATE)
		{
			self->errornumber = STMT_CREATE_TABLE_ERROR;
			self->errormsg = "Error creating the table";

			/*
			 * This would allow the table to already exists, thus
			 * appending rows to it.  BUT, if the table didn't have the
			 * same attributes, it would fail. return
			 * SQL_SUCCESS_WITH_INFO;
			 */
		}
		else
		{
			self->errornumber = STMT_EXEC_ERROR;
			self->errormsg = "Error while executing the query";
		}

		if (!self->internal)
			CC_abort(conn);
	}

	if (self->statement_type == STMT_TYPE_PROCCALL &&
		(self->errornumber == STMT_OK ||
		 self->errornumber == STMT_INFO_ONLY) &&
		self->parameters &&
		self->parameters[0].buffer &&
		self->parameters[0].paramType == SQL_PARAM_OUTPUT)
	{							/* get the return value of the procedure
								 * call */
		RETCODE		ret;
		HSTMT		hstmt = (HSTMT) self;

		ret = SC_fetch(hstmt);
		if (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO)
		{
			ret = PGAPI_GetData(hstmt, 1, self->parameters[0].CType, self->parameters[0].buffer, self->parameters[0].buflen, self->parameters[0].used);
			if (ret != SQL_SUCCESS)
			{
				self->errornumber = STMT_EXEC_ERROR;
				self->errormsg = "GetData to Procedure return failed.";
			}
		}
		else
		{
			self->errornumber = STMT_EXEC_ERROR;
			self->errormsg = "SC_fetch to get a Procedure return failed.";
		}
	}
	if (self->errornumber == STMT_OK)
		return SQL_SUCCESS;
	else if (self->errornumber == STMT_INFO_ONLY)
		return SQL_SUCCESS_WITH_INFO;
	else
	{
		self->errormsg = "Error while executing the query";
		SC_log_error(func, "", self);
		return SQL_ERROR;
	}
}


void
SC_log_error(char *func, char *desc, StatementClass *self)
{
#ifdef PRN_NULLCHECK
#define nullcheck(a) (a ? a : "(NULL)")
#endif
	if (self)
	{
		qlog("STATEMENT ERROR: func=%s, desc='%s', errnum=%d, errmsg='%s'\n", func, desc, self->errornumber, nullcheck(self->errormsg));
		mylog("STATEMENT ERROR: func=%s, desc='%s', errnum=%d, errmsg='%s'\n", func, desc, self->errornumber, nullcheck(self->errormsg));
		qlog("                 ------------------------------------------------------------\n");
		qlog("                 hdbc=%u, stmt=%u, result=%u\n", self->hdbc, self, self->result);
		qlog("                 manual_result=%d, prepare=%d, internal=%d\n", self->manual_result, self->prepare, self->internal);
		qlog("                 bindings=%u, bindings_allocated=%d\n", self->bindings, self->bindings_allocated);
		qlog("                 parameters=%u, parameters_allocated=%d\n", self->parameters, self->parameters_allocated);
		qlog("                 statement_type=%d, statement='%s'\n", self->statement_type, nullcheck(self->statement));
		qlog("                 stmt_with_params='%s'\n", nullcheck(self->stmt_with_params));
		qlog("                 data_at_exec=%d, current_exec_param=%d, put_data=%d\n", self->data_at_exec, self->current_exec_param, self->put_data);
		qlog("                 currTuple=%d, current_col=%d, lobj_fd=%d\n", self->currTuple, self->current_col, self->lobj_fd);
		qlog("                 maxRows=%d, rowset_size=%d, keyset_size=%d, cursor_type=%d, scroll_concurrency=%d\n", self->options.maxRows, self->options.rowset_size, self->options.keyset_size, self->options.cursor_type, self->options.scroll_concurrency);
		qlog("                 cursor_name='%s'\n", nullcheck(self->cursor_name));

		qlog("                 ----------------QResult Info -------------------------------\n");

		if (self->result)
		{
			QResultClass *res = self->result;

			qlog("                 fields=%u, manual_tuples=%u, backend_tuples=%u, tupleField=%d, conn=%u\n", res->fields, res->manual_tuples, res->backend_tuples, res->tupleField, res->conn);
			qlog("                 fetch_count=%d, fcount=%d, num_fields=%d, cursor='%s'\n", res->fetch_count, res->fcount, res->num_fields, nullcheck(res->cursor));
			qlog("                 message='%s', command='%s', notice='%s'\n", nullcheck(res->message), nullcheck(res->command), nullcheck(res->notice));
			qlog("                 status=%d, inTuples=%d\n", res->status, res->inTuples);
		}

		/* Log the connection error if there is one */
		CC_log_error(func, desc, self->hdbc);
	}
	else
		qlog("INVALID STATEMENT HANDLE ERROR: func=%s, desc='%s'\n", func, desc);
#undef PRN_NULLCHECK
}
