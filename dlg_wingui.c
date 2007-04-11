#ifdef	WIN32
/*-------
 * Module:			dlg_wingui.c
 *
 * Description:		This module contains any specific code for handling
 *					dialog boxes such as driver/datasource options.  Both the
 *					ConfigDSN() and the SQLDriverConnect() functions use
 *					functions in this module.  If you were to add a new option
 *					to any dialog box, you would most likely only have to change
 *					things in here rather than in 2 separate places as before.
 *
 * Classes:			none
 *
 * API functions:	none
 *
 * Comments:		See "notice.txt" for copyright and license information.
 *-------
 */
/* Multibyte support	Eiji Tokuya 2001-03-15 */

#include "dlg_specific.h"
#include "win_setup.h"

#include "convert.h"
#include "loadlib.h"

#include "multibyte.h"
#include "pgapifunc.h"

extern GLOBAL_VALUES globals;

extern HINSTANCE NEAR s_hModule;
static int	driver_optionsDraw(HWND, const ConnInfo *, int src, BOOL enable);
static int	driver_options_update(HWND hdlg, ConnInfo *ci, const char *);

void
SetDlgStuff(HWND hdlg, const ConnInfo *ci)
{
	char	buff[MEDIUM_REGISTRY_LEN + 1];
	BOOL	libpq_exist = FALSE;

	/*
	 * If driver attribute NOT present, then set the datasource name and
	 * description
	 */
	SetDlgItemText(hdlg, IDC_DSNAME, ci->dsn);
	SetDlgItemText(hdlg, IDC_DESC, ci->desc);

	SetDlgItemText(hdlg, IDC_DATABASE, ci->database);
	SetDlgItemText(hdlg, IDC_SERVER, ci->server);
	SetDlgItemText(hdlg, IDC_USER, ci->username);
	SetDlgItemText(hdlg, IDC_PASSWORD, ci->password);
	SetDlgItemText(hdlg, IDC_PORT, ci->port);

	libpq_exist = LIBPQ_check();
mylog("libpq_exist=%d\n", libpq_exist);
	if (libpq_exist)
		ShowWindow(GetDlgItem(hdlg, IDC_NOTICE_USER), SW_HIDE);
	else
	{
mylog("SendMessage CTL_COLOR\n");
		SendMessage(GetDlgItem(hdlg, IDC_NOTICE_USER), WM_CTLCOLOR, 0, 0);
	}
	LoadString(GetWindowInstance(hdlg), IDS_SSLREQUEST_DISABLE, buff, MEDIUM_REGISTRY_LEN);
	SendDlgItemMessage(hdlg, IDC_SSLMODE, CB_ADDSTRING, 0, (WPARAM) buff);
	if (libpq_exist || (ci->sslmode[0] && stricmp(ci->sslmode, "disable")))
	{
		LoadString(GetWindowInstance(hdlg), IDS_SSLREQUEST_PREFER, buff, MEDIUM_REGISTRY_LEN);
		SendDlgItemMessage(hdlg, IDC_SSLMODE, CB_ADDSTRING, 0, (WPARAM) buff);
		LoadString(GetWindowInstance(hdlg), IDS_SSLREQUEST_ALLOW, buff, MEDIUM_REGISTRY_LEN);
		SendDlgItemMessage(hdlg, IDC_SSLMODE, CB_ADDSTRING, 0, (WPARAM) buff);
		LoadString(GetWindowInstance(hdlg), IDS_SSLREQUEST_REQUIRE, buff, MEDIUM_REGISTRY_LEN);
		SendDlgItemMessage(hdlg, IDC_SSLMODE, CB_ADDSTRING, 0, (WPARAM) buff);
	}
	if (!stricmp(ci->sslmode, "allow"))
		LoadString(GetWindowInstance(hdlg), IDS_SSLREQUEST_ALLOW, buff, MEDIUM_REGISTRY_LEN);
	else if (!stricmp(ci->sslmode, "require"))
		LoadString(GetWindowInstance(hdlg), IDS_SSLREQUEST_REQUIRE, buff, MEDIUM_REGISTRY_LEN);
	else if (!stricmp(ci->sslmode, "prefer"))
		LoadString(GetWindowInstance(hdlg), IDS_SSLREQUEST_PREFER, buff, MEDIUM_REGISTRY_LEN);
	else
		LoadString(GetWindowInstance(hdlg), IDS_SSLREQUEST_DISABLE, buff, MEDIUM_REGISTRY_LEN);

	SendDlgItemMessage(hdlg, IDC_SSLMODE, CB_SELECTSTRING, -1, (WPARAM) ((LPSTR) buff));
}


void
GetDlgStuff(HWND hdlg, ConnInfo *ci)
{
	int	sslposition;

	GetDlgItemText(hdlg, IDC_DESC, ci->desc, sizeof(ci->desc));

	GetDlgItemText(hdlg, IDC_DATABASE, ci->database, sizeof(ci->database));
	GetDlgItemText(hdlg, IDC_SERVER, ci->server, sizeof(ci->server));
	GetDlgItemText(hdlg, IDC_USER, ci->username, sizeof(ci->username));
	GetDlgItemText(hdlg, IDC_PASSWORD, ci->password, sizeof(ci->password));
	GetDlgItemText(hdlg, IDC_PORT, ci->port, sizeof(ci->port));
	sslposition = (int)(DWORD)SendMessage(GetDlgItem(hdlg, IDC_SSLMODE), CB_GETCURSEL, 0L, 0L);
	switch (sslposition)
	{
		case 1:	strncpy(ci->sslmode, "prefer", sizeof(ci->sslmode));
			break;
		case 2:	strncpy(ci->sslmode, "allow", sizeof(ci->sslmode));
			break;
		case 3:	strncpy(ci->sslmode, "require", sizeof(ci->sslmode));
			break;
		default:strncpy(ci->sslmode, "disable", sizeof(ci->sslmode));
			break;
	}
}


static int
driver_optionsDraw(HWND hdlg, const ConnInfo *ci, int src, BOOL enable)
{
	const GLOBAL_VALUES *comval;
	static BOOL defset = FALSE;
	static GLOBAL_VALUES defval;

	switch (src)
	{
		case 0:			/* driver common */
			comval = &globals;
			break;
		case 1:			/* dsn specific */
			comval = &(ci->drivers);
			break;
		case 2:			/* default */
			if (!defset)
			{
				defval.commlog = DEFAULT_COMMLOG;
				defval.disable_optimizer = DEFAULT_OPTIMIZER;
				defval.ksqo = DEFAULT_KSQO;
				defval.unique_index = DEFAULT_UNIQUEINDEX;
				defval.onlyread = DEFAULT_READONLY;
				defval.use_declarefetch = DEFAULT_USEDECLAREFETCH;

				defval.parse = DEFAULT_PARSE;
				defval.cancel_as_freestmt = DEFAULT_CANCELASFREESTMT;
				defval.debug = DEFAULT_DEBUG;

				/* Unknown Sizes */
				defval.unknown_sizes = DEFAULT_UNKNOWNSIZES;
				defval.text_as_longvarchar = DEFAULT_TEXTASLONGVARCHAR;
				defval.unknowns_as_longvarchar = DEFAULT_UNKNOWNSASLONGVARCHAR;
				defval.bools_as_char = DEFAULT_BOOLSASCHAR;
			}
			defset = TRUE;
			comval = &defval;
			break;
	}

	ShowWindow(GetDlgItem(hdlg, DRV_MSG_LABEL2), enable ? SW_SHOW : SW_HIDE);
	CheckDlgButton(hdlg, DRV_COMMLOG, comval->commlog);
#ifndef Q_LOG
	EnableWindow(GetDlgItem(hdlg, DRV_COMMLOG), FALSE);
#endif /* Q_LOG */
	CheckDlgButton(hdlg, DRV_OPTIMIZER, comval->disable_optimizer);
	CheckDlgButton(hdlg, DRV_KSQO, comval->ksqo);
	CheckDlgButton(hdlg, DRV_UNIQUEINDEX, comval->unique_index);
	/* EnableWindow(GetDlgItem(hdlg, DRV_UNIQUEINDEX), enable); */
	CheckDlgButton(hdlg, DRV_READONLY, comval->onlyread);
	EnableWindow(GetDlgItem(hdlg, DRV_READONLY), enable);
	CheckDlgButton(hdlg, DRV_USEDECLAREFETCH, comval->use_declarefetch);

	/* Unknown Sizes clear */
	CheckDlgButton(hdlg, DRV_UNKNOWN_DONTKNOW, 0);
	CheckDlgButton(hdlg, DRV_UNKNOWN_LONGEST, 0);
	CheckDlgButton(hdlg, DRV_UNKNOWN_MAX, 0);
	/* Unknown (Default) Data Type sizes */
	switch (comval->unknown_sizes)
	{
		case UNKNOWNS_AS_DONTKNOW:
			CheckDlgButton(hdlg, DRV_UNKNOWN_DONTKNOW, 1);
			break;
		case UNKNOWNS_AS_LONGEST:
			CheckDlgButton(hdlg, DRV_UNKNOWN_LONGEST, 1);
			break;
		case UNKNOWNS_AS_MAX:
		default:
			CheckDlgButton(hdlg, DRV_UNKNOWN_MAX, 1);
			break;
	}

	CheckDlgButton(hdlg, DRV_TEXT_LONGVARCHAR, comval->text_as_longvarchar);
	CheckDlgButton(hdlg, DRV_UNKNOWNS_LONGVARCHAR, comval->unknowns_as_longvarchar);
	CheckDlgButton(hdlg, DRV_BOOLS_CHAR, comval->bools_as_char);
	CheckDlgButton(hdlg, DRV_PARSE, comval->parse);
	CheckDlgButton(hdlg, DRV_CANCELASFREESTMT, comval->cancel_as_freestmt);
	CheckDlgButton(hdlg, DRV_DEBUG, comval->debug);
#ifndef MY_LOG
	EnableWindow(GetDlgItem(hdlg, DRV_DEBUG), FALSE);
#endif /* MY_LOG */
	SetDlgItemInt(hdlg, DRV_CACHE_SIZE, comval->fetch_max, FALSE);
	SetDlgItemInt(hdlg, DRV_VARCHAR_SIZE, comval->max_varchar_size, FALSE);
	SetDlgItemInt(hdlg, DRV_LONGVARCHAR_SIZE, comval->max_longvarchar_size, TRUE);
	SetDlgItemText(hdlg, DRV_EXTRASYSTABLEPREFIXES, comval->extra_systable_prefixes);

	/* Driver Connection Settings */
	SetDlgItemText(hdlg, DRV_CONNSETTINGS, comval->conn_settings);
	EnableWindow(GetDlgItem(hdlg, DRV_CONNSETTINGS), enable);
	ShowWindow(GetDlgItem(hdlg, IDPREVPAGE), enable ? SW_HIDE : SW_SHOW);
	ShowWindow(GetDlgItem(hdlg, IDNEXTPAGE), enable ? SW_HIDE : SW_SHOW);
	return 0;
}

static int
driver_options_update(HWND hdlg, ConnInfo *ci, const char *updateDriver)
{
	GLOBAL_VALUES *comval;

	if (ci)
		comval = &(ci->drivers);
	else
		comval = &globals;
	comval->commlog = IsDlgButtonChecked(hdlg, DRV_COMMLOG);
	comval->disable_optimizer = IsDlgButtonChecked(hdlg, DRV_OPTIMIZER);
	comval->ksqo = IsDlgButtonChecked(hdlg, DRV_KSQO);
	comval->unique_index = IsDlgButtonChecked(hdlg, DRV_UNIQUEINDEX);
	if (!ci)
	{
		comval->onlyread = IsDlgButtonChecked(hdlg, DRV_READONLY);
	}
	comval->use_declarefetch = IsDlgButtonChecked(hdlg, DRV_USEDECLAREFETCH);

	/* Unknown (Default) Data Type sizes */
	if (IsDlgButtonChecked(hdlg, DRV_UNKNOWN_MAX))
		comval->unknown_sizes = UNKNOWNS_AS_MAX;
	else if (IsDlgButtonChecked(hdlg, DRV_UNKNOWN_DONTKNOW))
		comval->unknown_sizes = UNKNOWNS_AS_DONTKNOW;
	else if (IsDlgButtonChecked(hdlg, DRV_UNKNOWN_LONGEST))
		comval->unknown_sizes = UNKNOWNS_AS_LONGEST;
	else
		comval->unknown_sizes = UNKNOWNS_AS_MAX;

	comval->text_as_longvarchar = IsDlgButtonChecked(hdlg, DRV_TEXT_LONGVARCHAR);
	comval->unknowns_as_longvarchar = IsDlgButtonChecked(hdlg, DRV_UNKNOWNS_LONGVARCHAR);
	comval->bools_as_char = IsDlgButtonChecked(hdlg, DRV_BOOLS_CHAR);

	comval->parse = IsDlgButtonChecked(hdlg, DRV_PARSE);

	comval->cancel_as_freestmt = IsDlgButtonChecked(hdlg, DRV_CANCELASFREESTMT);
	comval->debug = IsDlgButtonChecked(hdlg, DRV_DEBUG);

	comval->fetch_max = GetDlgItemInt(hdlg, DRV_CACHE_SIZE, NULL, FALSE);
	comval->max_varchar_size = GetDlgItemInt(hdlg, DRV_VARCHAR_SIZE, NULL, FALSE);
	comval->max_longvarchar_size = GetDlgItemInt(hdlg, DRV_LONGVARCHAR_SIZE, NULL, TRUE);		/* allows for
																								 * SQL_NO_TOTAL */

	GetDlgItemText(hdlg, DRV_EXTRASYSTABLEPREFIXES, comval->extra_systable_prefixes, sizeof(comval->extra_systable_prefixes));

	/* Driver Connection Settings */
	if (!ci)
		GetDlgItemText(hdlg, DRV_CONNSETTINGS, comval->conn_settings, sizeof(comval->conn_settings));

	if (updateDriver)
	{
		if (writeDriverCommoninfo(ODBCINST_INI, updateDriver, comval) < 0)
			MessageBox(hdlg, "impossible to update the values, sorry", "Update Error", MB_ICONEXCLAMATION | MB_OK);
;
	}

	/* fall through */
	return 0;
}

LRESULT		CALLBACK
driver_optionsProc(HWND hdlg,
				   UINT wMsg,
				   WPARAM wParam,
				   LPARAM lParam)
{
	ConnInfo   *ci;
	char	strbuf[128];

	switch (wMsg)
	{
		case WM_INITDIALOG:
			SetWindowLongPtr(hdlg, DWLP_USER, lParam);		/* save for OK etc */
			ci = (ConnInfo *) lParam;
			LoadString(s_hModule, IDS_ADVANCE_OPTION_DEF, strbuf, sizeof(strbuf)); 
			SetWindowText(hdlg, strbuf);
			LoadString(s_hModule, IDS_ADVANCE_SAVE, strbuf, sizeof(strbuf)); 
			SetWindowText(GetDlgItem(hdlg, IDOK), strbuf);
			ShowWindow(GetDlgItem(hdlg, IDAPPLY), SW_HIDE);
			driver_optionsDraw(hdlg, ci, 0, TRUE);
			break;

		case WM_COMMAND:
			switch (GET_WM_COMMAND_ID(wParam, lParam))
			{
				case IDOK:
					ci = (ConnInfo *) GetWindowLongPtr(hdlg, DWLP_USER);
					driver_options_update(hdlg, NULL,
						ci ? ci->drivername : NULL);

				case IDCANCEL:
					EndDialog(hdlg, GET_WM_COMMAND_ID(wParam, lParam) == IDOK);
					return TRUE;

				case IDDEFAULTS:
					driver_optionsDraw(hdlg, NULL, 2, TRUE);
					break;
			}
	}

	return FALSE;
}

#ifdef _HANDLE_ENLIST_IN_DTC_
static
HMODULE DtcProc(const char *procname, FARPROC *proc)
{
	HMODULE	hmodule;

	*proc = NULL;
	if (hmodule = LoadLibrary(GetXaLibPath()), NULL != hmodule)
	{
mylog("GetProcAddres for %s\n", procname);
		*proc = GetProcAddress(hmodule, procname);
	}

	return hmodule;
}
#endif /* _HANDLE_ENLIST_IN_DTC_ */

LRESULT			CALLBACK
global_optionsProc(HWND hdlg,
				   UINT wMsg,
				   WPARAM wParam,
				   LPARAM lParam)
{
	HMODULE	hmodule;
	FARPROC	proc;

	switch (wMsg)
	{
		case WM_INITDIALOG:
			CheckDlgButton(hdlg, DRV_COMMLOG, globals.commlog);
#ifndef Q_LOG
			EnableWindow(GetDlgItem(hdlg, DRV_COMMLOG), FALSE);
#endif /* Q_LOG */
			CheckDlgButton(hdlg, DRV_DEBUG, globals.debug);
#ifndef MY_LOG
			EnableWindow(GetDlgItem(hdlg, DRV_DEBUG), FALSE);
#endif /* MY_LOG */
#ifdef _HANDLE_ENLIST_IN_DTC_
			hmodule = DtcProc("GetMsdtclog", &proc);
			if (proc)
			{
				INT_PTR res = (*proc)();
				CheckDlgButton(hdlg, DRV_DTCLOG, 0 != res);
			}
			else
				EnableWindow(GetDlgItem(hdlg, DRV_DTCLOG), FALSE);
			if (hmodule)
				FreeLibrary(hmodule);
#else
			ShowWindow(GetDlgItem(hdlg, DRV_DTCLOG), SW_HIDE);
#endif /* _HANDLE_ENLIST_IN_DTC_ */
			break;

		case WM_COMMAND:
			switch (GET_WM_COMMAND_ID(wParam, lParam))
			{
				case IDOK:
					globals.commlog = IsDlgButtonChecked(hdlg, DRV_COMMLOG);
					globals.debug = IsDlgButtonChecked(hdlg, DRV_DEBUG);
					if (writeDriverCommoninfo(ODBCINST_INI, NULL, &globals) < 0)
						MessageBox(hdlg, "Sorry, impossible to update the values\nWrite permission seems to be needed", "Update Error", MB_ICONEXCLAMATION | MB_OK);
#ifdef _HANDLE_ENLIST_IN_DTC_
			hmodule = DtcProc("SetMsdtclog", &proc);
			if (proc)
				(*proc)(IsDlgButtonChecked(hdlg, DRV_DTCLOG));
			if (hmodule)
				FreeLibrary(hmodule);
#endif /* _HANDLE_ENLIST_IN_DTC_ */

				case IDCANCEL:
					EndDialog(hdlg, GET_WM_COMMAND_ID(wParam, lParam) == IDOK);
					return TRUE;
			}
	}

	return FALSE;
}

LRESULT			CALLBACK
ds_options1Proc(HWND hdlg,
				   UINT wMsg,
				   WPARAM wParam,
				   LPARAM lParam)
{
	ConnInfo   *ci;
	char	strbuf[128];

	switch (wMsg)
	{
		case WM_INITDIALOG:
			SetWindowLongPtr(hdlg, DWLP_USER, lParam);		/* save for OK etc */
			ci = (ConnInfo *) lParam;
			if (ci && ci->dsn && ci->dsn[0])
			{
				DWORD	cmd;
				char	fbuf[64];

				cmd = LoadString(s_hModule,
						IDS_ADVANCE_OPTION_DSN1,
						fbuf,
						sizeof(fbuf));
				if (cmd <= 0)
					strcpy(fbuf, "Advanced Options (%s) 1/2");
				sprintf(strbuf, fbuf, ci->dsn);
				SetWindowText(hdlg, strbuf);
			}
			else
			{
				LoadString(s_hModule, IDS_ADVANCE_OPTION_CON1, strbuf, sizeof(strbuf)); 
				SetWindowText(hdlg, strbuf);
				ShowWindow(GetDlgItem(hdlg, IDAPPLY), SW_HIDE);
			}
			driver_optionsDraw(hdlg, ci, 1, FALSE);
			break;

		case WM_COMMAND:
			ci = (ConnInfo *) GetWindowLongPtr(hdlg, DWLP_USER);
			switch (GET_WM_COMMAND_ID(wParam, lParam))
			{
				case IDOK:
					driver_options_update(hdlg, ci, NULL);

				case IDCANCEL:
					EndDialog(hdlg, GET_WM_COMMAND_ID(wParam, lParam) == IDOK);
					return TRUE;

				case IDAPPLY:
					driver_options_update(hdlg, ci, NULL);
					SendMessage(GetWindow(hdlg, GW_OWNER), WM_COMMAND, wParam, lParam);
					break;

				case IDDEFAULTS:
					driver_optionsDraw(hdlg, ci, 0, FALSE);
					break;

				case IDNEXTPAGE:
					driver_options_update(hdlg, ci, NULL);

					EndDialog(hdlg, FALSE);
					DialogBoxParam(s_hModule,
						MAKEINTRESOURCE(DLG_OPTIONS_DS),
                                                 	hdlg, ds_options2Proc, (LPARAM)
ci);
					break;
			}
	}

	return FALSE;
}


LRESULT			CALLBACK
ds_options2Proc(HWND hdlg,
			   UINT wMsg,
			   WPARAM wParam,
			   LPARAM lParam)
{
	ConnInfo   *ci;
	char		buf[128];
	DWORD		cmd;
	BOOL		enable;

	switch (wMsg)
	{
		case WM_INITDIALOG:
			ci = (ConnInfo *) lParam;
			SetWindowLongPtr(hdlg, DWLP_USER, lParam);		/* save for OK */

			/* Change window caption */
			if (ci && ci->dsn && ci->dsn[0])
			{
				char	fbuf[64];

				cmd = LoadString(s_hModule,
						IDS_ADVANCE_OPTION_DSN2,
						fbuf,
						sizeof(fbuf));
				if (cmd <= 0)
					strcpy(fbuf, "Advanced Options (%s) 2/2");
				sprintf(buf, fbuf, ci->dsn);
				SetWindowText(hdlg, buf);
			}
			else
			{
				LoadString(s_hModule, IDS_ADVANCE_OPTION_CON2, buf, sizeof(buf)); 
				SetWindowText(hdlg, buf);
				ShowWindow(GetDlgItem(hdlg, IDAPPLY), SW_HIDE);				}

			/* Readonly */
			CheckDlgButton(hdlg, DS_READONLY, atoi(ci->onlyread));

			/* Protocol */
			enable = (ci->sslmode[0] == 'd' || ci->username[0] == '\0');
			EnableWindow(GetDlgItem(hdlg, DS_PG62), enable);
			EnableWindow(GetDlgItem(hdlg, DS_PG63), enable);
			EnableWindow(GetDlgItem(hdlg, DS_PG64), enable);
			EnableWindow(GetDlgItem(hdlg, DS_PG74), enable);
			if (PROTOCOL_62(ci))
				CheckDlgButton(hdlg, DS_PG62, 1);
			else if (PROTOCOL_63(ci))
				CheckDlgButton(hdlg, DS_PG63, 1);
			else if (PROTOCOL_64(ci))
				CheckDlgButton(hdlg, DS_PG64, 1);
			else
				/* latest */
				CheckDlgButton(hdlg, DS_PG74, 1);

			/* How to issue Rollback */
			switch (ci->rollback_on_error)
			{
				case 0:
					CheckDlgButton(hdlg, DS_NO_ROLLBACK, 1);
					break;
				case 1:
					CheckDlgButton(hdlg, DS_TRANSACTION_ROLLBACK, 1);
					break;
				case 2:
					CheckDlgButton(hdlg, DS_STATEMENT_ROLLBACK, 1);
					break;
			}

			/* Int8 As */
			switch (ci->int8_as)
			{
				case SQL_BIGINT:
					CheckDlgButton(hdlg, DS_INT8_AS_BIGINT, 1);
					break;
				case SQL_NUMERIC:
					CheckDlgButton(hdlg, DS_INT8_AS_NUMERIC, 1);
					break;
				case SQL_VARCHAR:
					CheckDlgButton(hdlg, DS_INT8_AS_VARCHAR, 1);
					break;
				case SQL_DOUBLE:
					CheckDlgButton(hdlg, DS_INT8_AS_DOUBLE, 1);
					break;
				case SQL_INTEGER:
					CheckDlgButton(hdlg, DS_INT8_AS_INT4, 1);
					break;
				default:
					CheckDlgButton(hdlg, DS_INT8_AS_DEFAULT, 1);
			}
			sprintf(buf, "0x%x", getExtraOptions(ci));
			SetDlgItemText(hdlg, DS_EXTRA_OPTIONS, buf);

			CheckDlgButton(hdlg, DS_SHOWOIDCOLUMN, atoi(ci->show_oid_column));
			CheckDlgButton(hdlg, DS_FAKEOIDINDEX, atoi(ci->fake_oid_index));
			CheckDlgButton(hdlg, DS_ROWVERSIONING, atoi(ci->row_versioning));
			CheckDlgButton(hdlg, DS_SHOWSYSTEMTABLES, atoi(ci->show_system_tables));
			CheckDlgButton(hdlg, DS_DISALLOWPREMATURE, ci->disallow_premature);
			CheckDlgButton(hdlg, DS_LFCONVERSION, ci->lf_conversion);
			CheckDlgButton(hdlg, DS_TRUEISMINUS1, ci->true_is_minus1);
			CheckDlgButton(hdlg, DS_UPDATABLECURSORS, ci->allow_keyset);
			CheckDlgButton(hdlg, DS_SERVERSIDEPREPARE, ci->use_server_side_prepare);
			CheckDlgButton(hdlg, DS_BYTEAASLONGVARBINARY, ci->bytea_as_longvarbinary);
			/*CheckDlgButton(hdlg, DS_LOWERCASEIDENTIFIER, ci->lower_case_identifier);*/

			EnableWindow(GetDlgItem(hdlg, DS_FAKEOIDINDEX), atoi(ci->show_oid_column));

			/* Datasource Connection Settings */
			SetDlgItemText(hdlg, DS_CONNSETTINGS, ci->conn_settings);
			break;

		case WM_COMMAND:
			switch (cmd = GET_WM_COMMAND_ID(wParam, lParam))
			{
				case DS_SHOWOIDCOLUMN:
					mylog("WM_COMMAND: DS_SHOWOIDCOLUMN\n");
					EnableWindow(GetDlgItem(hdlg, DS_FAKEOIDINDEX), IsDlgButtonChecked(hdlg, DS_SHOWOIDCOLUMN));
					return TRUE;

				case IDOK:
				case IDAPPLY:
				case IDPREVPAGE:
					ci = (ConnInfo *) GetWindowLongPtr(hdlg, DWLP_USER);
					mylog("IDOK: got ci = %x\n", ci);

					/* Readonly */
					sprintf(ci->onlyread, "%d", IsDlgButtonChecked(hdlg, DS_READONLY));

					/* Protocol */
					if (IsDlgButtonChecked(hdlg, DS_PG62))
						strcpy(ci->protocol, PG62);
					else if (IsDlgButtonChecked(hdlg, DS_PG63))
						strcpy(ci->protocol, PG63);
					else if (IsDlgButtonChecked(hdlg, DS_PG64))
						strcpy(ci->protocol, PG64);
					else
						/* latest */
						strcpy(ci->protocol, PG74);

					/* Issue rollback command on error */
					if (IsDlgButtonChecked(hdlg, DS_NO_ROLLBACK))
						ci->rollback_on_error = 0;
					else if (IsDlgButtonChecked(hdlg, DS_TRANSACTION_ROLLBACK))
						ci->rollback_on_error = 1;
					else if (IsDlgButtonChecked(hdlg, DS_STATEMENT_ROLLBACK))
						ci->rollback_on_error = 2;
					else
						/* legacy */
						ci->rollback_on_error = 1;

					/* Int8 As */
					if (IsDlgButtonChecked(hdlg, DS_INT8_AS_DEFAULT))
						ci->int8_as = 0;
					else if (IsDlgButtonChecked(hdlg, DS_INT8_AS_BIGINT))
						ci->int8_as = SQL_BIGINT;
					else if (IsDlgButtonChecked(hdlg, DS_INT8_AS_NUMERIC))
						ci->int8_as = SQL_NUMERIC;
					else if (IsDlgButtonChecked(hdlg, DS_INT8_AS_DOUBLE))
						ci->int8_as = SQL_DOUBLE;
					else if (IsDlgButtonChecked(hdlg, DS_INT8_AS_INT4))
						ci->int8_as = SQL_INTEGER;
					else
						ci->int8_as = SQL_VARCHAR;

					GetDlgItemText(hdlg, DS_EXTRA_OPTIONS, buf, sizeof(buf));
					setExtraOptions(ci, buf, NULL);
					sprintf(ci->show_system_tables, "%d", IsDlgButtonChecked(hdlg, DS_SHOWSYSTEMTABLES));

					sprintf(ci->row_versioning, "%d", IsDlgButtonChecked(hdlg, DS_ROWVERSIONING));
					ci->disallow_premature = IsDlgButtonChecked(hdlg, DS_DISALLOWPREMATURE);
					ci->lf_conversion = IsDlgButtonChecked(hdlg, DS_LFCONVERSION);
					ci->true_is_minus1 = IsDlgButtonChecked(hdlg, DS_TRUEISMINUS1);
					ci->allow_keyset = IsDlgButtonChecked(hdlg, DS_UPDATABLECURSORS);
					ci->use_server_side_prepare = IsDlgButtonChecked(hdlg, DS_SERVERSIDEPREPARE);
					ci->bytea_as_longvarbinary = IsDlgButtonChecked(hdlg, DS_BYTEAASLONGVARBINARY);
					/*ci->lower_case_identifier = IsDlgButtonChecked(hdlg, DS_LOWERCASEIDENTIFIER);*/

					/* OID Options */
					sprintf(ci->fake_oid_index, "%d", IsDlgButtonChecked(hdlg, DS_FAKEOIDINDEX));
					sprintf(ci->show_oid_column, "%d", IsDlgButtonChecked(hdlg, DS_SHOWOIDCOLUMN));

					/* Datasource Connection Settings */
					GetDlgItemText(hdlg, DS_CONNSETTINGS, ci->conn_settings, sizeof(ci->conn_settings));
					if (IDAPPLY == cmd)
					{
						SendMessage(GetWindow(hdlg, GW_OWNER), WM_COMMAND, wParam, lParam);
						break;
					}

					EndDialog(hdlg, cmd == IDOK);
					if (IDOK == cmd) 
						return TRUE;
					DialogBoxParam(s_hModule,
						MAKEINTRESOURCE(DLG_OPTIONS_DRV),
                                         	hdlg, ds_options1Proc, (LPARAM) ci);
					break;

				case IDCANCEL:
					EndDialog(hdlg, GET_WM_COMMAND_ID(wParam, lParam) == IDOK);
					return TRUE;
			}
	}

	return FALSE;
}

typedef	SQLRETURN (SQL_API *SQLAPIPROC)();
static int
makeDriversList(HWND lwnd, const ConnInfo *ci)
{
	HMODULE	hmodule;
	SQLHENV	henv;
	int	lcount = 0;
	LRESULT iidx;
	char	drvname[64], drvatt[128];
	SQLUSMALLINT	direction = SQL_FETCH_FIRST;
	SQLSMALLINT	drvncount, drvacount;
	SQLRETURN	ret;
	SQLAPIPROC	addr;

	hmodule = GetModuleHandle("ODBC32");
	if (!hmodule)	return lcount;
	addr = (SQLAPIPROC) GetProcAddress(hmodule, "SQLAllocEnv");
	if (!addr)	return lcount;
	ret = (*addr)(&henv);
	if (SQL_SUCCESS != ret)	return lcount;
	do
	{
		ret = SQLDrivers(henv, direction,
			drvname, sizeof(drvname), &drvncount, 
			drvatt, sizeof(drvatt), &drvacount); 
		if (SQL_SUCCESS != ret && SQL_SUCCESS_WITH_INFO != ret)
			break;
		if (strnicmp(drvname, "postgresql", 10) == 0)
		{
			iidx = SendMessage(lwnd, LB_ADDSTRING, 0, (LPARAM) drvname);
			if (LB_ERR != iidx && stricmp(drvname, ci->drivername) == 0)
{
				SendMessage(lwnd, LB_SETCURSEL, (WPARAM) iidx, (LPARAM) 0);
}
			lcount++;
		}
		direction = SQL_FETCH_NEXT;
	} while (1);
	addr = (SQLAPIPROC) GetProcAddress(hmodule, "SQLFreeEnv");
	if (addr)
		(*addr)(henv);

	return lcount;
}

LRESULT		CALLBACK
manage_dsnProc(HWND hdlg, UINT wMsg,
		WPARAM wParam, LPARAM lParam)
{
	LPSETUPDLG	lpsetupdlg;
	ConnInfo	*ci;
	HWND		lwnd;
	LRESULT		sidx;
	char		drvname[64];

	switch (wMsg)
	{
		case WM_INITDIALOG:
			SetWindowLongPtr(hdlg, DWLP_USER, lParam);
			lpsetupdlg = (LPSETUPDLG) lParam;
			ci = &lpsetupdlg->ci;
			lwnd = GetDlgItem(hdlg, IDC_DRIVER_LIST);
			makeDriversList(lwnd, ci);
			break;

		case WM_COMMAND:
			switch (GET_WM_COMMAND_ID(wParam, lParam))
			{
				case IDOK:
					lpsetupdlg = (LPSETUPDLG) GetWindowLongPtr(hdlg, DWLP_USER);
					lwnd = GetDlgItem(hdlg, IDC_DRIVER_LIST);
					sidx = SendMessage(lwnd, LB_GETCURSEL,
						(WPARAM) 0, (LPARAM) 0);
					if (LB_ERR == sidx)
						return FALSE;
					sidx = SendMessage(lwnd, LB_GETTEXT,
						(WPARAM) sidx, (LPARAM) drvname);
					if (LB_ERR == sidx)
						return FALSE;
					ChangeDriverName(hdlg, lpsetupdlg, drvname);

				case IDCANCEL:
					EndDialog(hdlg, GET_WM_COMMAND_ID(wParam, lParam) == IDOK);
					return TRUE;
			}
	}

	return FALSE;
}

#endif /* WIN32 */
