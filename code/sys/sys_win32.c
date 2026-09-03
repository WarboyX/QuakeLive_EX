/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

// Use EnumProcesses() with Windows XP compatibility
#define PSAPI_VERSION 1

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "sys_local.h"

#include <windows.h>
#include <lmerr.h>
#include <lmcons.h>
#include <lmwksta.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <direct.h>
#include <io.h>
#include <conio.h>
#include <wincrypt.h>
#include <shlobj.h>
#include <psapi.h>
#include <float.h>

#ifndef KEY_WOW64_32KEY
#define KEY_WOW64_32KEY 0x0200
#endif

// Used to determine where to store user-specific files
static char homePath[MAX_OSPATH] = {0};

// Used to store the Steam Quake Live installation path
static char steamPath[MAX_OSPATH] = {0};

#ifndef DEDICATED
static UINT timerResolution = 0;
#endif

/*
================
Sys_SetFPUCW
Set FPU control word to default value
================
*/

#ifndef _RC_CHOP
// mingw doesn't seem to have these defined :(

#define _MCW_EM 0x0008001fU
#define _MCW_RC 0x00000300U
#define _MCW_PC 0x00030000U
#define _RC_NEAR 0x00000000U
#define _PC_53 0x00010000U

unsigned int _controlfp(unsigned int new, unsigned int mask);
#endif

#define FPUCWMASK1 (_MCW_RC | _MCW_EM)
#define FPUCW (_RC_NEAR | _MCW_EM | _PC_53)

#if idx64
#define FPUCWMASK (FPUCWMASK1)
#else
#define FPUCWMASK (FPUCWMASK1 | _MCW_PC)
#endif

void Sys_SetFloatEnv(void) {
    _controlfp(FPUCW, FPUCWMASK);
}

/*
================
Sys_DefaultHomePath
================
*/
char* Sys_DefaultHomePath(void) {
    TCHAR szPath[MAX_PATH];
    FARPROC qSHGetFolderPath;
    HMODULE shfolder = LoadLibrary("shfolder.dll");

    if (shfolder == NULL) {
        Com_Printf("Unable to load SHFolder.dll\n");
        return NULL;
    }

    if (!*homePath && com_homepath) {
        qSHGetFolderPath = GetProcAddress(shfolder, "SHGetFolderPathA");
        if (qSHGetFolderPath == NULL) {
            Com_Printf("Unable to find SHGetFolderPath in SHFolder.dll\n");
            FreeLibrary(shfolder);
            return NULL;
        }

        if (!SUCCEEDED(qSHGetFolderPath(NULL, CSIDL_APPDATA,
                                        NULL, 0, szPath))) {
            Com_Printf("Unable to detect CSIDL_APPDATA\n");
            FreeLibrary(shfolder);
            return NULL;
        }

        Com_sprintf(homePath, sizeof(homePath), "%s%c", szPath, PATH_SEP);

        if (com_homepath->string[0])
            Q_strcat(homePath, sizeof(homePath), com_homepath->string);
        else
            Q_strcat(homePath, sizeof(homePath), HOMEPATH_NAME_WIN);
    }

    FreeLibrary(shfolder);
    return homePath;
}

/*
================
Sys_SteamPath
================
*/
char* Sys_SteamPath(void) {
#if defined(STEAMPATH_NAME) || defined(STEAMPATH_APPID)
    HKEY steamRegKey;
    DWORD pathLen = MAX_OSPATH;
    qboolean finishPath = qfalse;

#ifdef STEAMPATH_APPID
    // Assuming Steam is a 32-bit app
    if (!steamPath[0] && !RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App " STEAMPATH_APPID, 0, KEY_QUERY_VALUE | KEY_WOW64_32KEY, &steamRegKey)) {
        pathLen = MAX_OSPATH;
        if (RegQueryValueEx(steamRegKey, "InstallLocation", NULL, NULL, (LPBYTE)steamPath, &pathLen))
            steamPath[0] = '\0';

        RegCloseKey(steamRegKey);
    }
#endif

#ifdef STEAMPATH_NAME
    if (!steamPath[0] && !RegOpenKeyEx(HKEY_CURRENT_USER, "Software\\Valve\\Steam", 0, KEY_QUERY_VALUE, &steamRegKey)) {
        pathLen = MAX_OSPATH;
        if (RegQueryValueEx(steamRegKey, "SteamPath", NULL, NULL, (LPBYTE)steamPath, &pathLen))
            if (RegQueryValueEx(steamRegKey, "InstallPath", NULL, NULL, (LPBYTE)steamPath, &pathLen))
                steamPath[0] = '\0';

        if (steamPath[0])
            finishPath = qtrue;

        RegCloseKey(steamRegKey);
    }
#endif

    if (steamPath[0]) {
        if (pathLen == MAX_OSPATH)
            pathLen--;

        steamPath[pathLen] = '\0';

        // Steam stores SteamPath with forward slashes - normalize to backslashes
        {
            char *s;
            for (s = steamPath; *s; s++) {
                if (*s == '/') *s = '\\';
            }
        }

        if (finishPath)
            Q_strcat(steamPath, MAX_OSPATH, "\\SteamApps\\common\\" STEAMPATH_NAME);
    }
#endif

    return steamPath;
}

/*
================
Sys_Milliseconds
================
*/
int sys_timeBase;
int Sys_Milliseconds(void) {
    int sys_curtime;
    static qboolean initialized = qfalse;

    if (!initialized) {
        sys_timeBase = timeGetTime();
        initialized = qtrue;
    }
    sys_curtime = timeGetTime() - sys_timeBase;

    return sys_curtime;
}

/*
================
Sys_Microseconds

[QL] Monotonic microsecond clock, for the frame limiter only - see the
com_framePacing block in Com_Frame. Deliberately NOT a finer Sys_Milliseconds:
the engine's whole time base (cls.realtime, cl.serverTime, cg.time, level.time,
and serverTime on the wire) is integer milliseconds, and this does not change
that. It exists so the loop can sleep for the right *fraction* of a millisecond
instead of rounding the interval to a whole one.

QueryPerformanceCounter rather than timeGetTime, which is what Sys_Milliseconds
uses: timeGetTime's resolution is the system timer period - 1ms at best, and
~15.6ms if nothing has called timeBeginPeriod - so it cannot measure the
interval this needs to measure.
================
*/
int64_t Sys_Microseconds(void) {
    static LARGE_INTEGER freq;
    static LARGE_INTEGER base;
    LARGE_INTEGER now;

    if (freq.QuadPart == 0) {
        if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0) {
            // No high-resolution counter. Fall back to the millisecond clock;
            // the limiter then behaves exactly as it did before com_framePacing.
            freq.QuadPart = 0;
            return (int64_t)Sys_Milliseconds() * 1000LL;
        }
        QueryPerformanceCounter(&base);
    }

    QueryPerformanceCounter(&now);

    // Scale before dividing would overflow after ~29 minutes at a 10MHz
    // counter, so split the difference: whole seconds and remainder.
    return ((now.QuadPart - base.QuadPart) / freq.QuadPart) * 1000000LL +
           (((now.QuadPart - base.QuadPart) % freq.QuadPart) * 1000000LL) / freq.QuadPart;
}

/*
================
Sys_LibraryErrorWin32

[QL] Report why LoadLibrary refused a module.

sys_loadlib.h defined Sys_LibraryError() as the string "unknown" on Windows, so
a dedicated server that could not load qagame printed exactly that and nothing
else - while Windows itself had a specific reason waiting in GetLastError. A
structurally valid DLL that the loader rejects is almost always a policy
decision rather than a bad file, and the distinction is invisible without this.
================
*/
const char* Sys_LibraryErrorWin32(void) {
    static char buf[1024];
    DWORD err = GetLastError();
    DWORD len;

    if (err == 0) {
        return "no error reported by the OS";
    }

    len = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                         NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                         buf, sizeof(buf) - 32, NULL);
    if (len == 0) {
        Com_sprintf(buf, sizeof(buf), "error 0x%08lx (no description available)",
                    (unsigned long)err);
        return buf;
    }

    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' ')) {
        buf[--len] = '\0';
    }
    Q_strcat(buf, sizeof(buf), va(" [0x%08lx]", (unsigned long)err));
    return buf;
}

/*
================
Sys_RandomBytes
================
*/
qboolean Sys_RandomBytes(byte* string, int len) {
    HCRYPTPROV prov;

    if (!CryptAcquireContext(&prov, NULL, NULL,
                             PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        return qfalse;
    }

    if (!CryptGenRandom(prov, len, (BYTE*)string)) {
        CryptReleaseContext(prov, 0);
        return qfalse;
    }
    CryptReleaseContext(prov, 0);
    return qtrue;
}

/*
================
Sys_GetCurrentUser
================
*/
char* Sys_GetCurrentUser(void) {
    static char s_userName[1024];
    unsigned long size = sizeof(s_userName);

    if (!GetUserName(s_userName, &size))
        strcpy(s_userName, "player");

    if (!s_userName[0]) {
        strcpy(s_userName, "player");
    }

    return s_userName;
}

#define MEM_THRESHOLD 96 * 1024 * 1024

/*
==================
Sys_LowPhysicalMemory
==================
*/
qboolean Sys_LowPhysicalMemory(void) {
    MEMORYSTATUS stat;
    GlobalMemoryStatus(&stat);
    return (stat.dwTotalPhys <= MEM_THRESHOLD) ? qtrue : qfalse;
}

/*
==============
Sys_Basename
==============
*/
const char* Sys_Basename(char* path) {
    static char base[MAX_OSPATH] = {0};
    int length;

    length = strlen(path) - 1;

    // Skip trailing slashes
    while (length > 0 && path[length] == '\\')
        length--;

    while (length > 0 && path[length - 1] != '\\')
        length--;

    Q_strncpyz(base, &path[length], sizeof(base));

    length = strlen(base) - 1;

    // Strip trailing slashes
    while (length > 0 && base[length] == '\\')
        base[length--] = '\0';

    return base;
}

/*
==============
Sys_Dirname
==============
*/
const char* Sys_Dirname(char* path) {
    static char dir[MAX_OSPATH] = {0};
    int length;

    Q_strncpyz(dir, path, sizeof(dir));
    length = strlen(dir) - 1;

    while (length > 0 && dir[length] != '\\')
        length--;

    dir[length] = '\0';

    return dir;
}

/*
==============
Sys_FOpen
==============
*/
FILE* Sys_FOpen(const char* ospath, const char* mode) {
    size_t length;

    // Windows API ignores all trailing spaces and periods which can get around Quake 3 file system restrictions.
    length = strlen(ospath);
    if (length == 0 || ospath[length - 1] == ' ' || ospath[length - 1] == '.') {
        return NULL;
    }

    return fopen(ospath, mode);
}

/*
==============
Sys_Mkdir
==============
*/
qboolean Sys_Mkdir(const char* path) {
    if (!CreateDirectory(path, NULL)) {
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            return qfalse;
    }

    return qtrue;
}

/*
==================
Sys_Mkfifo
Noop on windows because named pipes do not function the same way
==================
*/
FILE* Sys_Mkfifo(const char* ospath) {
    return NULL;
}

/*
==============
Sys_Cwd
==============
*/
char* Sys_Cwd(void) {
    static char cwd[MAX_OSPATH];

    _getcwd(cwd, sizeof(cwd) - 1);
    cwd[MAX_OSPATH - 1] = 0;

    return cwd;
}

/*
==============================================================

DIRECTORY SCANNING

==============================================================
*/

#define MAX_FOUND_FILES 0x1000

/*
==============
Sys_ListFilteredFiles
==============
*/
void Sys_ListFilteredFiles(const char* basedir, char* subdirs, char* filter, char** list, int* numfiles) {
    char search[MAX_OSPATH], newsubdirs[MAX_OSPATH];
    char filename[MAX_OSPATH];
    intptr_t findhandle;
    struct _finddata_t findinfo;

    if (*numfiles >= MAX_FOUND_FILES - 1) {
        return;
    }

    if (basedir[0] == '\0') {
        return;
    }

    if (strlen(subdirs)) {
        Com_sprintf(search, sizeof(search), "%s\\%s\\*", basedir, subdirs);
    } else {
        Com_sprintf(search, sizeof(search), "%s\\*", basedir);
    }

    findhandle = _findfirst(search, &findinfo);
    if (findhandle == -1) {
        return;
    }

    do {
        if (findinfo.attrib & _A_SUBDIR) {
            if (Q_stricmp(findinfo.name, ".") && Q_stricmp(findinfo.name, "..")) {
                if (strlen(subdirs)) {
                    Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s\\%s", subdirs, findinfo.name);
                } else {
                    Com_sprintf(newsubdirs, sizeof(newsubdirs), "%s", findinfo.name);
                }
                Sys_ListFilteredFiles(basedir, newsubdirs, filter, list, numfiles);
            }
        }
        if (*numfiles >= MAX_FOUND_FILES - 1) {
            break;
        }
        Com_sprintf(filename, sizeof(filename), "%s\\%s", subdirs, findinfo.name);
        if (!Com_FilterPath(filter, filename, qfalse))
            continue;
        list[*numfiles] = CopyString(filename);
        (*numfiles)++;
    } while (_findnext(findhandle, &findinfo) != -1);

    _findclose(findhandle);
}

/*
==============
strgtr
==============
*/
static qboolean strgtr(const char* s0, const char* s1) {
    int l0, l1, i;

    l0 = strlen(s0);
    l1 = strlen(s1);

    if (l1 < l0) {
        l0 = l1;
    }

    for (i = 0; i < l0; i++) {
        if (s1[i] > s0[i]) {
            return qtrue;
        }
        if (s1[i] < s0[i]) {
            return qfalse;
        }
    }
    return qfalse;
}

/*
==============
Sys_ListFiles
==============
*/
char** Sys_ListFiles(const char* directory, const char* extension, char* filter, int* numfiles, qboolean wantsubs) {
    char search[MAX_OSPATH];
    int nfiles;
    char** listCopy;
    char* list[MAX_FOUND_FILES];
    struct _finddata_t findinfo;
    intptr_t findhandle;
    int flag;
    int i;
    int extLen;

    if (filter) {
        nfiles = 0;
        Sys_ListFilteredFiles(directory, "", filter, list, &nfiles);

        list[nfiles] = 0;
        *numfiles = nfiles;

        if (!nfiles)
            return NULL;

        listCopy = Z_Malloc((nfiles + 1) * sizeof(*listCopy));
        for (i = 0; i < nfiles; i++) {
            listCopy[i] = list[i];
        }
        listCopy[i] = NULL;

        return listCopy;
    }

    if (directory[0] == '\0') {
        *numfiles = 0;
        return NULL;
    }

    if (!extension) {
        extension = "";
    }

    // passing a slash as extension will find directories
    if (extension[0] == '/' && extension[1] == 0) {
        extension = "";
        flag = 0;
    } else {
        flag = _A_SUBDIR;
    }

    extLen = strlen(extension);

    Com_sprintf(search, sizeof(search), "%s\\*%s", directory, extension);

    // search
    nfiles = 0;

    findhandle = _findfirst(search, &findinfo);
    if (findhandle == -1) {
        *numfiles = 0;
        return NULL;
    }

    do {
        if ((!wantsubs && flag ^ (findinfo.attrib & _A_SUBDIR)) || (wantsubs && findinfo.attrib & _A_SUBDIR)) {
            if (*extension) {
                if (strlen(findinfo.name) < extLen ||
                    Q_stricmp(
                        findinfo.name + strlen(findinfo.name) - extLen,
                        extension)) {
                    continue;  // didn't match
                }
            }
            if (nfiles == MAX_FOUND_FILES - 1) {
                break;
            }
            list[nfiles] = CopyString(findinfo.name);
            nfiles++;
        }
    } while (_findnext(findhandle, &findinfo) != -1);

    list[nfiles] = 0;

    _findclose(findhandle);

    // return a copy of the list
    *numfiles = nfiles;

    if (!nfiles) {
        return NULL;
    }

    listCopy = Z_Malloc((nfiles + 1) * sizeof(*listCopy));
    for (i = 0; i < nfiles; i++) {
        listCopy[i] = list[i];
    }
    listCopy[i] = NULL;

    do {
        flag = 0;
        for (i = 1; i < nfiles; i++) {
            if (strgtr(listCopy[i - 1], listCopy[i])) {
                char* temp = listCopy[i];
                listCopy[i] = listCopy[i - 1];
                listCopy[i - 1] = temp;
                flag = 1;
            }
        }
    } while (flag);

    return listCopy;
}

/*
==============
Sys_FreeFileList
==============
*/
void Sys_FreeFileList(char** list) {
    int i;

    if (!list) {
        return;
    }

    for (i = 0; list[i]; i++) {
        Z_Free(list[i]);
    }

    Z_Free(list);
}

/*
==============
Sys_Sleep

Block execution for msec or until input is received.
==============
*/
void Sys_Sleep(int msec) {
    if (msec == 0)
        return;

#ifdef DEDICATED
    if (msec < 0)
        WaitForSingleObject(GetStdHandle(STD_INPUT_HANDLE), INFINITE);
    else
        WaitForSingleObject(GetStdHandle(STD_INPUT_HANDLE), msec);
#else
    // Client Sys_Sleep doesn't support waiting on stdin
    if (msec < 0)
        return;

    Sleep(msec);
#endif
}

/*
==============
Sys_ErrorDialog

Display an error message
==============
*/
void Sys_ErrorDialog(const char* error) {
    Sys_Print(va("%s\n", error));

    if (Sys_Dialog(DT_YES_NO, va("%s. Copy console log to clipboard?", error),
                   "Error") == DR_YES) {
        HGLOBAL memoryHandle;
        char* clipMemory;

        memoryHandle = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, CON_LogSize() + 1);
        clipMemory = (char*)GlobalLock(memoryHandle);

        if (clipMemory) {
            char* p = clipMemory;
            char buffer[1024];
            unsigned int size;

            while ((size = CON_LogRead(buffer, sizeof(buffer))) > 0) {
                Com_Memcpy(p, buffer, size);
                p += size;
            }

            *p = '\0';

            if (OpenClipboard(NULL) && EmptyClipboard())
                SetClipboardData(CF_TEXT, memoryHandle);

            GlobalUnlock(clipMemory);
            CloseClipboard();
        }
    }
}

/*
==============
Sys_Dialog

Display a win32 dialog box
==============
*/
dialogResult_t Sys_Dialog(dialogType_t type, const char* message, const char* title) {
    UINT uType;

    switch (type) {
        default:
        case DT_INFO:
            uType = MB_ICONINFORMATION | MB_OK;
            break;
        case DT_WARNING:
            uType = MB_ICONWARNING | MB_OK;
            break;
        case DT_ERROR:
            uType = MB_ICONERROR | MB_OK;
            break;
        case DT_YES_NO:
            uType = MB_ICONQUESTION | MB_YESNO;
            break;
        case DT_OK_CANCEL:
            uType = MB_ICONWARNING | MB_OKCANCEL;
            break;
    }

    switch (MessageBox(NULL, message, title, uType)) {
        default:
        case IDOK:
            return DR_OK;
        case IDCANCEL:
            return DR_CANCEL;
        case IDYES:
            return DR_YES;
        case IDNO:
            return DR_NO;
    }
}

/*
==============
Sys_GLimpSafeInit

Windows specific "safe" GL implementation initialisation
==============
*/
void Sys_GLimpSafeInit(void) {
}

/*
==============
Sys_GLimpInit

Windows specific GL implementation initialisation
==============
*/
void Sys_GLimpInit(void) {
}

/*
==============
Sys_PlatformInit

Windows specific initialisation
==============
*/
/*
==============
Sys_Win32ExceptionFilter

[QL] Write crashlog.txt on Windows.

sys_unix.c has had Sys_ErrorDialog writing a crashlog for as long as this tree
has existed; sys_win32.c has never had anything, so a hard crash on Windows -
the platform this actually ships on - left nothing behind at all. "Does the
server have a log file" had no good answer.

An address on its own is not much use without symbols, so this reports the
module and the offset within it. That is enough to point at which of
quakelive.exe, cgamex86_64.dll, qagamex86_64.dll or a renderer the fault was
in, which is the question worth answering first, and enough to line up against
a map file or a disassembly later.

Written with the Win32 file API rather than FS_FOpenFileWrite: one of the ways
to get here is exhausting the file handles, and recursing into the filesystem
while unwinding a crash is how a crash handler becomes a second crash.

A single address names the function that faulted but not the path that got
there, which for a crash reached through a menu script - Item_Action ->
Item_RunScript -> UI_RunMenuScript -> back into the engine - is the only part
worth knowing. So the report also walks the return addresses on the stack and
resolves each to module+offset. RtlCaptureStackBackTrace is resolved from
kernel32 at call time rather than linked, so this adds no build dependency and
degrades to the single address if it is not there.
==============
*/
typedef USHORT(WINAPI *captureStackBackTrace_t)(ULONG, ULONG, PVOID *, PULONG);

// Module + offset for a code address. buf is written in every case.
static void Sys_Win32AddressName(void *address, char *buf, int bufSize) {
    HMODULE module = NULL;
    char full[MAX_OSPATH];
    const char *name = "unknown";

    if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCSTR)address, &module) &&
        module != NULL) {
        if (GetModuleFileNameA(module, full, sizeof(full))) {
            const char *slash = strrchr(full, '\\');
            name = slash ? slash + 1 : full;
        }
        Com_sprintf(buf, bufSize, "%s+0x%llx", name,
                    (unsigned long long)((DWORD_PTR)address - (DWORD_PTR)module));
        return;
    }

    Com_sprintf(buf, bufSize, "unknown (0x%p)", address);
}

static LONG WINAPI Sys_Win32ExceptionFilter(EXCEPTION_POINTERS *ep) {
    static volatile LONG entered;

    char path[MAX_OSPATH * 2];
    char text[8192];
    char where[MAX_OSPATH + 32];
    const char *homepath;
    void *address;
    HANDLE f;
    DWORD written;
    int len;
    captureStackBackTrace_t capture;

    // A fault inside this handler would otherwise loop forever.
    if (InterlockedExchange(&entered, 1) != 0) {
        return EXCEPTION_EXECUTE_HANDLER;
    }

    address = (void *)ep->ExceptionRecord->ExceptionAddress;
    Sys_Win32AddressName(address, where, sizeof(where));

    len = Com_sprintf(text, sizeof(text),
                      "Quake Live crashed.\r\n\r\n"
                      "exception : 0x%08lx\r\n"
                      "address   : 0x%p\r\n"
                      "faulted in: %s\r\n",
                      (unsigned long)ep->ExceptionRecord->ExceptionCode, address, where);

    /*
    [QL] For an access violation, what was touched and how.

    "faulted in" names the instruction; this names the memory. The two answer
    different questions and the second is usually the one that identifies the
    bug: an address a little above zero is a null pointer with a field offset
    on it, one in the tens of megabytes past a known buffer is an index run
    wild, and read-versus-write halves the candidates on its own. The record
    has carried it all along - ExceptionInformation[0] is the access type and
    [1] the address - and the first two reports from the field went without it.
    */
    if (ep->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        ep->ExceptionRecord->NumberParameters >= 2) {
        ULONG_PTR kind = ep->ExceptionRecord->ExceptionInformation[0];
        const char *what = (kind == 0) ? "reading" : (kind == 1) ? "writing"
                                                                : (kind == 8) ? "executing" : "accessing";

        len += Com_sprintf(text + len, sizeof(text) - len,
                           "bad access: %s 0x%llx\r\n", what,
                           (unsigned long long)ep->ExceptionRecord->ExceptionInformation[1]);
    }

    /*
    [QL] The faulting thread's own registers, and what it was about to return to.

    RtlCaptureStackBackTrace below runs inside this filter, so on x64 it reports
    the filter's frames and ntdll's dispatch machinery and stops - which is what
    the first crash report from the field came back with, eight frames and not
    one of them ours.

    The ContextRecord is the faulting thread, so use it. Two things matter:

      Rip 0 means the fault was not a null *data* dereference at all - it means
      execution jumped to address zero, i.e. a call through a null function
      pointer. And on x64 a CALL pushes its return address before transferring
      control, so when the target is null the word at Rsp is the instruction
      immediately after the offending call site: the caller, named exactly.

    Below that, the first stack words are scanned and any that land inside a
    loaded module's code are printed. That is a guess, not an unwind - saved
    registers and stale slots produce false entries - but it puts the module
    names of the frames underneath in front of a human, which is what turns
    "somewhere in cgame" into a place to look.
    */
    {
        CONTEXT *ctx = ep->ContextRecord;

        if (ctx) {
#ifdef _WIN64
            DWORD_PTR ip = (DWORD_PTR)ctx->Rip;
            DWORD_PTR sp = (DWORD_PTR)ctx->Rsp;
#else
            DWORD_PTR ip = (DWORD_PTR)ctx->Eip;
            DWORD_PTR sp = (DWORD_PTR)ctx->Esp;
#endif
            len += Com_sprintf(text + len, sizeof(text) - len,
                               "ip        : 0x%llx\r\n"
                               "sp        : 0x%llx\r\n",
                               (unsigned long long)ip, (unsigned long long)sp);

            if (ip == 0) {
                len += Com_sprintf(text + len, sizeof(text) - len,
                                   "\r\nExecution jumped to address 0 - a call through a null function\r\n"
                                   "pointer, not a null dereference. The return address below names the\r\n"
                                   "call site.\r\n");
            }

            if (sp && !IsBadReadPtr((void *)sp, sizeof(void *))) {
                Sys_Win32AddressName(*(void **)sp, where, sizeof(where));
                len += Com_sprintf(text + len, sizeof(text) - len,
                                   "called from: %s\r\n", where);
            }

            // plausible return addresses further down the same stack
            if (sp && !IsBadReadPtr((void *)sp, 256 * sizeof(void *))) {
                void **slot = (void **)sp;
                int i, shown = 0;

                len += Com_sprintf(text + len, sizeof(text) - len,
                                   "\r\ncode addresses on the stack (unverified - not an unwind):\r\n");
                for (i = 0; i < 256 && shown < 24 && len < (int)sizeof(text) - 160; i++) {
                    HMODULE mod = NULL;

                    if (!slot[i]) {
                        continue;
                    }
                    if (!GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                           (LPCSTR)slot[i], &mod) ||
                        mod == NULL) {
                        continue;
                    }
                    Sys_Win32AddressName(slot[i], where, sizeof(where));
                    len += Com_sprintf(text + len, sizeof(text) - len, "  %s\r\n", where);
                    shown++;
                }
            }
        }
    }

    capture = (captureStackBackTrace_t)(void *)GetProcAddress(GetModuleHandleA("kernel32.dll"),
                                                              "RtlCaptureStackBackTrace");
    if (capture) {
        void *frames[32];
        USHORT count = capture(0, ARRAY_LEN(frames), frames, NULL);
        USHORT i;

        if (count > 0) {
            len += Com_sprintf(text + len, sizeof(text) - len,
                               "\r\nhandler stack (this filter, not the fault):\r\n");
            for (i = 0; i < count && len < (int)sizeof(text) - 128; i++) {
                Sys_Win32AddressName(frames[i], where, sizeof(where));
                len += Com_sprintf(text + len, sizeof(text) - len, "  %2u  %s\r\n", (unsigned)i, where);
            }
        }
    }

    len += Com_sprintf(text + len, sizeof(text) - len,
                       "\r\n"
                       "Offsets are relative to each module's load address, so they stay valid\r\n"
                       "across runs and can be matched against a build of the same revision.\r\n"
                       "\r\n"
                       "For the console output leading up to this, set \"logfile 2\" before\r\n"
                       "reproducing - %s is written next to this file and flushed\r\n"
                       "after every line, so it survives a crash. \"logfile_keep 1\" adds to\r\n"
                       "it instead of replacing it; \"logfile_keep 2\" writes one file per run.\r\n",
                       Com_LogFileName());

    homepath = Cvar_VariableString("fs_homepath");
    if (*homepath) {
        Com_sprintf(path, sizeof(path), "%s\\crashlog.txt", homepath);
    } else {
        Q_strncpyz(path, "crashlog.txt", sizeof(path));
    }

    f = CreateFileA(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f != INVALID_HANDLE_VALUE) {
        WriteFile(f, text, len, &written, NULL);
        CloseHandle(f);
    }

    Sys_Print(text);

    return EXCEPTION_EXECUTE_HANDLER;
}

void Sys_PlatformInit(void) {
#ifndef DEDICATED
    TIMECAPS ptc;
#endif

    SetUnhandledExceptionFilter(Sys_Win32ExceptionFilter);

    Sys_SetFloatEnv();

#ifndef DEDICATED
    if (timeGetDevCaps(&ptc, sizeof(ptc)) == MMSYSERR_NOERROR) {
        timerResolution = ptc.wPeriodMin;

        if (timerResolution > 1) {
            Com_Printf(
                "Warning: Minimum supported timer resolution is %ums "
                "on this system, recommended resolution 1ms\n",
                timerResolution);
        }

        timeBeginPeriod(timerResolution);
    } else
        timerResolution = 0;
#endif
}

/*
==============
Sys_PlatformExit

Windows specific initialisation
==============
*/
void Sys_PlatformExit(void) {
#ifndef DEDICATED
    if (timerResolution)
        timeEndPeriod(timerResolution);
#endif
}

/*
==============
Sys_SetEnv

set/unset environment variables (empty value removes it)
==============
*/
void Sys_SetEnv(const char* name, const char* value) {
    if (value)
        _putenv(va("%s=%s", name, value));
    else
        _putenv(va("%s=", name));
}

/*
==============
Sys_PID
==============
*/
int Sys_PID(void) {
    return GetCurrentProcessId();
}

/*
==============
Sys_PIDIsRunning
==============
*/
qboolean Sys_PIDIsRunning(int pid) {
    DWORD processes[1024];
    DWORD numBytes, numProcesses;
    int i;

    if (!EnumProcesses(processes, sizeof(processes), &numBytes))
        return qfalse;  // Assume it's not running

    numProcesses = numBytes / sizeof(DWORD);

    // Search for the pid
    for (i = 0; i < numProcesses; i++) {
        if (processes[i] == pid)
            return qtrue;
    }

    return qfalse;
}

/*
=================
Sys_DllExtension

Check if filename should be allowed to be loaded as a DLL.
=================
*/
qboolean Sys_DllExtension(const char* name) {
    return COM_CompareExtension(name, DLL_EXT);
}
