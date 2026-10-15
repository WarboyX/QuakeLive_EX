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

// q_math_sse.c -- Q_ftol and Q_SnapVector implementations
// SSE on x86/x86_64, generic C fallbacks on other architectures (ARM, etc.)

#include "q_shared.h"

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)

#include <emmintrin.h>

long Q_ftol(float f) {
    return (long)_mm_cvttss_si32(_mm_set_ss(f));
}

void Q_SnapVector(vec3_t vec) {
    /*
    Three scalar converts rather than one packed load.

    The obvious version is _mm_loadu_ps(vec), round all four lanes at once, and
    shuffle the results out. It reads 16 bytes. A vec3_t is 12. That fourth lane
    is off the end of the array every single time, which gcc reports as
    "array subscript '__m128_u[0]' is partly outside array bounds", and it is
    right: most vec3_t live inside a larger struct so the read lands on a
    neighbouring field and nothing is noticed, but one at the end of a mapping
    faults, and the compiler is entitled to assume it never happens.

    _mm_cvtss_si32 rounds by the current mode - round-to-nearest-even - which is
    what _mm_cvtps_epi32 did, so the results are unchanged. This is also why the
    warning is x86-only: the ARM fallback below uses rintf and never had it.
    */
    vec[0] = (float)_mm_cvtss_si32(_mm_set_ss(vec[0]));
    vec[1] = (float)_mm_cvtss_si32(_mm_set_ss(vec[1]));
    vec[2] = (float)_mm_cvtss_si32(_mm_set_ss(vec[2]));
}

#else

#include <math.h>

long Q_ftol(float f) {
    return (long)f;
}

void Q_SnapVector(vec3_t vec) {
    vec[0] = rintf(vec[0]);
    vec[1] = rintf(vec[1]);
    vec[2] = rintf(vec[2]);
}

#endif
