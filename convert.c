/*-------
 * Module:               convert.c
 *
 * Description:    This module contains routines related to
 *				   converting parameters and columns into requested data types.
 *				   Parameters are converted from their SQL_C data types into
 *				   the appropriate postgres type.  Columns are converted from
 *				   their postgres type (SQL type) into the appropriate SQL_C
 *				   data type.
 *
 * Classes:		   n/a
 *
 * API functions:  none
 *
 * Comments:	   See "notice.txt" for copyright and license information.
 *-------
 */
/* Multibyte support  Eiji Tokuya	2001-03-15	*/

#include "convert.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#ifdef MULTIBYTE
#include "multibyte.h"
#endif

#include <time.h>
#include <math.h>
#include <stdlib.h>
#include "statement.h"
#include "qresult.h"
#include "bind.h"
#include "pgtypes.h"
#include "lobj.h"
#include "connection.h"
#include "pgapifunc.h"


/*
 *	How to map ODBC scalar functions {fn func(args)} to Postgres.
 *	This is just a simple substitution.  List augmented from:
 *	http://www.merant.com/datadirect/download/docs/odbc16/Odbcref/rappc.htm
 *	- thomas 2000-04-03
 */
char	   *mapFuncs[][2] = {
/*	{ "ASCII",		 "ascii"	  }, */
	{"CHAR", "chr"},
	{"CONCAT", "textcat"},
/*	{ "DIFFERENCE",  "difference" }, */
/*	{ "INSERT",		 "insert"	  }, */
	{"LCASE", "lower"},
	{"LEFT", "ltrunc"},
	{"LOCATE", "strpos"},
	{"LENGTH", "char_length"},
/*	{ "LTRIM",		 "ltrim"	  }, */
	{"RIGHT", "rtrunc"},
/*	{ "REPEAT",		 "repeat"	  }, */
/*	{ "REPLACE",	 "replace"	  }, */
/*	{ "RTRIM",		 "rtrim"	  }, */
/*	{ "SOUNDEX",	 "soundex"	  }, */
	{"SUBSTRING", "substr"},
	{"UCASE", "upper"},

/*	{ "ABS",		 "abs"		  }, */
/*	{ "ACOS",		 "acos"		  }, */
/*	{ "ASIN",		 "asin"		  }, */
/*	{ "ATAN",		 "atan"		  }, */
/*	{ "ATAN2",		 "atan2"	  }, */
	{"CEILING", "ceil"},
/*	{ "COS",		 "cos"		  }, */
/*	{ "COT",		 "cot"		  }, */
/*	{ "DEGREES",	 "degrees"	  }, */
/*	{ "EXP",		 "exp"		  }, */
/*	{ "FLOOR",		 "floor"	  }, */
	{"LOG", "ln"},
	{"LOG10", "log"},
/*	{ "MOD",		 "mod"		  }, */
/*	{ "PI",			 "pi"		  }, */
	{"POWER", "pow"},
/*	{ "RADIANS",	 "radians"	  }, */
	{"RAND", "random"},
/*	{ "ROUND",		 "round"	  }, */
/*	{ "SIGN",		 "sign"		  }, */
/*	{ "SIN",		 "sin"		  }, */
/*	{ "SQRT",		 "sqrt"		  }, */
/*	{ "TAN",		 "tan"		  }, */
	{"TRUNCATE", "trunc"},

/*	{ "CURDATE",	 "curdate"	  }, */
/*	{ "CURTIME",	 "curtime"	  }, */
/*	{ "DAYNAME",	 "dayname"	  }, */
/*	{ "DAYOFMONTH",  "dayofmonth" }, */
/*	{ "DAYOFWEEK",	 "dayofweek"  }, */
/*	{ "DAYOFYEAR",	 "dayofyear"  }, */
/*	{ "HOUR",		 "hour"		  }, */
/*	{ "MINUTE",		 "minute"	  }, */
/*	{ "MONTH",		 "month"	  }, */
/*	{ "MONTHNAME",	 "monthname"  }, */
/*	{ "NOW",		 "now"		  }, */
/*	{ "QUARTER",	 "quarter"	  }, */
/*	{ "SECOND",		 "second"	  }, */
/*	{ "WEEK",		 "week"		  }, */
/*	{ "YEAR",		 "year"		  }, */

/*	{ "DATABASE",	 "database"   }, */
	{"IFNULL", "coalesce"},
	{"USER", "odbc_user"},
	{0, 0}
};

static char   *mapFunction(const char *func);
static unsigned int conv_from_octal(const unsigned char *s);
static unsigned int conv_from_hex(const unsigned char *s);
static char	   *conv_to_octal(unsigned char val);

/*---------
 *			A Guide for date/time/timestamp conversions
 *
 *			field_type		fCType				Output
 *			----------		------				----------
 *			PG_TYPE_DATE	SQL_C_DEFAULT		SQL_C_DATE
 *			PG_TYPE_DATE	SQL_C_DATE			SQL_C_DATE
 *			PG_TYPE_DATE	SQL_C_TIMESTAMP		SQL_C_TIMESTAMP		(time = 0 (midnight))
 *			PG_TYPE_TIME	SQL_C_DEFAULT		SQL_C_TIME
 *			PG_TYPE_TIME	SQL_C_TIME			SQL_C_TIME
 *			PG_TYPE_TIME	SQL_C_TIMESTAMP		SQL_C_TIMESTAMP		(date = current date)
 *			PG_TYPE_ABSTIME SQL_C_DEFAULT		SQL_C_TIMESTAMP
 *			PG_TYPE_ABSTIME SQL_C_DATE			SQL_C_DATE			(time is truncated)
 *			PG_TYPE_ABSTIME SQL_C_TIME			SQL_C_TIME			(date is truncated)
 *			PG_TYPE_ABSTIME SQL_C_TIMESTAMP		SQL_C_TIMESTAMP
 *---------
 */



/*	This is called by SQLFetch() */
int
copy_and_convert_field_bindinfo(StatementClass *stmt, Int4 field_type, void *value, int col)
{
	BindInfoClass *bic = &(stmt->bindings[col]);

	return copy_and_convert_field(stmt, field_type, value, (Int2) bic->returntype, (PTR) bic->buffer,
							 (SDWORD) bic->buflen, (SDWORD *) bic->used);
}


/*	This is called by SQLGetData() */
int
copy_and_convert_field(StatementClass *stmt, Int4 field_type, void *value, Int2 fCType,
					   PTR rgbValue, SDWORD cbValueMax, SDWORD *pcbValue)
{
	Int4		len = 0,
				copy_len = 0;
	SIMPLE_TIME st;
	time_t		t = time(NULL);
	struct tm  *tim;
	int			pcbValueOffset,
				rgbValueOffset;
	char	   *rgbValueBindRow;
	const char	*ptr;
	int			bind_row = stmt->bind_row;
	int			bind_size = stmt->options.bind_size;
	int			result = COPY_OK;
	BOOL		changed;
	static		char *tempBuf= NULL;
	static		unsigned int tempBuflen = 0;
	const char *neut_str = value;
	char	midtemp[2][32];
	int	mtemp_cnt = 0;

	if (!tempBuf)
		tempBuflen = 0;
	/*---------
	 *	rgbValueOffset is *ONLY* for character and binary data.
	 *	pcbValueOffset is for computing any pcbValue location
	 *---------
	 */

	if (bind_size > 0)
		pcbValueOffset = rgbValueOffset = (bind_size * bind_row);
	else
	{
		pcbValueOffset = bind_row * sizeof(SDWORD);
		rgbValueOffset = bind_row * cbValueMax;

	}

	memset(&st, 0, sizeof(SIMPLE_TIME));

	/* Initialize current date */
	tim = localtime(&t);
	st.m = tim->tm_mon + 1;
	st.d = tim->tm_mday;
	st.y = tim->tm_year + 1900;

	mylog("copy_and_convert: field_type = %d, fctype = %d, value = '%s', cbValueMax=%d\n", field_type, fCType, (value == NULL) ? "<NULL>" : value, cbValueMax);

	if (!value)
	{

		/*
		 * handle a null just by returning SQL_NULL_DATA in pcbValue, and
		 * doing nothing to the buffer.
		 */
		if (pcbValue)
			*(SDWORD *) ((char *) pcbValue + pcbValueOffset) = SQL_NULL_DATA;
		return COPY_OK;
	}

	if (stmt->hdbc->DataSourceToDriver != NULL)
	{
		int			length = strlen(value);

		stmt->hdbc->DataSourceToDriver(stmt->hdbc->translation_option,
									   SQL_CHAR,
									   value, length,
									   value, length, NULL,
									   NULL, 0, NULL);
	}

	/*
	 * First convert any specific postgres types into more useable data.
	 *
	 * NOTE: Conversions from PG char/varchar of a date/time/timestamp value
	 * to SQL_C_DATE,SQL_C_TIME, SQL_C_TIMESTAMP not supported
	 */
	switch (field_type)
	{

			/*
			 * $$$ need to add parsing for date/time/timestamp strings in
			 * PG_TYPE_CHAR,VARCHAR $$$
			 */
		case PG_TYPE_DATE:
			sscanf(value, "%4d-%2d-%2d", &st.y, &st.m, &st.d);
			break;

		case PG_TYPE_TIME:
			sscanf(value, "%2d:%2d:%2d", &st.hh, &st.mm, &st.ss);
			break;

		case PG_TYPE_ABSTIME:
		case PG_TYPE_DATETIME:
		case PG_TYPE_TIMESTAMP:
			if (strnicmp(value, "invalid", 7) != 0)
				sscanf(value, "%4d-%2d-%2d %2d:%2d:%2d", &st.y, &st.m, &st.d, &st.hh, &st.mm, &st.ss);
			else
			{

				/*
				 * The timestamp is invalid so set something conspicuous,
				 * like the epoch
				 */
				t = 0;
				tim = localtime(&t);
				st.m = tim->tm_mon + 1;
				st.d = tim->tm_mday;
				st.y = tim->tm_year + 1900;
				st.hh = tim->tm_hour;
				st.mm = tim->tm_min;
				st.ss = tim->tm_sec;
			}
			break;

		case PG_TYPE_BOOL:
			{			/* change T/F to 1/0 */
				char	   *s;

				s = midtemp[mtemp_cnt];
				strcpy(s, (char *) value);
				if (s[0] == 'f' || s[0] == 'F' || s[0] == 'n' || s[0] == 'N' || s[0] == '0')
					s[0] = '0';
				else
					s[0] = '1';
				s[1] = '\0';
				neut_str = midtemp[mtemp_cnt];
				mtemp_cnt++;

			}
			break;

			/* This is for internal use by SQLStatistics() */
		case PG_TYPE_INT2VECTOR:
			{
				int			nval,
							i;
				const char	*vp;

				/* this is an array of eight integers */
				short	   *short_array = (short *) ((char *) rgbValue + rgbValueOffset);

				len = 32;
				vp = value;
				nval = 0;
				mylog("index=(");
				for (i = 0; i < 16; i++)
				{
					if (sscanf(vp, "%hd", &short_array[i]) != 1)
						break;

					mylog(" %d", short_array[i]);
					nval++;

					/* skip the current token */
					while ((*vp != '\0') && (!isspace((unsigned char) *vp)))
						vp++;
					/* and skip the space to the next token */
					while ((*vp != '\0') && (isspace((unsigned char) *vp)))
						vp++;
					if (*vp == '\0')
						break;
				}
				mylog(") nval = %d\n", nval);

				for (i = nval; i < 16; i++)
					short_array[i] = 0;

#if 0
				sscanf(value, "%hd %hd %hd %hd %hd %hd %hd %hd",
					   &short_array[0],
					   &short_array[1],
					   &short_array[2],
					   &short_array[3],
					   &short_array[4],
					   &short_array[5],
					   &short_array[6],
					   &short_array[7]);
#endif

				/* There is no corresponding fCType for this. */
				if (pcbValue)
					*(SDWORD *) ((char *) pcbValue + pcbValueOffset) = len;

				return COPY_OK; /* dont go any further or the data will be
								 * trashed */
			}

			/*
			 * This is a large object OID, which is used to store
			 * LONGVARBINARY objects.
			 */
		case PG_TYPE_LO:

			return convert_lo(stmt, value, fCType, ((char *) rgbValue + rgbValueOffset), cbValueMax, (SDWORD *) ((char *) pcbValue + pcbValueOffset));

		default:

			if (field_type == stmt->hdbc->lobj_type)	/* hack until permanent
														 * type available */
				return convert_lo(stmt, value, fCType, ((char *) rgbValue + rgbValueOffset), cbValueMax, (SDWORD *) ((char *) pcbValue + pcbValueOffset));
	}

	/* Change default into something useable */
	if (fCType == SQL_C_DEFAULT)
	{
		fCType = pgtype_to_ctype(stmt, field_type);

		mylog("copy_and_convert, SQL_C_DEFAULT: fCType = %d\n", fCType);
	}

	rgbValueBindRow = (char *) rgbValue + rgbValueOffset;

	if (fCType == SQL_C_CHAR)
	{
		/* Special character formatting as required */

		/*
		 * These really should return error if cbValueMax is not big
		 * enough.
		 */
		switch (field_type)
		{
			case PG_TYPE_DATE:
				len = 10;
				if (cbValueMax > len)
					sprintf(rgbValueBindRow, "%.4d-%.2d-%.2d", st.y, st.m, st.d);
				break;

			case PG_TYPE_TIME:
				len = 8;
				if (cbValueMax > len)
					sprintf(rgbValueBindRow, "%.2d:%.2d:%.2d", st.hh, st.mm, st.ss);
				break;

			case PG_TYPE_ABSTIME:
			case PG_TYPE_DATETIME:
			case PG_TYPE_TIMESTAMP:
				len = 19;
				if (cbValueMax > len)
					sprintf(rgbValueBindRow, "%.4d-%.2d-%.2d %.2d:%.2d:%.2d",
							st.y, st.m, st.d, st.hh, st.mm, st.ss);
				break;

			case PG_TYPE_BOOL:
				len = 1;
				if (cbValueMax > len)
				{
					strcpy(rgbValueBindRow, neut_str);
					mylog("PG_TYPE_BOOL: rgbValueBindRow = '%s'\n", rgbValueBindRow);
				}
				break;

				/*
				 * Currently, data is SILENTLY TRUNCATED for BYTEA and
				 * character data types if there is not enough room in
				 * cbValueMax because the driver can't handle multiple
				 * calls to SQLGetData for these, yet.	Most likely, the
				 * buffer passed in will be big enough to handle the
				 * maximum limit of postgres, anyway.
				 *
				 * LongVarBinary types are handled correctly above, observing
				 * truncation and all that stuff since there is
				 * essentially no limit on the large object used to store
				 * those.
				 */
			case PG_TYPE_BYTEA:/* convert binary data to hex strings
								 * (i.e, 255 = "FF") */
				len = convert_pgbinary_to_char(neut_str, rgbValueBindRow, cbValueMax);

				/***** THIS IS NOT PROPERLY IMPLEMENTED *****/
				break;

			default:
				if (stmt->current_col >= 0 && stmt->bindings[stmt->current_col].data_left == -2)
					stmt->bindings[stmt->current_col].data_left = (cbValueMax > 0) ? 0 : -1; /* This seems to be needed for ADO ? */
				if (stmt->current_col < 0 || stmt->bindings[stmt->current_col].data_left < 0)
				{
					/* convert linefeeds to carriage-return/linefeed */
					len = convert_linefeeds(neut_str, NULL, 0, &changed);
					if (cbValueMax == 0) /* just returns length info */
					{
						result = COPY_RESULT_TRUNCATED;
						break;
					}
					if (changed || len >= cbValueMax)
					{
						if (len >= (int) tempBuflen)
						{
							tempBuf = realloc(tempBuf, len + 1);
							tempBuflen = len + 1;
						}
						convert_linefeeds(neut_str, tempBuf, tempBuflen, &changed);
						ptr = tempBuf;
					}
					else
					{
						if (tempBuf)
						{
							free(tempBuf);
							tempBuf = NULL;
						}
						ptr = neut_str;
					}
				}
				else
					ptr = tempBuf;

				mylog("DEFAULT: len = %d, ptr = '%s'\n", len, ptr);

				if (stmt->current_col >= 0)
				{
					if (stmt->bindings[stmt->current_col].data_left == 0)
					{
						if (tempBuf)
						{
							free(tempBuf);
							tempBuf = NULL;
						}
						/* The following seems to be needed for ADO ? */
						stmt->bindings[stmt->current_col].data_left = -2;
						return COPY_NO_DATA_FOUND;
					}
					else if (stmt->bindings[stmt->current_col].data_left > 0)
					{
						ptr += strlen(ptr) - stmt->bindings[stmt->current_col].data_left;
						len = stmt->bindings[stmt->current_col].data_left;
					}
					else
						stmt->bindings[stmt->current_col].data_left = len;
				}

				if (cbValueMax > 0)
				{
					copy_len = (len >= cbValueMax) ? cbValueMax - 1 : len;

					/* Copy the data */
					memcpy(rgbValueBindRow, ptr, copy_len);
					rgbValueBindRow[copy_len] = '\0';

					/* Adjust data_left for next time */
					if (stmt->current_col >= 0)
						stmt->bindings[stmt->current_col].data_left -= copy_len;
				}

				/*
				 * Finally, check for truncation so that proper status can
				 * be returned
				 */
				if (cbValueMax > 0 && len >= cbValueMax)
					result = COPY_RESULT_TRUNCATED;
				else
				{
					if (tempBuf)
					{
						free(tempBuf);
						tempBuf = NULL;
					}
				}


				mylog("    SQL_C_CHAR, default: len = %d, cbValueMax = %d, rgbValueBindRow = '%s'\n", len, cbValueMax, rgbValueBindRow);
				break;
		}


	}
	else
	{

		/*
		 * for SQL_C_CHAR, it's probably ok to leave currency symbols in.
		 * But to convert to numeric types, it is necessary to get rid of
		 * those.
		 */
		if (field_type == PG_TYPE_MONEY)
		{
			if (convert_money(neut_str, midtemp[mtemp_cnt], sizeof(midtemp[0])))
			{
				neut_str = midtemp[mtemp_cnt];
				mtemp_cnt++;
			}
			else
				return COPY_UNSUPPORTED_TYPE;
		}

		switch (fCType)
		{
			case SQL_C_DATE:
				len = 6;
				{
					DATE_STRUCT *ds;

					if (bind_size > 0)
						ds = (DATE_STRUCT *) ((char *) rgbValue + (bind_row * bind_size));
					else
						ds = (DATE_STRUCT *) rgbValue + bind_row;
					ds->year = st.y;
					ds->month = st.m;
					ds->day = st.d;
				}
				break;

			case SQL_C_TIME:
				len = 6;
				{
					TIME_STRUCT *ts;

					if (bind_size > 0)
						ts = (TIME_STRUCT *) ((char *) rgbValue + (bind_row * bind_size));
					else
						ts = (TIME_STRUCT *) rgbValue + bind_row;
					ts->hour = st.hh;
					ts->minute = st.mm;
					ts->second = st.ss;
				}
				break;

			case SQL_C_TIMESTAMP:
				len = 16;
				{
					TIMESTAMP_STRUCT *ts;

					if (bind_size > 0)
						ts = (TIMESTAMP_STRUCT *) ((char *) rgbValue + (bind_row * bind_size));
					else
						ts = (TIMESTAMP_STRUCT *) rgbValue + bind_row;
					ts->year = st.y;
					ts->month = st.m;
					ts->day = st.d;
					ts->hour = st.hh;
					ts->minute = st.mm;
					ts->second = st.ss;
					ts->fraction = 0;
				}
				break;

			case SQL_C_BIT:
				len = 1;
				if (bind_size > 0)
					*(UCHAR *) ((char *) rgbValue + (bind_row * bind_size)) = atoi(neut_str);
				else
					*((UCHAR *) rgbValue + bind_row) = atoi(neut_str);

				/*
				 * mylog("SQL_C_BIT: bind_row = %d val = %d, cb = %d, rgb=%d\n",
				 * bind_row, atoi(neut_str), cbValueMax, *((UCHAR *)rgbValue));
				 */
				break;

			case SQL_C_STINYINT:
			case SQL_C_TINYINT:
				len = 1;
				if (bind_size > 0)
					*(SCHAR *) ((char *) rgbValue + (bind_row * bind_size)) = atoi(neut_str);
				else
					*((SCHAR *) rgbValue + bind_row) = atoi(neut_str);
				break;

			case SQL_C_UTINYINT:
				len = 1;
				if (bind_size > 0)
					*(UCHAR *) ((char *) rgbValue + (bind_row * bind_size)) = atoi(neut_str);
				else
					*((UCHAR *) rgbValue + bind_row) = atoi(neut_str);
				break;

			case SQL_C_FLOAT:
				len = 4;
				if (bind_size > 0)
					*(SFLOAT *) ((char *) rgbValue + (bind_row * bind_size)) = (float) atof(neut_str);
				else
					*((SFLOAT *) rgbValue + bind_row) = (float) atof(neut_str);
				break;

			case SQL_C_DOUBLE:
				len = 8;
				if (bind_size > 0)
					*(SDOUBLE *) ((char *) rgbValue + (bind_row * bind_size)) = atof(neut_str);
				else
					*((SDOUBLE *) rgbValue + bind_row) = atof(neut_str);
				break;

			case SQL_C_SSHORT:
			case SQL_C_SHORT:
				len = 2;
				if (bind_size > 0)
					*(SWORD *) ((char *) rgbValue + (bind_row * bind_size)) = atoi(neut_str);
				else
					*((SWORD *) rgbValue + bind_row) = atoi(neut_str);
				break;

			case SQL_C_USHORT:
				len = 2;
				if (bind_size > 0)
					*(UWORD *) ((char *) rgbValue + (bind_row * bind_size)) = atoi(neut_str);
				else
					*((UWORD *) rgbValue + bind_row) = atoi(neut_str);
				break;

			case SQL_C_SLONG:
			case SQL_C_LONG:
				len = 4;
				if (bind_size > 0)
					*(SDWORD *) ((char *) rgbValue + (bind_row * bind_size)) = atol(neut_str);
				else
					*((SDWORD *) rgbValue + bind_row) = atol(neut_str);
				break;

			case SQL_C_ULONG:
				len = 4;
				if (bind_size > 0)
					*(UDWORD *) ((char *) rgbValue + (bind_row * bind_size)) = atol(neut_str);
				else
					*((UDWORD *) rgbValue + bind_row) = atol(neut_str);
				break;

			case SQL_C_BINARY:

				/* truncate if necessary */
				/* convert octal escapes to bytes */

				if (len = strlen(neut_str), len >= (int) tempBuflen)
				{
					tempBuf = realloc(tempBuf, len + 1);
					tempBuflen = len + 1;
				}
				len = convert_from_pgbinary(neut_str, tempBuf, tempBuflen);
				ptr = tempBuf;

				if (stmt->current_col >= 0)
				{
					/* No more data left for this column */
					if (stmt->bindings[stmt->current_col].data_left == 0)
					{
						free(tempBuf);
						tempBuf = NULL;
						return COPY_NO_DATA_FOUND;
					}

					/*
					 * Second (or more) call to SQLGetData so move the
					 * pointer
					 */
					else if (stmt->bindings[stmt->current_col].data_left > 0)
					{
						ptr += len - stmt->bindings[stmt->current_col].data_left;
						len = stmt->bindings[stmt->current_col].data_left;
					}

					/* First call to SQLGetData so initialize data_left */
					else
						stmt->bindings[stmt->current_col].data_left = len;

				}

				if (cbValueMax > 0)
				{
					copy_len = (len > cbValueMax) ? cbValueMax : len;

					/* Copy the data */
					memcpy(rgbValueBindRow, ptr, copy_len);

					/* Adjust data_left for next time */
					if (stmt->current_col >= 0)
						stmt->bindings[stmt->current_col].data_left -= copy_len;
				}

				/*
				 * Finally, check for truncation so that proper status can
				 * be returned
				 */
				if (len > cbValueMax)
					result = COPY_RESULT_TRUNCATED;

				if (tempBuf)
				{
					free(tempBuf);
					tempBuf = NULL;
				}
				mylog("SQL_C_BINARY: len = %d, copy_len = %d\n", len, copy_len);
				break;

			default:
				return COPY_UNSUPPORTED_TYPE;
		}
	}

	/* store the length of what was copied, if there's a place for it */
	if (pcbValue)
		*(SDWORD *) ((char *) pcbValue + pcbValueOffset) = len;

	return result;

}


/*--------------------------------------------------------------------
 *	Functions/Macros to get rid of query size limit.
 *
 *	I always used the follwoing macros to convert from
 *	old_statement to new_statement.	 Please improve it
 *	if you have a better way.	Hiroshi 2001/05/22
 *--------------------------------------------------------------------
 */
#define	INIT_MIN_ALLOC	4096
static int enlarge_statement(StatementClass *stmt, unsigned int newsize)
{
	unsigned int	newalsize = INIT_MIN_ALLOC;
	static char *func = "enlarge_statement";

	if (stmt->stmt_size_limit > 0 && stmt->stmt_size_limit < (int) newsize)
	{
		stmt->errormsg = "Query buffer overflow in copy_statement_with_parameters";
		stmt->errornumber = STMT_EXEC_ERROR;
		SC_log_error(func, "", stmt);
		return -1;
	}
	while (newalsize <= newsize)
		newalsize *= 2;
	if (!(stmt->stmt_with_params = realloc(stmt->stmt_with_params, newalsize)))
	{
		stmt->errormsg = "Query buffer allocate error in copy_statement_with_parameters";
		stmt->errornumber = STMT_EXEC_ERROR;
		SC_log_error(func, "", stmt);
		return 0;
	}
	return newalsize;
}

/*----------
 *	Enlarge stmt_with_params if necessary.
 *----------
 */
#define	ENLARGE_NEWSTATEMENT(newpos) \
	if (newpos >= new_stsize) \
	{ \
		if ((new_stsize = enlarge_statement(stmt, newpos)) <= 0) \
			return SQL_ERROR; \
		new_statement = stmt->stmt_with_params; \
	}
/*----------
 *	Initialize stmt_with_params, new_statement etc.
 *----------
 */
#define	CVT_INIT(size) \
{ \
	if (stmt->stmt_with_params) \
		free(stmt->stmt_with_params); \
	if (stmt->stmt_size_limit > 0) \
		new_stsize = stmt->stmt_size_limit; \
	else \
	{ \
		new_stsize = INIT_MIN_ALLOC; \
		while (new_stsize <= size) \
			new_stsize *= 2; \
	} \
	new_statement = malloc(new_stsize); \
	stmt->stmt_with_params = new_statement; \
	npos = 0; \
	new_statement[0] = '\0'; \
}
/*----------
 *	Terminate the stmt_with_params string with NULL.
 *----------
 */
#define	CVT_TERMINATE { new_statement[npos] = '\0'; }

/*----------
 *	Append a data.
 *----------
 */
#define	CVT_APPEND_DATA(s, len) \
{ \
	unsigned int	newpos = npos + len; \
	ENLARGE_NEWSTATEMENT(newpos) \
	memcpy(&new_statement[npos], s, len); \
	npos = newpos; \
	new_statement[npos] = '\0'; \
}
/*----------
 *	Append a string.
 *----------
 */
#define	CVT_APPEND_STR(s) \
{ \
	unsigned int len = strlen(s); \
	CVT_APPEND_DATA(s, len); \
}
/*----------
 *	Append a char.	
 *----------
 */
#define	CVT_APPEND_CHAR(c) \
{ \
	ENLARGE_NEWSTATEMENT(npos + 1); \
	new_statement[npos++] = c; \
}
/*----------
 *	Append a binary data.
 *	Newly reqeuired size may be overestimated currently. 
 *----------
 */
#define	CVT_APPEND_BINARY(buf, used) \
{ \
	unsigned int	newlimit = npos + 5 * used; \
	ENLARGE_NEWSTATEMENT(newlimit); \
	npos += convert_to_pgbinary(buf, &new_statement[npos], used); \
}
/*----------
 *
 *----------
 */
#define	CVT_SPECIAL_CHARS(buf, used) \
{ \
	int	cnvlen = convert_special_chars(buf, NULL, used); \
	unsigned int	newlimit = npos + cnvlen; \
\
	ENLARGE_NEWSTATEMENT(newlimit); \
	convert_special_chars(buf, &new_statement[npos], used); \
	npos += cnvlen; \
}

/*----------
 *	Check if the statement is	
 *	SELECT ... INTO table FROM .....
 *	This isn't really a strict check but ...
 *---------- 
 */
static BOOL
into_table_from(const char *stmt)
{
	if (strnicmp(stmt, "into", 4))
		return FALSE;
	stmt += 4;
	if (!isspace((unsigned char) *stmt))
		return FALSE;
	while (isspace((unsigned char) *(++stmt)));
	switch (*stmt)
	{
		case '\0':
		case ',':
		case '\'':
			return FALSE;
		case '\"': /* double quoted table name ? */
			do
			{
				do
				{
					 while (*(++stmt) != '\"' && *stmt);
				}
				while (*stmt && *(++stmt) == '\"');
				while (*stmt && !isspace((unsigned char) *stmt) && *stmt != '\"') stmt++;
			}
			while (*stmt == '\"');
			break;
		default:
			while (!isspace((unsigned char) *(++stmt)));
			break;
	}
	if (! *stmt)
		return FALSE;
	while (isspace((unsigned char) *(++stmt)));
	if (strnicmp(stmt, "from", 4))
		return FALSE;
	return isspace((unsigned char) stmt[4]);
}

/*----------
 *	Check if the statement is	
 *	SELECT ... FOR UPDATE .....
 *	This isn't really a strict check but ...
 *---------- 
 */
static BOOL
table_for_update(const char *stmt, int *endpos)
{
	const char *wstmt = stmt;
	while (isspace((unsigned char) *(++wstmt)));
	if (! *wstmt)
		return FALSE;
	if (strnicmp(wstmt, "update", 6))
		return FALSE;
	wstmt += 6;
	*endpos = wstmt - stmt;
	return !wstmt[0] || isspace((unsigned char) wstmt[0]);
}

/*
 *	This function inserts parameters into an SQL statements.
 *	It will also modify a SELECT statement for use with declare/fetch cursors.
 *	This function does a dynamic memory allocation to get rid of query size limit!
 */
int
copy_statement_with_parameters(StatementClass *stmt)
{
	static char *func = "copy_statement_with_parameters";
	unsigned int opos,
				npos,
				oldstmtlen;
	char		param_string[128],
				tmp[256],
				cbuf[PG_NUMERIC_MAX_PRECISION * 2]; /* seems big enough to handle the data in this function */
	int			param_number;
	Int2		param_ctype,
				param_sqltype;
	char	   *old_statement = stmt->statement, oldchar;
	char	   *new_statement = stmt->stmt_with_params;
	unsigned int	new_stsize = 0;
	SIMPLE_TIME st;
	time_t		t = time(NULL);
	struct tm  *tim;
	SDWORD		used;
	char	   	*buffer, *buf;
	BOOL		in_quote = FALSE, in_dquote = FALSE, in_escape = FALSE;
	Oid			lobj_oid;
	int			lobj_fd,
				retval;
	BOOL	check_cursor_ok = FALSE; /* check cursor restriction */
	BOOL	proc_no_param = TRUE;
	unsigned int	declare_pos = 0;
	ConnectionClass	*conn = SC_get_conn(stmt);
	ConnInfo	*ci = &(conn->connInfo);
	BOOL		prepare_dummy_cursor = FALSE, begin_first = FALSE;
	char	token_save[32];
	int	token_len;
	BOOL	prev_token_end;
#ifdef	DRIVER_CURSOR_IMPLEMENT
	BOOL search_from_pos = FALSE;
#endif /* DRIVER_CURSOR_IMPLEMENT */
	if (ci->disallow_premature)
		prepare_dummy_cursor = stmt->pre_executing;

	if (!old_statement)
	{
		SC_log_error(func, "No statement string", stmt);
		return SQL_ERROR;
	}

	memset(&st, 0, sizeof(SIMPLE_TIME));

	/* Initialize current date */
	tim = localtime(&t);
	st.m = tim->tm_mon + 1;
	st.d = tim->tm_mday;
	st.y = tim->tm_year + 1900;

#ifdef	DRIVER_CURSOR_IMPLEMENT
	if (stmt->statement_type != STMT_TYPE_SELECT)
	{
		stmt->options.cursor_type = SQL_CURSOR_FORWARD_ONLY;
		stmt->options.scroll_concurrency = SQL_CONCUR_READ_ONLY;
	}
	else if (stmt->options.cursor_type == SQL_CURSOR_FORWARD_ONLY)
    		stmt->options.scroll_concurrency = SQL_CONCUR_READ_ONLY;
	else if (stmt->options.scroll_concurrency != SQL_CONCUR_READ_ONLY)
	{
		if (stmt->parse_status == STMT_PARSE_NONE)
			parse_statement(stmt);
		if (stmt->parse_status != STMT_PARSE_COMPLETE)
			stmt->options.scroll_concurrency = SQL_CONCUR_READ_ONLY;
		else if (!stmt->ti || stmt->ntab != 1)
    			stmt->options.scroll_concurrency = SQL_CONCUR_READ_ONLY;
		else
			search_from_pos = TRUE;
	}
#endif /* DRIVER_CURSOR_IMPLEMENT */

	/* If the application hasn't set a cursor name, then generate one */
	if (stmt->cursor_name[0] == '\0')
		sprintf(stmt->cursor_name, "SQL_CUR%p", stmt);
	oldstmtlen = strlen(old_statement);
	CVT_INIT(oldstmtlen);

	stmt->miscinfo = 0;
	token_len = 0;
	prev_token_end = TRUE;
	/* For selects, prepend a declare cursor to the statement */
	if (stmt->statement_type == STMT_TYPE_SELECT)
	{
		SC_set_pre_executable(stmt);
		if (prepare_dummy_cursor || ci->drivers.use_declarefetch)
		{
			if (prepare_dummy_cursor)
			{
				if (!CC_is_in_trans(conn) && PG_VERSION_GE(conn, 7.1))
				{
					strcpy(new_statement, "BEGIN;");
					begin_first = TRUE;
				}
			}
			else if (ci->drivers.use_declarefetch)
				SC_set_fetchcursor(stmt);
			sprintf(new_statement, "%sdeclare %s cursor for ",
				 new_statement, stmt->cursor_name);
			npos = strlen(new_statement);
			check_cursor_ok = TRUE;
			declare_pos = npos;
		}
	}
	param_number = -1;
#ifdef MULTIBYTE
	multibyte_init();
#endif

	for (opos = 0; opos < oldstmtlen; opos++)
	{
		oldchar = old_statement[opos];
#ifdef MULTIBYTE
		if (multibyte_char_check(oldchar) != 0)
		{
			CVT_APPEND_CHAR(oldchar);
			continue;
		}
		/*
		 *	From here we are guaranteed to handle a
		 *	1-byte character.
		 */
#endif

		if (in_escape) /* escape check */
		{
			in_escape = FALSE;
			CVT_APPEND_CHAR(oldchar);
			continue;
		}	
		else if (in_quote || in_dquote) /* quote/double quote check */
		{
			if (oldchar == '\\')
				in_escape = TRUE;
			else if (oldchar == '\'' && in_quote)
				in_quote = FALSE;
			else if (oldchar == '\"' && in_dquote)
				in_dquote = FALSE;
			CVT_APPEND_CHAR(oldchar);
			continue;	
		}
		/*
		 *	From here we are guranteed to be in neither
		 *	an escape, a quote nor a double quote.
		 */
		/* Squeeze carriage-return/linefeed pairs to linefeed only */
		else if (oldchar == '\r' && opos + 1 < oldstmtlen &&
			old_statement[opos + 1] == '\n')
			continue;
		/*
		 * Handle literals (date, time, timestamp) and ODBC scalar
		 * functions
		 */
		else if (oldchar == '{')
		{
			char	   *esc;
			char	   *begin = &old_statement[opos + 1];

#ifdef MULTIBYTE
			char	   *end = multibyte_strchr(begin, '}');

#else
			char	   *end = strchr(begin, '}');

#endif

			if (!end)
				continue;
			/* procedure calls */
			if (stmt->statement_type == STMT_TYPE_PROCCALL)
			{
				int	lit_call_len = 4;
				while (isspace((unsigned char) old_statement[++opos]));
				/* '=?' to accept return values exists ? */
				if (old_statement[opos] == '?')
				{
					param_number++;
					while (isspace((unsigned char) old_statement[++opos]));
					if (old_statement[opos] != '=')
					{
						opos--;
						continue;
					}
					while (isspace((unsigned char) old_statement[++opos]));
				}
				if (strnicmp(&old_statement[opos], "call", lit_call_len) ||
					!isspace(old_statement[opos + lit_call_len]))
				{
					opos--;
					continue;
				}
				opos += lit_call_len; 
				CVT_APPEND_STR("SELECT ");
#ifdef MULTIBYTE
				if (multibyte_strchr(&old_statement[opos], '('))
#else
				if (strchr(&old_statement[opos], '('))
#endif /* MULTIBYTE */
					proc_no_param = FALSE;
				continue; 
			}
			*end = '\0';

			esc = convert_escape(begin);
			if (esc)
			{
				CVT_APPEND_STR(esc);
			}
			else
			{					/* it's not a valid literal so just copy */
				*end = '}';
				CVT_APPEND_CHAR(oldchar);
				continue;
			}

			opos += end - begin + 1;
			*end = '}';
			continue;
		}
		/* End of a procedure call */
		else if (oldchar == '}' && stmt->statement_type == STMT_TYPE_PROCCALL)
		{
			if (proc_no_param)
				CVT_APPEND_STR("()");
			continue;
		}

		/*
		 * Can you have parameter markers inside of quotes?  I dont think
		 * so. All the queries I've seen expect the driver to put quotes
		 * if needed.
		 */
		else if (oldchar == '?')
			;					/* ok */
		else
		{
			if (oldchar == '\'')
				in_quote = TRUE;
			else if (oldchar == '\\')
				in_escape = TRUE;
			else if (oldchar == '\"')
				in_dquote = TRUE;
			else 
			{
				if (isspace(oldchar))
				{
					if (!prev_token_end)
					{
						prev_token_end = TRUE;
						token_save[token_len] = '\0';
						if (token_len == 4)
						{
							if (check_cursor_ok &&
    				 			   into_table_from(&old_statement[opos - token_len]))
							{
								stmt->statement_type = STMT_TYPE_CREATE;
								SC_no_pre_executable(stmt);
								SC_no_fetchcursor(stmt);
								stmt->options.scroll_concurrency = SQL_CONCUR_READ_ONLY;
								memmove(new_statement, new_statement + declare_pos, npos - declare_pos);
								npos -= declare_pos;
							}
#ifdef	DRIVER_CURSOR_IMPLEMENT
							else if (search_from_pos && /* where's from clause */
    				 				 strnicmp(token_save, "from", 4) == 0)
							{
								search_from_pos = FALSE;
								npos -= 5;
								CVT_APPEND_STR(", CTID, OID from");
							}
#endif /* DRIVER_CURSOR_IMPLEMENT */
						}
						if (token_len == 3)
						{
							int	endpos;
							if (check_cursor_ok &&
    				 			    strnicmp(token_save, "for", 3) == 0 &&
    				 			    table_for_update(&old_statement[opos], &endpos))
							{
								SC_no_fetchcursor(stmt);
								stmt->options.scroll_concurrency = SQL_CONCUR_READ_ONLY;
								if (prepare_dummy_cursor)
								{
									npos -= 4;
									opos += endpos;
								}
								else
								{
									memmove(new_statement, new_statement + declare_pos, npos - declare_pos);
									npos -= declare_pos;
								}
							}
						}
					}	
				}
				else if (prev_token_end)
				{
					prev_token_end = FALSE;
					token_save[0] = oldchar;
					token_len = 1;
				}
				else
					token_save[token_len++] = oldchar;
			} 
			CVT_APPEND_CHAR(oldchar);
			continue;
		}

		/*
		 * Its a '?' parameter alright
		 */
		param_number++;

		if (param_number >= stmt->parameters_allocated)
		{
			if (stmt->pre_executing)
			{
				CVT_APPEND_STR("NULL");
				stmt->inaccurate_result = TRUE;
				continue;
			}
			else
			{
				CVT_APPEND_CHAR('?');
				continue;
			}
		}

		/* Assign correct buffers based on data at exec param or not */
		if (stmt->parameters[param_number].data_at_exec)
		{
			used = stmt->parameters[param_number].EXEC_used ? *stmt->parameters[param_number].EXEC_used : SQL_NTS;
			buffer = stmt->parameters[param_number].EXEC_buffer;
		}
		else
		{
			
			
			used = stmt->parameters[param_number].used ? *stmt->parameters[param_number].used : SQL_NTS;
			
			buffer = stmt->parameters[param_number].buffer;
		}

		/* Handle NULL parameter data */
		if (used == SQL_NULL_DATA)
		{
			CVT_APPEND_STR("NULL");
			continue;
		}

		/*
		 * If no buffer, and it's not null, then what the hell is it? Just
		 * leave it alone then.
		 */
		if (!buffer)
		{
			if (stmt->pre_executing)
			{
				CVT_APPEND_STR("NULL");
				stmt->inaccurate_result = TRUE;
				continue;
			}
			else
			{
				CVT_APPEND_CHAR('?');
				continue;
			}
		}

		param_ctype = stmt->parameters[param_number].CType;
		param_sqltype = stmt->parameters[param_number].SQLType;

		mylog("copy_statement_with_params: from(fcType)=%d, to(fSqlType)=%d\n", param_ctype, param_sqltype);

		/* replace DEFAULT with something we can use */
		if (param_ctype == SQL_C_DEFAULT)
			param_ctype = sqltype_to_default_ctype(param_sqltype);

		buf = NULL;
		param_string[0] = '\0';
		cbuf[0] = '\0';

		/* Convert input C type to a neutral format */
		switch (param_ctype)
		{
			case SQL_C_BINARY:
			case SQL_C_CHAR:
				buf = buffer;
				break;

			case SQL_C_DOUBLE:
				sprintf(param_string, "%.15g",
						*((SDOUBLE *) buffer));
				break;

			case SQL_C_FLOAT:
				sprintf(param_string, "%.6g",
						*((SFLOAT *) buffer));
				break;

			case SQL_C_SLONG:
			case SQL_C_LONG:
				sprintf(param_string, "%ld",
						*((SDWORD *) buffer));
				break;

			case SQL_C_SSHORT:
			case SQL_C_SHORT:
				sprintf(param_string, "%d",
						*((SWORD *) buffer));
				break;

			case SQL_C_STINYINT:
			case SQL_C_TINYINT:
				sprintf(param_string, "%d",
						*((SCHAR *) buffer));
				break;

			case SQL_C_ULONG:
				sprintf(param_string, "%lu",
						*((UDWORD *) buffer));
				break;

			case SQL_C_USHORT:
				sprintf(param_string, "%u",
						*((UWORD *) buffer));
				break;

			case SQL_C_UTINYINT:
				sprintf(param_string, "%u",
						*((UCHAR *) buffer));
				break;

			case SQL_C_BIT:
				{
					int			i = *((UCHAR *) buffer);

					sprintf(param_string, "%d", i ? 1 : 0);
					break;
				}

			case SQL_C_DATE:
				{
					DATE_STRUCT *ds = (DATE_STRUCT *) buffer;

					st.m = ds->month;
					st.d = ds->day;
					st.y = ds->year;

					break;
				}

			case SQL_C_TIME:
				{
					TIME_STRUCT *ts = (TIME_STRUCT *) buffer;

					st.hh = ts->hour;
					st.mm = ts->minute;
					st.ss = ts->second;

					break;
				}

			case SQL_C_TIMESTAMP:
				{
					TIMESTAMP_STRUCT *tss = (TIMESTAMP_STRUCT *) buffer;

					st.m = tss->month;
					st.d = tss->day;
					st.y = tss->year;
					st.hh = tss->hour;
					st.mm = tss->minute;
					st.ss = tss->second;

					mylog("m=%d,d=%d,y=%d,hh=%d,mm=%d,ss=%d\n", st.m, st.d, st.y, st.hh, st.mm, st.ss);

					break;

				}
			default:
				/* error */
				stmt->errormsg = "Unrecognized C_parameter type in copy_statement_with_parameters";
				stmt->errornumber = STMT_NOT_IMPLEMENTED_ERROR;
				CVT_TERMINATE	/* just in case */
				SC_log_error(func, "", stmt);
				return SQL_ERROR;
		}

		/*
		 * Now that the input data is in a neutral format, convert it to
		 * the desired output format (sqltype)
		 */

		switch (param_sqltype)
		{
			case SQL_CHAR:
			case SQL_VARCHAR:
			case SQL_LONGVARCHAR:

				CVT_APPEND_CHAR('\'');	/* Open Quote */

				/* it was a SQL_C_CHAR */
				if (buf)
				{
					CVT_SPECIAL_CHARS(buf, used);
				}

				/* it was a numeric type */
				else if (param_string[0] != '\0')
				{
					CVT_APPEND_STR(param_string);
				}

				/* it was date,time,timestamp -- use m,d,y,hh,mm,ss */
				else
				{
					sprintf(tmp, "%.4d-%.2d-%.2d %.2d:%.2d:%.2d",
							st.y, st.m, st.d, st.hh, st.mm, st.ss);

					CVT_APPEND_STR(tmp);
				}

				CVT_APPEND_CHAR('\'');	/* Close Quote */

				break;

			case SQL_DATE:
				if (buf)
				{				/* copy char data to time */
					my_strcpy(cbuf, sizeof(cbuf), buf, used);
					parse_datetime(cbuf, &st);
				}

				sprintf(tmp, "'%.4d-%.2d-%.2d'", st.y, st.m, st.d);

				CVT_APPEND_STR(tmp);
				break;

			case SQL_TIME:
				if (buf)
				{				/* copy char data to time */
					my_strcpy(cbuf, sizeof(cbuf), buf, used);
					parse_datetime(cbuf, &st);
				}

				sprintf(tmp, "'%.2d:%.2d:%.2d'", st.hh, st.mm, st.ss);

				CVT_APPEND_STR(tmp);
				break;

			case SQL_TIMESTAMP:

				if (buf)
				{
					my_strcpy(cbuf, sizeof(cbuf), buf, used);
					parse_datetime(cbuf, &st);
				}

				sprintf(tmp, "'%.4d-%.2d-%.2d %.2d:%.2d:%.2d'",
						st.y, st.m, st.d, st.hh, st.mm, st.ss);

				CVT_APPEND_STR(tmp);

				break;

			case SQL_BINARY:
			case SQL_VARBINARY:/* non-ascii characters should be
								 * converted to octal */
				CVT_APPEND_CHAR('\'');	/* Open Quote */

				mylog("SQL_VARBINARY: about to call convert_to_pgbinary, used = %d\n", used);

				CVT_APPEND_BINARY(buf, used);

				CVT_APPEND_CHAR('\'');	/* Close Quote */

				break;

			case SQL_LONGVARBINARY:

				if (stmt->parameters[param_number].data_at_exec)
					lobj_oid = stmt->parameters[param_number].lobj_oid;
				else
				{
					/* begin transaction if needed */
					if (!CC_is_in_trans(conn))
					{
						QResultClass *res;
						char		ok;

						res = CC_send_query(conn, "BEGIN", NULL);
						if (!res)
						{
							stmt->errormsg = "Could not begin (in-line) a transaction";
							stmt->errornumber = STMT_EXEC_ERROR;
							SC_log_error(func, "", stmt);
							return SQL_ERROR;
						}
						ok = QR_command_successful(res);
						QR_Destructor(res);
						if (!ok)
						{
							stmt->errormsg = "Could not begin (in-line) a transaction";
							stmt->errornumber = STMT_EXEC_ERROR;
							SC_log_error(func, "", stmt);
							return SQL_ERROR;
						}

						CC_set_in_trans(conn);
					}

					/* store the oid */
					lobj_oid = lo_creat(conn, INV_READ | INV_WRITE);
					if (lobj_oid == 0)
					{
						stmt->errornumber = STMT_EXEC_ERROR;
						stmt->errormsg = "Couldnt create (in-line) large object.";
						SC_log_error(func, "", stmt);
						return SQL_ERROR;
					}

					/* store the fd */
					lobj_fd = lo_open(conn, lobj_oid, INV_WRITE);
					if (lobj_fd < 0)
					{
						stmt->errornumber = STMT_EXEC_ERROR;
						stmt->errormsg = "Couldnt open (in-line) large object for writing.";
						SC_log_error(func, "", stmt);
						return SQL_ERROR;
					}

					retval = lo_write(conn, lobj_fd, buffer, used);

					lo_close(conn, lobj_fd);

					/* commit transaction if needed */
					if (!ci->drivers.use_declarefetch && CC_is_in_autocommit(conn))
					{
						QResultClass *res;
						char		ok;

						res = CC_send_query(conn, "COMMIT", NULL);
						if (!res)
						{
							stmt->errormsg = "Could not commit (in-line) a transaction";
							stmt->errornumber = STMT_EXEC_ERROR;
							SC_log_error(func, "", stmt);
							return SQL_ERROR;
						}
						ok = QR_command_successful(res);
						QR_Destructor(res);
						if (!ok)
						{
							stmt->errormsg = "Could not commit (in-line) a transaction";
							stmt->errornumber = STMT_EXEC_ERROR;
							SC_log_error(func, "", stmt);
							return SQL_ERROR;
						}

						CC_set_no_trans(conn);
					}
				}

				/*
				 * the oid of the large object -- just put that in for the
				 * parameter marker -- the data has already been sent to
				 * the large object
				 */
				sprintf(param_string, "'%d'", lobj_oid);
				CVT_APPEND_STR(param_string);

				break;

				/*
				 * because of no conversion operator for bool and int4,
				 * SQL_BIT
				 */
				/* must be quoted (0 or 1 is ok to use inside the quotes) */

			case SQL_REAL:
				if (buf)
					my_strcpy(param_string, sizeof(param_string), buf, used);
				sprintf(tmp, "'%s'::float4", param_string);
				CVT_APPEND_STR(tmp);
				break;
			case SQL_FLOAT:
			case SQL_DOUBLE:
				if (buf)
					my_strcpy(param_string, sizeof(param_string), buf, used);
				sprintf(tmp, "'%s'::float8", param_string);
				CVT_APPEND_STR(tmp);
				break;
			case SQL_NUMERIC:
				if (buf)
				{
					cbuf[0] = '\'';
					my_strcpy(cbuf + 1, sizeof(cbuf) - 12, buf, used);	/* 12 = 1('\'') +
																		 * strlen("'::numeric")
																		 * + 1('\0') */
					strcat(cbuf, "'::numeric");
				}
				else
					sprintf(cbuf, "'%s'::numeric", param_string);
				CVT_APPEND_STR(cbuf);
				break;
			default:			/* a numeric type or SQL_BIT */
				if (param_sqltype == SQL_BIT)
					CVT_APPEND_CHAR('\'');		/* Open Quote */

				if (buf)
				{
					switch (used)
					{
						case SQL_NULL_DATA:
							break;
						case SQL_NTS:
							CVT_APPEND_STR(buf);
							break;
						default:
							CVT_APPEND_DATA(buf, used);
					}
				}
				else
					CVT_APPEND_STR(param_string);

				if (param_sqltype == SQL_BIT)
					CVT_APPEND_CHAR('\'');		/* Close Quote */

				break;
		}
	}							/* end, for */

	/* make sure new_statement is always null-terminated */
	CVT_TERMINATE

	if (conn->DriverToDataSource != NULL)
	{
		int			length = strlen(new_statement);

		conn->DriverToDataSource(conn->translation_option,
									   SQL_CHAR,
									   new_statement, length,
									   new_statement, length, NULL,
									   NULL, 0, NULL);
	}

#ifdef	DRIVER_CURSOR_IMPLEMENT
	if (search_from_pos)
		stmt->options.scroll_concurrency = SQL_CONCUR_READ_ONLY;
#endif /* DRIVER_CURSOR_IMPLEMENT */
	if (prepare_dummy_cursor && SC_is_pre_executable(stmt))
	{
		char fetchstr[128];	
		sprintf(fetchstr, ";fetch backward in %s;close %s;",
				stmt->cursor_name, stmt->cursor_name);
		if (begin_first && CC_is_in_autocommit(conn))
			strcat(fetchstr, "COMMIT;");
		CVT_APPEND_STR(fetchstr);
		stmt->inaccurate_result = TRUE;
	}

	return SQL_SUCCESS;
}


char *
mapFunction(const char *func)
{
	int			i;

	for (i = 0; mapFuncs[i][0]; i++)
		if (!stricmp(mapFuncs[i][0], func))
			return mapFuncs[i][1];

	return NULL;
}


/*
 * convert_escape()
 *
 * This function returns a pointer to static memory!
 */
char *
convert_escape(char *value)
{
	static char escape[1024];
	char		key[33];

	/* Separate off the key, skipping leading and trailing whitespace */
	while ((*value != '\0') && isspace((unsigned char) *value))
		value++;
	sscanf(value, "%32s", key);
	while ((*value != '\0') && (!isspace((unsigned char) *value)))
		value++;
	while ((*value != '\0') && isspace((unsigned char) *value))
		value++;

	mylog("convert_escape: key='%s', val='%s'\n", key, value);

	if ((strcmp(key, "d") == 0) ||
		(strcmp(key, "t") == 0) ||
		(strcmp(key, "oj") == 0) ||		/* {oj syntax support for 7.1
										 * servers */
		(strcmp(key, "ts") == 0))
	{
		/* Literal; return the escape part as-is */
		strncpy(escape, value, sizeof(escape) - 1);
	}
	else if (strcmp(key, "fn") == 0)
	{

		/*
		 * Function invocation Separate off the func name, skipping
		 * trailing whitespace.
		 */
		char	   *funcEnd = value;
		char		svchar;
		char	   *mapFunc;

		while ((*funcEnd != '\0') && (*funcEnd != '(') &&
			   (!isspace((unsigned char) *funcEnd)))
			funcEnd++;
		svchar = *funcEnd;
		*funcEnd = '\0';
		sscanf(value, "%32s", key);
		*funcEnd = svchar;
		while ((*funcEnd != '\0') && isspace((unsigned char) *funcEnd))
			funcEnd++;

		/*
		 * We expect left parenthesis here, else return fn body as-is
		 * since it is one of those "function constants".
		 */
		if (*funcEnd != '(')
		{
			strncpy(escape, value, sizeof(escape) - 1);
			return escape;
		}
		mapFunc = mapFunction(key);

		/*
		 * We could have mapFunction() return key if not in table... -
		 * thomas 2000-04-03
		 */
		if (mapFunc == NULL)
		{
			/* If unrecognized function name, return fn body as-is */
			strncpy(escape, value, sizeof(escape) - 1);
			return escape;
		}
		/* copy mapped name and remaining input string */
		strcpy(escape, mapFunc);
		strncat(escape, funcEnd, sizeof(escape) - 1 - strlen(mapFunc));
	}
	else
	{
		/* Bogus key, leave untranslated */
		return NULL;
	}

	return escape;
}


BOOL
convert_money(const char *s, char *sout, size_t soutmax)
{
	size_t		i = 0, out = 0;

	for (i = 0; s[i]; i++)
	{
		if (s[i] == '$' || s[i] == ',' || s[i] == ')')
			;					/* skip these characters */
		else
		{
			if (out + 1 >= soutmax)
				return FALSE; /* sout is too short */
			if (s[i] == '(')
				sout[out++] = '-';
			else
				sout[out++] = s[i];
		}
	}
	sout[out] = '\0';
	return TRUE;
}


/*
 *	This function parses a character string for date/time info and fills in SIMPLE_TIME
 *	It does not zero out SIMPLE_TIME in case it is desired to initialize it with a value
 */
char
parse_datetime(char *buf, SIMPLE_TIME *st)
{
	int			y,
				m,
				d,
				hh,
				mm,
				ss;
	int			nf;

	y = m = d = hh = mm = ss = 0;

	/* escape sequence ? */
	if (buf[0] == '{')
	{
		while (*(++buf) && *buf != '\'');
		if (!(*buf))
			return FALSE;
		buf++;
	}
	if (buf[4] == '-')			/* year first */
		nf = sscanf(buf, "%4d-%2d-%2d %2d:%2d:%2d", &y, &m, &d, &hh, &mm, &ss);
	else
		nf = sscanf(buf, "%2d-%2d-%4d %2d:%2d:%2d", &m, &d, &y, &hh, &mm, &ss);

	if (nf == 5 || nf == 6)
	{
		st->y = y;
		st->m = m;
		st->d = d;
		st->hh = hh;
		st->mm = mm;
		st->ss = ss;

		return TRUE;
	}

	if (buf[4] == '-')			/* year first */
		nf = sscanf(buf, "%4d-%2d-%2d", &y, &m, &d);
	else
		nf = sscanf(buf, "%2d-%2d-%4d", &m, &d, &y);

	if (nf == 3)
	{
		st->y = y;
		st->m = m;
		st->d = d;

		return TRUE;
	}

	nf = sscanf(buf, "%2d:%2d:%2d", &hh, &mm, &ss);
	if (nf == 2 || nf == 3)
	{
		st->hh = hh;
		st->mm = mm;
		st->ss = ss;

		return TRUE;
	}

	return FALSE;
}


/*	Change linefeed to carriage-return/linefeed */
int
convert_linefeeds(const char *si, char *dst, size_t max, BOOL *changed)
{
	size_t		i = 0,
				out = 0;

	if (max == 0)
		max = 0xffffffff;
	*changed = FALSE;
	for (i = 0; si[i] && out < max - 1; i++)
	{
		if (si[i] == '\n')
		{
			/* Only add the carriage-return if needed */
			if (i > 0 && si[i - 1] == '\r')
			{
				if (dst)
					dst[out++] = si[i];
				else
					out++;
				continue;
			}
			*changed = TRUE;

			if (dst)
			{
				dst[out++] = '\r';
				dst[out++] = '\n';
			}
			else
				out += 2;
		}
		else
		{
			if (dst)
				dst[out++] = si[i];
			else
				out++;
		}
	}
	if (dst)
		dst[out] = '\0';
	return out;
}


/*
 *	Change carriage-return/linefeed to just linefeed
 *	Plus, escape any special characters.
 */
int
convert_special_chars(const char *si, char *dst, int used)
{
	size_t		i = 0,
				out = 0,
				max;
	char	   *p = NULL;


	if (used == SQL_NTS)
		max = strlen(si);
	else
		max = used;
	if (dst)
	{
		p = dst;
		p[0] = '\0';
	}
#ifdef MULTIBYTE
	multibyte_init();
#endif

	for (i = 0; i < max; i++)
	{
#ifdef MULTIBYTE
		if (multibyte_char_check(si[i]) != 0)
		{
			if (p)
				p[out] = si[i];
			out++;
			continue;
		}
#endif
		if (si[i] == '\r' && si[i + 1] == '\n')
			continue;
		else if (si[i] == '\'' || si[i] == '\\')
		{
			if (p)
				p[out++] = '\\';
			else
				out++;
		}
		if (p)
			p[out++] = si[i];
		else
			out++;
	}
	if (p)
		p[out] = '\0';
	return out;
}


/*	!!! Need to implement this function !!!  */
int
convert_pgbinary_to_char(const char *value, char *rgbValue, int cbValueMax)
{
	mylog("convert_pgbinary_to_char: value = '%s'\n", value);

	strncpy_null(rgbValue, value, cbValueMax);
	return 0;
}


unsigned int
conv_from_octal(const unsigned char *s)
{
	int			i,
				y = 0;

	for (i = 1; i <= 3; i++)
		y += (s[i] - 48) * (int) pow(8, 3 - i);

	return y;

}


unsigned int
conv_from_hex(const unsigned char *s)
{
	int			i,
				y = 0,
				val;

	for (i = 1; i <= 2; i++)
	{
		if (s[i] >= 'a' && s[i] <= 'f')
			val = s[i] - 'a' + 10;
		else if (s[i] >= 'A' && s[i] <= 'F')
			val = s[i] - 'A' + 10;
		else
			val = s[i] - '0';

		y += val * (int) pow(16, 2 - i);
	}

	return y;
}


/*	convert octal escapes to bytes */
int
convert_from_pgbinary(const unsigned char *value, unsigned char *rgbValue, int cbValueMax)
{
	size_t		i, ilen = strlen(value);
	int			o = 0;


	for (i = 0; i < ilen;)
	{
		if (value[i] == '\\')
		{
			if (value[i + 1] == '\\')
			{
				rgbValue[o] = value[i];
				i += 2;
			}
			else
			{
				rgbValue[o] = conv_from_octal(&value[i]);
				i += 4;
			}
		}
		else
			rgbValue[o] = value[i++];
		mylog("convert_from_pgbinary: i=%d, rgbValue[%d] = %d, %c\n", i, o, rgbValue[o], rgbValue[o]);
		o++;
	}

	rgbValue[o] = '\0';			/* extra protection */

	return o;
}


char *
conv_to_octal(unsigned char val)
{
	int			i;
	static char x[6];

	x[0] = '\\';
	x[1] = '\\';
	x[5] = '\0';

	for (i = 4; i > 1; i--)
	{
		x[i] = (val & 7) + 48;
		val >>= 3;
	}

	return x;
}


/*	convert non-ascii bytes to octal escape sequences */
int
convert_to_pgbinary(const unsigned char *in, char *out, int len)
{
	int			i,
				o = 0;

	for (i = 0; i < len; i++)
	{
		mylog("convert_to_pgbinary: in[%d] = %d, %c\n", i, in[i], in[i]);
		if (isalnum(in[i]) || in[i] == ' ')
			out[o++] = in[i];
		else
		{
			strcpy(&out[o], conv_to_octal(in[i]));
			o += 5;
		}
	}

	mylog("convert_to_pgbinary: returning %d, out='%.*s'\n", o, o, out);

	return o;
}


void
encode(const char *in, char *out)
{
	unsigned int i, ilen = strlen(in),
				o = 0;

	for (i = 0; i < ilen; i++)
	{
		if (in[i] == '+')
		{
			sprintf(&out[o], "%%2B");
			o += 3;
		}
		else if (isspace((unsigned char) in[i]))
			out[o++] = '+';
		else if (!isalnum((unsigned char) in[i]))
		{
			sprintf(&out[o], "%%%02x", (unsigned char) in[i]);
			o += 3;
		}
		else
			out[o++] = in[i];
	}
	out[o++] = '\0';
}


void
decode(const char *in, char *out)
{
	unsigned int i, ilen = strlen(in),
				o = 0;

	for (i = 0; i < ilen; i++)
	{
		if (in[i] == '+')
			out[o++] = ' ';
		else if (in[i] == '%')
		{
			sprintf(&out[o++], "%c", conv_from_hex(&in[i]));
			i += 2;
		}
		else
			out[o++] = in[i];
	}
	out[o++] = '\0';
}


/*-------
 *	1. get oid (from 'value')
 *	2. open the large object
 *	3. read from the large object (handle multiple GetData)
 *	4. close when read less than requested?  -OR-
 *		lseek/read each time
 *		handle case where application receives truncated and
 *		decides not to continue reading.
 *
 *	CURRENTLY, ONLY LONGVARBINARY is handled, since that is the only
 *	data type currently mapped to a PG_TYPE_LO.  But, if any other types
 *	are desired to map to a large object (PG_TYPE_LO), then that would
 *	need to be handled here.  For example, LONGVARCHAR could possibly be
 *	mapped to PG_TYPE_LO someday, instead of PG_TYPE_TEXT as it is now.
 *-------
 */
int
convert_lo(StatementClass *stmt, const void *value, Int2 fCType, PTR rgbValue,
		   SDWORD cbValueMax, SDWORD *pcbValue)
{
	Oid			oid;
	int			retval,
				result,
				left = -1;
	BindInfoClass *bindInfo = NULL;
	ConnectionClass	*conn = SC_get_conn(stmt);
	ConnInfo	*ci = &(conn->connInfo);

	/* If using SQLGetData, then current_col will be set */
	if (stmt->current_col >= 0)
	{
		bindInfo = &stmt->bindings[stmt->current_col];
		left = bindInfo->data_left;
	}

	/*
	 * if this is the first call for this column, open the large object
	 * for reading
	 */

	if (!bindInfo || bindInfo->data_left == -1)
	{
		/* begin transaction if needed */
		if (!CC_is_in_trans(conn))
		{
			QResultClass *res;
			char		ok;

			res = CC_send_query(conn, "BEGIN", NULL);
			if (!res)
			{
				stmt->errormsg = "Could not begin (in-line) a transaction";
				stmt->errornumber = STMT_EXEC_ERROR;
				return COPY_GENERAL_ERROR;
			}
			ok = QR_command_successful(res);
			QR_Destructor(res);
			if (!ok)
			{
				stmt->errormsg = "Could not begin (in-line) a transaction";
				stmt->errornumber = STMT_EXEC_ERROR;
				return COPY_GENERAL_ERROR;
			}

			CC_set_in_trans(conn);
		}

		oid = atoi(value);
		stmt->lobj_fd = lo_open(conn, oid, INV_READ);
		if (stmt->lobj_fd < 0)
		{
			stmt->errornumber = STMT_EXEC_ERROR;
			stmt->errormsg = "Couldnt open large object for reading.";
			return COPY_GENERAL_ERROR;
		}

		/* Get the size */
		retval = lo_lseek(conn, stmt->lobj_fd, 0L, SEEK_END);
		if (retval >= 0)
		{
			left = lo_tell(conn, stmt->lobj_fd);
			if (bindInfo)
				bindInfo->data_left = left;

			/* return to beginning */
			lo_lseek(conn, stmt->lobj_fd, 0L, SEEK_SET);
		}
	}
	mylog("lo data left = %d\n", left);

	if (left == 0)
		return COPY_NO_DATA_FOUND;

	if (stmt->lobj_fd < 0)
	{
		stmt->errornumber = STMT_EXEC_ERROR;
		stmt->errormsg = "Large object FD undefined for multiple read.";
		return COPY_GENERAL_ERROR;
	}

	retval = lo_read(conn, stmt->lobj_fd, (char *) rgbValue, cbValueMax);
	if (retval < 0)
	{
		lo_close(conn, stmt->lobj_fd);

		/* commit transaction if needed */
		if (!ci->drivers.use_declarefetch && CC_is_in_autocommit(conn))
		{
			QResultClass *res;
			char		ok;

			res = CC_send_query(conn, "COMMIT", NULL);
			if (!res)
			{
				stmt->errormsg = "Could not commit (in-line) a transaction";
				stmt->errornumber = STMT_EXEC_ERROR;
				return COPY_GENERAL_ERROR;
			}
			ok = QR_command_successful(res);
			QR_Destructor(res);
			if (!ok)
			{
				stmt->errormsg = "Could not commit (in-line) a transaction";
				stmt->errornumber = STMT_EXEC_ERROR;
				return COPY_GENERAL_ERROR;
			}

			CC_set_no_trans(conn);
		}

		stmt->lobj_fd = -1;

		stmt->errornumber = STMT_EXEC_ERROR;
		stmt->errormsg = "Error reading from large object.";
		return COPY_GENERAL_ERROR;
	}

	if (retval < left)
		result = COPY_RESULT_TRUNCATED;
	else
		result = COPY_OK;

	if (pcbValue)
		*pcbValue = left < 0 ? SQL_NO_TOTAL : left;

	if (bindInfo && bindInfo->data_left > 0)
		bindInfo->data_left -= retval;

	if (!bindInfo || bindInfo->data_left == 0)
	{
		lo_close(conn, stmt->lobj_fd);

		/* commit transaction if needed */
		if (!ci->drivers.use_declarefetch && CC_is_in_autocommit(conn))
		{
			QResultClass *res;
			char		ok;

			res = CC_send_query(conn, "COMMIT", NULL);
			if (!res)
			{
				stmt->errormsg = "Could not commit (in-line) a transaction";
				stmt->errornumber = STMT_EXEC_ERROR;
				return COPY_GENERAL_ERROR;
			}
			ok = QR_command_successful(res);
			QR_Destructor(res);
			if (!ok)
			{
				stmt->errormsg = "Could not commit (in-line) a transaction";
				stmt->errornumber = STMT_EXEC_ERROR;
				return COPY_GENERAL_ERROR;
			}

			CC_set_no_trans(conn);
		}

		stmt->lobj_fd = -1;		/* prevent further reading */
	}

	return result;
}
