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
/* Now defined in qcommon/q_shared.h - it is shared with drawVert_t. */

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

/*
Quake3e: code/qcommon/q_shared.h — vector and sign helpers this tree lacks.
*/
#ifndef SGN
#define SGN(x) (((x) >= 0) ? !!(x) : -1)
#endif
#ifndef DotProduct4
#define DotProduct4(a, b) ((a)[0] * (b)[0] + (a)[1] * (b)[1] + (a)[2] * (b)[2] + (a)[3] * (b)[3])
#endif
#ifndef VectorScale4
#define VectorScale4(a, b, c) ((c)[0] = (a)[0] * (b), (c)[1] = (a)[1] * (b), (c)[2] = (a)[2] * (b), (c)[3] = (a)[3] * (b))
#endif
#ifndef Vector4Set
#define Vector4Set(v, x, y, z, w) ((v)[0] = (x), (v)[1] = (y), (v)[2] = (z), (v)[3] = (w))
#endif

/*
Quake3e uses myftol where this tree uses Q_ftol. Upstream's comment on the
call sites is explicit that it avoids ri.ftol "to avoid precision losses", so
this maps to the direct cast rather than back through the import table.
*/
#ifndef myftol
#define myftol(x) ((int)(x))
#endif

/*
Quake3e: code/qcommon/qcommon.h — size of the BSP visibility lump header
(two ints: cluster count and cluster size).
*/
#ifndef VIS_HEADER
#define VIS_HEADER 8
#endif

/*
Quake3e: code/qcommon/q_shared.h — cvar flags and groups this tree lacks.
CVAR_NODEFAULT does not exist here either, so ARCHIVE_ND degrades to plain
CVAR_ARCHIVE: the "no default" half only affects config writing, which is
engine-side and unchanged by the renderer.
*/
#ifndef CVAR_DEVELOPER
#define CVAR_DEVELOPER 0x10000
#endif
#ifndef CVAR_ARCHIVE_ND
#define CVAR_ARCHIVE_ND CVAR_ARCHIVE
#endif

typedef enum { CV_NONE = 0, CV_FLOAT, CV_INTEGER } cvarValidator_t;

typedef enum { CVG_NONE = 0, CVG_RENDERER, CVG_MAX } cvarGroup_t;

/*
Quake3e: code/qcommon/q_shared.h — token types produced by COM_ParseComplex,
which is Quake3e's shader-script tokeniser and has no counterpart here.
*/
typedef enum {
    TK_GENERIC = 0,  // for single-char tokens
    TK_STRING,
    TK_QUOTED,
    TK_EQ,
    TK_NEQ,
    TK_GT,
    TK_GTE,
    TK_LT,
    TK_LTE,
    TK_MATCH,
    TK_OR,
    TK_AND,
    TK_SCOPE_OPEN,
    TK_SCOPE_CLOSE,
    TK_NEWLINE,
    TK_EOF,
} tokenType_t;

extern tokenType_t com_tokentype;

/*
Implemented in tr_q3e_compat.c, ported verbatim from Quake3e's q_shared.c.
*/
char *COM_ParseComplex(const char **data_p, qboolean allowLineBreaks);
char *Q_stradd(char *dst, const char *src);
unsigned int crc32_buffer(const byte *buf, unsigned int len);
int Com_Split(char *in, char **out, int outsz, int delim);
unsigned long Com_GenerateHashValue(const char *fname, const unsigned int size);

/*
Quake3e: code/qcommon/qcommon.h, a static inline. Rounds v to a power of two,
up or down.
*/
static ID_INLINE unsigned int log2pad(unsigned int v, int roundup) {
    unsigned int x = 1;

    while (x < v) {
        x <<= 1;
    }

    if (roundup == 0) {
        if (x > v) {
            x >>= 1;
        }
    }

    return x;
}

/*
Quake3e: code/qcommon/q_shared.h. This tree has atof but not the wrapper.
*/
static ID_INLINE float Q_atof(const char *str) { return (float)atof(str); }

/*
Quake3e's Cvar_CheckRange takes string bounds and a validator enum; this
tree's takes floats and a qboolean, and renderergl2 depends on that signature.
Both are in refimport_t under different names, and this rewrites renderervk's
`ri.Cvar_CheckRange(...)` to reach the Quake3e-shaped one. A macro is enough
because the member name is an ordinary token.
*/
#ifndef Cvar_CheckRange
#define Cvar_CheckRange Cvar_CheckRangeQ3E
#endif

#endif  // TR_Q3E_COMPAT_H
