/*--------
 * Module:			parse.c
 *
 * Description:		This module contains routines related to parsing SQL
 *					statements.  This can be useful for two reasons:
 *
 *					1. So the query does not actually have to be executed
 *					to return data about it
 *
 *					2. To be able to return information about precision,
 *					nullability, aliases, etc. in the functions
 *					SQLDescribeCol and SQLColAttributes.  Currently,
 *					Postgres doesn't return any information about
 *					these things in a query.
 *
 * Classes:			none
 *
 * API functions:	none
 *
 * Comments:		See "notice.txt" for copyright and license information.
 *--------
 */
/* Multibyte support	Eiji Tokuya 2001-03-15 */

#include "psqlodbc.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include "statement.h"
#include "connection.h"
#include "qresult.h"
#include "pgtypes.h"
#include "pgapifunc.h"

#ifdef MULTIBYTE
#include "multibyte.h"
#endif

#define FLD_INCR	32
#define TAB_INCR	8
#define COL_INCR	16

#ifdef	MULTIBYTE
char	   *getNextToken(int ccsc, char *s, char *token, int smax, char *delim, char *quote, char *dquote, char *numeric);
#else
char	   *getNextToken(char *s, char *token, int smax, char *delim, char *quote, char *dquote, char *numeric);
#endif /* MULTIBYTE */
void		getColInfo(COL_INFO *col_info, FIELD_INFO *fi, int k);
char		searchColInfo(COL_INFO *col_info, FIELD_INFO *fi);


char *
getNextToken(
#ifdef	MULTIBYTE
	int ccsc, /* client encoding */
#endif /* MULTIBYTE */
	char *s, char *token, int smax, char *delim, char *quote, char *dquote, char *numeric)
{
	int			i = 0;
	int			out = 0;
	char		qc,
				in_escape = FALSE;
#ifdef MULTIBYTE
	encoded_str	encstr;
#endif

	if (smax <= 1)
		return NULL;

	smax--;

	/* skip leading delimiters */
	while (isspace((unsigned char) s[i]) || s[i] == ',')
	{
		/* mylog("skipping '%c'\n", s[i]); */
		i++;
	}

	if (s[i] == '\0')
	{
		token[0] = '\0';
		return NULL;
	}

	if (quote)
		*quote = FALSE;
	if (dquote)
		*dquote = FALSE;
	if (numeric)
		*numeric = FALSE;

#ifdef MULTIBYTE
	encoded_str_constr(&encstr, ccsc, &s[i]);
#endif
	/* get the next token */
	while (s[i] != '\0' && out < smax)
	{
#ifdef MULTIBYTE
		encoded_nextchar(&encstr);
		if (ENCODE_STATUS(encstr) != 0)
		{
			token[out++] = s[i++];
			continue;
		}
#endif
		if (isspace((unsigned char) s[i]) || s[i] == ',')
			break;
		/* Handle quoted stuff */
		if (out == 0 && (s[i] == '\"' || s[i] == '\''))
		{
			qc = s[i];
			if (qc == '\"')
			{
				if (dquote)
					*dquote = TRUE;
			}
			if (qc == '\'')
			{
				if (quote)
					*quote = TRUE;
			}

			i++;				/* dont return the quote */
			while (s[i] != '\0' && out != smax)
			{
#ifdef MULTIBYTE
				encoded_nextchar(&encstr);
				if (ENCODE_STATUS(encstr) != 0)
				{
					token[out++] = s[i++];
					continue;
				}
#endif
				if (s[i] == qc && !in_escape)
					break;
				if (s[i] == '\\' && !in_escape)
					in_escape = TRUE;
				else
				{
					in_escape = FALSE;
					token[out++] = s[i];
				}
				i++;
			}
			if (s[i] == qc)
				i++;
			break;
		}

		/* Check for numeric literals */
		if (out == 0 && isdigit((unsigned char) s[i]))
		{
			if (numeric)
				*numeric = TRUE;
			token[out++] = s[i++];
			while (isalnum((unsigned char) s[i]) || s[i] == '.')
				token[out++] = s[i++];

			break;
		}

		if (ispunct((unsigned char) s[i]) && s[i] != '_')
		{
			mylog("got ispunct: s[%d] = '%c'\n", i, s[i]);

			if (out == 0)
			{
				token[out++] = s[i++];
				break;
			}
			else
				break;
		}

		if (out != smax)
			token[out++] = s[i];

		i++;
	}

	/* mylog("done -- s[%d] = '%c'\n", i, s[i]); */

	token[out] = '\0';

	/* find the delimiter  */
	while (isspace((unsigned char) s[i]))
		i++;

	/* return the most priority delimiter */
	if (s[i] == ',')
	{
		if (delim)
			*delim = s[i];
	}
	else if (s[i] == '\0')
	{
		if (delim)
			*delim = '\0';
	}
	else
	{
		if (delim)
			*delim = ' ';
	}

	/* skip trailing blanks  */
	while (isspace((unsigned char) s[i]))
		i++;

	return &s[i];
}


#if 0
QR_set_num_fields(SC_get_Curres(stmt), 14);
QR_set_field_info(SC_get_Curres(stmt), 0, "TABLE_QUALIFIER", PG_TYPE_TEXT, MAX_INFO_STRING);
QR_set_field_info(SC_get_Curres(stmt), 1, "TABLE_OWNER", PG_TYPE_TEXT, MAX_INFO_STRING);
QR_set_field_info(SC_get_Curres(stmt), 2, "TABLE_NAME", PG_TYPE_TEXT, MAX_INFO_STRING);
QR_set_field_info(SC_get_Curres(stmt), 3, "COLUMN_NAME", PG_TYPE_TEXT, MAX_INFO_STRING);
QR_set_field_info(SC_get_Curres(stmt), 4, "DATA_TYPE", PG_TYPE_INT2, 2);
QR_set_field_info(SC_get_Curres(stmt), 5, "TYPE_NAME", PG_TYPE_TEXT, MAX_INFO_STRING);
QR_set_field_info(SC_get_Curres(stmt), 6, "PRECISION", PG_TYPE_INT4, 4);
QR_set_field_info(SC_get_Curres(stmt), 7, "LENGTH", PG_TYPE_INT4, 4);
QR_set_field_info(SC_get_Curres(stmt), 8, "SCALE", PG_TYPE_INT2, 2);
QR_set_field_info(SC_get_Curres(stmt), 9, "RADIX", PG_TYPE_INT2, 2);
QR_set_field_info(SC_get_Curres(stmt), 10, "NULLABLE", PG_TYPE_INT2, 2);
QR_set_field_info(SC_get_Curres(stmt), 11, "REMARKS", PG_TYPE_TEXT, 254);
/*	User defined fields */
QR_set_field_info(SC_get_Curres(stmt), 12, "DISPLAY_SIZE", PG_TYPE_INT4, 4);
QR_set_field_info(SC_get_Curres(stmt), 13, "FIELD_TYPE", PG_TYPE_INT4, 4);
#endif

void
getColInfo(COL_INFO *col_info, FIELD_INFO *fi, int k)
{
	char	   *str;

	if (fi->name[0] == '\0')
		strcpy(fi->name, QR_get_value_manual(col_info->result, k, 3));

	fi->type = atoi(QR_get_value_manual(col_info->result, k, 13));
	fi->precision = atoi(QR_get_value_manual(col_info->result, k, 6));
	fi->length = atoi(QR_get_value_manual(col_info->result, k, 7));
	if (str = QR_get_value_manual(col_info->result, k, 8), str)
		fi->scale = atoi(str);
	else
		fi->scale = -1;
	fi->nullable = atoi(QR_get_value_manual(col_info->result, k, 10));
	fi->display_size = atoi(QR_get_value_manual(col_info->result, k, 12));
}


char
searchColInfo(COL_INFO *col_info, FIELD_INFO *fi)
{
	int			k,
				cmp;
	char	   *col;

	for (k = 0; k < QR_get_num_tuples(col_info->result); k++)
	{
		col = QR_get_value_manual(col_info->result, k, 3);
		if (fi->dquote)
			cmp = strcmp(col, fi->name);
		else
			cmp = stricmp(col, fi->name);
		if (!cmp)
		{
			if (!fi->dquote)
				strcpy(fi->name, col);
			getColInfo(col_info, fi, k);

			mylog("PARSE: searchColInfo: \n");
			return TRUE;
		}
	}

	return FALSE;
}


char
parse_statement(StatementClass *stmt)
{
	static char *func = "parse_statement";
	char		token[256];
	char		delim,
				quote,
				dquote,
				numeric,
				unquoted;
	char	   *ptr,
			   *pptr = NULL;
	char		in_select = FALSE,
				in_distinct = FALSE,
				in_on = FALSE,
				in_from = FALSE,
				in_where = FALSE,
				in_table = FALSE;
	char		in_field = FALSE,
				in_expr = FALSE,
				in_func = FALSE,
				in_dot = FALSE,
				in_as = FALSE;
	int			j,
				i,
				k = 0,
				n,
				blevel = 0;
	FIELD_INFO **fi;
	TABLE_INFO **ti;
	char		parse;
	ConnectionClass *conn = stmt->hdbc;
	HSTMT		hcol_stmt;
	StatementClass *col_stmt;
	RETCODE		result;
	BOOL		updatable = TRUE;

	mylog("%s: entering...\n", func);

	ptr = stmt->statement;
	fi = stmt->fi;
	ti = stmt->ti;

	stmt->nfld = 0;
	stmt->ntab = 0;
	stmt->from_pos = -1;
	stmt->where_pos = -1;

#ifdef MULTIBYTE
	while (pptr = ptr, (ptr = getNextToken(conn->ccsc, pptr, token, sizeof(token), &delim, &quote, &dquote, &numeric)) != NULL)
#else
	while (pptr = ptr, (ptr = getNextToken(pptr, token, sizeof(token), &delim, &quote, &dquote, &numeric)) != NULL)
#endif
	{
		unquoted = !(quote || dquote);

		mylog("unquoted=%d, quote=%d, dquote=%d, numeric=%d, delim='%c', token='%s', ptr='%s'\n", unquoted, quote, dquote, numeric, delim, token, ptr);

		if (in_select && unquoted && blevel == 0)
		{
			if (!stricmp(token, "distinct"))
			{
				in_distinct = TRUE;
				updatable = FALSE;

				mylog("DISTINCT\n");
				continue;
			}
			if (!stricmp(token, "into"))
			{
				in_select = FALSE;
				mylog("INTO\n");
				stmt->statement_type = STMT_TYPE_CREATE;
				stmt->parse_status = STMT_PARSE_FATAL;
				return FALSE;
			}
			if (!stricmp(token, "from"))
			{
				in_select = FALSE;
				in_from = TRUE;
				if (stmt->from_pos < 0 &&
					(!strnicmp(pptr, "from", 4)))
				{
					mylog("First ");
					stmt->from_pos = pptr - stmt->statement;
				}

				mylog("FROM\n");
				continue;
			}
		}
		if (unquoted && blevel == 0)
		{
			if ((!stricmp(token, "where") ||
				 !stricmp(token, "union") ||
				 !stricmp(token, "intersect") ||
				 !stricmp(token, "except") ||
				 !stricmp(token, "order") ||
				 !stricmp(token, "group") ||
				 !stricmp(token, "having")))
			{
				in_select = FALSE;
				in_from = FALSE;
				in_where = TRUE;

				if (!stricmp(token, "where"))
				{
					if (stmt->where_pos < 0)
						stmt->where_pos = pptr - stmt->statement;
				}
				else if (stricmp(token, "order"))
					updatable = FALSE;

				mylog("WHERE...\n");
				break;
			}
		}
		if (in_select && (in_expr || in_func))
		{
			/* just eat the expression */
			mylog("in_expr=%d or func=%d\n", in_expr, in_func);

			if (unquoted)
			{
				if (token[0] == '(')
				{
					blevel++;
					mylog("blevel++ = %d\n", blevel);
				}
				else if (token[0] == ')')
				{
					blevel--;
					mylog("blevel-- = %d\n", blevel);
				}
			}
			if (blevel == 0)
			{
				if (delim == ',')
				{
					mylog("**** Got comma in_expr/func\n");
					in_func = FALSE;
					in_expr = FALSE;
					in_field = FALSE;
				}
				else if (unquoted && !stricmp(token, "as"))
				{
					mylog("got AS in_expr\n");
					in_func = FALSE;
					in_expr = FALSE;
					in_as = TRUE;
					in_field = TRUE;
				}
			}
			continue;
		}

		if (unquoted && !stricmp(token, "select"))
		{
			in_select = TRUE;

			mylog("SELECT\n");
			continue;
		}
		if (in_select)
		{
			if (in_distinct)
			{
				mylog("in distinct\n");

				if (unquoted && !stricmp(token, "on"))
				{
					in_on = TRUE;
					mylog("got on\n");
					continue;
				}
				if (in_on)
				{
					in_distinct = FALSE;
					in_on = FALSE;
					continue;	/* just skip the unique on field */
				}
				mylog("done distinct\n");
				in_distinct = FALSE;
			}

			if (!in_field)
			{
				if (!token[0])
					continue;

				if (!(stmt->nfld % FLD_INCR))
				{
					mylog("reallocing at nfld=%d\n", stmt->nfld);
					fi = (FIELD_INFO **) realloc(fi, (stmt->nfld + FLD_INCR) * sizeof(FIELD_INFO *));
					if (!fi)
					{
						stmt->parse_status = STMT_PARSE_FATAL;
						return FALSE;
					}
					stmt->fi = fi;
				}

				fi[stmt->nfld] = (FIELD_INFO *) malloc(sizeof(FIELD_INFO));
				if (fi[stmt->nfld] == NULL)
				{
					stmt->parse_status = STMT_PARSE_FATAL;
					return FALSE;
				}

				/* Initialize the field info */
				memset(fi[stmt->nfld], 0, sizeof(FIELD_INFO));

				/* double quotes are for qualifiers */
				if (dquote)
					fi[stmt->nfld]->dquote = TRUE;

				if (quote)
				{
					fi[stmt->nfld]->quote = TRUE;
					fi[stmt->nfld]->precision = strlen(token);
				}
				else if (numeric)
				{
					mylog("**** got numeric: nfld = %d\n", stmt->nfld);
					fi[stmt->nfld]->numeric = TRUE;
				}
				else if (token[0] == '(')
				{				/* expression */
					mylog("got EXPRESSION\n");
					fi[stmt->nfld++]->expr = TRUE;
					in_expr = TRUE;
					blevel = 1;
					continue;
				}
				else
				{
					strcpy(fi[stmt->nfld]->name, token);
					fi[stmt->nfld]->dot[0] = '\0';
				}
				mylog("got field='%s', dot='%s'\n", fi[stmt->nfld]->name, fi[stmt->nfld]->dot);

				if (delim == ',')
					mylog("comma (1)\n");
				else
					in_field = TRUE;
				stmt->nfld++;
				continue;
			}

			/*
			 * We are in a field now
			 */
			if (in_dot)
			{
				stmt->nfld--;
				strcpy(fi[stmt->nfld]->dot, fi[stmt->nfld]->name);
				strcpy(fi[stmt->nfld]->name, token);
				stmt->nfld++;
				in_dot = FALSE;

				if (delim == ',')
				{
					mylog("in_dot: got comma\n");
					in_field = FALSE;
				}
				continue;
			}

			if (in_as)
			{
				stmt->nfld--;
				strcpy(fi[stmt->nfld]->alias, token);
				mylog("alias for field '%s' is '%s'\n", fi[stmt->nfld]->name, fi[stmt->nfld]->alias);
				in_as = FALSE;
				in_field = FALSE;

				stmt->nfld++;

				if (delim == ',')
					mylog("comma(2)\n");
				continue;
			}

			/* Function */
			if (token[0] == '(')
			{
				in_func = TRUE;
				blevel = 1;
				fi[stmt->nfld - 1]->func = TRUE;

				/*
				 * name will have the function name -- maybe useful some
				 * day
				 */
				mylog("**** got function = '%s'\n", fi[stmt->nfld - 1]->name);
				continue;
			}

			if (token[0] == '.')
			{
				in_dot = TRUE;
				mylog("got dot\n");
				continue;
			}

			if (!stricmp(token, "as"))
			{
				in_as = TRUE;
				mylog("got AS\n");
				continue;
			}

			/* otherwise, it's probably an expression */
			in_expr = TRUE;
			fi[stmt->nfld - 1]->expr = TRUE;
			fi[stmt->nfld - 1]->name[0] = '\0';
			fi[stmt->nfld - 1]->precision = 0;
			mylog("*** setting expression\n");
		}

		if (in_from)
		{
			if (!in_table)
			{
				if (!token[0])
					continue;

				if (!(stmt->ntab % TAB_INCR))
				{
					ti = (TABLE_INFO **) realloc(ti, (stmt->ntab + TAB_INCR) * sizeof(TABLE_INFO *));
					if (!ti)
					{
						stmt->parse_status = STMT_PARSE_FATAL;
						return FALSE;
					}
					stmt->ti = ti;
				}
				ti[stmt->ntab] = (TABLE_INFO *) malloc(sizeof(TABLE_INFO));
				if (ti[stmt->ntab] == NULL)
				{
					stmt->parse_status = STMT_PARSE_FATAL;
					return FALSE;
				}

				ti[stmt->ntab]->alias[0] = '\0';

				strcpy(ti[stmt->ntab]->name, token);
				if (!dquote)
				{
					char	   *ptr;
#ifdef	MULTIBYTE
					encoded_str	encstr;
					make_encoded_str(&encstr, conn, ti[stmt->ntab]->name);
#endif   /* MULTIBYTE */

					/* lower case table name */
					for (ptr = ti[stmt->ntab]->name; *ptr; ptr++)
					{
#ifdef	MULTIBYTE
						encoded_nextchar(&encstr);
						if (ENCODE_STATUS(encstr) != 0)
							ptr++;
						else
#endif   /* MULTIBYTE */
							*ptr = tolower((unsigned char) *ptr);
					}
				}
				mylog("got table = '%s'\n", ti[stmt->ntab]->name);

				if (delim == ',')
					mylog("more than 1 tables\n");
				else
					in_table = TRUE;
				stmt->ntab++;
				continue;
			}

			strcpy(ti[stmt->ntab - 1]->alias, token);
			mylog("alias for table '%s' is '%s'\n", ti[stmt->ntab - 1]->name, ti[stmt->ntab - 1]->alias);
			in_table = FALSE;
			if (delim == ',')
				mylog("more than 1 tables\n");
		}
	}

	/*
	 * Resolve any possible field names with tables
	 */

	parse = TRUE;

	/* Resolve field names with tables */
	for (i = 0; i < stmt->nfld; i++)
	{
		if (fi[i]->func || fi[i]->expr || fi[i]->numeric)
		{
			fi[i]->ti = NULL;
			fi[i]->type = -1;
			parse = FALSE;
			continue;
		}
		else if (fi[i]->quote)
		{						/* handle as text */
			fi[i]->ti = NULL;

			/*
			 * fi[i]->type = PG_TYPE_TEXT; fi[i]->precision = 0; the
			 * following may be better
			 */
			fi[i]->type = PG_TYPE_UNKNOWN;
			if (fi[i]->precision == 0)
			{
				fi[i]->type = PG_TYPE_VARCHAR;
				fi[i]->precision = 254;
			}
			fi[i]->length = fi[i]->precision;
			continue;
		}
		/* it's a dot, resolve to table or alias */
		else if (fi[i]->dot[0])
		{
			for (k = 0; k < stmt->ntab; k++)
			{
				if (!stricmp(ti[k]->name, fi[i]->dot))
				{
					fi[i]->ti = ti[k];
					break;
				}
				else if (!stricmp(ti[k]->alias, fi[i]->dot))
				{
					fi[i]->ti = ti[k];
					break;
				}
			}
		}
		else if (stmt->ntab == 1)
			fi[i]->ti = ti[0];
	}

	mylog("--------------------------------------------\n");
	mylog("nfld=%d, ntab=%d\n", stmt->nfld, stmt->ntab);

	for (i = 0; i < stmt->nfld; i++)
	{
		mylog("Field %d:  expr=%d, func=%d, quote=%d, dquote=%d, numeric=%d, name='%s', alias='%s', dot='%s'\n", i, fi[i]->expr, fi[i]->func, fi[i]->quote, fi[i]->dquote, fi[i]->numeric, fi[i]->name, fi[i]->alias, fi[i]->dot);
		if (fi[i]->ti)
			mylog("     ----> table_name='%s', table_alias='%s'\n", fi[i]->ti->name, fi[i]->ti->alias);
	}

	for (i = 0; i < stmt->ntab; i++)
		mylog("Table %d: name='%s', alias='%s'\n", i, ti[i]->name, ti[i]->alias);


	/*
	 * Now save the SQLColumns Info for the parse tables
	 */

	/* Call SQLColumns for each table and store the result */
	if (stmt->ntab > 1)
		updatable = FALSE;
	else if (stmt->from_pos < 0)
		updatable = FALSE;
	for (i = 0; i < stmt->ntab; i++)
	{
		/* See if already got it */
		char		found = FALSE;

		for (k = 0; k < conn->ntables; k++)
		{
			if (!stricmp(conn->col_info[k]->name, ti[i]->name))
			{
				mylog("FOUND col_info table='%s'\n", ti[i]->name);
				found = TRUE;
				break;
			}
		}

		if (!found)
		{
			mylog("PARSE: Getting PG_Columns for table[%d]='%s'\n", i, ti[i]->name);

			result = PGAPI_AllocStmt(stmt->hdbc, &hcol_stmt);
			if ((result != SQL_SUCCESS) && (result != SQL_SUCCESS_WITH_INFO))
			{
				stmt->errormsg = "PGAPI_AllocStmt failed in parse_statement for columns.";
				stmt->errornumber = STMT_NO_MEMORY_ERROR;
				stmt->parse_status = STMT_PARSE_FATAL;
				return FALSE;
			}

			col_stmt = (StatementClass *) hcol_stmt;
			col_stmt->internal = TRUE;

			result = PGAPI_Columns(hcol_stmt, "", 0, "", 0,
						ti[i]->name, (SWORD) strlen(ti[i]->name), "", 0, PODBC_NOT_SEARCH_PATTERN);

			mylog("        Past PG_Columns\n");
			if (result == SQL_SUCCESS)
			{
				mylog("      Success\n");
				if (!(conn->ntables % COL_INCR))
				{
					mylog("PARSE: Allocing col_info at ntables=%d\n", conn->ntables);

					conn->col_info = (COL_INFO **) realloc(conn->col_info, (conn->ntables + COL_INCR) * sizeof(COL_INFO *));
					if (!conn->col_info)
					{
						stmt->parse_status = STMT_PARSE_FATAL;
						return FALSE;
					}
				}

				mylog("PARSE: malloc at conn->col_info[%d]\n", conn->ntables);
				conn->col_info[conn->ntables] = (COL_INFO *) malloc(sizeof(COL_INFO));
				if (!conn->col_info[conn->ntables])
				{
					stmt->parse_status = STMT_PARSE_FATAL;
					return FALSE;
				}

				/*
				 * Store the table name and the SQLColumns result
				 * structure
				 */
				strcpy(conn->col_info[conn->ntables]->name, ti[i]->name);
				conn->col_info[conn->ntables]->result = SC_get_Curres(col_stmt);

				/*
				 * The connection will now free the result structures, so
				 * make sure that the statement doesn't free it
				 */
				SC_set_Result(col_stmt, NULL);

				conn->ntables++;

				PGAPI_FreeStmt(hcol_stmt, SQL_DROP);
				mylog("Created col_info table='%s', ntables=%d\n", ti[i]->name, conn->ntables);
			}
			else
			{
				PGAPI_FreeStmt(hcol_stmt, SQL_DROP);
				break;
			}
		}

		/* Associate a table from the statement with a SQLColumn info */
		ti[i]->col_info = conn->col_info[k];
		mylog("associate col_info: i=%d, k=%d\n", i, k);
	}

	mylog("Done PG_Columns\n");

	/*
	 * Now resolve the fields to point to column info
	 */
	for (i = 0; i < stmt->nfld;)
	{
		fi[i]->updatable = updatable;
		/* Dont worry about functions or quotes */
		if (fi[i]->func || fi[i]->quote || fi[i]->numeric)
		{
			fi[i]->updatable = FALSE;
			i++;
			continue;
		}

		/* Stars get expanded to all fields in the table */
		else if (fi[i]->name[0] == '*')
		{
			char		do_all_tables;
			int			total_cols,
						old_alloc,
						new_size,
						cols;
			int			increased_cols;

			mylog("expanding field %d\n", i);

			total_cols = 0;

			if (fi[i]->ti)		/* The star represents only the qualified
								 * table */
				total_cols = QR_get_num_tuples(fi[i]->ti->col_info->result);

			else
			{					/* The star represents all tables */

				/* Calculate the total number of columns after expansion */
				for (k = 0; k < stmt->ntab; k++)
					total_cols += QR_get_num_tuples(ti[k]->col_info->result);
			}
			increased_cols = total_cols - 1;

			/* Allocate some more field pointers if necessary */
			old_alloc = ((stmt->nfld - 1) / FLD_INCR + 1) * FLD_INCR;
			new_size = stmt->nfld + increased_cols;

			mylog("k=%d, increased_cols=%d, old_alloc=%d, new_size=%d\n", k, increased_cols, old_alloc, new_size);

			if (new_size > old_alloc)
			{
				int			new_alloc = ((new_size / FLD_INCR) + 1) * FLD_INCR;

				mylog("need more cols: new_alloc = %d\n", new_alloc);
				fi = (FIELD_INFO **) realloc(fi, new_alloc * sizeof(FIELD_INFO *));
				if (!fi)
				{
					stmt->parse_status = STMT_PARSE_FATAL;
					return FALSE;
				}
				stmt->fi = fi;
			}

			/*
			 * copy any other fields (if there are any) up past the
			 * expansion
			 */
			for (j = stmt->nfld - 1; j > i; j--)
			{
				mylog("copying field %d to %d\n", j, increased_cols + j);
				fi[increased_cols + j] = fi[j];
			}
			mylog("done copying fields\n");

			/* Set the new number of fields */
			stmt->nfld += increased_cols;
			mylog("stmt->nfld now at %d\n", stmt->nfld);


			/* copy the new field info */
			do_all_tables = (fi[i]->ti ? FALSE : TRUE);

			for (k = 0; k < (do_all_tables ? stmt->ntab : 1); k++)
			{
				TABLE_INFO *the_ti = do_all_tables ? ti[k] : fi[i]->ti;

				cols = QR_get_num_tuples(the_ti->col_info->result);

				for (n = 0; n < cols; n++)
				{
					mylog("creating field info: n=%d\n", n);
					/* skip malloc (already did it for the Star) */
					if (k > 0 || n > 0)
					{
						mylog("allocating field info at %d\n", n + i);
						fi[n + i] = (FIELD_INFO *) malloc(sizeof(FIELD_INFO));
						if (fi[n + i] == NULL)
						{
							stmt->parse_status = STMT_PARSE_FATAL;
							return FALSE;
						}
					}
					/* Initialize the new space (or the * field) */
					memset(fi[n + i], 0, sizeof(FIELD_INFO));
					fi[n + i]->ti = the_ti;

					mylog("about to copy at %d\n", n + i);

					getColInfo(the_ti->col_info, fi[n + i], n);
					fi[n + i]->updatable = updatable;

					mylog("done copying\n");
				}

				i += cols;
				mylog("i now at %d\n", i);
			}
		}

		/*
		 * We either know which table the field was in because it was
		 * qualified with a table name or alias -OR- there was only 1
		 * table.
		 */
		else if (fi[i]->ti)
		{
			if (!searchColInfo(fi[i]->ti->col_info, fi[i]))
			{
				parse = FALSE;
				fi[i]->updatable = FALSE;
			}
			i++;
		}

		/* Don't know the table -- search all tables in "from" list */
		else
		{
			for (k = 0; k < stmt->ntab; k++)
			{
				if (searchColInfo(ti[k]->col_info, fi[i]))
				{
					fi[i]->ti = ti[k];	/* now know the table */
					break;
				}
			}
			if (k >= stmt->ntab)
			{
				parse = FALSE;
				fi[i]->updatable = FALSE;
			}
			i++;
		}
	}

	if (!parse)
		stmt->parse_status = STMT_PARSE_INCOMPLETE;
	else
		stmt->parse_status = STMT_PARSE_COMPLETE;

	mylog("done parse_statement: parse=%d, parse_status=%d\n", parse, stmt->parse_status);
	return parse;
}
