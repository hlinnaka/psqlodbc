
/* File:            statement.h
 *
 * Description:     See "statement.c"
 *
 * Comments:        See "notice.txt" for copyright and license information.
 *
 */

#ifndef __STATEMENT_H__
#define __STATEMENT_H__

#include <windows.h>
#include <sql.h>
#include "psqlodbc.h"

typedef enum {
    STMT_ALLOCATED,     /* The statement handle is allocated, but not used so far */
    STMT_READY,         /* the statement is waiting to be executed */
    STMT_PREMATURE,     /* ODBC states that it is legal to call e.g. SQLDescribeCol before
                           a call to SQLExecute, but after SQLPrepare. To get all the necessary
                           information in such a case, we simply execute the query _before_ the
                           actual call to SQLExecute, so that statement is considered to be "premature".
                        */   
    STMT_FINISHED,      /* statement execution has finished */
    STMT_EXECUTING      /* statement execution is still going on */
} STMT_Status;

#define STMT_TRUNCATED -2
#define STMT_INFO_ONLY -1 /* not an error message, just a notification to be returned by SQLError */
#define STMT_OK 0 /* will be interpreted as "no error pending" */
#define STMT_EXEC_ERROR 1
#define STMT_STATUS_ERROR 2
#define STMT_SEQUENCE_ERROR 3
#define STMT_NO_MEMORY_ERROR 4
#define STMT_COLNUM_ERROR 5
#define STMT_NO_STMTSTRING 6
#define STMT_ERROR_TAKEN_FROM_BACKEND 7
#define STMT_INTERNAL_ERROR 8
#define STMT_STILL_EXECUTING 9
#define STMT_NOT_IMPLEMENTED_ERROR 10
#define STMT_BAD_PARAMETER_NUMBER_ERROR 11
#define STMT_OPTION_OUT_OF_RANGE_ERROR 12
#define STMT_INVALID_COLUMN_NUMBER_ERROR 13
#define STMT_RESTRICTED_DATA_TYPE_ERROR 14
#define STMT_INVALID_CURSOR_STATE_ERROR 15
#define STMT_OPTION_VALUE_CHANGED 16
#define STMT_CREATE_TABLE_ERROR 17
#define STMT_NO_CURSOR_NAME 18
#define STMT_INVALID_CURSOR_NAME 19

/* statement types */
enum {
	STMT_TYPE_UNKNOWN = -2,
	STMT_TYPE_OTHER = -1,
	STMT_TYPE_SELECT = 0,
	STMT_TYPE_INSERT,
	STMT_TYPE_UPDATE,
	STMT_TYPE_DELETE,
	STMT_TYPE_CREATE,
	STMT_TYPE_ALTER,
	STMT_TYPE_DROP,
	STMT_TYPE_GRANT,
	STMT_TYPE_REVOKE,
};

#define STMT_UPDATE(stmt)	(stmt->statement_type > STMT_TYPE_SELECT)

/********	Statement Handle	***********/
struct StatementClass_ {
    ConnectionClass *hdbc;		/* pointer to ConnectionClass this statement belongs to */

    QResultClass *result;		/* result of the current statement */

    STMT_Status status;
    char *errormsg;
    int errornumber;
	int maxRows;
	int rowset_size;
	int keyset_size;
	int cursor_type;
	int scroll_concurrency;

    /* information on bindings */
    BindInfoClass *bindings;	/* array to store the binding information */
    int bindings_allocated;

    /* information on statement parameters */
    int parameters_allocated;
    ParameterInfoClass *parameters;

	Int4 currTuple;
	int  current_col;			/* current column for GetData -- used to handle multiple calls */
	int  lobj_fd;				/* fd of the current large object */

    char *statement;			/* if non--null pointer to the SQL statement that has been executed */

    int statement_type;			/* According to the defines above */
	int data_at_exec;			/* Number of params needing SQLPutData */
	int current_exec_param;		/* The current parameter for SQLPutData */

	char put_data;				/* Has SQLPutData been called yet? */

	char errormsg_created;		/* has an informative error msg been created?  */
	char manual_result;			/* Is the statement result manually built? */
	char prepare;				/* is this statement a prepared statement or direct */

	char internal;				/* Is this statement being called internally? */

	char cursor_name[32];
	char stmt_with_params[65536 /* MAX_STATEMENT_LEN */];		/* statement after parameter substitution */

};

#define SC_get_conn(a)    (a->hdbc)
#define SC_get_Result(a)  (a->result);

/*	options for SC_free_params() */
#define STMT_FREE_PARAMS_ALL				0
#define STMT_FREE_PARAMS_DATA_AT_EXEC_ONLY	1

/*	Statement prototypes */
StatementClass *SC_Constructor();
char SC_Destructor(StatementClass *self);
int statement_type(char *statement);
void SC_pre_execute(StatementClass *self);
char SC_unbind_cols(StatementClass *self);
char SC_recycle_statement(StatementClass *self);

void SC_clear_error(StatementClass *self);
char SC_get_error(StatementClass *self, int *number, char **message);
char *SC_create_errormsg(StatementClass *self);
RETCODE SC_execute(StatementClass *stmt);
void SC_free_params(StatementClass *self, char option);
void SC_log_error(char *func, char *desc, StatementClass *self);

#endif
