/*
===========================================================================
tr_q3e_compat.h — Quake3e types this tree does not have.

code/renderervk is vendored from ec-/Quake3e unmodified, so it expects a few
declarations that live in Quake3e's engine headers and have no counterpart
here. Rather than edit 29 vendored files — which would have to be redone on
every update from upstream — the differences are collected in this one header
and force-included by the renderervk build only.

Nothing else in the tree sees it, and the OpenGL renderer is untouched.

Each entry records where Quake3e declares it, so this stays checkable against
upstream rather than becoming folklore.
===========================================================================
*/

#ifndef TR_Q3E_COMPAT_H
#define TR_Q3E_COMPAT_H

#include "../qcommon/q_shared.h"

/*
Quake3e: code/qcommon/q_shared.h

A packed RGBA quadruple, addressable either as four bytes or as one 32-bit
word — the renderer compares and copies whole colours as u32. It is a *union*,
not an array: I first declared it as `byte[4]` and the compiler rejected every
`.rgba` and `.u32` use, which is the outcome to want. This tree uses raw
`byte[4]` in the equivalent structures, so the type does not exist here.
*/
typedef union {
    byte rgba[4];
    uint32_t u32;
} color4ub_t;

/*
Quake3e: code/qcommon/q_shared.h
*/
#ifndef MAX_UINT
#define MAX_UINT ((unsigned int)(~0))
#endif

/*
Quake3e: code/qcommon/surfaceflags.h — marks a BSP node rather than a leaf.
*/
#ifndef CONTENTS_NODE
#define CONTENTS_NODE -1
#endif

/*
Quake3e: code/renderercommon/tr_types.h

Number of simultaneous cinematic (ROQ) handles the renderer tracks. This tree
manages cinematics from the client side instead, so the renderer never had a
limit of its own.
*/
#ifndef MAX_VIDEO_HANDLES
#define MAX_VIDEO_HANDLES 16
#endif

/*
Quake3e: code/renderercommon/tr_public.h

How much to tear down on RE_Shutdown. Quake3e's engine drives window lifetime
and tells the renderer how far to go; this tree's renderer owns its own window
and takes a plain qboolean, which is the same difference that puts the six
GLimp_* entries and the four VK_* entries on the import side of the gap.

Kept in Quake3e's order — the values are passed as an enum, not by name.
*/
typedef enum {
    REF_KEEP_CONTEXT,  // don't destroy window and context
    REF_KEEP_WINDOW,   // destroy context, keep window
    REF_DESTROY_WINDOW,
    REF_UNLOAD_DLL
} refShutdownCode_t;

/*
Quake3e: declared in its own renderercommon/tr_common.h.

The vendored renderervk/tr_common.h does not declare it, and this tree's
renderercommon/tr_common.h — which does, at line 71 — is a different file that
renderervk never includes. The renderer's copy of the import table is defined
in renderervk/tr_init.c by GetRefAPI.
*/
#include "../renderercommon/tr_public.h"
extern refimport_t ri;

#endif  // TR_Q3E_COMPAT_H
