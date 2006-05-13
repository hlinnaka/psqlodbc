/*--------
 * Module :			multibyte.c
 *
 * Description:		New Mlutibyte related additional function.
 *
 *					Create 2001-03-03 Eiji Tokuya
 *					New Create 2001-09-16 Eiji Tokuya
 *--------
 */

#include "multibyte.h"
#include "connection.h"
#include "pgapifunc.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef	TRUE
#define	TRUE	1
#endif

pg_CS CS_Table[] =
{
	{ "SQL_ASCII",	SQL_ASCII },
	{ "EUC_JP",	EUC_JP },
	{ "EUC_CN",	EUC_CN },
	{ "EUC_KR",	EUC_KR },
	{ "EUC_TW",	EUC_TW },
	{ "JOHAB",	JOHAB },
	{ "UTF8",	UTF8 },
	{ "MULE_INTERNAL",MULE_INTERNAL },
	{ "LATIN1",	LATIN1 },
	{ "LATIN2",	LATIN2 },
	{ "LATIN3",	LATIN3 },
	{ "LATIN4",	LATIN4 },
	{ "LATIN5",	LATIN5 },
	{ "LATIN6",	LATIN6 },
	{ "LATIN7",	LATIN7 },
	{ "LATIN8",	LATIN8 },
	{ "LATIN9",	LATIN9 },
	{ "LATIN10",	LATIN10 },
	{ "WIN1256",	WIN1256 },
	{ "WIN1258",	WIN1258 },	/* since 8.1 */
	{ "WIN874",	WIN874 },
	{ "KOI8",	KOI8R },
	{ "WIN1251",	WIN1251 },
	{ "WIN866",	WIN866 },	/* since 8.1 */
	{ "ISO_8859_5", ISO_8859_5 },
	{ "ISO_8859_6", ISO_8859_6 },
	{ "ISO_8859_7", ISO_8859_7 },
	{ "ISO_8859_8", ISO_8859_8 },


	{ "SJIS",	SJIS },
	{ "BIG5",	BIG5 },
	{ "GBK",	GBK },
	{ "UHC",	UHC },
	{ "WIN1250",	WIN1250 },
	{ "GB18030",	GB18030 },
	{ "UNICODE",	UNICODE_PODBC },
	{ "TCVN",	TCVN },
	{ "ALT",	ALT },
	{ "WIN",	WIN },
	{ "OTHER",	OTHER }
};


int
pg_CS_code(const UCHAR *characterset_string)
{
	int i, c = -1;
  	unsigned len = 0;

	for(i = 0; CS_Table[i].code != OTHER; i++)
	{
		if (0 == stricmp(characterset_string, CS_Table[i].name))
		{
                       	c = CS_Table[i].code;
			break;
		}
	}
	if (c < 0)
		for(i = 0; CS_Table[i].code != OTHER; i++)
		{
			if (strstr(characterset_string, CS_Table[i].name))
			{
                  		if(strlen(CS_Table[i].name) >= len)
				{
                         		len = strlen(CS_Table[i].name);
                         		c = CS_Table[i].code;
                        	}
			}
		}
	if (c < 0)
		c = i;
	return (c);
}

UCHAR *
pg_CS_name(int characterset_code)
{
	int i;
	for (i = 0; CS_Table[i].code != OTHER; i++)
	{
		if (CS_Table[i].code == characterset_code)
			return CS_Table[i].name;
	}
	return ("OTHER");
}

static int
pg_mb_maxlen(characterset_code)
{
	switch (characterset_code)
	{
		case UTF8:
		case UNICODE_PODBC:
			return 6;
		case EUC_TW:
			return 4;
		case EUC_JP:
		case GB18030:
			return 3;
		case SJIS:
		case BIG5:
		case GBK:
		case UHC:
		case EUC_CN:
		case EUC_KR:
		case JOHAB:
			return 2;
		default:
			return 1;
	}
}

int
pg_CS_stat(int stat,unsigned int character,int characterset_code)
{
	if (character == 0)
		stat = 0;
	switch (characterset_code)
	{
		case UTF8:
		case UNICODE_PODBC:
			{
				if (stat < 2 &&
					character >= 0x80)
				{
					if (character >= 0xfc)
						stat = 6;
					else if (character >= 0xf8)
						stat = 5;
					else if (character >= 0xf0)
						stat = 4;
					else if (character >= 0xe0)
						stat = 3;
					else if (character >= 0xc0)
						stat = 2;
				}
				else if (stat > 2 &&
					character > 0x7f)
					stat--;
				else
					stat=0;
			}
			break;
/* Shift-JIS Support. */
			case SJIS:
			{
				if (stat < 2 &&
					character > 0x80 &&
					!(character > 0x9f &&
					character < 0xe0))
					stat = 2;
				else if (stat == 2)
					stat = 1;
				else
					stat = 0;
			}
			break;
/* Chinese Big5 Support. */
		case BIG5:
			{
				if (stat < 2 &&
					character > 0xA0)
					stat = 2;
				else if (stat == 2)
					stat = 1;
				else
					stat = 0;
			}
			break;
/* Chinese GBK Support. */
		case GBK:
			{
				if (stat < 2 &&
					character > 0x7F)
					stat = 2;
				else if (stat == 2)
					stat = 1;
				else
					stat = 0;
			}
			break;

/* Korian UHC Support. */
		case UHC:
			{
				if (stat < 2 &&
					character > 0x7F)
					stat = 2;
				else if (stat == 2)
					stat = 1;
				else
					stat = 0;
			}
			break;

/* EUC_JP Support */
		case EUC_JP:
			{
				if (stat < 3 && 
					character == 0x8f)	/* JIS X 0212 */
					stat = 3;
				else
				if (stat != 2 && 
					(character == 0x8e ||
					character > 0xa0))	/* Half Katakana HighByte & Kanji HighByte */
					stat = 2;
				else if (stat == 2)
					stat = 1;
				else
					stat = 0;
			}
			break;

/* EUC_CN, EUC_KR, JOHAB Support */
		case EUC_CN:
		case EUC_KR:
		case JOHAB:
			{
				if (stat < 2 &&
					character > 0xa0)
					stat = 2;
				else if (stat == 2)
					stat = 1;
				else
					stat = 0;
			}
			break;
		case EUC_TW:
			{
				if (stat < 4 &&
					character == 0x8e)
					stat = 4;
				else if (stat == 4 &&
					character > 0xa0)
					stat = 3;
				else if ((stat == 3 ||
					stat < 2) &&
					character > 0xa0)
					stat = 2;
				else if (stat == 2)
					stat = 1;
				else
					stat = 0;
			}
			break;
			/*Chinese GB18030 support.Added by Bill Huang <bhuang@redhat.com> <bill_huanghb@ybb.ne.jp>*/
		case GB18030:
			{
				if (stat < 2 && character > 0x80)
					stat = 2;
				else if (stat == 2)
				{
					if (character >= 0x30 && character <= 0x39)
						stat = 3;
					else
						stat = 1;
				}
				else if (stat == 3)
				{
					if (character >= 0x30 && character <= 0x39)
						stat = 1;
					else
						stat = 3;
				}
				else
					stat = 0;
			}
			break;
		default:
			{
				stat = 0;
			}
			break;
	}
	return stat;
}


UCHAR *
pg_mbschr(int csc, const UCHAR *string, unsigned int character)
{
	int			mb_st = 0;
	const UCHAR *s, *rs = NULL;

	for(s = string; *s ; s++) 
	{
		mb_st = pg_CS_stat(mb_st, (UCHAR) *s, csc);
		if (mb_st == 0 && (*s == character))
		{
			rs = s;
			break;
		}
	}
	return ((UCHAR *) rs);
}

int
pg_mbslen(int csc, const UCHAR *string)
{
	UCHAR *s;
	int len, cs_stat;
	for (len = 0, cs_stat = 0, s = (UCHAR *) string; *s != 0; s++)
	{
		cs_stat = pg_CS_stat(cs_stat,(unsigned int) *s, csc);
		if (cs_stat < 2)
			len++;
	}
	return len;
}

UCHAR *
pg_mbsinc(int csc, const UCHAR *current )
{
	int mb_stat = 0;
	if (*current != 0)
	{
		mb_stat = (int) pg_CS_stat(mb_stat, *current, csc);
		if (mb_stat == 0)
			mb_stat = 1;
		return ((UCHAR *) current + mb_stat);
	}
	else
		return NULL;
}

static char *
CC_lookup_cs_new(ConnectionClass *self)
{
	char		*encstr = NULL;
	QResultClass	*res;

	res = CC_send_query(self, "select pg_client_encoding()", NULL, IGNORE_ABORT_ON_CONN | ROLLBACK_ON_ERROR, NULL);
	if (QR_command_maybe_successful(res))
	{
		char 	*enc = QR_get_value_backend_row(res, 0, 0);

		if (enc)
			encstr = strdup(enc);
	}
	QR_Destructor(res);
	return encstr;
}
static char *
CC_lookup_cs_old(ConnectionClass *self)
{
	char		*encstr = NULL;
	HSTMT		hstmt;
	RETCODE		result;

	result = PGAPI_AllocStmt(self, &hstmt);
	if ((result != SQL_SUCCESS) && (result != SQL_SUCCESS_WITH_INFO))
		return encstr;

	result = PGAPI_ExecDirect(hstmt, "Show Client_Encoding", SQL_NTS, 0);
	if (result == SQL_SUCCESS_WITH_INFO)
	{
		char sqlState[8], errormsg[128], enc[32];

		if (PGAPI_Error(NULL, NULL, hstmt, sqlState, NULL, errormsg,
			sizeof(errormsg), NULL) == SQL_SUCCESS &&
		    sscanf(errormsg, "%*s %*s %*s %*s %*s %s", enc) > 0)
			encstr = strdup(enc);
	}
	PGAPI_FreeStmt(hstmt, SQL_DROP);
	return encstr;
}

/*
 *	This function works under Windows or Unicode case only.
 *	Simply returns NULL under other OSs.
 */
const char * get_environment_encoding(const ConnectionClass *conn, const char *oldenc)
{
	const char *wenc = NULL;

#ifdef	UNICODE_SUPPORT
	if (CC_is_in_unicode_driver(conn))
		return "UTF8";
#endif /* UNICODE_SUPPORT */
#ifdef	WIN32
	switch (GetACP())
	{
		case 932:
			wenc = "SJIS";
			break;
		case 936:
			if (!oldenc && PG_VERSION_GT(conn, 7.2))
				wenc = "GBK";
			break;
		case 949:
			if (!oldenc || (PG_VERSION_GT(conn, 7.2) && stricmp(oldenc, "EUC_KR")))  
				wenc = "UHC";
			break;
		case 950:
			wenc = "BIG5";
			break;
		case 1250:
			wenc = "WIN1250";
			break;
		case 1252:
			if (oldenc)
				;
			else if (PG_VERSION_GE(conn, 8.1))
				wenc = "WIN1252";
			else
				wenc = "latin1";
			break;
	}
#endif /* WIN32 */
	return wenc;
}

void
CC_lookup_characterset(ConnectionClass *self)
{
	char		*encstr;
	CSTR func = "CC_lookup_characterset";

	mylog("%s: entering...\n", func);
	if (self->current_client_encoding)
		encstr = strdup(self->current_client_encoding);
	else if (PG_VERSION_LT(self, 7.2))
		encstr = CC_lookup_cs_old(self);
	else
		encstr = CC_lookup_cs_new(self);
	if (self->original_client_encoding)
		free(self->original_client_encoding);
#ifndef	UNICODE_SUPPORT
	else
	{
		const char *wenc = get_environment_encoding(self, encstr);
		if (wenc && (!encstr || stricmp(encstr, wenc)))
		{
			QResultClass	*res;
			char		query[64];
			int		errnum = CC_get_errornumber(self);
			BOOL		cmd_success;

			sprintf(query, "set client_encoding to '%s'", wenc);
			res = CC_send_query(self, query, NULL, IGNORE_ABORT_ON_CONN | ROLLBACK_ON_ERROR, NULL);
			cmd_success = QR_command_maybe_successful(res);
			QR_Destructor(res);
			CC_set_errornumber(self, errnum);
			if (cmd_success)
			{
				self->original_client_encoding = strdup(wenc);
				self->ccsc = pg_CS_code(self->original_client_encoding);
				free(encstr);
				return;
			}
		}
	}
#endif /* UNICODE_SUPPORT */
	if (encstr)
	{
		self->original_client_encoding = encstr;
		self->ccsc = pg_CS_code(encstr);
		qlog("    [ Client encoding = '%s' (code = %d) ]\n", self->original_client_encoding, self->ccsc);
		if (stricmp(pg_CS_name(self->ccsc), encstr))
		{
			qlog(" Client encoding = '%s' and %s\n", self->original_client_encoding, pg_CS_name(self->ccsc));
			CC_set_error(self, CONN_VALUE_OUT_OF_RANGE, "client encoding mismatch", func); 
		}
	}
	else
	{
		self->ccsc = SQL_ASCII;
		self->original_client_encoding = NULL;
	}
	self->mb_maxbyte_per_char = pg_mb_maxlen(self->ccsc);
}

void encoded_str_constr(encoded_str *encstr, int ccsc, const char *str)
{
	encstr->ccsc = ccsc;
	encstr->encstr = str;
	encstr->pos = -1;
	encstr->ccst = 0;
}
int encoded_nextchar(encoded_str *encstr)
{
	int	chr;

	chr = encstr->encstr[++encstr->pos]; 
	encstr->ccst = pg_CS_stat(encstr->ccst, (unsigned int) chr, encstr->ccsc);
	return chr; 
}
int encoded_position_shift(encoded_str *encstr, int shift)
{
	encstr->pos += shift; 
	return encstr->pos; 
}
int encoded_byte_check(encoded_str *encstr, int abspos)
{
	int	chr;

	chr = encstr->encstr[encstr->pos = abspos]; 
	encstr->ccst = pg_CS_stat(encstr->ccst, (unsigned int) chr, encstr->ccsc);
	return chr; 
}
