/* File:			psqlodbc.h
 *
 * Description:		This file contains defines and declarations that are related to
 *					the entire driver.
 *
 * Comments:		See "notice.txt" for copyright and license information.
 *
 * $Id: psqlodbc.h,v 1.56 2001/11/05 17:46:38 momjian Exp $
 *
 */

#ifndef __PSQLODBC_H__
#define __PSQLODBC_H__

#ifndef WIN32
#include "pg_config.h"
#else
#include <windows.h>
#endif

#include <stdio.h>				/* for FILE* pointers: see GLOBAL_VALUES */

/* Must come before sql.h */
#ifndef ODBCVER
#define ODBCVER						0x0250
#endif   /* ODBCVER_REP */


#if defined(WIN32) || defined(WITH_UNIXODBC) || defined(WITH_IODBC)
#include <sql.h>
#include <sqlext.h>
#else
#include "iodbc.h"
#include "isql.h"
#include "isqlext.h"
#endif

#if defined(WIN32)
#include <odbcinst.h>
#elif defined(WITH_UNIXODBC)
#include <odbcinst.h>
#elif defined(WITH_IODBC)
#include <iodbcinst.h>
#else
#include "gpps.h"
#endif

#ifndef WIN32
#define Int4 long int
#define UInt4 unsigned int
#define Int2 short
#define UInt2 unsigned short

#if !defined(WITH_UNIXODBC) && !defined(WITH_IODBC)
typedef float SFLOAT;
typedef double SDOUBLE;
#endif

#ifndef CALLBACK
#define CALLBACK
#endif

#else
#define Int4 int
#define UInt4 unsigned int
#define Int2 short
#define UInt2 unsigned short
#endif

typedef UInt4 Oid;

#ifndef WIN32
#define stricmp strcasecmp
#define strnicmp strncasecmp
#endif

/* Driver stuff */

#define DRIVERNAME					"PostgreSQL ODBC"
#if (ODBCVER >= 0x0300)
#define DRIVER_ODBC_VER				"03.00"
#define DBMS_NAME					"PostgreSQL30"
#else
#define DRIVER_ODBC_VER				"02.50"
#define DBMS_NAME					"PostgreSQL"
#endif   /* ODBCVER */

#define POSTGRESDRIVERVERSION		"07.01.0009"

#ifdef WIN32
#if (ODBCVER >= 0x0300)
#define DRIVER_FILE_NAME			"PSQLODBC30.DLL"
#else
#define DRIVER_FILE_NAME			"PSQLODBC.DLL"
#endif   /* ODBCVER */
#else
#define DRIVER_FILE_NAME			"libpsqlodbc.so"
#endif   /* WIN32 */

/* Limits */
#ifdef WIN32
#define BLCKSZ						4096
#endif

#define MAX_MESSAGE_LEN				65536		/* This puts a limit on
												 * query size but I don't */
 /* see an easy way round this - DJP 24-1-2001 */
#define MAX_CONNECT_STRING			4096
#define ERROR_MSG_LENGTH			4096
#define FETCH_MAX					100 /* default number of rows to cache
										 * for declare/fetch */
#define TUPLE_MALLOC_INC			100
#define SOCK_BUFFER_SIZE			4096		/* default socket buffer
												 * size */
#define MAX_CONNECTIONS				128 /* conns per environment
										 * (arbitrary)	*/
#define MAX_FIELDS					512
#define BYTELEN						8
#define VARHDRSZ					sizeof(Int4)

#define MAX_TABLE_LEN				32
#define MAX_COLUMN_LEN				32
#define MAX_CURSOR_LEN				32

/*	Registry length limits */
#define LARGE_REGISTRY_LEN			4096		/* used for special cases */
#define MEDIUM_REGISTRY_LEN			256 /* normal size for
										 * user,database,etc. */
#define SMALL_REGISTRY_LEN			10	/* for 1/0 settings */


/*	These prefixes denote system tables */
#define POSTGRES_SYS_PREFIX			"pg_"
#define KEYS_TABLE					"dd_fkey"

/*	Info limits */
#define MAX_INFO_STRING				128
#define MAX_KEYPARTS				20
#define MAX_KEYLEN					512 /* max key of the form
										 * "date+outlet+invoice" */
#define MAX_ROW_SIZE				0	/* Unlimited rowsize with the
										 * Tuple Toaster */
#define MAX_STATEMENT_LEN			0	/* Unlimited statement size with
										 * 7.0 */

/* Previously, numerous query strings were defined of length MAX_STATEMENT_LEN */
/* Now that's 0, lets use this instead. DJP 24-1-2001 */
#define STD_STATEMENT_LEN			MAX_MESSAGE_LEN

#define PG62						"6.2"		/* "Protocol" key setting
												 * to force Postgres 6.2 */
#define PG63						"6.3"		/* "Protocol" key setting
												 * to force postgres 6.3 */
#define PG64						"6.4"

typedef struct ConnectionClass_ ConnectionClass;
typedef struct StatementClass_ StatementClass;
typedef struct QResultClass_ QResultClass;
typedef struct SocketClass_ SocketClass;
typedef struct BindInfoClass_ BindInfoClass;
typedef struct ParameterInfoClass_ ParameterInfoClass;
typedef struct ColumnInfoClass_ ColumnInfoClass;
typedef struct TupleListClass_ TupleListClass;
typedef struct EnvironmentClass_ EnvironmentClass;
typedef struct TupleNode_ TupleNode;
typedef struct TupleField_ TupleField;

typedef struct col_info COL_INFO;
typedef struct lo_arg LO_ARG;

typedef struct GlobalValues_
{
	int			fetch_max;
	int			socket_buffersize;
	int			unknown_sizes;
	int			max_varchar_size;
	int			max_longvarchar_size;
	char		debug;
	char		commlog;
	char		disable_optimizer;
	char		ksqo;
	char		unique_index;
	char		onlyread;		/* readonly is reserved on Digital C++
								 * compiler */
	char		use_declarefetch;
	char		text_as_longvarchar;
	char		unknowns_as_longvarchar;
	char		bools_as_char;
	char		lie;
	char		parse;
	char		cancel_as_freestmt;
	char		extra_systable_prefixes[MEDIUM_REGISTRY_LEN];
	char		conn_settings[LARGE_REGISTRY_LEN];
	char		protocol[SMALL_REGISTRY_LEN];
} GLOBAL_VALUES;

typedef struct StatementOptions_
{
	int			maxRows;
	int			maxLength;
	int			rowset_size;
	int			keyset_size;
	int			cursor_type;
	int			scroll_concurrency;
	int			retrieve_data;
	int			bind_size;		/* size of each structure if using Row
								 * Binding */
	int			use_bookmarks;
	UInt4	   *rowsFetched;
	UInt2	   *rowStatusArray;
	void	   *bookmark_ptr;
	UInt2	   *row_operation_ptr;
	UInt4	   *row_offset_ptr;
	UInt4		paramset_size;
	UInt4		param_bind_type;
	UInt4	   *param_processed_ptr;
	UInt2	   *param_status_ptr;
	UInt2	   *param_operation_ptr;
	UInt4	   *param_offset_ptr;
} StatementOptions;

/*	Used to pass extra query info to send_query */
typedef struct QueryInfo_
{
	int			row_size;
	QResultClass *result_in;
	char	   *cursor;
} QueryInfo;

void		logs_on_off(int cnopen, int, int);

#define PG_TYPE_LO					(-999)		/* hack until permanent
												 * type available */
#define PG_TYPE_LO_NAME				"lo"
#define OID_ATTNUM					(-2)		/* the attnum in pg_index
												 * of the oid */

/* sizes */
#define TEXT_FIELD_SIZE				8190		/* size of text fields
												 * (not including null
												 * term) */
#define NAME_FIELD_SIZE				32	/* size of name fields */
#define MAX_VARCHAR_SIZE			254 /* maximum size of a varchar (not
										 * including null term) */

#define PG_NUMERIC_MAX_PRECISION	1000
#define PG_NUMERIC_MAX_SCALE		1000

#define INFO_INQUIRY_LEN		8192	/* this seems sufficiently big for
										 * queries used in info.c inoue
										 * 2001/05/17 */

#include "misc.h"

#ifdef	_MEMORY_DEBUG_
void	   *debug_alloc(size_t);
void	   *debug_realloc(void *, size_t);
char	   *debug_strdup(const char *);
void		debug_free(void *);
void		debug_memory_check(void);

#define malloc	debug_alloc
#define realloc debug_realloc
#define strdup	debug_strdup
#define free	debug_free
#endif   /* _MEMORY_DEBUG_ */

#endif
