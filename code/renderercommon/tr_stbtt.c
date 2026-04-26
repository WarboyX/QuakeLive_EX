/*
===========================================================================
Quake Live font subsystem - TrueType rasteriser (tr_stbtt)

Faithful reimplementation of the renamed stb_truetype that Quake Live ships
(R_LoadFontTables / R_GetFontGlyphIndex / R_RasterizeGlyphOutline / ...).
See tr_stbtt.h for the mapping to the QL binary's R_* symbols.

Based on Sean Barrett's public-domain stb_truetype (v1.x algorithms): the
TrueType 'glyf' outline extractor and the version-2 antialiased sorted-edge
scanline rasteriser that accumulates 8-bit coverage.  This is the chain
observed in the QL binary (R_RenderGlyphBitmap -> R_RasterizeGlyphOutline,
20-byte edge records, quicksort+insertion sort, flatness 0.35).

C89.
===========================================================================
*/

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "tr_stbtt.h"

#define STBTT_ifloor(x) ((int)floor(x))
#define STBTT_iceil(x) ((int)ceil(x))
#define STBTT_sqrt(x) sqrt(x)
#define STBTT_fabs(x) fabs(x)
#define STBTT_malloc(x, u) ((void)(u), malloc(x))
#define STBTT_free(x, u) ((void)(u), free(x))
#define STBTT_assert(x)

typedef unsigned char stbtt_uint8;
typedef signed char stbtt_int8;
typedef unsigned short stbtt_uint16;
typedef signed short stbtt_int16;
typedef unsigned int stbtt_uint32;
typedef signed int stbtt_int32;

// -------------------------------------------------------------------------
//  big-endian buffer accessors
// -------------------------------------------------------------------------

#define ttBYTE(p) (*(stbtt_uint8 *)(p))
#define ttCHAR(p) (*(stbtt_int8 *)(p))

static stbtt_uint16 ttUSHORT(const stbtt_uint8 *p) { return p[0] * 256 + p[1]; }
static stbtt_int16 ttSHORT(const stbtt_uint8 *p) { return p[0] * 256 + p[1]; }
static stbtt_uint32 ttULONG(const stbtt_uint8 *p) { return (p[0] << 24) + (p[1] << 16) + (p[2] << 8) + p[3]; }

#define stbtt_tag4(p, c0, c1, c2, c3) ((p)[0] == (c0) && (p)[1] == (c1) && (p)[2] == (c2) && (p)[3] == (c3))
#define stbtt_tag(p, str) stbtt_tag4(p, str[0], str[1], str[2], str[3])

// -------------------------------------------------------------------------
//  table directory
// -------------------------------------------------------------------------

// return the offset of the named table, or 0 if it does not exist
static stbtt_uint32 stbtt__find_table(const stbtt_uint8 *data, stbtt_uint32 fontstart, const char *tag) {
    stbtt_int32 num_tables = ttUSHORT(data + fontstart + 4);
    stbtt_uint32 tabledir = fontstart + 12;
    stbtt_int32 i;
    for (i = 0; i < num_tables; ++i) {
        stbtt_uint32 loc = tabledir + 16 * i;
        if (stbtt_tag(data + loc + 0, tag))
            return ttULONG(data + loc + 8);
    }
    return 0;
}

int R_LoadFontTables(stbtt_fontinfo *info, const unsigned char *data, int fontstart) {
    stbtt_uint32 cmap, t;
    stbtt_int32 i, numTables;

    info->data = (unsigned char *)data;
    info->fontstart = fontstart;
    info->userdata = NULL;

    cmap = stbtt__find_table(data, fontstart, "cmap");
    info->loca = stbtt__find_table(data, fontstart, "loca");
    info->head = stbtt__find_table(data, fontstart, "head");
    info->glyf = stbtt__find_table(data, fontstart, "glyf");
    info->hhea = stbtt__find_table(data, fontstart, "hhea");
    info->hmtx = stbtt__find_table(data, fontstart, "hmtx");
    info->kern = stbtt__find_table(data, fontstart, "kern");
    info->gpos = stbtt__find_table(data, fontstart, "GPOS");

    // this port only supports TrueType 'glyf' outlines
    if (!cmap || !info->head || !info->hhea || !info->hmtx || !info->loca || !info->glyf)
        return 0;

    t = stbtt__find_table(data, fontstart, "maxp");
    if (t)
        info->numGlyphs = ttUSHORT(data + t + 4);
    else
        info->numGlyphs = 0xffff;

    // find a cmap encoding table we understand: prefer Unicode
    numTables = ttUSHORT(data + cmap + 2);
    info->index_map = 0;
    for (i = 0; i < numTables; ++i) {
        stbtt_uint32 encoding_record = cmap + 4 + 8 * i;
        stbtt_uint16 pid = ttUSHORT(data + encoding_record);
        stbtt_uint16 eid = ttUSHORT(data + encoding_record + 2);
        switch (pid) {
            case 3: // STBTT_PLATFORM_ID_MICROSOFT
                if (eid == 1 || eid == 10)
                    info->index_map = cmap + ttULONG(data + encoding_record + 4);
                break;
            case 0: // STBTT_PLATFORM_ID_UNICODE
                info->index_map = cmap + ttULONG(data + encoding_record + 4);
                break;
            default:
                break;
        }
    }
    if (info->index_map == 0)
        return 0;

    info->indexToLocFormat = ttUSHORT(data + info->head + 50);
    return 1;
}

// -------------------------------------------------------------------------
//  cmap lookup
// -------------------------------------------------------------------------

int R_GetFontGlyphIndex(const stbtt_fontinfo *info, int unicode_codepoint) {
    stbtt_uint8 *data = info->data;
    stbtt_uint32 index_map = info->index_map;
    stbtt_uint16 format = ttUSHORT(data + index_map + 0);

    if (format == 0) { // apple byte encoding
        stbtt_int32 bytes = ttUSHORT(data + index_map + 2);
        if (unicode_codepoint < bytes - 6)
            return ttBYTE(data + index_map + 6 + unicode_codepoint);
        return 0;
    } else if (format == 6) {
        stbtt_uint32 first = ttUSHORT(data + index_map + 6);
        stbtt_uint32 count = ttUSHORT(data + index_map + 8);
        if ((stbtt_uint32)unicode_codepoint >= first && (stbtt_uint32)unicode_codepoint < first + count)
            return ttUSHORT(data + index_map + 10 + (unicode_codepoint - first) * 2);
        return 0;
    } else if (format == 2) {
        STBTT_assert(0); // high-byte mapping - unsupported (CJK legacy)
        return 0;
    } else if (format == 4) { // standard mapping for windows fonts: binary search collection of ranges
        stbtt_uint16 segcount = ttUSHORT(data + index_map + 6) >> 1;
        stbtt_uint16 searchRange = ttUSHORT(data + index_map + 8) >> 1;
        stbtt_uint16 entrySelector = ttUSHORT(data + index_map + 10);
        stbtt_uint16 rangeShift = ttUSHORT(data + index_map + 12) >> 1;
        stbtt_uint32 endCount = index_map + 14;
        stbtt_uint32 search = endCount;
        stbtt_uint16 item, offset, start, last;

        if (unicode_codepoint > 0xffff)
            return 0;

        // they lie from endCount .. endCount + segCount but searchRange is the
        // nearest power of two, so...
        if (unicode_codepoint >= ttUSHORT(data + search + rangeShift * 2))
            search += rangeShift * 2;

        // now decrement to bias correctly to find smallest
        search -= 2;
        while (entrySelector) {
            stbtt_uint16 end;
            searchRange >>= 1;
            end = ttUSHORT(data + search + searchRange * 2);
            if (unicode_codepoint > end)
                search += searchRange * 2;
            --entrySelector;
        }
        search += 2;

        item = (stbtt_uint16)((search - endCount) >> 1);

        start = ttUSHORT(data + index_map + 14 + segcount * 2 + 2 + 2 * item);
        last = ttUSHORT(data + index_map + 14 + 2 * item);
        if (unicode_codepoint < start || unicode_codepoint > last)
            return 0;

        offset = ttUSHORT(data + index_map + 14 + segcount * 6 + 2 + 2 * item);
        if (offset == 0)
            return (stbtt_uint16)(unicode_codepoint + ttSHORT(data + index_map + 14 + segcount * 4 + 2 + 2 * item));

        return ttUSHORT(data + offset + (unicode_codepoint - start) * 2 + index_map + 14 + segcount * 6 + 2 + 2 * item);
    } else if (format == 12 || format == 13) {
        stbtt_uint32 ngroups = ttULONG(data + index_map + 12);
        stbtt_int32 low, high;
        low = 0;
        high = (stbtt_int32)ngroups;
        // binary search the right group
        while (low < high) {
            stbtt_int32 mid = low + ((high - low) >> 1); // rounds down, so low <= mid < high
            stbtt_uint32 start_char = ttULONG(data + index_map + 16 + mid * 12);
            stbtt_uint32 end_char = ttULONG(data + index_map + 16 + mid * 12 + 4);
            if ((stbtt_uint32)unicode_codepoint < start_char)
                high = mid;
            else if ((stbtt_uint32)unicode_codepoint > end_char)
                low = mid + 1;
            else {
                stbtt_uint32 start_glyph = ttULONG(data + index_map + 16 + mid * 12 + 8);
                if (format == 12)
                    return start_glyph + unicode_codepoint - start_char;
                else // format == 13
                    return start_glyph;
            }
        }
        return 0; // not found
    }
    STBTT_assert(0);
    return 0;
}

// -------------------------------------------------------------------------
//  glyph location and metrics
// -------------------------------------------------------------------------

// return glyph offset within the glyf table, or -1 for an empty glyph
static int stbtt__GetGlyfOffset(const stbtt_fontinfo *info, int glyph_index) {
    int g1, g2;

    if (glyph_index >= info->numGlyphs)
        return -1;
    if (info->indexToLocFormat >= 2)
        return -1;

    if (info->indexToLocFormat == 0) {
        g1 = info->glyf + ttUSHORT(info->data + info->loca + glyph_index * 2) * 2;
        g2 = info->glyf + ttUSHORT(info->data + info->loca + glyph_index * 2 + 2) * 2;
    } else {
        g1 = info->glyf + ttULONG(info->data + info->loca + glyph_index * 4);
        g2 = info->glyf + ttULONG(info->data + info->loca + glyph_index * 4 + 4);
    }

    return g1 == g2 ? -1 : g1; // if length is 0, return -1
}

int R_GetGlyphBox(const stbtt_fontinfo *info, int glyph_index, int *x0, int *y0, int *x1, int *y1) {
    int g = stbtt__GetGlyfOffset(info, glyph_index);
    if (g < 0)
        return 0;

    if (x0)
        *x0 = ttSHORT(info->data + g + 2);
    if (y0)
        *y0 = ttSHORT(info->data + g + 4);
    if (x1)
        *x1 = ttSHORT(info->data + g + 6);
    if (y1)
        *y1 = ttSHORT(info->data + g + 8);
    return 1;
}

void R_GetGlyphHMetrics(const stbtt_fontinfo *info, int glyph_index, int *advanceWidth, int *leftSideBearing) {
    stbtt_uint16 numOfLongHorMetrics = ttUSHORT(info->data + info->hhea + 34);
    if (glyph_index < numOfLongHorMetrics) {
        if (advanceWidth)
            *advanceWidth = ttSHORT(info->data + info->hmtx + 4 * glyph_index);
        if (leftSideBearing)
            *leftSideBearing = ttSHORT(info->data + info->hmtx + 4 * glyph_index + 2);
    } else {
        if (advanceWidth)
            *advanceWidth = ttSHORT(info->data + info->hmtx + 4 * (numOfLongHorMetrics - 1));
        if (leftSideBearing)
            *leftSideBearing = ttSHORT(info->data + info->hmtx + 4 * numOfLongHorMetrics + 2 * (glyph_index - numOfLongHorMetrics));
    }
}

void R_GetFontLineMetrics(const stbtt_fontinfo *info, int *ascent, int *descent, int *lineGap) {
    if (ascent)
        *ascent = ttSHORT(info->data + info->hhea + 4);
    if (descent)
        *descent = ttSHORT(info->data + info->hhea + 6);
    if (lineGap)
        *lineGap = ttSHORT(info->data + info->hhea + 8);
}

int R_GetFontUnitsPerEm(const stbtt_fontinfo *info) {
    return ttUSHORT(info->data + info->head + 18);
}

float R_ScaleForPixelHeight(const stbtt_fontinfo *info, float height) {
    int fheight = ttSHORT(info->data + info->hhea + 4) - ttSHORT(info->data + info->hhea + 6);
    return (float)height / fheight;
}

float R_ScaleForMappingEmToPixels(const stbtt_fontinfo *info, float pixels) {
    int unitsPerEm = ttUSHORT(info->data + info->head + 18);
    return pixels / unitsPerEm;
}

// -------------------------------------------------------------------------
//  kerning (kern table format 0)
// -------------------------------------------------------------------------

int R_GetGlyphKernAdvance(const stbtt_fontinfo *info, int glyph1, int glyph2) {
    stbtt_uint8 *data = info->data + info->kern;
    stbtt_uint32 needle, straw;
    int l, r, m;

    if (!info->kern)
        return 0;
    if (ttUSHORT(data + 2) < 1) // number of tables, need at least 1
        return 0;
    if (ttUSHORT(data + 8) != 1) // horizontal flag must be set in format
        return 0;

    l = 0;
    r = ttUSHORT(data + 10) - 1;
    needle = glyph1 << 16 | glyph2;
    while (l <= r) {
        m = (l + r) >> 1;
        straw = ttULONG(data + 18 + (m * 6)); // note: unaligned read
        if (needle < straw)
            r = m - 1;
        else if (needle > straw)
            l = m + 1;
        else
            return ttSHORT(data + 22 + (m * 6));
    }
    return 0;
}

// -------------------------------------------------------------------------
//  glyph outline extraction
// -------------------------------------------------------------------------

static int stbtt__close_shape(stbtt_vertex *vertices, int num_vertices, int was_off, int start_off,
                              stbtt_int32 sx, stbtt_int32 sy, stbtt_int32 scx, stbtt_int32 scy,
                              stbtt_int32 cx, stbtt_int32 cy) {
    if (start_off) {
        if (was_off) {
            vertices[num_vertices].type = STBTT_vcurve;
            vertices[num_vertices].x = (stbtt_int16)((cx + scx) >> 1);
            vertices[num_vertices].y = (stbtt_int16)((cy + scy) >> 1);
            vertices[num_vertices].cx = (stbtt_int16)cx;
            vertices[num_vertices].cy = (stbtt_int16)cy;
            ++num_vertices;
        }
        vertices[num_vertices].type = STBTT_vcurve;
        vertices[num_vertices].x = (stbtt_int16)sx;
        vertices[num_vertices].y = (stbtt_int16)sy;
        vertices[num_vertices].cx = (stbtt_int16)scx;
        vertices[num_vertices].cy = (stbtt_int16)scy;
        ++num_vertices;
    } else {
        if (was_off) {
            vertices[num_vertices].type = STBTT_vcurve;
            vertices[num_vertices].x = (stbtt_int16)sx;
            vertices[num_vertices].y = (stbtt_int16)sy;
            vertices[num_vertices].cx = (stbtt_int16)cx;
            vertices[num_vertices].cy = (stbtt_int16)cy;
            ++num_vertices;
        } else {
            vertices[num_vertices].type = STBTT_vline;
            vertices[num_vertices].x = (stbtt_int16)sx;
            vertices[num_vertices].y = (stbtt_int16)sy;
            ++num_vertices;
        }
    }
    return num_vertices;
}

static void stbtt__setvertex(stbtt_vertex *v, stbtt_uint8 type, stbtt_int32 x, stbtt_int32 y, stbtt_int32 cx, stbtt_int32 cy) {
    v->type = type;
    v->x = (stbtt_int16)x;
    v->y = (stbtt_int16)y;
    v->cx = (stbtt_int16)cx;
    v->cy = (stbtt_int16)cy;
}

static int stbtt__GetGlyphShapeTT(const stbtt_fontinfo *info, int glyph_index, stbtt_vertex **pvertices) {
    stbtt_int16 numberOfContours;
    stbtt_uint8 *endPtsOfContours;
    stbtt_uint8 *data = info->data;
    stbtt_vertex *vertices = 0;
    int num_vertices = 0;
    int g = stbtt__GetGlyfOffset(info, glyph_index);

    *pvertices = NULL;
    if (g < 0)
        return 0;

    numberOfContours = ttSHORT(data + g);

    if (numberOfContours > 0) {
        stbtt_uint8 flags = 0, flagcount;
        stbtt_int32 ins, i, j = 0, m, n, next_move, was_off = 0, off, start_off = 0;
        stbtt_int32 x, y, cx, cy, sx, sy, scx, scy;
        stbtt_uint8 *points;
        endPtsOfContours = (data + g + 10);
        ins = ttUSHORT(data + g + 10 + numberOfContours * 2);
        points = data + g + 10 + numberOfContours * 2 + 2 + ins;

        n = 1 + ttUSHORT(endPtsOfContours + numberOfContours * 2 - 2);

        m = n + 2 * numberOfContours; // a loose bound on how many vertices we might need
        vertices = (stbtt_vertex *)STBTT_malloc(m * sizeof(vertices[0]), info->userdata);
        if (vertices == 0)
            return 0;

        next_move = 0;
        flagcount = 0;

        // in first pass, we load uninterpreted data into the allocated array
        // above, shifted to the end of the array so we won't overwrite it when
        // we create our final data starting from the front
        off = m - n; // starting offset for uninterpreted data, regardless of how m ends up being calculated

        // first load flags
        for (i = 0; i < n; ++i) {
            if (flagcount == 0) {
                flags = *points++;
                if (flags & 8)
                    flagcount = *points++;
            } else
                --flagcount;
            vertices[off + i].type = flags;
        }

        // now load x coordinates
        x = 0;
        for (i = 0; i < n; ++i) {
            flags = vertices[off + i].type;
            if (flags & 2) {
                stbtt_int16 dx = *points++;
                x += (flags & 16) ? dx : -dx; // ? add : subtract
            } else {
                if (!(flags & 16)) {
                    x = x + (stbtt_int16)(points[0] * 256 + points[1]);
                    points += 2;
                }
            }
            vertices[off + i].x = (stbtt_int16)x;
        }

        // now load y coordinates
        y = 0;
        for (i = 0; i < n; ++i) {
            flags = vertices[off + i].type;
            if (flags & 4) {
                stbtt_int16 dy = *points++;
                y += (flags & 32) ? dy : -dy; // ? add : subtract
            } else {
                if (!(flags & 32)) {
                    y = y + (stbtt_int16)(points[0] * 256 + points[1]);
                    points += 2;
                }
            }
            vertices[off + i].y = (stbtt_int16)y;
        }

        // now convert them to our format
        num_vertices = 0;
        sx = sy = cx = cy = scx = scy = 0;
        for (i = 0; i < n; ++i) {
            flags = vertices[off + i].type;
            x = (stbtt_int16)vertices[off + i].x;
            y = (stbtt_int16)vertices[off + i].y;

            if (next_move == i) {
                if (i != 0)
                    num_vertices = stbtt__close_shape(vertices, num_vertices, was_off, start_off, sx, sy, scx, scy, cx, cy);

                // now start the new one
                start_off = !(flags & 1);
                if (start_off) {
                    // if we start off with an off-curve point, then when we need
                    // to find a point on the curve where we can start, and we
                    // need to save some state for when we wraparound.
                    scx = x;
                    scy = y;
                    if (!(vertices[off + i + 1].type & 1)) {
                        // next point is also a curve point, so interpolate an on-point curve
                        sx = (x + (stbtt_int32)vertices[off + i + 1].x) >> 1;
                        sy = (y + (stbtt_int32)vertices[off + i + 1].y) >> 1;
                    } else {
                        // otherwise just use the next point as our start point
                        sx = (stbtt_int32)vertices[off + i + 1].x;
                        sy = (stbtt_int32)vertices[off + i + 1].y;
                        ++i; // we're using point i+1 as the starting point, so skip it
                    }
                } else {
                    sx = x;
                    sy = y;
                }
                stbtt__setvertex(&vertices[num_vertices++], STBTT_vmove, sx, sy, 0, 0);
                was_off = 0;
                next_move = 1 + ttUSHORT(endPtsOfContours + j * 2);
                ++j;
            } else {
                if (!(flags & 1)) { // if it's a curve
                    if (was_off)   // two off-curve control points in a row means interpolate an on-curve midpoint
                        stbtt__setvertex(&vertices[num_vertices++], STBTT_vcurve, (cx + x) >> 1, (cy + y) >> 1, cx, cy);
                    cx = x;
                    cy = y;
                    was_off = 1;
                } else {
                    if (was_off)
                        stbtt__setvertex(&vertices[num_vertices++], STBTT_vcurve, x, y, cx, cy);
                    else
                        stbtt__setvertex(&vertices[num_vertices++], STBTT_vline, x, y, 0, 0);
                    was_off = 0;
                }
            }
        }
        num_vertices = stbtt__close_shape(vertices, num_vertices, was_off, start_off, sx, sy, scx, scy, cx, cy);
    } else if (numberOfContours < 0) {
        // Compound shapes.
        int more = 1;
        stbtt_uint8 *comp = data + g + 10;
        num_vertices = 0;
        vertices = 0;
        while (more) {
            stbtt_uint16 flags, gidx;
            int comp_num_verts = 0, i;
            stbtt_vertex *comp_verts = 0, *tmp = 0;
            float mtx[6] = {1, 0, 0, 1, 0, 0}, m2, n2;

            flags = ttSHORT(comp);
            comp += 2;
            gidx = ttSHORT(comp);
            comp += 2;

            if (flags & 2) {      // XY values
                if (flags & 1) {  // shorts
                    mtx[4] = ttSHORT(comp);
                    comp += 2;
                    mtx[5] = ttSHORT(comp);
                    comp += 2;
                } else {
                    mtx[4] = ttCHAR(comp);
                    comp += 1;
                    mtx[5] = ttCHAR(comp);
                    comp += 1;
                }
            } else {
                // @TODO handle matching point
                STBTT_assert(0);
            }
            if (flags & (1 << 3)) { // WE_HAVE_A_SCALE
                mtx[0] = mtx[3] = ttSHORT(comp) / 16384.0f;
                comp += 2;
                mtx[1] = mtx[2] = 0;
            } else if (flags & (1 << 6)) { // WE_HAVE_AN_X_AND_YSCALE
                mtx[0] = ttSHORT(comp) / 16384.0f;
                comp += 2;
                mtx[1] = mtx[2] = 0;
                mtx[3] = ttSHORT(comp) / 16384.0f;
                comp += 2;
            } else if (flags & (1 << 7)) { // WE_HAVE_A_TWO_BY_TWO
                mtx[0] = ttSHORT(comp) / 16384.0f;
                comp += 2;
                mtx[1] = ttSHORT(comp) / 16384.0f;
                comp += 2;
                mtx[2] = ttSHORT(comp) / 16384.0f;
                comp += 2;
                mtx[3] = ttSHORT(comp) / 16384.0f;
                comp += 2;
            }

            // Find transformation scales.
            m2 = (float)STBTT_sqrt(mtx[0] * mtx[0] + mtx[1] * mtx[1]);
            n2 = (float)STBTT_sqrt(mtx[2] * mtx[2] + mtx[3] * mtx[3]);

            // Get indexed glyph.
            comp_num_verts = R_GetGlyphShape(info, gidx, &comp_verts);
            if (comp_num_verts > 0) {
                // Transform vertices.
                for (i = 0; i < comp_num_verts; ++i) {
                    stbtt_vertex *v = &comp_verts[i];
                    stbtt_int16 x2, y2;
                    x2 = v->x;
                    y2 = v->y;
                    v->x = (stbtt_int16)(m2 * (mtx[0] * x2 + mtx[2] * y2 + mtx[4]));
                    v->y = (stbtt_int16)(n2 * (mtx[1] * x2 + mtx[3] * y2 + mtx[5]));
                    x2 = v->cx;
                    y2 = v->cy;
                    v->cx = (stbtt_int16)(m2 * (mtx[0] * x2 + mtx[2] * y2 + mtx[4]));
                    v->cy = (stbtt_int16)(n2 * (mtx[1] * x2 + mtx[3] * y2 + mtx[5]));
                }
                // Append vertices.
                tmp = (stbtt_vertex *)STBTT_malloc((num_vertices + comp_num_verts) * sizeof(stbtt_vertex), info->userdata);
                if (!tmp) {
                    if (vertices)
                        STBTT_free(vertices, info->userdata);
                    if (comp_verts)
                        STBTT_free(comp_verts, info->userdata);
                    return 0;
                }
                if (num_vertices > 0 && vertices)
                    memcpy(tmp, vertices, num_vertices * sizeof(stbtt_vertex));
                memcpy(tmp + num_vertices, comp_verts, comp_num_verts * sizeof(stbtt_vertex));
                if (vertices)
                    STBTT_free(vertices, info->userdata);
                vertices = tmp;
                STBTT_free(comp_verts, info->userdata);
                num_vertices += comp_num_verts;
            }
            // More components ?
            more = flags & (1 << 5);
        }
    } else {
        // numberOfContours == 0, do nothing
    }

    *pvertices = vertices;
    return num_vertices;
}

int R_GetGlyphShape(const stbtt_fontinfo *info, int glyph_index, stbtt_vertex **pvertices) {
    // TrueType only
    return stbtt__GetGlyphShapeTT(info, glyph_index, pvertices);
}

void R_FreeGlyphShape(const stbtt_fontinfo *info, stbtt_vertex *v) {
    STBTT_free(v, info->userdata);
}

// -------------------------------------------------------------------------
//  bitmap bounding box
// -------------------------------------------------------------------------

void R_GetGlyphBitmapBoxSubpixel(const stbtt_fontinfo *font, int glyph, float scale_x, float scale_y, float shift_x, float shift_y, int *ix0, int *iy0, int *ix1, int *iy1) {
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    if (!R_GetGlyphBox(font, glyph, &x0, &y0, &x1, &y1)) {
        // e.g. space character
        if (ix0)
            *ix0 = 0;
        if (iy0)
            *iy0 = 0;
        if (ix1)
            *ix1 = 0;
        if (iy1)
            *iy1 = 0;
    } else {
        // move to integral bboxes (treating pixels as little squares, what pixels get touched)?
        if (ix0)
            *ix0 = STBTT_ifloor(x0 * scale_x + shift_x);
        if (iy0)
            *iy0 = STBTT_ifloor(-y1 * scale_y + shift_y);
        if (ix1)
            *ix1 = STBTT_iceil(x1 * scale_x + shift_x);
        if (iy1)
            *iy1 = STBTT_iceil(-y0 * scale_y + shift_y);
    }
}

void R_GetGlyphBitmapBox(const stbtt_fontinfo *font, int glyph, float scale_x, float scale_y, int *ix0, int *iy0, int *ix1, int *iy1) {
    R_GetGlyphBitmapBoxSubpixel(font, glyph, scale_x, scale_y, 0.0f, 0.0f, ix0, iy0, ix1, iy1);
}

// -------------------------------------------------------------------------
//  antialiased rasteriser (stb_truetype version 2)
// -------------------------------------------------------------------------

typedef struct {
    float x, y;
} stbtt__point;

typedef struct {
    float x0, y0, x1, y1;
    int invert;
} stbtt__edge;

typedef struct stbtt__active_edge {
    struct stbtt__active_edge *next;
    float fx, fdx, fdy;
    float direction;
    float sy;
    float ey;
} stbtt__active_edge;

#define STBTT__COMPARE(a, b) ((a)->y0 < (b)->y0)

// small chunked allocator for active edges
typedef struct stbtt__hheap_chunk {
    struct stbtt__hheap_chunk *next;
} stbtt__hheap_chunk;

typedef struct stbtt__hheap {
    struct stbtt__hheap_chunk *head;
    void *first_free;
    int num_remaining_in_head_chunk;
} stbtt__hheap;

static void *stbtt__hheap_alloc(stbtt__hheap *hh, size_t size, void *userdata) {
    if (hh->first_free) {
        void *p = hh->first_free;
        hh->first_free = *(void **)p;
        return p;
    } else {
        if (hh->num_remaining_in_head_chunk == 0) {
            int count = (size < 32 ? 2000 : size < 128 ? 800 : 100);
            stbtt__hheap_chunk *c = (stbtt__hheap_chunk *)STBTT_malloc(sizeof(stbtt__hheap_chunk) + size * count, userdata);
            if (c == NULL)
                return NULL;
            c->next = hh->head;
            hh->head = c;
            hh->num_remaining_in_head_chunk = count;
        }
        --hh->num_remaining_in_head_chunk;
        return (char *)(hh->head) + sizeof(stbtt__hheap_chunk) + size * hh->num_remaining_in_head_chunk;
    }
}

static void stbtt__hheap_free(stbtt__hheap *hh, void *p) {
    *(void **)p = hh->first_free;
    hh->first_free = p;
}

static void stbtt__hheap_cleanup(stbtt__hheap *hh, void *userdata) {
    stbtt__hheap_chunk *c = hh->head;
    while (c) {
        stbtt__hheap_chunk *n = c->next;
        STBTT_free(c, userdata);
        c = n;
    }
}

static stbtt__active_edge *stbtt__new_active(stbtt__hheap *hh, stbtt__edge *e, int off_x, float start_point, void *userdata) {
    stbtt__active_edge *z = (stbtt__active_edge *)stbtt__hheap_alloc(hh, sizeof(*z), userdata);
    float dxdy = (e->x1 - e->x0) / (e->y1 - e->y0);
    if (!z)
        return z;
    z->fdx = dxdy;
    z->fdy = dxdy != 0.0f ? (1.0f / dxdy) : 0.0f;
    z->fx = e->x0 + dxdy * (start_point - e->y0);
    z->fx -= off_x;
    z->direction = e->invert ? 1.0f : -1.0f;
    z->sy = e->y0;
    z->ey = e->y1;
    z->next = 0;
    return z;
}

// the edge passed in here does not cross the vertical line at x or the vertical line at x+1
// (i.e. it has already been clipped to those)
static void stbtt__handle_clipped_edge(float *scanline, int x, stbtt__active_edge *e, float x0, float y0, float x1, float y1) {
    if (y0 == y1)
        return;
    if (y0 > e->ey)
        return;
    if (y1 < e->sy)
        return;
    if (y0 < e->sy) {
        x0 += (x1 - x0) * (e->sy - y0) / (y1 - y0);
        y0 = e->sy;
    }
    if (y1 > e->ey) {
        x1 += (x1 - x0) * (e->ey - y1) / (y1 - y0);
        y1 = e->ey;
    }

    if (x0 <= x && x1 <= x)
        scanline[x] += e->direction * (y1 - y0);
    else if (x0 >= x + 1 && x1 >= x + 1)
        ;
    else {
        // coverage = 1 - average x position
        scanline[x] += e->direction * (y1 - y0) * (1 - ((x0 - x) + (x1 - x)) / 2);
    }
}

static void stbtt__fill_active_edges_new(float *scanline, float *scanline_fill, int len, stbtt__active_edge *e, float y_top) {
    float y_bottom = y_top + 1;

    while (e) {
        // brute force every pixel

        // compute intersection points with top & bottom
        STBTT_assert(e->ey >= y_top);

        if (e->fdx == 0) {
            float x0 = e->fx;
            if (x0 < len) {
                if (x0 >= 0) {
                    stbtt__handle_clipped_edge(scanline, (int)x0, e, x0, y_top, x0, y_bottom);
                    stbtt__handle_clipped_edge(scanline_fill - 1, (int)x0 + 1, e, x0, y_top, x0, y_bottom);
                } else {
                    stbtt__handle_clipped_edge(scanline_fill - 1, 0, e, x0, y_top, x0, y_bottom);
                }
            }
        } else {
            float x0 = e->fx;
            float dx = e->fdx;
            float xb = x0 + dx;
            float x_top, x_bottom;
            float sy0, sy1;
            float dy = e->fdy;
            STBTT_assert(e->sy <= y_bottom && e->ey >= y_top);

            // compute endpoints of line segment clipped to this scanline (if the
            // line segment starts on this scanline. x0 is the intersection of the
            // line with y_top, but that may be off the line segment.
            if (e->sy > y_top) {
                x_top = x0 + dx * (e->sy - y_top);
                sy0 = e->sy;
            } else {
                x_top = x0;
                sy0 = y_top;
            }
            if (e->ey < y_bottom) {
                x_bottom = x0 + dx * (e->ey - y_top);
                sy1 = e->ey;
            } else {
                x_bottom = xb;
                sy1 = y_bottom;
            }

            if (x_top >= 0 && x_bottom >= 0 && x_top < len && x_bottom < len) {
                // from here on, we don't have to range check x values

                if ((int)x_top == (int)x_bottom) {
                    float height;
                    // simple case, only spans one pixel
                    int x = (int)x_top;
                    height = sy1 - sy0;
                    STBTT_assert(x >= 0 && x < len);
                    scanline[x] += e->direction * (1 - ((x_top - x) + (x_bottom - x)) / 2) * height;
                    scanline_fill[x] += e->direction * height; // everything right of this pixel is filled
                } else {
                    int x, x1, x2;
                    float y_crossing, y_final, step, sign, area;
                    // covers 2+ pixels
                    if (x_top > x_bottom) {
                        // flip scanline vertically; signed area is the same
                        float t;
                        sy0 = y_bottom - (sy0 - y_top);
                        sy1 = y_bottom - (sy1 - y_top);
                        t = sy0;
                        sy0 = sy1;
                        sy1 = t;
                        t = x_bottom;
                        x_bottom = x_top;
                        x_top = t;
                        dx = -dx;
                        dy = -dy;
                        t = x0;
                        x0 = xb;
                        xb = t;
                    }

                    x1 = (int)x_top;
                    x2 = (int)x_bottom;
                    // compute intersection with y axis at x1+1
                    y_crossing = y_top + dy * (x1 + 1 - x0);

                    // compute intersection with y axis at x2
                    y_final = y_top + dy * (x2 - x0);

                    //           x1    x_top                            x2    x_bottom
                    //     y_top  +------|-----+------------+------------+--------|---+------------+
                    //            |            |            |            |            |            |
                    //            |            |            |            |            |            |
                    //       sy0  |      Txxxxx|............|............|............|............|
                    // y_crossing |            *xxxxx.......|............|............|............|
                    //            |            |     xxxxx..|............|............|............|
                    //            |            |     /-   xx*xxxx........|............|............|
                    //            |            | dy <       |    xxxxxx..|............|............|
                    //   y_final  |            |     \-     |          xx*xxx.........|............|
                    //       sy1  |            |            |            |   xxxxxB...|............|
                    //            |            |            |            |            |            |
                    //            |            |            |            |            |            |
                    //  y_bottom  +------------+------------+------------+------------+------------+
                    //
                    // goal is to measure the area covered by '.' in each pixel

                    // if x2 is right at the right edge of x1, y_crossing can blow up, github #1057
                    if (y_crossing > sy1)
                        y_crossing = sy1;

                    sign = e->direction;

                    // area of the rectangle covered from sy0..y_crossing
                    area = sign * (y_crossing - sy0);

                    // area of the triangle (x_top,sy0), (x1+1,sy0), (x1+1,y_crossing)
                    scanline[x1] += area * (1 - ((x_top - x1) + (x1 + 1 - x1)) / 2);

                    // check if final y_crossing is blown up; no test case for this
                    if (y_final > y_bottom) {
                        y_final = y_bottom;
                        dy = (y_final - y_crossing) / (x2 - (x1 + 1)); // if denom=0, y_final = y_crossing, so y_final <= y_bottom
                    }

                    // in second pixel, area covered by line segment found in first pixel
                    // is always a rectangle 1 wide * the height of that line segment; this
                    // is exactly what the variable 'step' is
                    step = sign * dy * 1; // dy is dy/dx, change in y for every 1 change in x,

                    for (x = x1 + 1; x < x2; ++x) {
                        scanline[x] += area + step / 2; // area of trapezoid is 1*step/2
                        area += step;
                    }
                    STBTT_assert(STBTT_fabs(area) <= 1.01f); // accumulated error from area += step unless we round step down
                    STBTT_assert(sy1 > y_final - 0.01f);

                    // area covered in the last pixel is the rectangle from all the pixels to the left,
                    // plus the trapezoid filled by the line segment in this pixel all the way to the right edge
                    scanline[x2] += area + sign * (1 - ((x2 - x2) + (x_bottom - x2)) / 2) * (sy1 - y_final);

                    // the rest of the line is filled based on the total height of the line segment in this pixel
                    scanline_fill[x2] += sign * (sy1 - sy0);
                }
            } else {
                // if edge goes outside of box we're drawing, we require
                // clipping logic. since this does not match the intended use
                // of this library, we use a different, very slow brute
                // force implementation
                int x;
                for (x = 0; x < len; ++x) {
                    // cases:
                    //
                    // there can be up to two intersections with the pixel. any
                    // intersection with left or right edges can be handled by
                    // clipping. intersections with top & bottom do not necessarily
                    // require any special handling if we can figure out where the
                    // intersections occur.
                    float y0 = y_top;
                    float x1 = (float)(x);
                    float x2 = (float)(x + 1);
                    float x3 = xb;
                    float y3 = y_bottom;

                    // x = e->x + e->dx * (y-y_top)
                    // (y-y_top) = (x - e->x) / e->dx
                    // y = (x - e->x) / e->dx + y_top
                    float y1 = (x - x0) / dx + y_top;
                    float y2 = (x + 1 - x0) / dx + y_top;

                    if (x0 < x1 && xb > x2) { // three segments descending down-right
                        stbtt__handle_clipped_edge(scanline, x, e, x0, y0, x1, y1);
                        stbtt__handle_clipped_edge(scanline, x, e, x1, y1, x2, y2);
                        stbtt__handle_clipped_edge(scanline, x, e, x2, y2, x3, y3);
                    } else if (x3 < x1 && x0 > x2) { // three segments descending down-left
                        stbtt__handle_clipped_edge(scanline, x, e, x0, y0, x2, y2);
                        stbtt__handle_clipped_edge(scanline, x, e, x2, y2, x1, y1);
                        stbtt__handle_clipped_edge(scanline, x, e, x1, y1, x3, y3);
                    } else if (x0 < x1 && xb > x1) { // two segments across x, down-right
                        stbtt__handle_clipped_edge(scanline, x, e, x0, y0, x1, y1);
                        stbtt__handle_clipped_edge(scanline, x, e, x1, y1, x3, y3);
                    } else if (x3 < x1 && x0 > x1) { // two segments across x, down-left
                        stbtt__handle_clipped_edge(scanline, x, e, x0, y0, x1, y1);
                        stbtt__handle_clipped_edge(scanline, x, e, x1, y1, x3, y3);
                    } else if (x0 < x2 && xb > x2) { // two segments across x+1, down-right
                        stbtt__handle_clipped_edge(scanline, x, e, x0, y0, x2, y2);
                        stbtt__handle_clipped_edge(scanline, x, e, x2, y2, x3, y3);
                    } else if (x3 < x2 && x0 > x2) { // two segments across x+1, down-left
                        stbtt__handle_clipped_edge(scanline, x, e, x0, y0, x2, y2);
                        stbtt__handle_clipped_edge(scanline, x, e, x2, y2, x3, y3);
                    } else { // one segment
                        stbtt__handle_clipped_edge(scanline, x, e, x0, y0, x3, y3);
                    }
                }
            }
        }
        e = e->next;
    }
}

// directly AA rasterize edges w/o supersampling
static void stbtt__rasterize_sorted_edges(unsigned char *bitmap_pixels, int result_w, int result_h, int result_stride, stbtt__edge *e, int n, int vsubsample, int off_x, int off_y, void *userdata) {
    stbtt__hheap hh;
    stbtt__active_edge *active = NULL;
    int y, j = 0, i;
    float scanline_data[129], *scanline, *scanline2;

    (void)vsubsample;

    hh.head = 0;
    hh.first_free = 0;
    hh.num_remaining_in_head_chunk = 0;

    if (result_w > 64)
        scanline = (float *)STBTT_malloc((result_w * 2 + 1) * sizeof(float), userdata);
    else
        scanline = scanline_data;

    scanline2 = scanline + result_w;

    y = off_y;
    e[n].y0 = (float)(off_y + result_h) + 1;

    while (j < result_h) {
        // find center of pixel for this scanline
        float scan_y_top = y + 0.0f;
        float scan_y_bottom = y + 1.0f;
        stbtt__active_edge **step = &active;

        memset(scanline, 0, result_w * sizeof(scanline[0]));
        memset(scanline2, 0, (result_w + 1) * sizeof(scanline[0]));

        // update all active edges;
        // remove all active edges that terminate before the top of this scanline
        while (*step) {
            stbtt__active_edge *z = *step;
            if (z->ey <= scan_y_top) {
                *step = z->next; // delete from list
                STBTT_assert(z->direction);
                z->direction = 0;
                stbtt__hheap_free(&hh, z);
            } else {
                step = &((*step)->next); // advance through list
            }
        }

        // insert all edges that start before the bottom of this scanline
        while (e->y0 <= scan_y_bottom) {
            if (e->y0 != e->y1) {
                stbtt__active_edge *z = stbtt__new_active(&hh, e, off_x, scan_y_top, userdata);
                if (z != NULL) {
                    STBTT_assert(z->ey >= scan_y_top);
                    // insert at front
                    z->next = active;
                    active = z;
                }
            }
            ++e;
        }

        // now process all active edges
        if (active)
            stbtt__fill_active_edges_new(scanline, scanline2 + 1, result_w, active, scan_y_top);

        {
            float sum = 0;
            for (i = 0; i < result_w; ++i) {
                float k;
                int m;
                sum += scanline2[i];
                k = scanline[i] + sum;
                k = (float)STBTT_fabs(k) * 255 + 0.5f;
                m = (int)k;
                if (m > 255)
                    m = 255;
                bitmap_pixels[j * result_stride + i] = (unsigned char)m;
            }
        }
        // advance all the edges
        step = &active;
        while (*step) {
            stbtt__active_edge *z = *step;
            z->fx += z->fdx; // advance to position for current scanline
            step = &((*step)->next); // advance through list
        }

        ++y;
        ++j;
    }

    stbtt__hheap_cleanup(&hh, userdata);

    if (scanline != scanline_data)
        STBTT_free(scanline, userdata);
}

#define STBTT__COMPARE_EDGE(a, b) ((a)->y0 < (b)->y0)

static void stbtt__sort_edges_ins_sort(stbtt__edge *p, int n) {
    int i, j;
    for (i = 1; i < n; ++i) {
        stbtt__edge t = p[i], *a = &t;
        j = i;
        while (j > 0) {
            stbtt__edge *b = &p[j - 1];
            int c = STBTT__COMPARE_EDGE(a, b);
            if (!c)
                break;
            p[j] = p[j - 1];
            --j;
        }
        if (i != j)
            p[j] = t;
    }
}

static void stbtt__sort_edges_quicksort(stbtt__edge *p, int n) {
    /* threshold for transitioning to insertion sort */
    while (n > 12) {
        stbtt__edge t;
        int c01, c12, c, m, i, j;

        /* compute median of three */
        m = n >> 1;
        c01 = STBTT__COMPARE_EDGE(&p[0], &p[m]);
        c12 = STBTT__COMPARE_EDGE(&p[m], &p[n - 1]);
        /* if 0 >= mid >= end, or 0 < mid < end, then use mid */
        if (c01 != c12) {
            /* otherwise, we'll need to swap something else to middle */
            int z;
            c = STBTT__COMPARE_EDGE(&p[0], &p[n - 1]);
            /* 0>mid && mid<n:  0>n => n; 0<n => 0 */
            /* 0<mid && mid>n:  0>n => 0; 0<n => n */
            z = (c == c12) ? 0 : n - 1;
            t = p[z];
            p[z] = p[m];
            p[m] = t;
        }
        /* now p[m] is the median-of-three */
        /* swap it to the beginning so it won't move around */
        t = p[0];
        p[0] = p[m];
        p[m] = t;

        /* partition loop */
        i = 1;
        j = n - 1;
        for (;;) {
            /* handling of equality is crucial here */
            /* for sentinels & efficiency with duplicates */
            for (;; ++i) {
                if (!STBTT__COMPARE_EDGE(&p[i], &p[0]))
                    break;
            }
            for (;; --j) {
                if (!STBTT__COMPARE_EDGE(&p[0], &p[j]))
                    break;
            }
            /* make sure we haven't crossed */
            if (i >= j)
                break;
            t = p[i];
            p[i] = p[j];
            p[j] = t;

            ++i;
            --j;
        }
        /* recurse on smaller side, iterate on larger */
        if (j < (n - i)) {
            stbtt__sort_edges_quicksort(p, j);
            p = p + i;
            n = n - i;
        } else {
            stbtt__sort_edges_quicksort(p + i, n - i);
            n = j;
        }
    }
}

static void stbtt__sort_edges(stbtt__edge *p, int n) {
    stbtt__sort_edges_quicksort(p, n);
    stbtt__sort_edges_ins_sort(p, n);
}

static void stbtt__rasterize(unsigned char *result_pixels, int result_w, int result_h, int result_stride, stbtt__point *pts, int *wcount, int windings, float scale_x, float scale_y, float shift_x, float shift_y, int off_x, int off_y, int invert, void *userdata) {
    float y_scale_inv = invert ? -scale_y : scale_y;
    stbtt__edge *e;
    int n, i, j, k, m;
    int vsubsample = 1;

    // now we have to blow out the windings into explicit edge lists
    n = 0;
    for (i = 0; i < windings; ++i)
        n += wcount[i];

    e = (stbtt__edge *)STBTT_malloc(sizeof(*e) * (n + 1), userdata); // add an extra one as a sentinel
    if (e == 0)
        return;
    n = 0;

    m = 0;
    for (i = 0; i < windings; ++i) {
        stbtt__point *p = pts + m;
        m += wcount[i];
        j = wcount[i] - 1;
        for (k = 0; k < wcount[i]; j = k++) {
            int a = k, b = j;
            // skip the edge if horizontal
            if (p[j].y == p[k].y)
                continue;
            // add edge from j to k to the list
            e[n].invert = 0;
            if (invert ? p[j].y > p[k].y : p[j].y < p[k].y) {
                e[n].invert = 1;
                a = j, b = k;
            }
            e[n].x0 = p[a].x * scale_x + shift_x;
            e[n].y0 = (p[a].y * y_scale_inv + shift_y) * vsubsample;
            e[n].x1 = p[b].x * scale_x + shift_x;
            e[n].y1 = (p[b].y * y_scale_inv + shift_y) * vsubsample;
            ++n;
        }
    }

    // now sort the edges by their highest point (should snap to integer, and then by x)
    stbtt__sort_edges(e, n);

    // now, traverse the scanlines and find the intersections on each scanline, use xor winding rule
    stbtt__rasterize_sorted_edges(result_pixels, result_w, result_h, result_stride, e, n, vsubsample, off_x, off_y, userdata);

    STBTT_free(e, userdata);
}

static void stbtt__add_point(stbtt__point *points, int n, float x, float y) {
    if (!points)
        return; // during first pass, it's unallocated
    points[n].x = x;
    points[n].y = y;
}

// tessellate until threshold p is happy... @TODO warped to compensate for non-linear stretching
static int stbtt__tesselate_curve(stbtt__point *points, int *num_points, float x0, float y0, float x1, float y1, float x2, float y2, float objspace_flatness_squared, int n) {
    // midpoint
    float mx = (x0 + 2 * x1 + x2) / 4;
    float my = (y0 + 2 * y1 + y2) / 4;
    // versus directly drawn line
    float dx = (x0 + x2) / 2 - mx;
    float dy = (y0 + y2) / 2 - my;
    if (n > 16) // 65536 segments on one curve better be enough!
        return 1;
    if (dx * dx + dy * dy > objspace_flatness_squared) { // half-pixel error allowed... need to be smaller if AA
        stbtt__tesselate_curve(points, num_points, x0, y0, (x0 + x1) / 2.0f, (y0 + y1) / 2.0f, mx, my, objspace_flatness_squared, n + 1);
        stbtt__tesselate_curve(points, num_points, mx, my, (x1 + x2) / 2.0f, (y1 + y2) / 2.0f, x2, y2, objspace_flatness_squared, n + 1);
    } else {
        stbtt__add_point(points, *num_points, x2, y2);
        *num_points = *num_points + 1;
    }
    return 1;
}

static void stbtt__tesselate_cubic(stbtt__point *points, int *num_points, float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float objspace_flatness_squared, int n) {
    // @TODO this "flatness" calculation is just made-up nonsense that seems to work well enough
    float dx0 = x1 - x0;
    float dy0 = y1 - y0;
    float dx1 = x2 - x1;
    float dy1 = y2 - y1;
    float dx2 = x3 - x2;
    float dy2 = y3 - y2;
    float dx = x3 - x0;
    float dy = y3 - y0;
    float longlen = (float)(STBTT_sqrt(dx0 * dx0 + dy0 * dy0) + STBTT_sqrt(dx1 * dx1 + dy1 * dy1) + STBTT_sqrt(dx2 * dx2 + dy2 * dy2));
    float shortlen = (float)STBTT_sqrt(dx * dx + dy * dy);
    float flatness_squared = longlen * longlen - shortlen * shortlen;

    if (n > 16) // 65536 segments on one curve better be enough!
        return;

    if (flatness_squared > objspace_flatness_squared) {
        float x01 = (x0 + x1) / 2;
        float y01 = (y0 + y1) / 2;
        float x12 = (x1 + x2) / 2;
        float y12 = (y1 + y2) / 2;
        float x23 = (x2 + x3) / 2;
        float y23 = (y2 + y3) / 2;

        float xa = (x01 + x12) / 2;
        float ya = (y01 + y12) / 2;
        float xb = (x12 + x23) / 2;
        float yb = (y12 + y23) / 2;

        float mx = (xa + xb) / 2;
        float my = (ya + yb) / 2;

        stbtt__tesselate_cubic(points, num_points, x0, y0, x01, y01, xa, ya, mx, my, objspace_flatness_squared, n + 1);
        stbtt__tesselate_cubic(points, num_points, mx, my, xb, yb, x23, y23, x3, y3, objspace_flatness_squared, n + 1);
    } else {
        stbtt__add_point(points, *num_points, x3, y3);
        *num_points = *num_points + 1;
    }
}

// returns number of contours
static stbtt__point *stbtt_FlattenCurves(stbtt_vertex *vertices, int num_verts, float objspace_flatness, int **contour_lengths, int *num_contours, void *userdata) {
    stbtt__point *points = 0;
    int num_points = 0;

    float objspace_flatness_squared = objspace_flatness * objspace_flatness;
    int i, n = 0, start = 0, pass;

    // count how many "moves" there are to get the contour count
    for (i = 0; i < num_verts; ++i)
        if (vertices[i].type == STBTT_vmove)
            ++n;

    *num_contours = n;
    if (n == 0)
        return 0;

    *contour_lengths = (int *)STBTT_malloc(sizeof(**contour_lengths) * n, userdata);

    if (*contour_lengths == 0) {
        *num_contours = 0;
        return 0;
    }

    // make two passes through the points so we don't need to realloc
    for (pass = 0; pass < 2; ++pass) {
        float x = 0, y = 0;
        if (pass == 1) {
            points = (stbtt__point *)STBTT_malloc(num_points * sizeof(points[0]), userdata);
            if (points == 0)
                goto error;
        }
        num_points = 0;
        n = -1;
        for (i = 0; i < num_verts; ++i) {
            switch (vertices[i].type) {
                case STBTT_vmove:
                    // start the next contour
                    if (n >= 0)
                        (*contour_lengths)[n] = num_points - start;
                    ++n;
                    start = num_points;

                    x = vertices[i].x, y = vertices[i].y;
                    stbtt__add_point(points, num_points++, x, y);
                    break;
                case STBTT_vline:
                    x = vertices[i].x, y = vertices[i].y;
                    stbtt__add_point(points, num_points++, x, y);
                    break;
                case STBTT_vcurve:
                    stbtt__tesselate_curve(points, &num_points, x, y,
                                           vertices[i].cx, vertices[i].cy,
                                           vertices[i].x, vertices[i].y,
                                           objspace_flatness_squared, 0);
                    x = vertices[i].x, y = vertices[i].y;
                    break;
                case STBTT_vcubic:
                    stbtt__tesselate_cubic(points, &num_points, x, y,
                                           vertices[i].cx, vertices[i].cy,
                                           vertices[i].cx1, vertices[i].cy1,
                                           vertices[i].x, vertices[i].y,
                                           objspace_flatness_squared, 0);
                    x = vertices[i].x, y = vertices[i].y;
                    break;
            }
        }
        (*contour_lengths)[n] = num_points - start;
    }

    return points;
error:
    STBTT_free(points, userdata);
    STBTT_free(*contour_lengths, userdata);
    *contour_lengths = 0;
    *num_contours = 0;
    return NULL;
}

static void stbtt_Rasterize(unsigned char *result_pixels, int result_w, int result_h, int result_stride, float flatness_in_pixels, stbtt_vertex *vertices, int num_verts, float scale_x, float scale_y, float shift_x, float shift_y, int x_off, int y_off, int invert, void *userdata) {
    float scale = scale_x > scale_y ? scale_y : scale_x;
    int winding_count = 0;
    int *winding_lengths = NULL;
    stbtt__point *windings = stbtt_FlattenCurves(vertices, num_verts, flatness_in_pixels / scale, &winding_lengths, &winding_count, userdata);
    if (windings) {
        stbtt__rasterize(result_pixels, result_w, result_h, result_stride, windings, winding_lengths, winding_count, scale_x, scale_y, shift_x, shift_y, x_off, y_off, invert, userdata);
        STBTT_free(winding_lengths, userdata);
        STBTT_free(windings, userdata);
    }
}

// -------------------------------------------------------------------------
//  glyph bitmap generation
// -------------------------------------------------------------------------

void R_MakeGlyphBitmapSubpixel(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, float shift_x, float shift_y, int glyph) {
    int ix0, iy0;
    stbtt_vertex *vertices;
    int num_verts = R_GetGlyphShape(info, glyph, &vertices);

    R_GetGlyphBitmapBoxSubpixel(info, glyph, scale_x, scale_y, shift_x, shift_y, &ix0, &iy0, 0, 0);

    if (out_w && out_h)
        stbtt_Rasterize(output, out_w, out_h, out_stride, 0.35f, vertices, num_verts, scale_x, scale_y, shift_x, shift_y, ix0, iy0, 1, info->userdata);

    STBTT_free(vertices, info->userdata);
}

void R_MakeGlyphBitmap(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, int glyph) {
    R_MakeGlyphBitmapSubpixel(info, output, out_w, out_h, out_stride, scale_x, scale_y, 0.0f, 0.0f, glyph);
}
