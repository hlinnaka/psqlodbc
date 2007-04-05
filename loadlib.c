/*------
 * Module:			loadlib.c
 *
 * Description:		This module contains routines related to
 *			delay load import libraries.
 *			
 * Comments:		See "notice.txt" for copyright and license information.
 *-------
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#ifndef	WIN32
#include <errno.h>
#endif /* WIN32 */
#include <libpq-fe.h>
#include "loadlib.h"
#include "pgenlist.h"

#ifdef  WIN32
#ifdef  _MSC_VER
#pragma comment(lib, "Delayimp")
#pragma comment(lib, "libpq")
#pragma comment(lib, "ssleay32")
#ifdef	UNICODE_SUPPORT
#pragma comment(lib, "pgenlist")
#else
#pragma comment(lib, "pgenlista")
#endif /* UNICODE_SUPPORT */
// The followings works under VC++6.0 but doesn't work under VC++7.0.
// Please add the equivalent linker options using command line etc.
#if (_MSC_VER == 1200) && defined(DYNAMIC_LOAD) // VC6.0
#pragma comment(linker, "/Delayload:libpq.dll")
#pragma comment(linker, "/Delayload:ssleay32.dll")
#ifdef	UNICODE_SUPPORT
#pragma comment(linker, "/Delayload:pgenlist.dll")
#else
#pragma comment(linker, "/Delayload:pgenlista.dll")
#endif /* UNICODE_SUPPORT */
#pragma comment(linker, "/Delay:UNLOAD")
#endif /* _MSC_VER */
#endif /* _MSC_VER */
#if defined(DYNAMIC_LOAD)
#define	WIN_DYN_LOAD
CSTR	libpq = "libpq";
CSTR	libpqdll = "LIBPQ.dll";
#ifdef	UNICODE_SUPPORT
CSTR	pgenlist = "pgenlist";
CSTR	pgenlistdll = "PGENLIST.dll";
#else
CSTR	pgenlist = "pgenlista";
CSTR	pgenlistdll = "PGENLISTA.dll";
#endif /* UNICODE_SUPPORT */
#if defined(_MSC_VER) && (_MSC_VER >= 1200)
#define	_MSC_DELAY_LOAD_IMPORT
#endif /* MSC_VER */
#endif /* DYNAMIC_LOAD */
#endif /* WIN32 */

#if defined(_MSC_DELAY_LOAD_IMPORT)
static BOOL	loaded_libpq = FALSE, loaded_ssllib = FALSE;
static BOOL	loaded_pgenlist = FALSE;
/*
 *	Load psqlodbc path based libpq dll.
 */
static HMODULE MODULE_load_from_psqlodbc_path(const char *module_name)
{
	extern	HINSTANCE	s_hModule;
	HMODULE	hmodule = NULL;
	char	szFileName[MAX_PATH];

	if (GetModuleFileName(s_hModule, szFileName, sizeof(szFileName)) > 0)
	{
		char drive[_MAX_DRIVE], dir[_MAX_DIR], sysdir[MAX_PATH];

		_splitpath(szFileName, drive, dir, NULL, NULL);
		GetSystemDirectory(sysdir, MAX_PATH);
		snprintf(szFileName, sizeof(szFileName), "%s%s%s.dll", drive, dir, module_name);
		if (strnicmp(szFileName, sysdir, strlen(sysdir)) != 0)
		{
			hmodule = LoadLibraryEx(szFileName, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
			mylog("psqlodbc path based %s loaded module=%p\n", module_name, hmodule);
		}
	}
	return hmodule;
}

/*
 *	Error hook function for delay load import.
 *	Try to load psqlodbc path based libpq.
 *	Load alternative ssl library SSLEAY32 or LIBSSL32. 
 */
#if (_MSC_VER < 1300)
extern PfnDliHook __pfnDliFailureHook;
extern PfnDliHook __pfnDliNotifyHook;
#else
extern PfnDliHook __pfnDliFailureHook2;
extern PfnDliHook __pfnDliNotifyHook2;
#endif /* _MSC_VER */

static FARPROC WINAPI
DliErrorHook(unsigned	dliNotify,
		PDelayLoadInfo	pdli)
{
	HMODULE	hmodule = NULL;
	int	i;
	static const char * const libarray[] = {"libssl32", "ssleay32"};

	mylog("Dli%sHook Notify=%d %p\n", (dliFailLoadLib == dliNotify || dliFailGetProc == dliNotify) ? "Error" : "Notify", dliNotify, pdli);
	switch (dliNotify)
	{
		case dliNotePreLoadLibrary:
		case dliFailLoadLib:
#if (_MSC_VER < 1300)
			__pfnDliNotifyHook = NULL;
#else
			__pfnDliNotifyHook2 = NULL;
#endif /* _MSC_VER */
			if (strnicmp(pdli->szDll, libpq, 5) == 0)
			{
				if (hmodule = MODULE_load_from_psqlodbc_path(libpq), NULL == hmodule)
					hmodule = LoadLibrary(libpq);
			}
			else if (strnicmp(pdli->szDll, pgenlist, strlen(pgenlist)) == 0)
			{
				if (hmodule = MODULE_load_from_psqlodbc_path(pgenlist), NULL == hmodule)
					hmodule = LoadLibrary(pgenlist);
			}
			else
			{
        			mylog("getting alternative ssl library instead of %s\n", pdli->szDll);
        			for (i = 0; i < sizeof(libarray) / sizeof(const char * const); i++)
        			{
                			if (hmodule = GetModuleHandle(libarray[i]), NULL != hmodule)
						break;
				}
        		}
			break;
	}
	return (FARPROC) hmodule;
}

/*
 *	unload delay loaded libraries.
 *
 *	Openssl Library nmake defined
 *	ssleay32.dll is vc make, libssl32.dll is mingw make.
 */
#ifndef SSL_DLL
#define SSL_DLL "SSLEAY32.dll"
#endif /* SSL_DLL */

typedef BOOL (WINAPI *UnloadFunc)(LPCSTR);
void CleanupDelayLoadedDLLs(void)
{
	BOOL	success;
#if (_MSC_VER < 1300) /* VC6 DELAYLOAD IMPORT */
	UnloadFunc	func = __FUnloadDelayLoadedDLL;
#else
	UnloadFunc	func = __FUnloadDelayLoadedDLL2;
#endif
	/* The dll names are case sensitive for the unload helper */
	if (loaded_libpq)
	{
		success = (*func)(libpqdll);
		mylog("%s unload success=%d\n", libpqdll, success);
	}
	if (loaded_ssllib)
	{
		success = (*func)(SSL_DLL);
		mylog("ssldll unload success=%d\n", success);
	}
	if (loaded_pgenlist)
	{
		success = (*func)(pgenlistdll);
		mylog("%s unload success=%d\n", pgenlistdll, success);
	}
	return;
}
#else
void CleanupDelayLoadedDLLs(void)
{
	return;
}
#endif	/* _MSC_DELAY_LOAD_IMPORT */

void *CALL_PQconnectdb(const char *conninfo, BOOL *libpqLoaded)
{
	void *pqconn = NULL;
	*libpqLoaded = TRUE;
#if defined(_MSC_DELAY_LOAD_IMPORT)
	__try {
#if (_MSC_VER < 1300)
		__pfnDliFailureHook = DliErrorHook;
		__pfnDliNotifyHook = DliErrorHook;
#else
		__pfnDliFailureHook2 = DliErrorHook;
		__pfnDliNotifyHook2 = DliErrorHook;
#endif /* _MSC_VER */
		pqconn = PQconnectdb(conninfo);
	}
	__except ((GetExceptionCode() & 0xffff) == ERROR_MOD_NOT_FOUND ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
		*libpqLoaded = FALSE;
	}
#if (_MSC_VER < 1300)
	__pfnDliNotifyHook = NULL;
#else
	__pfnDliNotifyHook2 = NULL;
#endif /* _MSC_VER */
	if (*libpqLoaded)
	{
		loaded_libpq = TRUE;
		/* ssllibs are already loaded by libpq
		if (PQgetssl(pqconn))
			loaded_ssllib = TRUE;
		*/
	}
#else
	pqconn = PQconnectdb(conninfo);
#endif /* _MSC_DELAY_LOAD_IMPORT */
	return pqconn;
}

#ifdef	_HANDLE_ENLIST_IN_DTC_
RETCODE	CALL_EnlistInDtc(ConnectionClass *conn, void *pTra, int method)
{
	RETCODE	ret;
	BOOL	loaded = TRUE;
	
#if defined(_MSC_DELAY_LOAD_IMPORT)
	__try {
#if (_MSC_VER < 1300)
		__pfnDliFailureHook = DliErrorHook;
		__pfnDliNotifyHook = DliErrorHook;
#else
		__pfnDliFailureHook2 = DliErrorHook;
		__pfnDliNotifyHook2 = DliErrorHook;
#endif /* _MSC_VER */
		ret = EnlistInDtc(conn, pTra, method);
	}
	__except ((GetExceptionCode() & 0xffff) == ERROR_MOD_NOT_FOUND ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
		loaded = FALSE;
	}
	if (loaded)
		loaded_pgenlist = TRUE;
#if (_MSC_VER < 1300)
	__pfnDliNotifyHook = NULL;
#else
	__pfnDliNotifyHook2 = NULL;
#endif /* _MSC_VER */
#else
	ret = EnlistInDtc(conn, pTra, method);
	loaded_pgenlist = TRUE;
#endif /* _MSC_DELAY_LOAD_IMPORT */
	return ret;
}
RETCODE	CALL_DtcOnDisconnect(ConnectionClass *conn)
{
	if (loaded_pgenlist)
		return DtcOnDisconnect(conn);
	return FALSE;
}
RETCODE	CALL_DtcOnRelease(void)
{
	if (loaded_pgenlist)
		return DtcOnRelease();
	return FALSE;
}
#endif /* _HANDLE_ENLIST_IN_DTC_ */

#if defined(WIN_DYN_LOAD)
BOOL LIBPQ_check()
{
	extern	HINSTANCE	s_hModule;
	HMODULE	hmodule = NULL;

	mylog("checking libpq library\n");
	/* First search the driver's folder */
	if (NULL == (hmodule = MODULE_load_from_psqlodbc_path(libpq)))
		/* Second try the PATH ordinarily */
		hmodule = LoadLibrary(libpq);
	mylog("hmodule=%p\n", hmodule);
	if (hmodule)
		FreeLibrary(hmodule);
	return (NULL != hmodule);
}
#else
BOOL LIBPQ_check()
{
	return TRUE;
}
#endif	/* WIN_DYN_LOAD */
