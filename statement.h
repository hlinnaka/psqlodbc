/* File:			statement.h
 *
 * Description:		See "statement.c"
 *
 * Comments:		See "notice.txt" for copyright and license information.
 *
 */

#ifndef __STATEMENT_H__
#define __STATEMENT_H__

#include "psqlodbc.h"
#include <time.h>

#include "pgtypes.h"
#include "bind.h"
#include "descriptor.h"

#if defined (POSIX_MULTITHREAD_SUPPORT)
#include <pthread.h>
#endif

typedef enum
{
	STMT_ALLOCATED,				/* The statement handle is allocated, but
								 * not used so far */
	STMT_READY,					/* the statement is waiting to be executed */
	STMT_PREMATURE,				/* ODBC states that it is legal to call
								 * e.g. SQLDescribeCol before a call to
								 * SQLExecute, but after SQLPrepare. To
								 * get all the necessary information in
								 * such a case, we simply execute the
								 * query _before_ the actual call to
								 * SQLExecute, so that statement is
								 * considered to be "premature". */
	STMT_FINISHED,				/* statement execution has finished */
	STMT_EXECUTING				/* statement execution is still going on */
} STMT_Status;
/*
 *		ERROR status code
 *
 *		The code for warnings must be minus
 *		and  LOWEST_STMT_ERROR must be set to
 *		the least code number.
 *		The code for STMT_OK is 0 and error
 *		codes follow after it.
 */ 	
enum {
	LOWEST_STMT_ERROR		=		(-6)
	/* minus values mean warning returns */
	,STMT_ERROR_IN_ROW		=		(-6)
	,STMT_OPTION_VALUE_CHANGED	=		(-5)
	,STMT_ROW_VERSION_CHANGED	=		(-4)
	,STMT_POS_BEFORE_RECORDSET	=		(-3)
	,STMT_TRUNCATED			=		(-2)
	,STMT_INFO_ONLY			=		(-1)
				/* not an error message,
				 * just a notification
				 * to be returned by
				 * SQLError
				 */
	,STMT_OK			=		0
	,STMT_EXEC_ERROR
	,STMT_STATUS_ERROR
	,STMT_SEQUENCE_ERROR
	,STMT_NO_MEMORY_ERROR
	,STMT_COLNUM_ERROR
	,STMT_NO_STMTSTRING
	,STMT_ERROR_TAKEN_FROM_BACKEND
	,STMT_INTERNAL_ERROR
	,STMT_STILL_EXECUTING
	,STMT_NOT_IMPLEMENTED_ERROR
	,STMT_BAD_PARAMETER_NUMBER_ERROR
	,STMT_OPTION_OUT_OF_RANGE_ERROR	
	,STMT_INVALID_COLUMN_NUMBER_ERROR
	,STMT_RESTRICTED_DATA_TYPE_ERROR
	,STMT_INVALID_CURSOR_STATE_ERROR
	,STMT_CREATE_TABLE_ERROR
	,STMT_NO_CURSOR_NAME
	,STMT_INVALID_CURSOR_NAME
	,STMT_INVALID_ARGUMENT_NO
	,STMT_ROW_OUT_OF_RANGE
	,STMT_OPERATION_CANCELLED
	,STMT_INVALID_CURSOR_POSITION
	,STMT_VALUE_OUT_OF_RANGE
	,STMT_OPERATION_INVALID
	,STMT_PROGRAM_TYPE_OUT_OF_RANGE
	,STMT_BAD_ERROR
	,STMT_INVALID_OPTION_IDENTIFIER
	,STMT_RETURN_NULL_WITHOUT_INDICATOR
	,STMT_INVALID_DESCRIPTOR_IDENTIFIER
	,STMT_OPTION_NOT_FOR_THE_DRIVER
	,STMT_FETCH_OUT_OF_RANGE
	,STMT_COUNT_FIELD_INCORRECT
	,STMT_INVALID_NULL_ARG
};

/* statement types */
enum
{
	STMT_TYPE_UNKNOWN = -2
	,STMT_TYPE_OTHER = -1
	,STMT_TYPE_SELECT = 0
	,STMT_TYPE_INSERT
	,STMT_TYPE_UPDATE
	,STMT_TYPE_DELETE
	,STMT_TYPE_CREATE
	,STMT_TYPE_ALTER
	,STMT_TYPE_DROP
	,STMT_TYPE_GRANT
	,STMT_TYPE_REVOKE
	,STMT_TYPE_PROCCALL
	,STMT_TYPE_LOCK
	,STMT_TYPE_TRANSACTION
	,STMT_TYPE_CLOSE
	,STMT_TYPE_FETCH
	,STMT_TYPE_PREPARE
	,STMT_TYPE_EXECUTE
	,STMT_TYPE_DEALLOCATE
	,STMT_TYPE_ANALYZE
	,STMT_TYPE_NOTIFY
	,STMT_TYPE_EXPLAIN
	,STMT_TYPE_SET
	,STMT_TYPE_RESET
	,STMT_TYPE_DECLARE
	,STMT_TYPE_MOVE
	,STMT_TYPE_COPY
	,STMT_TYPE_START
	,STMT_TYPE_SPECIAL
};

#define STMT_UPDATE(stmt)	(stmt->statement_type > STMT_TYPE_SELECT)


/*	Parsing status */
enum
{
	STMT_PARSE_NONE = 0
	,STMT_PARSE_COMPLETE	/* the driver parsed the statement */
	,STMT_PARSE_INCOMPLETE
	,STMT_PARSE_FATAL
	,STMT_PARSE_MASK = 3L
	,STMT_PARSED_OIDS = (1L << 2)
	,STMT_FOUND_KEY = (1L << 3)
	,STMT_HAS_ROW_DESCRIPTION = (1L << 4) /* already got the col info */
	,STMT_REFLECTED_ROW_DESCRIPTION = (1L << 5)
};

/*	Result style */
enum
{
	STMT_FETCH_NONE = 0,
	STMT_FETCH_NORMAL,
	STMT_FETCH_EXTENDED
};

#define	PG_NUM_NORMAL_KEYS	2

typedef	RETCODE	(*NeedDataCallfunc)(RETCODE, void *);
typedef	struct
{
	NeedDataCallfunc	func;
	void			*data;
}	NeedDataCallback;

/********	Statement Handle	***********/
struct StatementClass_
{
	ConnectionClass *hdbc;		/* pointer to ConnectionClass this
								 * statement belongs to */
	QResultClass *result;		/* result of the current statement */
	QResultClass *curres;		/* the current result in the chain */
	HSTMT FAR  *phstmt;
	StatementOptions options;
	StatementOptions options_orig;
	/* attached descriptor handles */
	ARDClass	*ard;
	APDClass	*apd;
	IRDClass	*ird;
	IPDClass	*ipd;
	/* implicit descriptor handles */
	ARDClass	ardi;
	IRDClass	irdi;
	APDClass	apdi;
	IPDClass	ipdi;

	STMT_Status status;
	char	   *__error_message;
	int			__error_number;
	PG_ErrorInfo	*pgerror;

	SQLLEN		currTuple;	/* current absolute row number (GetData,
						 * SetPos, SQLFetch) */
	GetDataInfo	gdata_info;
	SQLLEN		save_rowset_size;	/* saved rowset size in case of
							 * change/FETCH_NEXT */
	SQLLEN		rowset_start;	/* start of rowset (an absolute row
								 * number) */
	SQLSETPOSIROW	bind_row;	/* current offset for Multiple row/column
						 * binding */
	Int2		current_col;	/* current column for GetData -- used to
						 * handle multiple calls */
	SQLLEN		last_fetch_count;	/* number of rows retrieved in
						 * last fetch/extended fetch */
	int		lobj_fd;		/* fd of the current large object */

	char	   *statement;		/* if non--null pointer to the SQL
					 * statement that has been executed */

	TABLE_INFO	**ti;
	Int2		ntab;
	Int2		num_key_fields;
	Int2		statement_type; /* According to the defines above */
	Int2		num_params;
	Int2		data_at_exec; /* Number of params needing SQLPutData */
	Int2		current_exec_param;	/* The current parameter for
						 * SQLPutData */
	PutDataInfo	pdata_info;
	char		parse_status;
	char		proc_return;
	char		put_data;	/* Has SQLPutData been called ? */
	char		catalog_result;	/* Is this a result of catalog function ? */
	char		prepare;	/* is this a prepared statement ? */
	char		prepared;	/* is this statement already
					 * prepared at the server ? */
	char		internal;	/* Is this statement being called
							 * internally ? */
	char		transition_status;	/* Transition status */
	char		multi_statement; /* -1:unknown 0:single 1:multi */
	char		rbonerr;	/* rollback on error */
	char		discard_output_params;	 /* discard output parameters on parse stage */
	char		cancel_info;	/* cancel information */
	char		ref_CC_error;	/* refer to CC_error ? */
	char		lock_CC_for_rb;	/* lock CC for statement rollback ? */
	char		join_info;	/* have joins ? */
	char		parse_method;	/* parse_statement is forced or ? */
	pgNAME		cursor_name;
	char		*plan_name;

	char		*stmt_with_params;	/* statement after parameter
							 * substitution */
	Int4		stmt_size_limit; /* PG restriction */
	SQLLEN		exec_start_row;
	SQLLEN		exec_end_row;
	SQLLEN		exec_current_row;

	char		pre_executing;	/* This statement is prematurely executing */
	char		inaccurate_result;	/* Current status is PREMATURE but
						 * result is inaccurate */
	unsigned char	miscinfo;
	char		updatable;
	SQLLEN		diag_row_count;
	char		*load_statement; /* to (re)load updatable individual rows */
	char		*execute_statement; /* to execute the prepared plans */
	Int4		from_pos;	
	Int4		where_pos;
	SQLLEN		last_fetch_count_include_ommitted;
	time_t		stmt_time;
	/* SQL_NEED_DATA Callback list */
	StatementClass	*execute_delegate;
	StatementClass	*execute_parent;
	UInt2		allocated_callbacks;
	UInt2		num_callbacks;
	NeedDataCallback	*callbacks;
#if defined(WIN_MULTITHREAD_SUPPORT)
	CRITICAL_SECTION	cs;
#elif defined(POSIX_THREADMUTEX_SUPPORT)
	pthread_mutex_t		cs;
#endif /* WIN_MULTITHREAD_SUPPORT */

};

#define SC_get_conn(a)	  (a->hdbc)
#define SC_init_Result(a)  (a->result = a->curres = NULL, mylog("SC_init_Result(%x)", a))
#define SC_set_Result(a, b) \
do { \
	if (b != a->result) \
	{ \
		mylog("SC_set_Result(%x, %x)", a, b); \
		QR_Destructor(a->result); \
		a->result = a->curres = b; \
	} \
} while (0)
#define SC_get_Result(a)  (a->result)
#define SC_set_Curres(a, b)  (a->curres = b)
#define SC_get_Curres(a)  (a->curres)
#define SC_get_ARD(a)  (a->ard)
#define SC_get_APD(a)  (a->apd)
#define SC_get_IRD(a)  (a->ird)
#define SC_get_IPD(a)  (a->ipd)
#define SC_get_ARDF(a)  (&(SC_get_ARD(a)->ardopts))
#define SC_get_APDF(a)  (&(SC_get_APD(a)->apdopts))
#define SC_get_IRDF(a)  (&(SC_get_IRD(a)->irdopts))
#define SC_get_IPDF(a)  (&(SC_get_IPD(a)->ipdopts))
#define SC_get_ARDi(a)  (&(a->ardi))
#define SC_get_APDi(a)  (&(a->apdi))
#define SC_get_IRDi(a)  (&(a->irdi))
#define SC_get_IPDi(a)  (&(a->ipdi))
#define SC_get_GDTI(a)  (&(a->gdata_info))
#define SC_get_PDTI(a)  (&(a->pdata_info))

#define	SC_get_errornumber(a) (a->__error_number)
#define	SC_set_errornumber(a, n) (a->__error_number = n)
#define	SC_get_errormsg(a) (a->__error_message)
#define	SC_get_errormsg(a) (a->__error_message)
#define	SC_is_prepare_statement(a) (0 != (a->prepare & PREPARE_STATEMENT))
#define	SC_get_prepare_method(a) (a->prepare & (~PREPARE_STATEMENT))

#define	SC_parsed_status(a)	(a->parse_status & STMT_PARSE_MASK)
#define	SC_set_parse_status(a, s) (a->parse_status |= s)
#define	SC_update_not_ready(a)	(SC_parsed_status(a) == STMT_PARSE_NONE || 0 == (a->parse_status & STMT_PARSED_OIDS))
#define	SC_update_ready(a)	(SC_parsed_status(a) == STMT_PARSE_COMPLETE && 0 != (a->parse_status & STMT_FOUND_KEY) && a->updatable)
#define	SC_set_checked_hasoids(a, b)	(a->parse_status |= (STMT_PARSED_OIDS | (b ? STMT_FOUND_KEY : 0)))
#define	SC_checked_hasoids(a)	(0 != (a->parse_status & STMT_PARSED_OIDS))
#define	SC_set_delegate(p, c) (p->execute_delegate = c, c->execute_parent = p)

#define	SC_clear_parse_method(s)	((s)->parse_method = 0)
#define	SC_is_parse_forced(s)	(0 != ((s)->parse_method & 1L))
#define	SC_set_parse_forced(s)	((s)->parse_method |= 1L)
#define	SC_is_parse_tricky(s)	(0 != ((s)->parse_method & 2L))
#define	SC_set_parse_tricky(s)	((s)->parse_method |= 2L)
#define	SC_no_parse_tricky(s)	((s)->parse_method &= ~2L)

#define	SC_cursor_is_valid(s)	(NAME_IS_VALID(s->cursor_name))
#define	SC_cursor_name(s)	(SAFE_NAME(s->cursor_name))

void	SC_reset_delegate(RETCODE, StatementClass *);
StatementClass *SC_get_ancestor(StatementClass *);

#if (ODBCVER >= 0x0300)
#define	SC_is_lower_case(a, b) (a->options.metadata_id || b->connInfo.lower_case_identifier)
#else
#define	SC_is_lower_case(a, b) (b->connInfo.lower_case_identifier)
#endif /* ODBCVER */

#define	SC_MALLOC_return_with_error(t, tp, s, a, m, r) \
do { \
	if (t = (tp *) malloc(s), NULL == t) \
	{ \
		SC_set_error(a, STMT_NO_MEMORY_ERROR, m, "SC_MALLOC"); \
		return r; \
	} \
} while (0)
#define	SC_REALLOC_return_with_error(t, tp, s, a, m, r) \
do { \
	if (t = (tp *) realloc(t, s), NULL == t) \
	{ \
		SC_set_error(a, STMT_NO_MEMORY_ERROR, m, "SC_REALLOC"); \
		return r; \
	} \
} while (0)

/*	options for SC_free_params() */
#define STMT_FREE_PARAMS_ALL				0
#define STMT_FREE_PARAMS_DATA_AT_EXEC_ONLY	1

/*	prepare state */
enum {
	  NON_PREPARE_STATEMENT = 0
	, PREPARE_STATEMENT = 1
	, PREPARE_BY_THE_DRIVER = (1L << 1)
	, USING_PREPARE_COMMAND = (2L << 1)
	, NAMED_PARSE_REQUEST = (3L << 1)
	, PARSE_TO_EXEC_ONCE = (4L << 1)
	, PARSE_REQ_FOR_INFO = (5L << 1)
};

/*	prepared state */
enum
{
	NOT_YET_PREPARED = 0
	,PREPARED_PERMANENTLY 
	,PREPARED_TEMPORARILY
	,ONCE_DESCRIBED
};

/*	misc info */
#define SC_set_pre_executable(a) (a->miscinfo |= 1L)
#define SC_no_pre_executable(a) (a->miscinfo &= ~1L)
#define SC_is_pre_executable(a) ((a->miscinfo & 1L) != 0)
#define SC_set_fetchcursor(a)	(a->miscinfo |= (1L << 1))
#define SC_no_fetchcursor(a)	(a->miscinfo &= ~(1L << 1))
#define SC_is_fetchcursor(a)	((a->miscinfo & (1L << 1)) != 0)
#define SC_set_concat_prepare_exec(a)	(a->miscinfo |= (1L << 2))
#define SC_no_concat_prepare_exec(a)	(a->miscinfo &= ~(1L << 2))
#define SC_is_concat_prepare_exec(a)	((a->miscinfo & (1L << 2)) != 0)
#define SC_set_with_hold(a)	(a->miscinfo |= (1L << 3))
#define SC_set_without_hold(a)	(a->miscinfo &= ~(1L << 3))
#define SC_is_with_hold(a)	((a->miscinfo & (1L << 3)) != 0)
#define SC_miscinfo_clear(a)	(a->miscinfo &= (1L << 3))
#define	STMT_HAS_OUTER_JOIN	1L
#define	STMT_HAS_INNER_JOIN	(1L << 1)
#define SC_has_join(a)		(0 != (a)->join_info)
#define SC_has_outer_join(a)	(0 != (STMT_HAS_OUTER_JOIN & (a)->join_info))
#define SC_has_inner_join(a)	(0 != (STMT_HAS_INNER_JOIN & (a)->join_info))
#define SC_set_outer_join(a)	((a)->join_info |= STMT_HAS_OUTER_JOIN)
#define SC_set_inner_join(a)	((a)->join_info |= STMT_HAS_INNER_JOIN)

#define SC_start_stmt(a)	(a->rbonerr = 0)
#define SC_start_tc_stmt(a)	(a->rbonerr = (1L << 1))
#define SC_is_tc_stmt(a)	((a->rbonerr & (1L << 1)) != 0)
#define SC_start_rb_stmt(a)	(a->rbonerr = (1L << 2))
#define SC_is_rb_stmt(a)	((a->rbonerr & (1L << 2)) != 0)
#define SC_set_accessed_db(a)	(a->rbonerr |= (1L << 3))
#define SC_accessed_db(a)	((a->rbonerr & (1L << 3)) != 0)
#define SC_start_rbpoint(a)	(a->rbonerr |= (1L << 4))
#define SC_started_rbpoint(a)	((a->rbonerr & (1L << 4)) != 0)
#define SC_unref_CC_error(a)	((a->ref_CC_error) = FALSE)
#define SC_ref_CC_error(a)	((a->ref_CC_error) = TRUE)
#define SC_forget_unnamed(a)	(PREPARED_TEMPORARILY == (a)->prepared ? SC_set_prepared(a, ONCE_DESCRIBED) : (void) 0)


/* For Multi-thread */
#if defined(WIN_MULTITHREAD_SUPPORT)
#define INIT_STMT_CS(x)		InitializeCriticalSection(&((x)->cs))
#define ENTER_STMT_CS(x)	EnterCriticalSection(&((x)->cs))
#define TRY_ENTER_STMT_CS(x)	TryEnterCriticalSection(&((x)->cs))
#define LEAVE_STMT_CS(x)	LeaveCriticalSection(&((x)->cs))
#define DELETE_STMT_CS(x)	DeleteCriticalSection(&((x)->cs))
#elif defined(POSIX_THREADMUTEX_SUPPORT)
#define INIT_STMT_CS(x)		pthread_mutex_init(&((x)->cs),0)
#define ENTER_STMT_CS(x)	pthread_mutex_lock(&((x)->cs))
#define TRY_ENTER_STMT_CS(x)	(0 == pthread_mutex_trylock(&((x)->cs)))
#define LEAVE_STMT_CS(x)	pthread_mutex_unlock(&((x)->cs))
#define DELETE_STMT_CS(x)	pthread_mutex_destroy(&((x)->cs))
#else
#define INIT_STMT_CS(x)
#define ENTER_STMT_CS(x)
#define TRY_ENTER_STMT_CS(x)	(1)
#define LEAVE_STMT_CS(x)
#define DELETE_STMT_CS(x)
#endif /* WIN_MULTITHREAD_SUPPORT */
/*	Statement prototypes */
StatementClass *SC_Constructor(ConnectionClass *);
void		InitializeStatementOptions(StatementOptions *opt);
char		SC_Destructor(StatementClass *self);
BOOL		SC_opencheck(StatementClass *self, const char *func);
RETCODE		SC_initialize_and_recycle(StatementClass *self);
void		SC_initialize_cols_info(StatementClass *self, BOOL DCdestroy, BOOL parseReset);
int		statement_type(const char *statement);
char		parse_statement(StatementClass *stmt, BOOL);
Int4		SC_pre_execute(StatementClass *self);
char		SC_unbind_cols(StatementClass *self);
char		SC_recycle_statement(StatementClass *self);

void		SC_clear_error(StatementClass *self);
void		SC_set_error(StatementClass *self, int errnum, const char *msg, const char *func);
void		SC_set_errormsg(StatementClass *self, const char *msg);
void		SC_error_copy(StatementClass *self, const StatementClass *from, BOOL);
void		SC_full_error_copy(StatementClass *self, const StatementClass *from, BOOL);
void		SC_replace_error_with_res(StatementClass *self, int errnum, const char *msg, const QResultClass*, BOOL);
void		SC_set_prepared(StatementClass *self, BOOL);
void		SC_set_planname(StatementClass *self, const char *plan_name);
void		SC_set_rowset_start(StatementClass *self, SQLLEN, BOOL);
void		SC_inc_rowset_start(StatementClass *self, SQLLEN);
RETCODE		SC_initialize_stmts(StatementClass *self, BOOL);
RETCODE		SC_execute(StatementClass *self);
RETCODE		SC_fetch(StatementClass *self);
void		SC_free_params(StatementClass *self, char option);
void		SC_log_error(const char *func, const char *desc, const StatementClass *self);
time_t		SC_get_time(StatementClass *self);
SQLULEN		SC_get_bookmark(StatementClass *self);
RETCODE		SC_pos_reload(StatementClass *self, SQLULEN index, UInt2 *, Int4);
RETCODE		SC_pos_update(StatementClass *self, SQLSETPOSIROW irow, SQLULEN index);
RETCODE		SC_pos_delete(StatementClass *self, SQLSETPOSIROW irow, SQLULEN index);
RETCODE		SC_pos_refresh(StatementClass *self, SQLSETPOSIROW irow, SQLULEN index);
RETCODE		SC_pos_add(StatementClass *self, SQLSETPOSIROW irow);
int		SC_set_current_col(StatementClass *self, int col);
void		SC_setInsertedTable(StatementClass *, RETCODE);
void		SC_scanQueryAndCountParams(const char *, const ConnectionClass *,
			Int4 *next_cmd, SQLSMALLINT *num_params,
			char *multi, char *proc_return);

BOOL	SC_IsExecuting(const StatementClass *self);
BOOL	SC_SetExecuting(StatementClass *self, BOOL on);
BOOL	SC_SetCancelRequest(StatementClass *self);
BOOL	SC_AcceptedCancelRequest(const StatementClass *self);

DescriptorClass	*SC_set_ARD(StatementClass *stmt, DescriptorClass *desc);
DescriptorClass	*SC_set_APD(StatementClass *stmt, DescriptorClass *desc);
int		enqueueNeedDataCallback(StatementClass *self, NeedDataCallfunc, void *);
RETCODE		dequeueNeedDataCallback(RETCODE, StatementClass *self);
void		cancelNeedDataState(StatementClass *self);
int		StartRollbackState(StatementClass *self);
RETCODE		SetStatementSvp(StatementClass *self);
RETCODE		DiscardStatementSvp(StatementClass *self, RETCODE, BOOL errorOnly);

BOOL		SendParseRequest(StatementClass *self, const char *name,
			const char *query, Int4 qlen, Int2 num_params);
BOOL		SendDescribeRequest(StatementClass *self, const char *name);
BOOL		SendBindRequest(StatementClass *self, const char *name);
BOOL		BuildBindRequest(StatementClass *stmt, const char *name);
BOOL		SendExecuteRequest(StatementClass *stmt, const char *portal, UInt4 count);
QResultClass	*SendSyncAndReceive(StatementClass *stmt, QResultClass *res, const char *comment);
/*
 *	Macros to convert global index <-> relative index in resultset/rowset
 */
/* a global index to the relative index in a rowset */
#define	SC_get_rowset_start(stmt) (stmt->rowset_start)
#define	GIdx2RowIdx(gidx, stmt)	(gidx - stmt->rowset_start)
/* a global index to the relative index in a resultset(not a rowset) */
#define	GIdx2CacheIdx(gidx, s, r)	(gidx - (QR_has_valid_base(r) ? (s->rowset_start - r->base) : 0))
#define	GIdx2KResIdx(gidx, s, r)	(gidx - (QR_has_valid_base(r) ? (s->rowset_start - r->key_base) : 0))
/* a relative index in a rowset to the global index */
#define	RowIdx2GIdx(ridx, stmt)	(ridx + stmt->rowset_start)
/* a relative index in a resultset to the global index */
#define	CacheIdx2GIdx(ridx, stmt, res)	(ridx - res->base + stmt->rowset_start)
#define	KResIdx2GIdx(ridx, stmt, res)	(ridx - res->key_base + stmt->rowset_start)

#define	BOOKMARK_SHIFT	1
#define	SC_make_bookmark(b)	((b < 0) ? (b) : (b + BOOKMARK_SHIFT))
#define	SC_resolve_bookmark(b)	((b < 0) ? (b) : (b - BOOKMARK_SHIFT))
#endif /* __STATEMENT_H__ */
