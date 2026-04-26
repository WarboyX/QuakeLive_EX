/*
===========================================================================
Quake Live font subsystem - TrueType rasteriser (tr_stbtt)

Faithful reimplementation of the TrueType glyph rasteriser Quake Live's engine
ships.  In the QL binary these functions are renamed with an "R_" prefix
(R_LoadFontTables, R_GetFontGlyphIndex, R_RasterizeGlyphOutline, ...), a
renamed copy of Sean Barrett's public-domain stb_truetype.  This port keeps the
QL names as public symbols and reproduces the algorithms (glyf outline
extraction, the version-2 antialiased sorted-edge scanline rasteriser,
hmtx/hhea metrics, kern format 0), matching observed QL behaviour (flatness
0.35, 8-bit coverage output).

Only the TrueType ('glyf') outline path is implemented.  Every font QL ships
(handelgothic, notosans-regular, droidsansmono, droidsansfallbackfull) is a
TrueType file.  CFF/Type2 ('CFF ') outlines are not handled.

C89.  No GL, no engine dependencies beyond the common headers.
===========================================================================
*/

#ifndef __TR_STBTT_H__
#define __TR_STBTT_H__

// vertex tag values for stbtt_vertex.type
#define STBTT_vmove 1
#define STBTT_vline 2
#define STBTT_vcurve 3
#define STBTT_vcubic 4

typedef struct {
    void *userdata;
    unsigned char *data; // pointer to .ttf file
    int fontstart;       // offset of start of font

    int numGlyphs; // number of glyphs, needed for range checking

    int loca, head, glyf, hhea, hmtx, kern, gpos; // table locations as offset from start of .ttf
    int index_map;                                // a cmap mapping for our chosen character encoding
    int indexToLocFormat;                         // format needed to map from glyph index to glyph
} stbtt_fontinfo;

// a single point of a glyph outline; coordinates are in font units
typedef struct {
    short x, y, cx, cy, cx1, cy1;
    unsigned char type, padding;
} stbtt_vertex;

//
// font loading
//

// Initialise a font from a TrueType blob.  'data' points at the file, 'offset'
// is the byte offset of the font within it (0 for a plain .ttf).  Returns 1 on
// success, 0 on failure.  (QL: R_LoadFontTables == stbtt_InitFont.)
int R_LoadFontTables(stbtt_fontinfo *info, const unsigned char *data, int offset);

// Map a Unicode codepoint to a glyph index (0 = .notdef).  (QL: stbtt_FindGlyphIndex)
int R_GetFontGlyphIndex(const stbtt_fontinfo *info, int unicode_codepoint);

//
// metrics
//

// units-per-em from the head table (QL: R_GetFontUnitsPerEm)
int R_GetFontUnitsPerEm(const stbtt_fontinfo *info);

// vertical metrics in font units (QL: R_GetFontLineMetrics == stbtt_GetFontVMetrics)
void R_GetFontLineMetrics(const stbtt_fontinfo *info, int *ascent, int *descent, int *lineGap);

// horizontal metrics for a glyph, in font units (QL: R_GetGlyphMetrics)
void R_GetGlyphHMetrics(const stbtt_fontinfo *info, int glyph_index, int *advanceWidth, int *leftSideBearing);

// additional kern advance between two glyphs, in font units (QL: R_GetGlyphKerning)
int R_GetGlyphKernAdvance(const stbtt_fontinfo *info, int glyph1, int glyph2);

// scale factor to map font units to a given pixel height (em box)
float R_ScaleForPixelHeight(const stbtt_fontinfo *info, float pixels);

// scale factor to map font units to a given pixel EM size
float R_ScaleForMappingEmToPixels(const stbtt_fontinfo *info, float pixels);

//
// glyph shapes and rasterisation
//

// glyph bounding box in font units; returns 0 for empty glyphs (QL: R_GetGlyphOutline box path)
int R_GetGlyphBox(const stbtt_fontinfo *info, int glyph_index, int *x0, int *y0, int *x1, int *y1);

// extract the outline of a glyph as a vertex list.  Caller frees *vertices via
// R_FreeGlyphShape.  Returns the number of vertices.  (QL: R_BuildGlyphContour /
// stbtt_GetGlyphShape.)
int R_GetGlyphShape(const stbtt_fontinfo *info, int glyph_index, stbtt_vertex **vertices);
void R_FreeGlyphShape(const stbtt_fontinfo *info, stbtt_vertex *vertices);

// pixel-space bounding box for a scaled glyph, with sub-pixel shift
// (QL: stbtt_GetGlyphBitmapBoxSubpixel).
void R_GetGlyphBitmapBoxSubpixel(const stbtt_fontinfo *font, int glyph, float scale_x, float scale_y, float shift_x, float shift_y, int *ix0, int *iy0, int *ix1, int *iy1);
void R_GetGlyphBitmapBox(const stbtt_fontinfo *font, int glyph, float scale_x, float scale_y, int *ix0, int *iy0, int *ix1, int *iy1);

// rasterise a scaled glyph into an existing 8-bit single-channel coverage
// buffer 'output' of dimensions out_w x out_h with row stride out_stride.
// (QL: R_MakeGlyphBitmapSubpixel == stbtt_MakeGlyphBitmapSubpixel.)
void R_MakeGlyphBitmapSubpixel(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, float shift_x, float shift_y, int glyph);
void R_MakeGlyphBitmap(const stbtt_fontinfo *info, unsigned char *output, int out_w, int out_h, int out_stride, float scale_x, float scale_y, int glyph);

#endif // __TR_STBTT_H__
