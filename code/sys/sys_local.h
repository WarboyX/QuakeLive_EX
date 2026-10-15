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

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

#ifndef DEDICATED
#ifdef USE_LOCAL_HEADERS
#include "SDL_version.h"
#else
#include <SDL_version.h>
#endif

// Require a minimum version of SDL
#define MINSDL_MAJOR 2
#define MINSDL_MINOR 0
#if SDL_VERSION_ATLEAST(2, 0, 5)
#define MINSDL_PATCH 5
#else
#define MINSDL_PATCH 0
#endif
#endif

// Console
void CON_Shutdown(void);
void CON_Init(void);
char* CON_Input(void);
void CON_Print(const char* message);

unsigned int CON_LogSize(void);
unsigned int CON_LogWrite(const char* in);
unsigned int CON_LogRead(char* out, unsigned int outSize);

#ifdef __APPLE__
char* Sys_StripAppBundle(char* pwd);
void Sys_DisableStateRestoration(void);
#endif

char* Sys_BinaryPath(void);

/* [QL] E222: the CPU, as the game should use it - see Sys_CpuInit (sys_main.c) */
#define SYS_CPU_MAX_L3 8
typedef struct {
    int logical, physical;              // threads and cores (processor group 0)
    int fastCores, slowCores;           // hybrid CPUs: performance / efficiency cores; 0 when uniform
    int numL3;                          // distinct L3 caches (one per CCD / die)
    int l3KB[SYS_CPU_MAX_L3];
    int l3Cores[SYS_CPU_MAX_L3];        // physical cores sharing each
    unsigned long long fastMask;        // logical CPUs of the preferred set, 0 = no preference
    int preferred;                      // 0 none, 1 performance cores, 2 the larger-cache die
    unsigned long long ramMB;
    char brand[64];                     // from the OS where cpuid has none (Apple, ARM)
    qboolean qosOnly;                   // macOS: no core pinning - placement is a QoS class
} sysCpuTopology_t;
void Sys_CpuTopology(sysCpuTopology_t* t);           // per platform
qboolean Sys_CpuPlaceMainThread(unsigned long long mask, qboolean noThrottle);   // per platform; mask 0 = all
void Sys_CpuInit(void);
void Sys_CpuFrame(void);

void Sys_GLimpSafeInit(void);
void Sys_GLimpInit(void);
void Sys_PlatformInit(void);
void Sys_PlatformExit(void);
void Sys_SigHandler(int signal);
void Sys_SigShutdown(int signal) __attribute__((noreturn));
void Sys_ErrorDialog(const char* error);
void Sys_AnsiColorPrint(const char* msg);

int Sys_PID(void);
qboolean Sys_PIDIsRunning(int pid);

#ifdef PROTOCOL_HANDLER
char* Sys_ParseProtocolUri(const char* uri);
#endif
