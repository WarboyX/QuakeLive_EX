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

#include "ui_local.h"

uiInfo_t uiInfo;

static char* netnames[] = {
    "???",
    "UDP",
    "UDP6"};

static int gamecodetoui[] = {4, 2, 3, 0, 5, 1, 6};

static const char *skillLevels[] = {
    "I Can Win",
    "Bring It On",
    "Hurt Me Plenty",
    "Hardcore",
    "Nightmare"
};
static const int numSkillLevels = ARRAY_LEN(skillLevels);

static void UI_FeederSelection(float feederID, int index);
static const char* UI_GetGameTypeString(int gametype);
static int UI_MapCountByGameType(qboolean singlePlayer);
static int UI_MapCountByCallvoteGameType(void);
static void UI_ParseGameInfo(const char* teamFile);
static void UI_ParseTeamInfo(const char* teamFile);
static const char* UI_SelectedMap(int index, int* actual);
static const char* UI_SelectedHead(int index, int* actual);
static void UI_DrawCinematic(int handle, float x, float y, float w, float h);

// [QL] Server-browser forward declarations (definitions live just before the feeders).
static void UI_InsertServerIntoDisplayList(int num, int position);
static void UI_RemoveServerFromDisplayList(int num);
static void UI_BinaryServerInsert(int num);
static void UI_BuildServerDisplayList(int force);
static void UI_SortServerStatusInfo(serverStatusInfo_t *info);
static qboolean UI_GetServerStatusInfo(const char *serverAddress, serverStatusInfo_t *info);
static void UI_BuildServerStatus(qboolean force);
static void UI_BuildFindPlayerList(qboolean force);
static void UI_ServersSort(int column, qboolean force);
static void UI_DoServerRefresh(void);
static void UI_ServerRefreshComplete(void);  // QL name for missionpack UI_StopServerRefresh
static void UI_StartServerRefresh(qboolean full);

// [QL] Owner-draw + key-handler helpers for the skill and server-browser widgets
// (definitions live further down). Forward-declared to satisfy the switch dispatchers.
static void UI_DrawSkill(rectDef_t *rect, float scale, vec4_t color, int textStyle);
static void UI_DrawNetSource(rectDef_t *rect, float scale, vec4_t color, int textStyle);
static void UI_DrawServerFilter(rectDef_t *rect, float scale, vec4_t color, int textStyle);
static void UI_DrawMOTD(rectDef_t *rect, float scale, vec4_t color, int textStyle);
static qboolean UI_Skill_HandleKey(int flags, float *special, int key);
static qboolean UI_NetSource_HandleKey(int flags, float *special, int key);
static qboolean UI_ServerFilter_HandleKey(int flags, float *special, int key);

// [QL] Net-source names, uix86.dll table @0x1002ae38 (indexed by UI_DrawNetSource). The
// clamp there admits index 4, one past the real sources, so entry [4] mirrors the binary's
// adjacent-rodata spillover ("FFA"); it only shows if ui_netSource is forced to 4.
static const char *netSourceNames[] = {
    "Local",
    "Mplayer",
    "Internet",
    "Favorites",
    "FFA"
};

// [QL] Month abbreviations for the ui_lastServerRefresh_%i timestamp cvar.
static const char *monthAbbrev[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

// [QL] Server browser "game" (mod) filter table, verified from uix86.dll @0x100239bc
// (7 {description, basedir} pairs). .basedir is compared against the server info "game"
// key when ui_serverFilterType > 0.
static serverFilter_t serverFilters[] = {
    { "All",                   "" },
    { "Quake Live",            "" },
    { "Team Arena",            "missionpack" },
    { "Rocket Arena",          "arena" },
    { "Alliance",              "alliance20" },
    { "Weapons Factory Arena", "wfa" },
    { "OSP",                   "osp" }
};

/*
================
vmMain

This is the only way control passes into the module.
This must be the very first function compiled into the .qvm file
================
*/
vmCvar_t ui_debug;
vmCvar_t ui_debugMenus;
vmCvar_t ui_initialized;

void _UI_Init(qboolean);
void _UI_Shutdown(void);
void _UI_KeyEvent(int key, qboolean down);
void _UI_MouseEvent(int dx, int dy);
void _UI_Refresh(int realtime);
qboolean _UI_IsFullscreen(void);
qboolean UI_CheckActiveMenu(void);
void UI_WalkMenus(void (*callback)(const char* mapLoadName));
// [QL] uix86.dll postgame scoreboard + cvar-change callbacks (forward decls)
static void UI_SetScoreBoardCvars(const postGameInfo_t* info, qboolean redBlue);
static void UI_SPPostgameMenu_f(const char* mapname, int gameType);
static void UI_CheckModelBright(const char* modelCvar, const char* skinCvar, const char* targetCvar);
static void UI_OnAnnouncerChanged(void);
Q_EXPORT intptr_t vmMain(int command, intptr_t arg0, intptr_t arg1, intptr_t arg2, intptr_t arg3, intptr_t arg4, intptr_t arg5, intptr_t arg6, intptr_t arg7, intptr_t arg8, intptr_t arg9, intptr_t arg10, intptr_t arg11) {
    switch (command) {
        case UI_GETAPIVERSION:
            return UI_API_VERSION;

        case UI_INIT:
            _UI_Init(arg0);
            return 0;

        case UI_SHUTDOWN:
            _UI_Shutdown();
            return 0;

        case UI_KEY_EVENT:
            _UI_KeyEvent(arg0, arg1);
            return 0;

        case UI_MOUSE_EVENT:
            _UI_MouseEvent(arg0, arg1);
            return 0;

        case UI_REFRESH:
            _UI_Refresh(arg0);
            return 0;

        case UI_IS_FULLSCREEN:
            return _UI_IsFullscreen();

        case UI_SET_ACTIVE_MENU:
            _UI_SetActiveMenu(arg0);
            return 0;

        case UI_CONSOLE_COMMAND:
            return UI_ConsoleCommand(arg0);

        case UI_DRAW_CONNECT_SCREEN:
            UI_DrawConnectScreen(arg0);
            return 0;
        case UI_HASUNIQUECDKEY:  // [Q3 remnant] always true, QL uses Steam auth
            return qtrue;

        case UI_REGISTER_CVARS:
            UI_RegisterCvars();
            return 0;

        case UI_CHECK_ACTIVE_MENU:
            return UI_CheckActiveMenu();

        case UI_WALK_MENUS:
            UI_WalkMenus((void (*)(const char*))arg0);
            return 0;

        case UI_DRAW_ADVERTISEMENT:
            return 0;
    }

    return -1;
}

void AssetCache(void) {
    uiInfo.uiDC.Assets.gradientBar = trap_R_RegisterShaderNoMip(ASSET_GRADIENTBAR);
    uiInfo.uiDC.Assets.fxBasePic = trap_R_RegisterShaderNoMip(ART_FX_BASE);
    uiInfo.uiDC.Assets.fxPic[0] = trap_R_RegisterShaderNoMip(ART_FX_RED);
    uiInfo.uiDC.Assets.fxPic[1] = trap_R_RegisterShaderNoMip(ART_FX_YELLOW);
    uiInfo.uiDC.Assets.fxPic[2] = trap_R_RegisterShaderNoMip(ART_FX_GREEN);
    uiInfo.uiDC.Assets.fxPic[3] = trap_R_RegisterShaderNoMip(ART_FX_TEAL);
    uiInfo.uiDC.Assets.fxPic[4] = trap_R_RegisterShaderNoMip(ART_FX_BLUE);
    uiInfo.uiDC.Assets.fxPic[5] = trap_R_RegisterShaderNoMip(ART_FX_CYAN);
    uiInfo.uiDC.Assets.fxPic[6] = trap_R_RegisterShaderNoMip(ART_FX_WHITE);
    uiInfo.uiDC.Assets.scrollBar = trap_R_RegisterShaderNoMip(ASSET_SCROLLBAR);
    uiInfo.uiDC.Assets.scrollBarArrowDown = trap_R_RegisterShaderNoMip(ASSET_SCROLLBAR_ARROWDOWN);
    uiInfo.uiDC.Assets.scrollBarArrowUp = trap_R_RegisterShaderNoMip(ASSET_SCROLLBAR_ARROWUP);
    uiInfo.uiDC.Assets.scrollBarArrowLeft = trap_R_RegisterShaderNoMip(ASSET_SCROLLBAR_ARROWLEFT);
    uiInfo.uiDC.Assets.scrollBarArrowRight = trap_R_RegisterShaderNoMip(ASSET_SCROLLBAR_ARROWRIGHT);
    uiInfo.uiDC.Assets.scrollBarThumb = trap_R_RegisterShaderNoMip(ASSET_SCROLL_THUMB);
    uiInfo.uiDC.Assets.sliderBar = trap_R_RegisterShaderNoMip(ASSET_SLIDER_BAR);
    uiInfo.uiDC.Assets.sliderThumb = trap_R_RegisterShaderNoMip(ASSET_SLIDER_THUMB);

    for (int n = 1; n < NUM_CROSSHAIRS; n++) {
        uiInfo.uiDC.Assets.crosshairShader[n] = trap_R_RegisterShaderNoMip(va("gfx/2d/crosshair%i", n));
    }

    uiInfo.newHighScoreSound = trap_S_RegisterSound("sound/vo/new_high_score.ogg", qfalse);
}

void _UI_DrawSides(float x, float y, float w, float h, float size) {
    UI_AdjustFrom640(&x, &y, &w, &h);
    size *= uiInfo.uiDC.xscale;
    trap_R_DrawStretchPic(x, y, size, h, 0, 0, 0, 0, uiInfo.uiDC.whiteShader);
    trap_R_DrawStretchPic(x + w - size, y, size, h, 0, 0, 0, 0, uiInfo.uiDC.whiteShader);
}

void _UI_DrawTopBottom(float x, float y, float w, float h, float size) {
    UI_AdjustFrom640(&x, &y, &w, &h);
    size *= uiInfo.uiDC.yscale;
    trap_R_DrawStretchPic(x, y, w, size, 0, 0, 0, 0, uiInfo.uiDC.whiteShader);
    trap_R_DrawStretchPic(x, y + h - size, w, size, 0, 0, 0, 0, uiInfo.uiDC.whiteShader);
}

/*
================
UI_DrawRect

Coordinates are 640*480 virtual values
=================
*/
void _UI_DrawRect(float x, float y, float width, float height, float size, const float* color) {
    trap_R_SetColor(color);

    _UI_DrawTopBottom(x, y, width, height, size);
    _UI_DrawSides(x, y, width, height, size);

    trap_R_SetColor(NULL);
}

// [QL] Text is rendered by the engine glyph atlas (fontstash/stb_truetype) via
// trap_R_Font_DrawString / trap_R_Font_TextExtents.  The legacy per-glyph
// fontInfo_t path (FreeType) is gone; these helpers convert the UI's 640x480
// virtual coordinates and text scale into the screen pixels and pixel font size
// the engine expects.  Font index: 0 = handelgothic ("normal"), 1 = notosans
// ("sans"), 2 = droidsansmono ("mono").
static int UI_EngineFont(int fontIndex) {
    if (fontIndex >= 0 && fontIndex < 3)
        return fontIndex;
    return 0;
}

// Pixel font size for a text scale (QL: screenFontScale = (vidHeight/768)*96).
static float UI_FontPixelSize(float scale) {
    return (((float)uiInfo.uiDC.glconfig.vidHeight / 768.0f) * 96.0f) * scale;
}

// Convert 640x480 virtual coords to screen pixels (honours any widescreen bias
// inside UI_AdjustFrom640).
static void UI_TextToScreen(float* x, float* y) {
    float w = 0.0f, h = 0.0f;
    UI_AdjustFrom640(x, y, &w, &h);
}

// Core painter: virtual coords + scale -> screen pixels, engine glyph atlas.
static void UI_PaintText(float x, float y, int fontIndex, float scale, const vec4_t color,
                         const char* text, int limit, int style) {
    float sx = x, sy = y;
    float size = UI_FontPixelSize(scale);
    int efont = UI_EngineFont(fontIndex);
    int lim = (limit > 0) ? limit : -1;

    if (!text || !*text)
        return;

    UI_TextToScreen(&sx, &sy);

    if (style == ITEM_TEXTSTYLE_SHADOWED || style == ITEM_TEXTSTYLE_SHADOWEDMORE) {
        float ofs = (style == ITEM_TEXTSTYLE_SHADOWED) ? 1.0f : 2.0f;
        vec4_t shadow;
        shadow[0] = shadow[1] = shadow[2] = 0.0f;
        shadow[3] = color ? color[3] : 1.0f;
        trap_R_SetColor(shadow);
        trap_R_Font_DrawString((int)(sx + ofs), (int)(sy + ofs), text, efont, size, lim, NULL, TEXT_NORECOLOR);
    }

    trap_R_SetColor(color);
    trap_R_Font_DrawString((int)sx, (int)sy, text, efont, size, lim, NULL, 0);
    trap_R_SetColor(NULL);
}

// Core measure: 640-space width/height for the given text.
static void UI_MeasureText(const char* text, float scale, int fontIndex, int limit, int* w640, int* h640) {
    int wpx = 0, hpx = 0;
    // Convert measured screen pixels back to 640-space using the SAME scale the
    // draw path (UI_AdjustFrom640) uses - i.e. DC->xscale/yscale, which on a
    // widescreen display is the 4:3-preserving scale (vidHeight/480), not the
    // stretched vidWidth/640.  Matches QL UI_GetTextDimensions (0x10003d90),
    // which divides the engine extents by DC->xscale/DC->yscale.  Using the
    // stretch scale here made measured widths too small on widescreen, so
    // right-aligned/centred menu text landed too far right.
    float xscale = uiInfo.uiDC.xscale;
    float yscale = uiInfo.uiDC.yscale;
    float size = UI_FontPixelSize(scale);
    int lim = (limit > 0) ? limit : -1;

    trap_R_Font_TextExtents(text, 0, lim, size, UI_EngineFont(fontIndex), NULL, NULL, &wpx, &hpx);

    if (xscale <= 0.0f)
        xscale = 1.0f;
    if (yscale <= 0.0f)
        yscale = 1.0f;
    if (w640)
        *w640 = (int)((float)wpx / xscale);
    if (h640)
        *h640 = (int)((float)hpx / yscale);
}

int Text_Width(const char* text, float scale, int limit) {
    int w = 0;
    UI_MeasureText(text, scale, 0, limit, &w, NULL);
    return w;
}

int Text_Height(const char* text, float scale, int limit) {
    int h = 0;
    UI_MeasureText(text, scale, 0, limit, NULL, &h);
    return h;
}

void Text_PaintChar(float x, float y, float width, float height, float scale, float s, float t, float s2, float t2, qhandle_t hShader) {
    float w, h;
    w = width * scale;
    h = height * scale;
    UI_AdjustFrom640(&x, &y, &w, &h);
    trap_R_DrawStretchPic(x, y, w, h, s, t, s2, t2, hShader);
}

void Text_Paint(float x, float y, float scale, vec4_t color, const char* text, float adjust, int limit, int style) {
    (void)adjust;
    UI_PaintText(x, y, 0, scale, color, text, limit, style);
}

void Text_PaintWithCursor(float x, float y, float scale, vec4_t color, const char* text, int cursorPos, char cursor, int limit, int style) {
    // [QL] Draw the text through the engine glyph atlas, then draw the blinking
    // cursor glyph at the measured pen position (cursorPos chars into the text).
    char cbuf[2];

    if (!text)
        return;

    UI_PaintText(x, y, 0, scale, color, text, limit, style);

    if (!((uiInfo.uiDC.realTime / BLINK_DIVISOR) & 1)) {
        int w640 = 0;
        int tl = (int)strlen(text);
        int cp = cursorPos;
        if (cp < 0)
            cp = 0;
        if (cp > tl)
            cp = tl;
        if (cp > 0)
            UI_MeasureText(text, scale, 0, cp, &w640, NULL);
        cbuf[0] = cursor;
        cbuf[1] = '\0';
        UI_PaintText(x + (float)w640, y, 0, scale, color, cbuf, 1, style);
    }
}

static void Text_Paint_Limit(float* maxX, float x, float y, float scale, vec4_t color, const char* text, float adjust, int limit) {
    // [QL] Draw text truncated to fit within *maxX (a 640-space bound), then
    // report the resulting right edge - see the cgame counterpart.
    int w640;
    float avail = *maxX - x;
    (void)adjust;

    if (!text || !*text)
        return;

    w640 = Text_Width(text, scale, limit);
    if (avail > 0.0f && (float)w640 > avail) {
        int full = (limit > 0) ? limit : (int)strlen(text);
        int fit = (int)((float)full * (avail / (float)w640));
        if (fit < 0)
            fit = 0;
        limit = fit;
        w640 = Text_Width(text, scale, limit);
    }

    Text_Paint(x, y, scale, color, text, 0, limit, 0);
    *maxX = x + (float)w640;
}

// [QL] DC wrapper functions - pass the font index straight to the engine.
static void UI_DrawText_DC(float x, float y, float scale, vec4_t color, const char* text,
                           float adjust, int limit, int style, int fontIndex) {
    (void)adjust;
    UI_PaintText(x, y, fontIndex, scale, color, text, limit, style);
}

static float UI_TextWidth_DC(const char* text, float scale, int limit, int fontIndex) {
    int w = 0;
    UI_MeasureText(text, scale, fontIndex, limit, &w, NULL);
    return (float)w;
}

static float UI_TextHeight_DC(const char* text, float scale, int limit, int fontIndex) {
    int h = 0;
    UI_MeasureText(text, scale, fontIndex, limit, NULL, &h);
    return (float)h;
}

static void UI_DrawTextWithCursor_DC(float x, float y, float scale, vec4_t color, const char* text,
                                     int cursorPos, char cursor, int limit, int style, int fontIndex) {
    (void)cursorPos;
    (void)cursor;
    UI_PaintText(x, y, fontIndex, scale, color, text, limit, style);
}

static void UI_OwnerDraw(float x, float y, float w, float h, float text_x, float text_y, int ownerDraw, int ownerDrawFlags, int align, float special, float scale, vec4_t color, qhandle_t shader, int textStyle);

static void UI_OwnerDraw_DC(float x, float y, float w, float h, float text_x, float text_y,
                            int ownerDraw, int ownerDrawFlags, int align, float special,
                            float scale, vec4_t color, qhandle_t shader, int textStyle, int fontIndex) {
    UI_OwnerDraw(x, y, w, h, text_x, text_y, ownerDraw, ownerDrawFlags, align, special, scale, color, shader, textStyle);
}

void UI_ShowPostGame(qboolean newHigh) {
    trap_Cvar_Set("cg_cameraOrbit", "0");
    trap_Cvar_Set("cg_thirdPerson", "0");
    uiInfo.soundHighScore = newHigh;
    _UI_SetActiveMenu(UIMENU_POSTGAME);
}
/*
=================
_UI_Refresh
=================
*/

void UI_DrawCenteredPic(qhandle_t image, int w, int h) {
    int x, y;
    x = (SCREEN_WIDTH - w) / 2;
    y = (SCREEN_HEIGHT - h) / 2;
    UI_DrawHandlePic(x, y, w, h, image);
}

int frameCount = 0;
int startTime;

#define UI_FPS_FRAMES 125
void _UI_Refresh(int realtime) {
    static int index;
    static int previousTimes[UI_FPS_FRAMES];

    uiInfo.uiDC.frameTime = realtime - uiInfo.uiDC.realTime;
    uiInfo.uiDC.realTime = realtime;

    previousTimes[index % UI_FPS_FRAMES] = uiInfo.uiDC.frameTime;
    index++;
    if (index > UI_FPS_FRAMES) {
        int i, total;
        // average multiple frames together to smooth changes out a bit
        total = 0;
        for (i = 0; i < UI_FPS_FRAMES; i++) {
            total += previousTimes[i];
        }
        if (!total) {
            total = 1;
        }
        uiInfo.uiDC.FPS = 1000 * UI_FPS_FRAMES / total;
    }

    UI_UpdateCvars();

    if (Menu_Count() > 0) {
        // paint all the menus
        Menu_PaintAll();
        // refresh server browser list
        UI_DoServerRefresh();
        // refresh server status (QL inlines UI_BuildServerStatus(qfalse) here)
        UI_BuildServerStatus(qfalse);
        // refresh find player list
        UI_BuildFindPlayerList(qfalse);
    }

    // draw cursor
    UI_SetColor(NULL);
    if (Menu_Count() > 0 && (trap_Key_GetCatcher() & KEYCATCH_UI)) {
        UI_DrawHandlePic(uiInfo.uiDC.cursorx - 16, uiInfo.uiDC.cursory - 16, 32, 32, uiInfo.uiDC.Assets.cursor);
    }
}

/*
=================
_UI_Shutdown
=================
*/
void _UI_Shutdown(void) {
}

char* defaultMenu = NULL;

char* GetMenuBuffer(const char* filename) {
    int len;
    fileHandle_t f;
    static char buf[MAX_MENUFILE];

    len = trap_FS_FOpenFile(filename, &f, FS_READ);
    if (!f) {
        trap_Print(va(S_COLOR_RED "menu file not found: %s, using default\n", filename));
        return defaultMenu;
    }
    if (len >= MAX_MENUFILE) {
        trap_Print(va(S_COLOR_RED "menu file too large: %s is %i, max allowed is %i\n", filename, len, MAX_MENUFILE));
        trap_FS_FCloseFile(f);
        return defaultMenu;
    }

    trap_FS_Read(buf, len, f);
    buf[len] = 0;
    trap_FS_FCloseFile(f);
    // COM_Compress(buf);
    return buf;
}

qboolean Asset_Parse(int handle) {
    pc_token_t token;
    const char* tempStr;

    if (!trap_PC_ReadToken(handle, &token))
        return qfalse;
    if (Q_stricmp(token.string, "{") != 0) {
        return qfalse;
    }

    while (1) {
        memset(&token, 0, sizeof(pc_token_t));

        if (!trap_PC_ReadToken(handle, &token))
            return qfalse;

        if (Q_stricmp(token.string, "}") == 0) {
            return qtrue;
        }

        // font
        if (Q_stricmp(token.string, "font") == 0) {
            int pointSize;
            if (!PC_String_Parse(handle, &tempStr) || !PC_Int_Parse(handle, &pointSize)) {
                return qfalse;
            }
            trap_R_RegisterFont(DEFAULT_SANS_FONT, pointSize, &uiInfo.uiDC.Assets.textFont);
            uiInfo.uiDC.Assets.fontRegistered = qtrue;
            continue;
        }

        if (Q_stricmp(token.string, "smallFont") == 0) {
            int pointSize;
            if (!PC_String_Parse(handle, &tempStr) || !PC_Int_Parse(handle, &pointSize)) {
                return qfalse;
            }
            trap_R_RegisterFont(DEFAULT_MONO_FONT, pointSize, &uiInfo.uiDC.Assets.smallFont);
            continue;
        }

        if (Q_stricmp(token.string, "bigFont") == 0) {
            int pointSize;
            if (!PC_String_Parse(handle, &tempStr) || !PC_Int_Parse(handle, &pointSize)) {
                return qfalse;
            }
            trap_R_RegisterFont(DEFAULT_FONT, pointSize, &uiInfo.uiDC.Assets.bigFont);
            continue;
        }

        // gradientbar
        if (Q_stricmp(token.string, "gradientbar") == 0) {
            if (!PC_String_Parse(handle, &tempStr)) {
                return qfalse;
            }
            uiInfo.uiDC.Assets.gradientBar = trap_R_RegisterShaderNoMip(tempStr);
            continue;
        }

        // enterMenuSound
        if (Q_stricmp(token.string, "menuEnterSound") == 0) {
            if (!PC_String_Parse(handle, &tempStr)) {
                return qfalse;
            }
            uiInfo.uiDC.Assets.menuEnterSound = trap_S_RegisterSound(tempStr, qfalse);
            continue;
        }

        // exitMenuSound
        if (Q_stricmp(token.string, "menuExitSound") == 0) {
            if (!PC_String_Parse(handle, &tempStr)) {
                return qfalse;
            }
            uiInfo.uiDC.Assets.menuExitSound = trap_S_RegisterSound(tempStr, qfalse);
            continue;
        }

        // itemFocusSound
        if (Q_stricmp(token.string, "itemFocusSound") == 0) {
            if (!PC_String_Parse(handle, &tempStr)) {
                return qfalse;
            }
            uiInfo.uiDC.Assets.itemFocusSound = trap_S_RegisterSound(tempStr, qfalse);
            continue;
        }

        // menuBuzzSound
        if (Q_stricmp(token.string, "menuBuzzSound") == 0) {
            if (!PC_String_Parse(handle, &tempStr)) {
                return qfalse;
            }
            uiInfo.uiDC.Assets.menuBuzzSound = trap_S_RegisterSound(tempStr, qfalse);
            continue;
        }

        if (Q_stricmp(token.string, "cursor") == 0) {
            if (!PC_String_Parse(handle, &uiInfo.uiDC.Assets.cursorStr)) {
                return qfalse;
            }
            uiInfo.uiDC.Assets.cursor = trap_R_RegisterShaderNoMip(uiInfo.uiDC.Assets.cursorStr);
            continue;
        }

        if (Q_stricmp(token.string, "fadeClamp") == 0) {
            if (!PC_Float_Parse(handle, &uiInfo.uiDC.Assets.fadeClamp)) {
                return qfalse;
            }
            continue;
        }

        if (Q_stricmp(token.string, "fadeCycle") == 0) {
            if (!PC_Int_Parse(handle, &uiInfo.uiDC.Assets.fadeCycle)) {
                return qfalse;
            }
            continue;
        }

        if (Q_stricmp(token.string, "fadeAmount") == 0) {
            if (!PC_Float_Parse(handle, &uiInfo.uiDC.Assets.fadeAmount)) {
                return qfalse;
            }
            continue;
        }

        if (Q_stricmp(token.string, "shadowX") == 0) {
            if (!PC_Float_Parse(handle, &uiInfo.uiDC.Assets.shadowX)) {
                return qfalse;
            }
            continue;
        }

        if (Q_stricmp(token.string, "shadowY") == 0) {
            if (!PC_Float_Parse(handle, &uiInfo.uiDC.Assets.shadowY)) {
                return qfalse;
            }
            continue;
        }

        if (Q_stricmp(token.string, "shadowColor") == 0) {
            if (!PC_Color_Parse(handle, &uiInfo.uiDC.Assets.shadowColor)) {
                return qfalse;
            }
            uiInfo.uiDC.Assets.shadowFadeClamp = uiInfo.uiDC.Assets.shadowColor[3];
            continue;
        }
    }
    return qfalse;
}

void Font_Report(void) {
    int i;
    Com_Printf("Font Info\n");
    Com_Printf("=========\n");
    for (i = 32; i < 96; i++) {
        Com_Printf("  Glyph handle %i: %i\n", i, uiInfo.uiDC.Assets.textFont.glyphs[i].glyph);
    }
}

void UI_Report(void) {
    String_Report();
    Font_Report();
}

void UI_ParseMenu(const char* menuFile) {
    int handle;
    pc_token_t token;

#ifdef _DEBUG
    Com_Printf("Parsing menu file: %s\n", menuFile);
#endif

    handle = trap_PC_LoadSource(menuFile);
    if (!handle) {
        return;
    }

    while (1) {
        memset(&token, 0, sizeof(pc_token_t));
        if (!trap_PC_ReadToken(handle, &token)) {
            break;
        }

        if (token.string[0] == '}') {
            break;
        }

        if (Q_stricmp(token.string, "assetGlobalDef") == 0) {
            if (Asset_Parse(handle)) {
                // [QL] Populate extraFonts[] for fontIndex-based selection:
                // 0 = textFont (NotoSans), 1 = bigFont (handelgothic), 2 = smallFont (DroidSansMono)
                memcpy(&uiInfo.uiDC.Assets.extraFonts[0], &uiInfo.uiDC.Assets.textFont, sizeof(fontInfo_t));
                memcpy(&uiInfo.uiDC.Assets.extraFonts[1], &uiInfo.uiDC.Assets.bigFont, sizeof(fontInfo_t));
                memcpy(&uiInfo.uiDC.Assets.extraFonts[2], &uiInfo.uiDC.Assets.smallFont, sizeof(fontInfo_t));
                continue;
            } else {
                break;
            }
        }

        if (Q_stricmp(token.string, "menudef") == 0) {
            // start a new menu
            Menu_New(handle);
        }
    }
    trap_PC_FreeSource(handle);
}

qboolean Load_Menu(int handle) {
    pc_token_t token;

    if (!trap_PC_ReadToken(handle, &token))
        return qfalse;
    if (token.string[0] != '{') {
        return qfalse;
    }

    while (1) {
        if (!trap_PC_ReadToken(handle, &token))
            return qfalse;

        if (token.string[0] == 0) {
            return qfalse;
        }

        if (token.string[0] == '}') {
            return qtrue;
        }

        UI_ParseMenu(token.string);
    }
    return qfalse;
}

void UI_LoadMenus(const char* menuFile, qboolean reset) {
    pc_token_t token;
    int handle;
    int start;

    start = trap_Milliseconds();

    handle = trap_PC_LoadSource(menuFile);
    if (!handle) {
        Com_Printf(S_COLOR_YELLOW "menu file not found: %s, using default\n", menuFile);
        handle = trap_PC_LoadSource("ui/menus.txt");
        if (!handle) {
            // [QL] uix86.dll UI_LoadMenus @0x1000f... prints and returns rather than fatally erroring
            Com_Printf(S_COLOR_RED "default menu file not found: ui/menus.txt, unable to continue!\n");
        }
    }

    if (reset) {
        Menu_Reset();
    }

    while (1) {
        if (!trap_PC_ReadToken(handle, &token))
            break;
        if (token.string[0] == 0 || token.string[0] == '}') {
            break;
        }

        if (token.string[0] == '}') {
            break;
        }

        if (Q_stricmp(token.string, "loadmenu") == 0) {
            if (Load_Menu(handle)) {
                continue;
            } else {
                break;
            }
        }
    }

    Com_Printf("UI menu load time = %d milli seconds\n", trap_Milliseconds() - start);

    trap_PC_FreeSource(handle);
}

/*
=================
UI_LoadExtraMenus

[QL] Load one of our own menu files into the current set.

UI_LoadMenus falls back to ui/menus.txt when the file it was asked for is
missing, which is right for the primary set and wrong for an addition: a
mistyped or absent extra file would silently load the whole main menu set a
second time, giving two of every menu and Menus_FindByName resolving to
whichever came first. This does nothing when the file is not there, and says so.
=================
*/
static void UI_LoadExtraMenus(const char* menuFile) {
    pc_token_t token;
    int handle;

    handle = trap_PC_LoadSource(menuFile);
    if (!handle) {
        Com_Printf(S_COLOR_YELLOW "extra menu file not found: %s\n", menuFile);
        return;
    }

    while (1) {
        if (!trap_PC_ReadToken(handle, &token))
            break;
        if (token.string[0] == 0 || token.string[0] == '}') {
            break;
        }
        if (Q_stricmp(token.string, "loadmenu") == 0) {
            if (!Load_Menu(handle)) {
                break;
            }
        }
    }

    trap_PC_FreeSource(handle);
}

void UI_Load(void) {
    char lastName[1024];
    menuDef_t* menu = Menu_GetFocused();
    char* menuSet = UI_Cvar_VariableString("ui_menuFiles");
    if (menu && menu->window.name) {
        Q_strncpyz(lastName, menu->window.name, sizeof(lastName));
    }
    if (menuSet == NULL || menuSet[0] == '\0') {
        menuSet = "ui/menus.txt";
    }

    String_Init();

    UI_ParseGameInfo("gameinfo.txt");
    UI_LoadArenas();

    UI_LoadMenus(menuSet, qtrue);
    Menus_CloseAll();
    Menus_ActivateByName(lastName);
}

static const char* handicapValues[] = {"None", "95", "90", "85", "80", "75", "70", "65", "60", "55", "50", "45", "40", "35", "30", "25", "20", "15", "10", "5", NULL};

static void UI_DrawHandicap(rectDef_t* rect, float scale, vec4_t color, int textStyle) {
    int i, h;

    h = Com_Clamp(5, 100, trap_Cvar_VariableValue("handicap"));
    i = 20 - h / 5;

    Text_Paint(rect->x, rect->y, scale, color, handicapValues[i], 0, 0, textStyle);
}

static void UI_SetCapFragLimits(qboolean uiVars) {
    int cap = 5;
    int frag = 10;
    if (uiInfo.gameTypes[ui_gameType.integer].gtEnum == GT_OBELISK) {
        cap = 4;
    } else if (uiInfo.gameTypes[ui_gameType.integer].gtEnum == GT_HARVESTER) {
        cap = 15;
    }
    if (uiVars) {
        trap_Cvar_Set("ui_captureLimit", va("%d", cap));
        trap_Cvar_Set("ui_fragLimit", va("%d", frag));
    } else {
        trap_Cvar_Set("capturelimit", va("%d", cap));
        trap_Cvar_Set("fraglimit", va("%d", frag));
    }
}
// ui_gameType assumes gametype 0 is -1 ALL and will not show
static void UI_DrawGameType(rectDef_t* rect, float scale, vec4_t color, int textStyle) {
    Text_Paint(rect->x, rect->y, scale, color, uiInfo.gameTypes[ui_gameType.integer].gameType, 0, 0, textStyle);
}

static void UI_DrawNetGameType(rectDef_t* rect, float scale, vec4_t color, int textStyle) {
    if (ui_netGameType.integer < 0 || ui_netGameType.integer > uiInfo.numGameTypes) {
        trap_Cvar_Set("ui_netGametype", "0");
        trap_Cvar_Set("ui_actualNetGametype", "0");
    }
    Text_Paint(rect->x, rect->y, scale, color, uiInfo.gameTypes[ui_netGameType.integer].gameType, 0, 0, textStyle);
}

static void UI_DrawJoinGameType(rectDef_t* rect, float scale, vec4_t color, int textStyle) {
    if (ui_joinGameType.integer < 0 || ui_joinGameType.integer > uiInfo.numJoinGameTypes) {
        trap_Cvar_Set("ui_joinGametype", "0");
    }
    Text_Paint(rect->x, rect->y, scale, color, uiInfo.joinGameTypes[ui_joinGameType.integer].gameType, 0, 0, textStyle);
}

static int UI_TeamIndexFromName(const char* name) {
    int i;

    if (name && *name) {
        for (i = 0; i < uiInfo.teamCount; i++) {
            if (Q_stricmp(name, uiInfo.teamList[i].teamName) == 0) {
                return i;
            }
        }
    }

    return 0;
}

static void UI_DrawClanCinematic(rectDef_t* rect, float scale, vec4_t color) {
    int i;
    i = UI_TeamIndexFromName(UI_Cvar_VariableString("ui_teamName"));
    if (i >= 0 && i < uiInfo.teamCount) {
        if (uiInfo.teamList[i].cinematic >= -2) {
            if (uiInfo.teamList[i].cinematic == -1) {
                uiInfo.teamList[i].cinematic = trap_CIN_PlayCinematic(va("%s.roq", uiInfo.teamList[i].imageName), 0, 0, 0, 0, (CIN_loop | CIN_silent));
            }
            if (uiInfo.teamList[i].cinematic >= 0) {
                trap_CIN_RunCinematic(uiInfo.teamList[i].cinematic);
                UI_DrawCinematic(uiInfo.teamList[i].cinematic, rect->x, rect->y, rect->w, rect->h);
            } else {
                trap_R_SetColor(color);
                UI_DrawHandlePic(rect->x, rect->y, rect->w, rect->h, uiInfo.teamList[i].teamIcon_Metal);
                trap_R_SetColor(NULL);
                uiInfo.teamList[i].cinematic = -2;
            }
        } else {
            trap_R_SetColor(color);
            UI_DrawHandlePic(rect->x, rect->y, rect->w, rect->h, uiInfo.teamList[i].teamIcon);
            trap_R_SetColor(NULL);
        }
    }
}

static void UI_DrawPreviewCinematic(rectDef_t* rect, float scale, vec4_t color) {
    if (uiInfo.previewMovie > -2) {
        uiInfo.previewMovie = trap_CIN_PlayCinematic(va("%s.roq", uiInfo.movieList[uiInfo.movieIndex]), 0, 0, 0, 0, (CIN_loop | CIN_silent));
        if (uiInfo.previewMovie >= 0) {
            trap_CIN_RunCinematic(uiInfo.previewMovie);
            UI_DrawCinematic(uiInfo.previewMovie, rect->x, rect->y, rect->w, rect->h);
        } else {
            uiInfo.previewMovie = -2;
        }
    }
}

static void UI_DrawMapPreview(rectDef_t* rect, float scale, vec4_t color, qboolean net) {
    int map = (net) ? ui_currentNetMap.integer : ui_currentMap.integer;
    if (map < 0 || map > uiInfo.mapCount) {
        if (net) {
            ui_currentNetMap.integer = 0;
            trap_Cvar_Set("ui_currentNetMap", "0");
        } else {
            ui_currentMap.integer = 0;
            trap_Cvar_Set("ui_currentMap", "0");
        }
        map = 0;
    }

    if (uiInfo.mapList[map].levelShot == -1) {
        uiInfo.mapList[map].levelShot = trap_R_RegisterShaderNoMip(uiInfo.mapList[map].imageName);
    }

    if (uiInfo.mapList[map].levelShot > 0) {
        UI_DrawHandlePic(rect->x, rect->y, rect->w, rect->h, uiInfo.mapList[map].levelShot);
    } else {
        UI_DrawHandlePic(rect->x, rect->y, rect->w, rect->h, trap_R_RegisterShaderNoMip("menu/art/unknownmap"));
    }
}

static void UI_DrawMapTimeToBeat(rectDef_t* rect, float scale, vec4_t color, int textStyle) {
    int minutes, seconds, time;
    if (ui_currentMap.integer < 0 || ui_currentMap.integer > uiInfo.mapCount) {
        ui_currentMap.integer = 0;
        trap_Cvar_Set("ui_currentMap", "0");
    }

    time = uiInfo.mapList[ui_currentMap.integer].timeToBeat[uiInfo.gameTypes[ui_gameType.integer].gtEnum];

    minutes = time / 60;
    seconds = time % 60;

    Text_Paint(rect->x, rect->y, scale, color, va("%02i:%02i", minutes, seconds), 0, 0, textStyle);
}

static void UI_DrawMapCinematic(rectDef_t* rect, float scale, vec4_t color, qboolean net) {
    int map = (net) ? ui_currentNetMap.integer : ui_currentMap.integer;
    if (map < 0 || map > uiInfo.mapCount) {
        if (net) {
            ui_currentNetMap.integer = 0;
            trap_Cvar_Set("ui_currentNetMap", "0");
        } else {
            ui_currentMap.integer = 0;
            trap_Cvar_Set("ui_currentMap", "0");
        }
        map = 0;
    }

    if (uiInfo.mapList[map].cinematic >= -1) {
        if (uiInfo.mapList[map].cinematic == -1) {
            uiInfo.mapList[map].cinematic = trap_CIN_PlayCinematic(va("%s.roq", uiInfo.mapList[map].mapLoadName), 0, 0, 0, 0, (CIN_loop | CIN_silent));
        }
        if (uiInfo.mapList[map].cinematic >= 0) {
            trap_CIN_RunCinematic(uiInfo.mapList[map].cinematic);
            UI_DrawCinematic(uiInfo.mapList[map].cinematic, rect->x, rect->y, rect->w, rect->h);
        } else {
            uiInfo.mapList[map].cinematic = -2;
        }
    } else {
        UI_DrawMapPreview(rect, scale, color, net);
    }
}

static qboolean updateModel = qtrue;

// [QL] Loadout-selectable flag per weapon, taken from field[0] of the UI weapon
// table at uix86.dll 0x1002c1c4 (stride 0x30). UI_DrawPlayerModel gates the WP_HMG
// fallback on this: a weapon that is not loadout-selectable snaps to WP_HMG.
static const int uiWeaponLoadoutSelectable[WP_NUM_WEAPONS] = {
    0,  // WP_NONE
    0,  // WP_GAUNTLET
    0,  // WP_MACHINEGUN
    1,  // WP_SHOTGUN
    0,  // WP_GRENADE_LAUNCHER
    1,  // WP_ROCKET_LAUNCHER
    1,  // WP_LIGHTNING
    1,  // WP_RAILGUN
    1,  // WP_PLASMAGUN
    0,  // WP_BFG
    0,  // WP_GRAPPLING_HOOK
    0,  // WP_NAILGUN
    0,  // WP_PROX_LAUNCHER
    0,  // WP_CHAINGUN
    1,  // WP_HMG
};

static void UI_DrawPlayerModel(rectDef_t* rect) {
    static playerInfo_t info;
    char model[MAX_QPATH];
    char team[256];
    char head[256];
    vec3_t viewangles;
    vec3_t moveangles;
    int weapon;

    // [QL] always use model/headmodel (no team_model)
    Q_strncpyz(model, UI_Cvar_VariableString("model"), sizeof(model));
    Q_strncpyz(head, UI_Cvar_VariableString("headmodel"), sizeof(head));
    team[0] = '\0';

    // [QL] The preview holds the loadout's primary weapon when the loadout is
    // active (cg_loadout), otherwise the machinegun; a weapon that is not
    // loadout-selectable snaps to WP_HMG. Matches uix86.dll UI_DrawPlayerModel
    // @0x10005690, which gates the fallback on the weapon table flag at 0x1002c1c4
    // (weapon*0x30 + [0x1002c1c4] != 1), not a plain range check.
    if (trap_Cvar_VariableValue("cg_loadout") != 0) {
        weapon = (int)trap_Cvar_VariableValue("cg_weaponPrimary");
    } else {
        weapon = WP_MACHINEGUN;
    }
    if (weapon < 0 || weapon >= WP_NUM_WEAPONS || uiWeaponLoadoutSelectable[weapon] != 1) {
        weapon = WP_HMG;
    }

    if (updateModel) {
        memset(&info, 0, sizeof(playerInfo_t));
        viewangles[PITCH] = 5;
        viewangles[YAW] = 210;
        viewangles[ROLL] = 0;
        VectorClear(moveangles);
        UI_PlayerInfo_SetModel(&info, model, head, team);
        UI_PlayerInfo_SetInfo(&info, LEGS_IDLE, TORSO_STAND, viewangles, vec3_origin, weapon, qfalse);
        //		UI_RegisterClientModelname( &info, model, head, team);
        updateModel = qfalse;
    }

    UI_DrawPlayer(rect->x, rect->y, rect->w, rect->h, &info, uiInfo.uiDC.realTime / 2);
}

static void UI_DrawNetMapPreview(rectDef_t* rect, float scale, vec4_t color) {
    if (uiInfo.serverStatus.currentServerPreview > 0) {
        UI_DrawHandlePic(rect->x, rect->y, rect->w, rect->h, uiInfo.serverStatus.currentServerPreview);
    } else {
        UI_DrawHandlePic(rect->x, rect->y, rect->w, rect->h, trap_R_RegisterShaderNoMip("menu/art/unknownmap"));
    }
}

static void UI_DrawNetMapCinematic(rectDef_t* rect, float scale, vec4_t color) {
    if (ui_currentNetMap.integer < 0 || ui_currentNetMap.integer > uiInfo.mapCount) {
        ui_currentNetMap.integer = 0;
        trap_Cvar_Set("ui_currentNetMap", "0");
    }

    if (uiInfo.serverStatus.currentServerCinematic >= 0) {
        trap_CIN_RunCinematic(uiInfo.serverStatus.currentServerCinematic);
        UI_DrawCinematic(uiInfo.serverStatus.currentServerCinematic, rect->x, rect->y, rect->w, rect->h);
    } else {
        UI_DrawNetMapPreview(rect, scale, color);
    }
}

static qboolean updateOpponentModel = qtrue;
static void UI_DrawOpponent(rectDef_t* rect) {
    static playerInfo_t info2;
    char model[MAX_QPATH];
    char headmodel[MAX_QPATH];
    char team[256];
    vec3_t viewangles;
    vec3_t moveangles;

    if (updateOpponentModel) {
        Q_strncpyz(model, UI_Cvar_VariableString("ui_opponentModel"), sizeof(model));
        Q_strncpyz(headmodel, UI_Cvar_VariableString("ui_opponentModel"), sizeof(headmodel));
        team[0] = '\0';

        memset(&info2, 0, sizeof(playerInfo_t));
        viewangles[YAW] = 180 - 10;
        viewangles[PITCH] = 0;
        viewangles[ROLL] = 0;
        VectorClear(moveangles);
        UI_PlayerInfo_SetModel(&info2, model, headmodel, "");
        UI_PlayerInfo_SetInfo(&info2, LEGS_IDLE, TORSO_STAND, viewangles, vec3_origin, WP_MACHINEGUN, qfalse);
        UI_RegisterClientModelname(&info2, model, headmodel, team);
        updateOpponentModel = qfalse;
    }

    UI_DrawPlayer(rect->x, rect->y, rect->w, rect->h, &info2, uiInfo.uiDC.realTime / 2);
}

static void UI_NextOpponent(void) {
    int i = UI_TeamIndexFromName(UI_Cvar_VariableString("ui_opponentName"));
    int j = UI_TeamIndexFromName(UI_Cvar_VariableString("ui_teamName"));
    i++;
    if (i >= uiInfo.teamCount) {
        i = 0;
    }
    if (i == j) {
        i++;
        if (i >= uiInfo.teamCount) {
            i = 0;
        }
    }
    trap_Cvar_Set("ui_opponentName", uiInfo.teamList[i].teamName);
}

static void UI_PriorOpponent(void) {
    int i = UI_TeamIndexFromName(UI_Cvar_VariableString("ui_opponentName"));
    int j = UI_TeamIndexFromName(UI_Cvar_VariableString("ui_teamName"));
    i--;
    if (i < 0) {
        i = uiInfo.teamCount - 1;
    }
    if (i == j) {
        i--;
        if (i < 0) {
            i = uiInfo.teamCount - 1;
        }
    }
    trap_Cvar_Set("ui_opponentName", uiInfo.teamList[i].teamName);
}

static void UI_DrawAllMapsSelection(rectDef_t* rect, float scale, vec4_t color, int textStyle, qboolean net) {
    int map = (net) ? ui_currentNetMap.integer : ui_currentMap.integer;
    if (map >= 0 && map < uiInfo.mapCount) {
        Text_Paint(rect->x, rect->y, scale, color, uiInfo.mapList[map].mapName, 0, 0, textStyle);
    }
}

static void UI_DrawOpponentName(rectDef_t* rect, float scale, vec4_t color, int textStyle) {
    Text_Paint(rect->x, rect->y, scale, color, UI_Cvar_VariableString("ui_opponentName"), 0, 0, textStyle);
}

static int UI_OwnerDrawWidth(int ownerDraw, float scale) {
    int i, h;
    const char* s = NULL;

    switch (ownerDraw) {
        case UI_HANDICAP:
            h = Com_Clamp(5, 100, trap_Cvar_VariableValue("handicap"));
            i = 20 - h / 5;
            s = handicapValues[i];
            break;
        case UI_GAMETYPE:
            s = uiInfo.gameTypes[ui_gameType.integer].gameType;
            break;
        case UI_ALLMAPS_SELECTION:
            break;
        case UI_OPPONENT_NAME:
            break;
        case UI_KEYBINDSTATUS:
            if (Display_KeyBindPending()) {
                s = "Waiting for new key... Press ESCAPE to cancel";
            } else {
                s = "Press ENTER or CLICK to change, Press BACKSPACE to clear";
            }
            break;
        default:
            break;
    }

    if (s) {
        return Text_Width(s, scale, 0);
    }
    return 0;
}

static void UI_DrawBotName(rectDef_t* rect, float scale, vec4_t color, int textStyle) {
    // [QL] uix86.dll UI_DrawBotName @0x10006b30 always draws the bot-info-list name
    // (no g_gametype>=GT_TEAM characterList branch). UI_GetBotNameByNumber validates
    // [0,numBots), prints "^1Invalid bot number: %i" and falls back to "Sarge".
    int value = uiInfo.botIndex;
    Text_Paint(rect->x, rect->y, scale, color, UI_GetBotNameByNumber(value), 0, 0, textStyle);
}

static void UI_DrawRedBlue(rectDef_t* rect, float scale, vec4_t color, int textStyle) {
    Text_Paint(rect->x, rect->y, scale, color, (uiInfo.redBlue == 0) ? "Red" : "Blue", 0, 0, textStyle);
}

static void UI_DrawCrosshair(rectDef_t* rect, float scale, vec4_t color) {
    if (!uiInfo.currentCrosshair) {
        return;
    }
    trap_R_SetColor(color);
    UI_DrawHandlePic(rect->x, rect->y - rect->h, rect->w, rect->h, uiInfo.uiDC.Assets.crosshairShader[uiInfo.currentCrosshair]);
    trap_R_SetColor(NULL);
}

// [QL] Bot skill level display
static void UI_DrawBotSkill(rectDef_t *rect, float scale, vec4_t color, int textStyle) {
    if (uiInfo.skillIndex >= 0 && uiInfo.skillIndex < numSkillLevels) {
        Text_Paint(rect->x, rect->y, scale, color, skillLevels[uiInfo.skillIndex], 0, 0, textStyle);
    }
}

// [QL] Vote string display
static void UI_DrawVoteString(rectDef_t *rect, float scale, vec4_t color, int textStyle) {
    if (ui_votestring.string[0]) {
        // [QL] centre the vote string on rect->x when it has a positive origin/width
        float x = rect->x;
        int width = Text_Width(ui_votestring.string, scale, 0);
        if (rect->x > 0 && width > 0) {
            x = rect->x - width / 2;
        }
        Text_Paint(x, rect->y, scale, color, ui_votestring.string, 0, 0, textStyle);
    }
}

// [QL] Single-player skill display (owner-draw UI_SKILL 517). uix86.dll UI_DrawSkill
// @0x10005350: read g_spSkill, snap out-of-range to 1, draw skillLevels[skill]. The binary
// indexes a 1-based table @0x1002ae20; skillLevels[] here is 0-based so subtract 1.
static void UI_DrawSkill(rectDef_t *rect, float scale, vec4_t color, int textStyle) {
    int skill = (int)trap_Cvar_VariableValue("g_spSkill");
    if (skill < 1 || skill > 5) {
        skill = 1;
    }
    Text_Paint(rect->x, rect->y, scale, color, skillLevels[skill - 1], 0, 0, textStyle);
}

// [QL] Server-browser source display (owner-draw UI_NETSOURCE 518). uix86.dll
// UI_DrawNetSource @0x10006550: reset ui_netSource to 0 when outside 0..4 (the > 4 bound is
// one past the real sources), then draw "Source: <name>".
static void UI_DrawNetSource(rectDef_t *rect, float scale, vec4_t color, int textStyle) {
    if (ui_netSource.integer < 0 || ui_netSource.integer > 4) {
        ui_netSource.integer = 0;
    }
    Text_Paint(rect->x, rect->y, scale, color,
               va("Source: %s", netSourceNames[ui_netSource.integer]), 0, 0, textStyle);
}

// [QL] Server-browser mod filter display (owner-draw UI_NETFILTER 520). uix86.dll
// UI_DrawServerFilter @0x100066d0: reset ui_serverFilterType to 0 when outside 0..7, then
// draw "Filter: <description>".
static void UI_DrawServerFilter(rectDef_t *rect, float scale, vec4_t color, int textStyle) {
    if (ui_serverFilterType.integer < 0 || ui_serverFilterType.integer > 7) {
        ui_serverFilterType.integer = 0;
    }
    Text_Paint(rect->x, rect->y, scale, color,
               va("Filter: %s", serverFilters[ui_serverFilterType.integer].description), 0, 0, textStyle);
}

// [QL] Next-map / MOTD display (owner-draw UI_NEXTMAP 551). uix86.dll UI_DrawMOTD
// @0x10006ea0: draw configstring CS_NEXTMAP (666) when it is set.
static void UI_DrawMOTD(rectDef_t *rect, float scale, vec4_t color, int textStyle) {
    char buf[1024];
    trap_GetConfigString(CS_NEXTMAP, buf, sizeof(buf));
    if (buf[0]) {
        Text_Paint(rect->x, rect->y, scale, color, buf, 0, 0, textStyle);
    }
}

// [QL] parses a packed 0xRRGGBBAA colour cvar; defined further down (used by the model previews).
static int UI_ParseModelColor(const char* cvarName);

// [QL] Compose a "base/skin" model name from a force-model + force-skin pair the way
// UI_DrawTeamModel/UI_DrawEnemyModel do (uix86.dll 0x10005850/0x10005c20): default the
// model to "sarge" when only a skin is set, and when a skin is present strip any skin
// already on the model and append the forced one. Returns qfalse when both are empty
// (the binary draws nothing in that case).
static qboolean UI_ComposeForcedModel(const char* modelCvar, const char* skinCvar, char* out, int outSize) {
    char model[MAX_QPATH];
    char skin[MAX_QPATH];
    char* slash;

    Q_strncpyz(model, UI_Cvar_VariableString(modelCvar), sizeof(model));
    Q_strncpyz(skin, UI_Cvar_VariableString(skinCvar), sizeof(skin));

    if (!model[0]) {
        if (!skin[0]) {
            return qfalse;
        }
        Q_strncpyz(model, "sarge", sizeof(model));
    }

    Q_strncpyz(out, model, outSize);
    if (skin[0]) {
        slash = strchr(out, '/');
        if (slash) {
            *slash = '\0';
        }
        Q_strcat(out, outSize, "/");
        Q_strcat(out, outSize, skin);
    }
    return qtrue;
}

// [QL] Team player model preview (owner-draw UI_TEAMPLAYERMODEL 553). Ported from
// uix86.dll UI_DrawTeamModel @0x10005850: reads cg_forceTeamModel + cg_forceTeamSkin
// (not ui_forceTeamModel), composes "base/skin" holding the machinegun (yaw 170), and
// every frame forces the cg_team{Head,Upper,Lower}Color values into the per-part tint.
static void UI_DrawTeamPlayerModel(rectDef_t *rect) {
    static playerInfo_t teamInfo;
    static char teamModelCached[MAX_QPATH];
    char composed[MAX_QPATH];
    vec3_t viewangles;

    if (!UI_ComposeForcedModel("cg_forceTeamModel", "cg_forceTeamSkin", composed, sizeof(composed))) {
        return;
    }
    if (Q_stricmp(composed, teamModelCached)) {
        Q_strncpyz(teamModelCached, composed, sizeof(teamModelCached));
        memset(&teamInfo, 0, sizeof(playerInfo_t));
        viewangles[YAW] = 170;
        viewangles[PITCH] = 0;
        viewangles[ROLL] = 0;
        UI_PlayerInfo_SetModel(&teamInfo, composed, composed, "");
        UI_PlayerInfo_SetInfo(&teamInfo, LEGS_IDLE, TORSO_STAND, viewangles, vec3_origin, WP_MACHINEGUN, qfalse);
    }
    // [QL] force the team colours into the tint every frame. uix86.dll @0x10005850.
    teamInfo.customColor = qtrue;
    teamInfo.headColor = UI_ParseModelColor("cg_teamHeadColor");
    teamInfo.torsoColor = UI_ParseModelColor("cg_teamUpperColor");
    teamInfo.legsColor = UI_ParseModelColor("cg_teamLowerColor");
    UI_DrawPlayer(rect->x, rect->y, rect->w, rect->h, &teamInfo, uiInfo.uiDC.realTime / 2);
}

// [QL] Enemy player model preview (owner-draw UI_ENEMYPLAYERMODEL 554). Ported from
// uix86.dll UI_DrawEnemyModel @0x10005c20: reads cg_forceEnemyModel + cg_forceEnemySkin
// (default "sarge", not "keel"), composes "base/skin", and every frame forces the
// cg_enemy{Head,Upper,Lower}Color values into the per-part tint.
static void UI_DrawEnemyPlayerModel(rectDef_t *rect) {
    static playerInfo_t enemyInfo;
    static char enemyModelCached[MAX_QPATH];
    char composed[MAX_QPATH];
    vec3_t viewangles;

    if (!UI_ComposeForcedModel("cg_forceEnemyModel", "cg_forceEnemySkin", composed, sizeof(composed))) {
        return;
    }
    if (Q_stricmp(composed, enemyModelCached)) {
        Q_strncpyz(enemyModelCached, composed, sizeof(enemyModelCached));
        memset(&enemyInfo, 0, sizeof(playerInfo_t));
        viewangles[YAW] = 170;
        viewangles[PITCH] = 0;
        viewangles[ROLL] = 0;
        UI_PlayerInfo_SetModel(&enemyInfo, composed, composed, "");
        UI_PlayerInfo_SetInfo(&enemyInfo, LEGS_IDLE, TORSO_STAND, viewangles, vec3_origin, WP_MACHINEGUN, qfalse);
    }
    // [QL] force the enemy colours into the tint every frame. uix86.dll @0x10005c20.
    enemyInfo.customColor = qtrue;
    enemyInfo.headColor = UI_ParseModelColor("cg_enemyHeadColor");
    enemyInfo.torsoColor = UI_ParseModelColor("cg_enemyUpperColor");
    enemyInfo.legsColor = UI_ParseModelColor("cg_enemyLowerColor");
    UI_DrawPlayer(rect->x, rect->y, rect->w, rect->h, &enemyInfo, uiInfo.uiDC.realTime / 2);
}

// [QL] Server settings / rules panel (owner-draw UI_SERVER_SETTINGS 557).
// Ported byte-for-byte from uix86.dll UI_DrawServerStatus @0x10007030. QL draws the whole
// rules panel from this single owner-draw: the gametype name, the active limit lines, the
// ruleset-flag lines and the "MODIFIED WEAPONS:" starting-weapon icon grid. It does NOT
// draw sv_hostname (the first line is the gametype name), and there is no separate id-558
// owner-draw in QL: the weapon grid lives here. Each drawn line advances y by +12; a shared
// column counter starts at 2 and, whenever it exceeds 7, wraps to the next column
// (col=0, x+=110, y-=84).
static void UI_DrawServerSettings(rectDef_t *rect, float scale, vec4_t color, int textStyle) {
    // Gametype-name table @0x1002c0f4 (index 0..12), default "Unknown Gametype" @0x10023954.
    static const char *gametypeNames[] = {
        "Free For All",      // 0
        "Duel",              // 1
        "Race",              // 2
        "Team Deathmatch",   // 3
        "Clan Arena",        // 4
        "Capture the Flag",  // 5
        "One Flag CTF",      // 6
        "Overload",          // 7
        "Harvester",         // 8
        "Freeze Tag",        // 9
        "Domination",        // 10
        "Attack and Defend", // 11
        "Red Rover",         // 12
    };
    char serverinfo[BIG_INFO_STRING];  // [QL] BIG_INFO_STRING: the serverinfo exceeds MAX_INFO_STRING (see SV_SpawnServer)
    char pmoveinfo[MAX_INFO_STRING];
    char armorinfo[MAX_INFO_STRING];
    char custombuf[MAX_INFO_STRING];
    unsigned int custom;
    int gametype, gravity, mercy, quad;
    float x, y;
    int col;
    const char *s;

    // CS reads: CS_SERVERINFO(0); CS_PMOVEINFO(0x2a9, read but unused by this panel, kept to
    // mirror the binary); CS_ARMORINFO(0x2aa, holds the armor_tiered info key);
    // CS_CUSTOM_SETTINGS(0x2c0, packed bitmask: bits 0..12 starting weapons, higher bits ruleset).
    trap_GetConfigString(CS_SERVERINFO, serverinfo, sizeof(serverinfo));
    trap_GetConfigString(CS_PMOVEINFO, pmoveinfo, sizeof(pmoveinfo));
    trap_GetConfigString(CS_ARMORINFO, armorinfo, sizeof(armorinfo));
    trap_GetConfigString(CS_CUSTOM_SETTINGS, custombuf, sizeof(custombuf));
    custom = (unsigned int)atoi(custombuf);
    gametype = atoi(Info_ValueForKey(serverinfo, "g_gametype"));

    x = rect->x;
    y = rect->y;

    // Line 1: gametype name. @0x10007116
    s = ((unsigned int)gametype <= 12u) ? gametypeNames[gametype] : "Unknown Gametype";
    Text_Paint(x, y, scale, color, s, 0, 0, textStyle);
    y += 12.0f;

    // Line 2: Time Limit. @0x1000718e
    Text_Paint(x, y, scale, color, va("Time Limit: %i", atoi(Info_ValueForKey(serverinfo, "timelimit"))), 0, 0, textStyle);
    y += 12.0f;
    col = 2;

    // Frag Limit, gametype < 4. @0x100071ff
    if (gametype < 4) {
        Text_Paint(x, y, scale, color, va("Frag Limit: %i", atoi(Info_ValueForKey(serverinfo, "fraglimit"))), 0, 0, textStyle);
        y += 12.0f;
        col = 3;
    }

    // Mercy Limit, value != 0 && gametype > 2. @0x1000727d
    mercy = atoi(Info_ValueForKey(serverinfo, "mercylimit"));
    if (mercy != 0 && gametype > 2) {
        Text_Paint(x, y, scale, color, va("Mercy Limit: %i", mercy), 0, 0, textStyle);
        y += 12.0f;
        col++;
    }

    // Capture Limit, gametype == 5. @0x100072ef
    if (gametype == 5) {
        Text_Paint(x, y, scale, color, va("Capture Limit: %i", atoi(Info_ValueForKey(serverinfo, "capturelimit"))), 0, 0, textStyle);
        y += 12.0f;
        col++;
    }
    if (col > 7) { col = 0; x += 110.0f; y -= 84.0f; }

    // Round Limit, switch{4,9,11,12} with an inner guard (gt<10 || gt>11) => {4,9,12}. @0x100073a7
    switch (gametype) {
    case 4: case 9: case 11: case 12:
        if (gametype < 10 || gametype > 11) {
            Text_Paint(x, y, scale, color, va("Round Limit: %i", atoi(Info_ValueForKey(serverinfo, "roundlimit"))), 0, 0, textStyle);
            y += 12.0f;
            col++;
        }
        break;
    default:
        break;
    }
    if (col > 7) { col = 0; x += 110.0f; y -= 84.0f; }

    // Score Limit, gametype in {10,11}. @0x10007445
    if (gametype > 9 && gametype < 12) {
        Text_Paint(x, y, scale, color, va("Score Limit: %i", atoi(Info_ValueForKey(serverinfo, "scorelimit"))), 0, 0, textStyle);
        y += 12.0f;
        col++;
    }
    if (col > 7) { col = 0; x += 110.0f; y -= 84.0f; }

    // Ruleset-flag lines. Each is drawn when its CS_CUSTOM_SETTINGS bit is set, or, for the
    // key-driven lines, when the serverinfo value differs from its default. Order, bits and
    // strings are exactly as in the binary. @0x100074b3..0x10007c47
    if (custom & 0x2000) {                                         // Air Control @0x100074b7
        Text_Paint(x, y, scale, color, "Air Control", 0, 0, textStyle);
        y += 12.0f; col++;
    }
    if (col > 7) { col = 0; x += 110.0f; y -= 84.0f; }
    if (custom & 0x4000) {                                         // Ramp Jumping @0x1000752d
        Text_Paint(x, y, scale, color, "Ramp Jumping", 0, 0, textStyle);
        y += 12.0f; col++;
    }
    if (col > 7) { col = 0; x += 110.0f; y -= 84.0f; }
    if (atoi(Info_ValueForKey(armorinfo, "armor_tiered")) != 0) {  // Tiered Armor @0x100075a3
        Text_Paint(x, y, scale, color, "Tiered Armor", 0, 0, textStyle);
        y += 12.0f; col++;
    }
    if (col > 7) { col = 0; x += 110.0f; y -= 84.0f; }
    if (custom & 0x10000) {                                        // Weapon Switching @0x10007630
        Text_Paint(x, y, scale, color, "Weapon Switching", 0, 0, textStyle);
        y += 12.0f; col++;
    }
    if (col > 7) { col = 0; x += 110.0f; y -= 84.0f; }
    quad = atoi(Info_ValueForKey(serverinfo, "g_quadDamageFactor"));  // %ix Quad, factor != 3 @0x100076a8
    if (quad != 3) {
        Text_Paint(x, y, scale, color, va("%ix Quad", quad), 0, 0, textStyle);
        y += 12.0f; col++;
    }
    if (col > 7) { col = 0; x += 110.0f; y -= 84.0f; }
    if (custom & 0x8000) {                                         // Physics @0x10007750
        Text_Paint(x, y, scale, color, "Physics", 0, 0, textStyle);
        y += 12.0f; col++;
    }
    if (col > 7) { col = 0; x += 110.0f; y -= 84.0f; }
    gravity = atoi(Info_ValueForKey(serverinfo, "g_gravity"));     // Gravity %i, value != 800 @0x100077c8
    if (gravity != 800) {
        Text_Paint(x, y, scale, color, va("Gravity %i", gravity), 0, 0, textStyle);
        y += 12.0f; col++;
    }
    if (col > 7) { col = 0; x += 110.0f; y -= 84.0f; }
    if (custom & 0x20000) {                                        // InstaGib @0x1000788e
        Text_Paint(x, y, scale, color, "InstaGib", 0, 0, textStyle);
        y += 12.0f; col++;
    }
    if (col > 7) { col = 0; x += 110.0f; y -= 84.0f; }
    if (custom & 0x40000) {                                        // Quad Hog @0x100078f7
        Text_Paint(x, y, scale, color, "Quad Hog", 0, 0, textStyle);
        y += 12.0f; col++;
    }
    if (gametype == 12 && (custom & 0x4000000)) {                  // Infected @0x10007945 (no wrap before)
        Text_Paint(x, y, scale, color, "Infected", 0, 0, textStyle);
        y += 12.0f; col++;
    }
    if (col > 7) { col = 0; x += 110.0f; y -= 84.0f; }
    if (custom & 0x80000) {                                        // Regen Health @0x100079bd
        Text_Paint(x, y, scale, color, "Regen Health", 0, 0, textStyle);
        y += 12.0f; col++;
    }
    if (col > 7) { col = 0; x += 110.0f; y -= 84.0f; }
    if ((custom & 0x100000) && !(custom & 0x20000)) {             // Drop Health @0x10007a2e (only if InstaGib clear)
        Text_Paint(x, y, scale, color, "Drop Health", 0, 0, textStyle);
        y += 12.0f; col++;
    }
    if (col > 7) { col = 0; x += 110.0f; y -= 84.0f; }
    if (custom & 0x200000) {                                       // Vampiric Damage @0x10007aa6
        Text_Paint(x, y, scale, color, "Vampiric Damage", 0, 0, textStyle);
        y += 12.0f; col++;
    }
    if (col > 7) { col = 0; x += 110.0f; y -= 84.0f; }
    if (custom & 0x400000) {                                       // Item Spawning @0x10007b17
        Text_Paint(x, y, scale, color, "Item Spawning", 0, 0, textStyle);
        y += 12.0f; col++;
    }
    if (col > 7) { col = 0; x += 110.0f; y -= 84.0f; }
    if (custom & 0x800000) {                                       // Headshots @0x10007b88
        Text_Paint(x, y, scale, color, "Headshots", 0, 0, textStyle);
        y += 12.0f; col++;
    }
    if (col > 7) { col = 0; x += 110.0f; y -= 84.0f; }
    if (custom & 0x1000000) {                                      // Rail Jumping @0x10007bf9
        Text_Paint(x, y, scale, color, "Rail Jumping", 0, 0, textStyle);
        y += 12.0f; col++;
    }
    if (col > 7) { col = 0; x += 110.0f; y -= 84.0f; }

    // "MODIFIED WEAPONS:" starting-weapon icon grid (bits 0..12 of CS_CUSTOM_SETTINGS). @0x10007c6a
    // Each present weapon draws its 8x8 icon plus an "icons/modified.tga" overlay (4x4 at +6,+4);
    // icons advance x by +12, and within the grid the shared counter wraps every 8 icons
    // (col=0, x-=96, y+=12). Shotgun/grenade/rocket/railgun/plasma are hidden when the g_gravity
    // value == 2 (the binary gates these on gravity, not gametype; reproduced verbatim).
    if ((custom & 1) || (custom & 0xfe) || (custom & 0x1f00)) {   // any weapon bit 0..12 set
        Text_Paint(x, y, scale, color, "MODIFIED WEAPONS:", 0, 0, textStyle);   // header @0x10007c96
        if (col >= 9) {                                          // @0x10007cc2 (unreachable in practice; bail)
            return;
        }
        y += 6.0f;                                              // @0x10007cd1
        trap_R_SetColor(color);
        if (custom & 1) {                                       // gauntlet @0x10007cdb; resets col to 1
            UI_DrawHandlePic(x, y, 8, 8, trap_R_RegisterShaderNoMip("icons/iconw_gauntlet.tga"));
            UI_DrawHandlePic(x + 6, y + 4, 4, 4, trap_R_RegisterShaderNoMip("icons/modified.tga"));
            x += 12.0f;
            col = 1;
        }
        if (custom & 2) {                                       // machinegun @0x10007d8a
            UI_DrawHandlePic(x, y, 8, 8, trap_R_RegisterShaderNoMip("icons/iconw_machinegun.tga"));
            UI_DrawHandlePic(x + 6, y + 4, 4, 4, trap_R_RegisterShaderNoMip("icons/modified.tga"));
            x += 12.0f;
            col++;
        }
        if ((custom & 4) && gravity != 2) {                     // shotgun @0x10007e32
            UI_DrawHandlePic(x, y, 8, 8, trap_R_RegisterShaderNoMip("icons/iconw_shotgun.tga"));
            UI_DrawHandlePic(x + 6, y + 4, 4, 4, trap_R_RegisterShaderNoMip("icons/modified.tga"));
            x += 12.0f;
            col++;
        }
        if ((custom & 8) && gravity != 2) {                     // grenade @0x10007ee6
            UI_DrawHandlePic(x, y, 8, 8, trap_R_RegisterShaderNoMip("icons/iconw_grenade.tga"));
            UI_DrawHandlePic(x + 6, y + 4, 4, 4, trap_R_RegisterShaderNoMip("icons/modified.tga"));
            x += 12.0f;
            col++;
        }
        if ((custom & 0x10) && gravity != 2) {                  // rocket @0x10007f99
            UI_DrawHandlePic(x, y, 8, 8, trap_R_RegisterShaderNoMip("icons/iconw_rocket.tga"));
            UI_DrawHandlePic(x + 6, y + 4, 4, 4, trap_R_RegisterShaderNoMip("icons/modified.tga"));
            x += 12.0f;
            col++;
        }
        if (custom & 0x20) {                                    // lightning @0x1000804c (no gravity gate)
            UI_DrawHandlePic(x, y, 8, 8, trap_R_RegisterShaderNoMip("icons/iconw_lightning.tga"));
            UI_DrawHandlePic(x + 6, y + 4, 4, 4, trap_R_RegisterShaderNoMip("icons/modified.tga"));
            x += 12.0f;
            col++;
        }
        if ((custom & 0x40) && gravity != 2) {                  // railgun @0x100080f5
            UI_DrawHandlePic(x, y, 8, 8, trap_R_RegisterShaderNoMip("icons/iconw_railgun.tga"));
            UI_DrawHandlePic(x + 6, y + 4, 4, 4, trap_R_RegisterShaderNoMip("icons/modified.tga"));
            x += 12.0f;
            col++;
        }
        if ((custom & 0x80) && gravity != 2) {                  // plasma @0x100081a8; first icon with inner wrap
            UI_DrawHandlePic(x, y, 8, 8, trap_R_RegisterShaderNoMip("icons/iconw_plasma.tga"));
            UI_DrawHandlePic(x + 6, y + 4, 4, 4, trap_R_RegisterShaderNoMip("icons/modified.tga"));
            col++;
            x += 12.0f;
            if (col > 7) { y += 12.0f; col = 0; x -= 96.0f; }
        }
        if (custom & 0x100) {                                   // bfg @0x1000827f
            UI_DrawHandlePic(x, y, 8, 8, trap_R_RegisterShaderNoMip("icons/iconw_bfg.tga"));
            UI_DrawHandlePic(x + 6, y + 4, 4, 4, trap_R_RegisterShaderNoMip("icons/modified.tga"));
            col++;
            x += 12.0f;
            if (col > 7) { y += 12.0f; col = 0; x -= 96.0f; }
        }
        if (custom & 0x200) {                                   // grapple @0x10008350
            UI_DrawHandlePic(x, y, 8, 8, trap_R_RegisterShaderNoMip("icons/iconw_grapple.tga"));
            UI_DrawHandlePic(x + 6, y + 4, 4, 4, trap_R_RegisterShaderNoMip("icons/modified.tga"));
            col++;
            x += 12.0f;
            if (col > 7) { y += 12.0f; col = 0; x -= 96.0f; }
        }
        if (custom & 0x400) {                                   // nailgun @0x10008420
            UI_DrawHandlePic(x, y, 8, 8, trap_R_RegisterShaderNoMip("icons/iconw_nailgun.tga"));
            UI_DrawHandlePic(x + 6, y + 4, 4, 4, trap_R_RegisterShaderNoMip("icons/modified.tga"));
            col++;
            x += 12.0f;
            if (col > 7) { y += 12.0f; col = 0; x -= 96.0f; }
        }
        if (custom & 0x800) {                                   // proxlauncher @0x100084f0
            UI_DrawHandlePic(x, y, 8, 8, trap_R_RegisterShaderNoMip("icons/iconw_proxlauncher.tga"));
            UI_DrawHandlePic(x + 6, y + 4, 4, 4, trap_R_RegisterShaderNoMip("icons/modified.tga"));
            col++;
            x += 12.0f;
            if (col > 7) { y += 12.0f; col = 0; x -= 96.0f; }
        }
        if (custom & 0x1000) {                                  // chaingun @0x100085c1 (no col reset on wrap)
            UI_DrawHandlePic(x, y, 8, 8, trap_R_RegisterShaderNoMip("icons/iconw_chaingun.tga"));
            UI_DrawHandlePic(x + 6, y + 4, 4, 4, trap_R_RegisterShaderNoMip("icons/modified.tga"));
            x += 12.0f;
            col++;
            if (col > 7) { y += 12.0f; x -= 96.0f; }
        }
        trap_R_SetColor(NULL);
    }

    // Fallback: if nothing above advanced y at all, show the default-rules label. @0x1000868f
    if (y == rect->y) {
        Text_Paint(x, y, scale, color, "Default Settings", 0, 0, textStyle);
    }
}

/*
===============
UI_BuildPlayerList
===============
*/
static void UI_BuildPlayerList(void) {
    uiClientState_t cs;
    int n, count, team, team2, playerTeamNumber;
    char info[BIG_INFO_STRING];// [QL] BIG_INFO_STRING: the serverinfo exceeds MAX_INFO_STRING (see SV_SpawnServer)

    trap_GetClientState(&cs);
    trap_GetConfigString(CS_PLAYERS + cs.clientNum, info, MAX_INFO_STRING);
    uiInfo.playerNumber = cs.clientNum;
    uiInfo.teamLeader = atoi(Info_ValueForKey(info, "tl"));
    team = atoi(Info_ValueForKey(info, "t"));
    trap_GetConfigString(CS_SERVERINFO, info, sizeof(info));
    count = atoi(Info_ValueForKey(info, "sv_maxclients"));
    uiInfo.playerCount = 0;
    uiInfo.myTeamCount = 0;
    playerTeamNumber = 0;
    for (n = 0; n < count; n++) {
        trap_GetConfigString(CS_PLAYERS + n, info, MAX_INFO_STRING);

        if (info[0]) {
            Q_strncpyz(uiInfo.playerNames[uiInfo.playerCount], Info_ValueForKey(info, "n"), MAX_NAME_LENGTH);
            Q_CleanStr(uiInfo.playerNames[uiInfo.playerCount]);
            uiInfo.playerCount++;
            team2 = atoi(Info_ValueForKey(info, "t"));
            if (team2 == team) {
                Q_strncpyz(uiInfo.teamNames[uiInfo.myTeamCount], Info_ValueForKey(info, "n"), MAX_NAME_LENGTH);
                Q_CleanStr(uiInfo.teamNames[uiInfo.myTeamCount]);
                uiInfo.teamClientNums[uiInfo.myTeamCount] = n;
                if (uiInfo.playerNumber == n) {
                    playerTeamNumber = uiInfo.myTeamCount;
                }
                uiInfo.myTeamCount++;
            }
        }
    }

    if (!uiInfo.teamLeader) {
        trap_Cvar_Set("cg_selectedPlayer", va("%d", playerTeamNumber));
    }

    n = trap_Cvar_VariableValue("cg_selectedPlayer");
    if (n < 0 || n > uiInfo.myTeamCount) {
        n = 0;
    }
    if (n < uiInfo.myTeamCount) {
        trap_Cvar_Set("cg_selectedPlayerName", uiInfo.teamNames[n]);
    }
}

static void UI_DrawSelectedPlayer(rectDef_t* rect, float scale, vec4_t color, int textStyle) {
    if (uiInfo.uiDC.realTime > uiInfo.playerRefresh) {
        uiInfo.playerRefresh = uiInfo.uiDC.realTime + 3000;
        UI_BuildPlayerList();
    }
    Text_Paint(rect->x, rect->y, scale, color, (uiInfo.teamLeader) ? UI_Cvar_VariableString("cg_selectedPlayerName") : UI_Cvar_VariableString("name"), 0, 0, textStyle);
}

static void UI_DrawServerMOTD(rectDef_t* rect, float scale, vec4_t color) {
    if (uiInfo.serverStatus.motdLen) {
        float maxX;

        if (uiInfo.serverStatus.motdWidth == -1) {
            uiInfo.serverStatus.motdWidth = 0;
            uiInfo.serverStatus.motdPaintX = rect->x + 1;
            uiInfo.serverStatus.motdPaintX2 = -1;
        }

        if (uiInfo.serverStatus.motdOffset > uiInfo.serverStatus.motdLen) {
            uiInfo.serverStatus.motdOffset = 0;
            uiInfo.serverStatus.motdPaintX = rect->x + 1;
            uiInfo.serverStatus.motdPaintX2 = -1;
        }

        if (uiInfo.uiDC.realTime > uiInfo.serverStatus.motdTime) {
            uiInfo.serverStatus.motdTime = uiInfo.uiDC.realTime + 10;
            if (uiInfo.serverStatus.motdPaintX <= rect->x + 2) {
                if (uiInfo.serverStatus.motdOffset < uiInfo.serverStatus.motdLen) {
                    uiInfo.serverStatus.motdPaintX += Text_Width(&uiInfo.serverStatus.motd[uiInfo.serverStatus.motdOffset], scale, 1) - 1;
                    uiInfo.serverStatus.motdOffset++;
                } else {
                    uiInfo.serverStatus.motdOffset = 0;
                    if (uiInfo.serverStatus.motdPaintX2 >= 0) {
                        uiInfo.serverStatus.motdPaintX = uiInfo.serverStatus.motdPaintX2;
                    } else {
                        uiInfo.serverStatus.motdPaintX = rect->x + rect->w - 2;
                    }
                    uiInfo.serverStatus.motdPaintX2 = -1;
                }
            } else {
                // serverStatus.motdPaintX--;
                uiInfo.serverStatus.motdPaintX -= 2;
                if (uiInfo.serverStatus.motdPaintX2 >= 0) {
                    // serverStatus.motdPaintX2--;
                    uiInfo.serverStatus.motdPaintX2 -= 2;
                }
            }
        }

        maxX = rect->x + rect->w - 2;
        Text_Paint_Limit(&maxX, uiInfo.serverStatus.motdPaintX, rect->y + rect->h - 3, scale, color, &uiInfo.serverStatus.motd[uiInfo.serverStatus.motdOffset], 0, 0);
        if (uiInfo.serverStatus.motdPaintX2 >= 0) {
            float maxX2 = rect->x + rect->w - 2;
            Text_Paint_Limit(&maxX2, uiInfo.serverStatus.motdPaintX2, rect->y + rect->h - 3, scale, color, uiInfo.serverStatus.motd, 0, uiInfo.serverStatus.motdOffset);
        }
        if (uiInfo.serverStatus.motdOffset && maxX > 0) {
            // if we have an offset ( we are skipping the first part of the string ) and we fit the string
            if (uiInfo.serverStatus.motdPaintX2 == -1) {
                uiInfo.serverStatus.motdPaintX2 = rect->x + rect->w - 2;
            }
        } else {
            uiInfo.serverStatus.motdPaintX2 = -1;
        }
    }
}

static void UI_DrawKeyBindStatus(rectDef_t* rect, float scale, vec4_t color, int textStyle) {
    //	int ofs = 0; TTimo: unused
    if (Display_KeyBindPending()) {
        Text_Paint(rect->x, rect->y, scale, color, "Waiting for new key... Press ESCAPE to cancel", 0, 0, textStyle);
    } else {
        Text_Paint(rect->x, rect->y, scale, color, "Press ENTER or CLICK to change, Press BACKSPACE to clear", 0, 0, textStyle);
    }
}

// [QL] Crosshair colour picker widget (owner-draw UI_CROSSHAIR_COLOR, id 0x226).
// Ported from uix86.dll UI_DrawCrosshairPicker @0x10009660. Draws the "fx_base"
// colour bar plus a swatch marker at the current cg_crosshairColor; the bar is
// dimmed to grey when cg_crosshairHealth is enabled. The swatch shaders are the
// standard fx_red/yel/grn/teal/blue/cyan/white set (uiInfo.uiDC.Assets.fxPic).
static void UI_DrawCrosshairColor(rectDef_t* rect, vec4_t color) {
    qboolean health = trap_Cvar_VariableValue("cg_crosshairHealth") != 0;
    int colorIndex = (int)trap_Cvar_VariableValue("cg_crosshairColor");

    // [QL] uix86.dll UI_DrawCrosshairPicker @0x10009660 clamps the index to [0,25]
    // and indexes the fx swatch table with the raw value (higher values read the
    // adjacent shader handles); no extra clamp to 6.
    if (colorIndex < 0 || colorIndex > 25) {
        colorIndex = 0;
    }

    if (health) {
        vec4_t dim = {0.25f, 0.25f, 0.25f, 1.0f};
        trap_R_SetColor(dim);
    }
    UI_DrawHandlePic(rect->x, rect->y - 14, 128, 8, uiInfo.uiDC.Assets.fxBasePic);
    UI_DrawHandlePic(rect->x + (colorIndex << 4) + 8, rect->y - 16, 16, 12, uiInfo.uiDC.Assets.fxPic[colorIndex]);
    if (health) {
        trap_R_SetColor(color);
    }
}

// [QL] Server-browser refresh status line (owner-draw UI_SERVERREFRESHDATE, id 0x21b).
// Ported from uix86.dll UI_DrawServerRefreshDate @0x10008f20.
static void UI_DrawServerRefreshDate(rectDef_t* rect, float scale, vec4_t color, int textStyle) {
    if (uiInfo.serverStatus.refreshActive) {
        vec4_t lowLight, newColor;
        lowLight[0] = 0.8f * color[0];
        lowLight[1] = 0.8f * color[1];
        lowLight[2] = 0.8f * color[2];
        lowLight[3] = 0.8f * color[3];
        LerpColor(color, lowLight, newColor, 0.5 + 0.5 * sin(uiInfo.uiDC.realTime / PULSE_DIVISOR));
        Text_Paint(rect->x, rect->y, scale, newColor,
                   va("Getting info for %d servers (ESC to cancel)", trap_LAN_GetServerCount(ui_netSource.integer)),
                   0, 0, textStyle);
    } else {
        char buff[64];
        Q_strncpyz(buff, UI_Cvar_VariableString(va("ui_lastServerRefresh_%i", ui_netSource.integer)), sizeof(buff));
        Text_Paint(rect->x, rect->y, scale, color, va("Refresh Time: %s", buff), 0, 0, textStyle);
    }
}

// [QL] GL info block (owner-draw UI_GLINFO, id 0x21d). Ported from uix86.dll
// UI_DrawGLExtensions @0x100093b0 (the id-0x21d handler; Ghidra's "UI_DrawGLInfo"
// at 0x10009340 is the advertisement owner-draw). Shows vendor/version/
// pixelformat then word-wraps the extensions string into two columns.
static void UI_DrawGLInfo(rectDef_t* rect, float scale, vec4_t color, int textStyle) {
    // [QL] uix86.dll UI_DrawGLExtensions @0x100093b0: 1024-byte extension buffer, 64-line cap
    char buff[1024];
    char* eptr;
    const char* lines[64];
    int i, numLines;
    float y;

    Text_Paint(rect->x + 2, rect->y, scale, color, va("VENDOR: %s", uiInfo.uiDC.glconfig.vendor_string), 0, 30, textStyle);
    Text_Paint(rect->x + 2, rect->y + 15, scale, color, va("VERSION: %s: %s", uiInfo.uiDC.glconfig.version_string, uiInfo.uiDC.glconfig.renderer_string), 0, 30, textStyle);
    Text_Paint(rect->x + 2, rect->y + 30, scale, color, va("PIXELFORMAT: color(%d-bits) Z(%d-bits) stencil(%d-bits)", uiInfo.uiDC.glconfig.colorBits, uiInfo.uiDC.glconfig.depthBits, uiInfo.uiDC.glconfig.stencilBits), 0, 30, textStyle);

    // Build NUL-terminated extension word list.
    Q_strncpyz(buff, uiInfo.uiDC.glconfig.extensions_string, sizeof(buff));
    numLines = 0;
    eptr = buff;
    while (numLines < (int)ARRAY_LEN(lines) && *eptr) {
        while (*eptr && *eptr == ' ') {
            *eptr++ = '\0';
        }
        if (*eptr && *eptr != ' ') {
            lines[numLines++] = eptr;
        }
        while (*eptr && *eptr != ' ') {
            eptr++;
        }
    }

    y = rect->y + 45;
    i = 0;
    while (i < numLines) {
        Text_Paint(rect->x + 2, y, scale, color, lines[i++], 0, 0, textStyle);
        if (i < numLines) {
            Text_Paint(rect->x + rect->w / 2, y, scale, color, lines[i++], 0, 0, textStyle);
        }
        y += 10;
        if (y > rect->y + rect->h - 11) {
            break;
        }
    }
}

// [QL] Parse one of the team/enemy colour cvars used to tint the model
// previews. Matches uix86.dll UI_DrawRedTeamModel @0x10005ff0: when the value
// contains "0x" it is read as a packed hex colour (0xRRGGBBAA), otherwise it is
// a plain decimal integer. (QL uses sscanf("0x%08x"); the UI QVM has no libc so
// the hex digits are consumed by hand.)
static int UI_ParseModelColor(const char* cvarName) {
    char buf[64];
    const char* p;
    int value = 0;

    trap_Cvar_VariableStringBuffer(cvarName, buf, sizeof(buf));
    p = strstr(buf, "0x");
    if (p) {
        for (p += 2; *p; p++) {
            char ch = *p;
            int digit;
            if (ch >= '0' && ch <= '9') {
                digit = ch - '0';
            } else if (ch >= 'a' && ch <= 'f') {
                digit = ch - 'a' + 10;
            } else if (ch >= 'A' && ch <= 'F') {
                digit = ch - 'A' + 10;
            } else {
                break;
            }
            value = (value << 4) | digit;
        }
        return value;
    }
    return atoi(buf);
}

// [QL] Red team model preview (owner-draw UI_REDTEAMMODEL, id 0x22b). Reads
// cg_forceRedTeamModel and previews it holding the machinegun (yaw 170). Ported
// from uix86.dll UI_DrawRedTeamModel @0x10005ff0, which also forces the
// cg_team{Head,Upper,Lower}Color values into the model's per-part tint colours
// (head/torso/legs) every frame before drawing.
static void UI_DrawRedTeamModel(rectDef_t* rect) {
    static playerInfo_t redInfo;
    static char redModelCached[MAX_QPATH];
    char model[MAX_QPATH];
    vec3_t viewangles;

    Q_strncpyz(model, UI_Cvar_VariableString("cg_forceRedTeamModel"), sizeof(model));
    if (!model[0]) {
        return;
    }
    if (Q_stricmp(model, redModelCached)) {
        Q_strncpyz(redModelCached, model, sizeof(redModelCached));
        memset(&redInfo, 0, sizeof(playerInfo_t));
        viewangles[YAW] = 170;
        viewangles[PITCH] = 0;
        viewangles[ROLL] = 0;
        UI_PlayerInfo_SetModel(&redInfo, model, model, "");
        UI_PlayerInfo_SetInfo(&redInfo, LEGS_IDLE, TORSO_STAND, viewangles, vec3_origin, WP_MACHINEGUN, qfalse);
    }
    // [QL] force the team colours into the tint every frame (they can change
    // without the model changing). uix86.dll @0x10005ff0.
    redInfo.customColor = qtrue;
    redInfo.headColor = UI_ParseModelColor("cg_teamHeadColor");
    redInfo.torsoColor = UI_ParseModelColor("cg_teamUpperColor");
    redInfo.legsColor = UI_ParseModelColor("cg_teamLowerColor");
    UI_DrawPlayer(rect->x, rect->y, rect->w, rect->h, &redInfo, uiInfo.uiDC.realTime / 2);
}

// [QL] Blue team model preview (owner-draw UI_BLUETEAMMODEL, id 0x22c). Reads
// cg_forceBlueTeamModel; otherwise identical to the red variant. Ported from
// uix86.dll UI_DrawBlueTeamModel @0x100062a0, which forces the
// cg_enemy{Head,Upper,Lower}Color values into the per-part tint colours.
static void UI_DrawBlueTeamModel(rectDef_t* rect) {
    static playerInfo_t blueInfo;
    static char blueModelCached[MAX_QPATH];
    char model[MAX_QPATH];
    vec3_t viewangles;

    Q_strncpyz(model, UI_Cvar_VariableString("cg_forceBlueTeamModel"), sizeof(model));
    if (!model[0]) {
        return;
    }
    if (Q_stricmp(model, blueModelCached)) {
        Q_strncpyz(blueModelCached, model, sizeof(blueModelCached));
        memset(&blueInfo, 0, sizeof(playerInfo_t));
        viewangles[YAW] = 170;
        viewangles[PITCH] = 0;
        viewangles[ROLL] = 0;
        UI_PlayerInfo_SetModel(&blueInfo, model, model, "");
        UI_PlayerInfo_SetInfo(&blueInfo, LEGS_IDLE, TORSO_STAND, viewangles, vec3_origin, WP_MACHINEGUN, qfalse);
    }
    // [QL] force the enemy colours into the tint every frame. uix86.dll @0x100062a0.
    blueInfo.customColor = qtrue;
    blueInfo.headColor = UI_ParseModelColor("cg_enemyHeadColor");
    blueInfo.torsoColor = UI_ParseModelColor("cg_enemyUpperColor");
    blueInfo.legsColor = UI_ParseModelColor("cg_enemyLowerColor");
    UI_DrawPlayer(rect->x, rect->y, rect->w, rect->h, &blueInfo, uiInfo.uiDC.realTime / 2);
}

// FIXME: table drive
//
static void UI_OwnerDraw(float x, float y, float w, float h, float text_x, float text_y, int ownerDraw, int ownerDrawFlags, int align, float special, float scale, vec4_t color, qhandle_t shader, int textStyle) {
    rectDef_t rect;

    rect.x = x + text_x;
    rect.y = y + text_y;
    rect.w = w;
    rect.h = h;

    switch (ownerDraw) {
        case UI_HANDICAP:
            UI_DrawHandicap(&rect, scale, color, textStyle);
            break;
        case UI_PLAYERMODEL:
            UI_DrawPlayerModel(&rect);
            break;
        case UI_CLANCINEMATIC:
            UI_DrawClanCinematic(&rect, scale, color);
            break;
        case UI_PREVIEWCINEMATIC:
            UI_DrawPreviewCinematic(&rect, scale, color);
            break;
        case UI_GAMETYPE:
            UI_DrawGameType(&rect, scale, color, textStyle);
            break;
        case UI_NETGAMETYPE:
            UI_DrawNetGameType(&rect, scale, color, textStyle);
            break;
        case UI_JOINGAMETYPE:
            UI_DrawJoinGameType(&rect, scale, color, textStyle);
            break;
        case UI_MAPPREVIEW:
            UI_DrawMapPreview(&rect, scale, color, qtrue);
            break;
        case UI_MAP_TIMETOBEAT:
            UI_DrawMapTimeToBeat(&rect, scale, color, textStyle);
            break;
        case UI_MAPCINEMATIC:
            UI_DrawMapCinematic(&rect, scale, color, qfalse);
            break;
        case UI_STARTMAPCINEMATIC:
            UI_DrawMapCinematic(&rect, scale, color, qtrue);
            break;
        case UI_NETMAPPREVIEW:
            UI_DrawNetMapPreview(&rect, scale, color);
            break;
        case UI_NETMAPCINEMATIC:
            UI_DrawNetMapCinematic(&rect, scale, color);
            break;
        case UI_OPPONENTMODEL:
            UI_DrawOpponent(&rect);
            break;
        case UI_ALLMAPS_SELECTION:
            UI_DrawAllMapsSelection(&rect, scale, color, textStyle, qtrue);
            break;
        case UI_MAPS_SELECTION:
            UI_DrawAllMapsSelection(&rect, scale, color, textStyle, qfalse);
            break;
        case UI_OPPONENT_NAME:
            UI_DrawOpponentName(&rect, scale, color, textStyle);
            break;
        case UI_BOTNAME:
            UI_DrawBotName(&rect, scale, color, textStyle);
            break;
        case UI_REDBLUE:
            UI_DrawRedBlue(&rect, scale, color, textStyle);
            break;
        case UI_CROSSHAIR:
            UI_DrawCrosshair(&rect, scale, color);
            break;
        case UI_SELECTEDPLAYER:
            UI_DrawSelectedPlayer(&rect, scale, color, textStyle);
            break;
        case UI_SERVERMOTD:
            UI_DrawServerMOTD(&rect, scale, color);
            break;
        case UI_KEYBINDSTATUS:
            UI_DrawKeyBindStatus(&rect, scale, color, textStyle);
            break;
        case UI_BOTSKILL:
            UI_DrawBotSkill(&rect, scale, color, textStyle);
            break;
        case UI_SKILL:
            UI_DrawSkill(&rect, scale, color, textStyle);
            break;
        case UI_NETSOURCE:
            UI_DrawNetSource(&rect, scale, color, textStyle);
            break;
        case UI_NETFILTER:
            UI_DrawServerFilter(&rect, scale, color, textStyle);
            break;
        case UI_NEXTMAP:
            UI_DrawMOTD(&rect, scale, color, textStyle);
            break;
        case UI_VOTESTRING:
            UI_DrawVoteString(&rect, scale, color, textStyle);
            break;
        case UI_TEAMPLAYERMODEL:
            UI_DrawTeamPlayerModel(&rect);
            break;
        case UI_ENEMYPLAYERMODEL:
            UI_DrawEnemyPlayerModel(&rect);
            break;
        case UI_REDTEAMMODEL:
            UI_DrawRedTeamModel(&rect);
            break;
        case UI_BLUETEAMMODEL:
            UI_DrawBlueTeamModel(&rect);
            break;
        case UI_SERVER_SETTINGS:
            // [QL] UI_SERVER_SETTINGS (557) draws the entire rules panel including the
            // starting-weapon grid. QL has no separate id-558 owner-draw. uix86.dll @0x10007030.
            UI_DrawServerSettings(&rect, scale, color, textStyle);
            break;
        case UI_SERVERREFRESHDATE:
            UI_DrawServerRefreshDate(&rect, scale, color, textStyle);
            break;
        case UI_GLINFO:
            UI_DrawGLInfo(&rect, scale, color, textStyle);
            break;
        case UI_CROSSHAIR_COLOR:
            UI_DrawCrosshairColor(&rect, color);
            break;
        case UI_ADVERT:
            // [QL] Advertisement display - no-op in standalone build
            break;
        default:
            break;
    }
}

static qboolean UI_OwnerDrawVisible(int flags, int flags2) {
    qboolean vis = qtrue;

    while (flags) {
        if (flags & UI_SHOW_FFA) {
            if (trap_Cvar_VariableValue("g_gametype") != GT_FFA) {
                vis = qfalse;
            }
            flags &= ~UI_SHOW_FFA;
        }

        if (flags & UI_SHOW_NOTFFA) {
            if (trap_Cvar_VariableValue("g_gametype") == GT_FFA) {
                vis = qfalse;
            }
            flags &= ~UI_SHOW_NOTFFA;
        }

        if (flags & UI_SHOW_LEADER) {
            // these need to show when this client can give orders to a player or a group
            if (!uiInfo.teamLeader) {
                vis = qfalse;
            } else {
                // if showing yourself
                if (ui_selectedPlayer.integer < uiInfo.myTeamCount && uiInfo.teamClientNums[ui_selectedPlayer.integer] == uiInfo.playerNumber) {
                    vis = qfalse;
                }
            }
            flags &= ~UI_SHOW_LEADER;
        }
        if (flags & UI_SHOW_NOTLEADER) {
            // these need to show when this client is assigning their own status or they are NOT the leader
            if (uiInfo.teamLeader) {
                // if not showing yourself
                if (!(ui_selectedPlayer.integer < uiInfo.myTeamCount && uiInfo.teamClientNums[ui_selectedPlayer.integer] == uiInfo.playerNumber)) {
                    vis = qfalse;
                }
                // these need to show when this client can give orders to a player or a group
            }
            flags &= ~UI_SHOW_NOTLEADER;
        }
        if (flags & UI_SHOW_ANYTEAMGAME) {
            if (uiInfo.gameTypes[ui_gameType.integer].gtEnum <= GT_TEAM) {
                vis = qfalse;
            }
            flags &= ~UI_SHOW_ANYTEAMGAME;
        }
        if (flags & UI_SHOW_ANYNONTEAMGAME) {
            if (uiInfo.gameTypes[ui_gameType.integer].gtEnum > GT_TEAM) {
                vis = qfalse;
            }
            flags &= ~UI_SHOW_ANYNONTEAMGAME;
        }
        if (flags & UI_SHOW_NETANYTEAMGAME) {
            if (uiInfo.gameTypes[ui_netGameType.integer].gtEnum <= GT_TEAM) {
                vis = qfalse;
            }
            flags &= ~UI_SHOW_NETANYTEAMGAME;
        }
        if (flags & UI_SHOW_NETANYNONTEAMGAME) {
            if (uiInfo.gameTypes[ui_netGameType.integer].gtEnum > GT_TEAM) {
                vis = qfalse;
            }
            flags &= ~UI_SHOW_NETANYNONTEAMGAME;
        }
        if (flags & UI_SHOW_NEWHIGHSCORE) {
            if (uiInfo.newHighScoreTime < uiInfo.uiDC.realTime) {
                vis = qfalse;
            } else {
                if (uiInfo.soundHighScore) {
                    if (trap_Cvar_VariableValue("sv_killserver") == 0) {
                        // wait on server to go down before playing sound
                        trap_S_StartLocalSound(uiInfo.newHighScoreSound, CHAN_ANNOUNCER);
                        uiInfo.soundHighScore = qfalse;
                    }
                }
            }
            flags &= ~UI_SHOW_NEWHIGHSCORE;
        }
        if (flags & UI_SHOW_NEWBESTTIME) {
            if (uiInfo.newBestTime < uiInfo.uiDC.realTime) {
                vis = qfalse;
            }
            flags &= ~UI_SHOW_NEWBESTTIME;
        }
        if (flags & UI_SHOW_DEMOAVAILABLE) {
            if (!uiInfo.demoAvailable) {
                vis = qfalse;
            }
            flags &= ~UI_SHOW_DEMOAVAILABLE;
        }
        // [QL] Favourites-source visibility flags. Binary UI_OwnerDrawVisible @0x10009d30 tests
        // ui_netSource against AS_FAVORITES (3): FAVORITESERVERS hides unless favourites are
        // selected, NOTFAVORITESERVERS hides when they are.
        if (flags & UI_SHOW_FAVORITESERVERS) {
            if (ui_netSource.integer != AS_FAVORITES) {
                vis = qfalse;
            }
            flags &= ~UI_SHOW_FAVORITESERVERS;
        }
        if (flags & UI_SHOW_NOTFAVORITESERVERS) {
            if (ui_netSource.integer == AS_FAVORITES) {
                vis = qfalse;
            }
            flags &= ~UI_SHOW_NOTFAVORITESERVERS;
        }
        // [QL] Warmup visibility flags. Binary @0x10009d30 keys both off the ui_warmup cvar:
        // SHOW_IF_WARMUP hides when ui_warmup >= 0, SHOW_IF_NOT_WARMUP hides when ui_warmup < 0.
        if (flags & UI_SHOW_IF_WARMUP) {
            if (trap_Cvar_VariableValue("ui_warmup") >= 0) {
                vis = qfalse;
            }
            flags &= ~UI_SHOW_IF_WARMUP;
        }
        if (flags & UI_SHOW_IF_NOT_WARMUP) {
            if (trap_Cvar_VariableValue("ui_warmup") < 0) {
                vis = qfalse;
            }
            flags &= ~UI_SHOW_IF_NOT_WARMUP;
        }
        // [QL] Loadout visibility flags. Binary @0x10009d30 tests cg_loadout == 1 for both.
        if (flags & UI_SHOW_IF_LOADOUT_ENABLED) {
            if (trap_Cvar_VariableValue("cg_loadout") != 1) {
                vis = qfalse;
            }
            flags &= ~UI_SHOW_IF_LOADOUT_ENABLED;
        }
        if (flags & UI_SHOW_IF_LOADOUT_DISABLED) {
            if (trap_Cvar_VariableValue("cg_loadout") == 1) {
                vis = qfalse;
            }
            flags &= ~UI_SHOW_IF_LOADOUT_DISABLED;
        }
        // [QL] Not intermission visibility flag. Binary @0x10009d30 tests the ui_intermission cvar == 1.
        if (flags & UI_SHOW_IF_NOT_INTERMISSION) {
            if (trap_Cvar_VariableValue("ui_intermission") == 1) {
                vis = qfalse;
            }
            flags &= ~UI_SHOW_IF_NOT_INTERMISSION;
        }
        // Clear any remaining unrecognized flags
        if (!(flags & (UI_SHOW_FFA | UI_SHOW_NOTFFA | UI_SHOW_LEADER | UI_SHOW_NOTLEADER |
                       UI_SHOW_ANYTEAMGAME | UI_SHOW_ANYNONTEAMGAME | UI_SHOW_NETANYTEAMGAME |
                       UI_SHOW_NETANYNONTEAMGAME | UI_SHOW_NEWHIGHSCORE | UI_SHOW_NEWBESTTIME |
                       UI_SHOW_DEMOAVAILABLE | UI_SHOW_FAVORITESERVERS | UI_SHOW_NOTFAVORITESERVERS |
                       UI_SHOW_IF_WARMUP | UI_SHOW_IF_NOT_WARMUP |
                       UI_SHOW_IF_LOADOUT_ENABLED | UI_SHOW_IF_LOADOUT_DISABLED |
                       UI_SHOW_IF_NOT_INTERMISSION))) {
            flags = 0;
        }
    }
    return vis;
}

static qboolean UI_Handicap_HandleKey(int flags, float* special, int key) {
    int select = UI_SelectForKey(key);
    if (select != 0) {
        int h;

        h = Com_Clamp(5, 100, trap_Cvar_VariableValue("handicap"));
        h += 5 * select;

        // [QL] uix86.dll UI_HandleHandicap @0x1000a040: wrap >100 -> 5, and <0 -> 100
        // (decrementing the clamped-to-5 value yields 0, so the low bound is 0 not 5).
        if (h > 100) {
            h = 5;
        } else if (h < 0) {
            h = 100;
        }

        trap_Cvar_SetValue("handicap", h);
        return qtrue;
    }
    return qfalse;
}

/*
===============
UI_SetScoreBoardCvars

uix86.dll UI_SetScoreBoardCvars @0x10001f70: publish a postGameInfo_t to the ui_score* cvars.
When redBlue != 0, the "...2" duplicate cvars are set as well.
===============
*/
static void UI_SetScoreBoardCvars(const postGameInfo_t* info, qboolean redBlue) {
    trap_Cvar_Set("ui_scoreAccuracy", va("%i%%", info->accuracy));
    trap_Cvar_Set("ui_scoreImpressives", va("%i", info->impressives));
    trap_Cvar_Set("ui_scoreExcellents", va("%i", info->excellents));
    trap_Cvar_Set("ui_scoreDefends", va("%i", info->defends));
    trap_Cvar_Set("ui_scoreAssists", va("%i", info->assists));
    trap_Cvar_Set("ui_scoreGauntlets", va("%i", info->gauntlets));
    trap_Cvar_Set("ui_scoreScore", va("%i", info->score));
    trap_Cvar_Set("ui_scorePerfect", va("%i", info->perfects));
    trap_Cvar_Set("ui_scoreTeam", va("%i to %i", info->redScore, info->blueScore));
    trap_Cvar_Set("ui_scoreBase", va("%i", info->baseScore));
    trap_Cvar_Set("ui_scoreTimeBonus", va("%i", info->timeBonus));
    trap_Cvar_Set("ui_scoreSkillBonus", va("%i", info->skillBonus));
    trap_Cvar_Set("ui_scoreShutoutBonus", va("%i", info->shutoutBonus));
    trap_Cvar_Set("ui_scoreTime", va("%02i:%02i", info->time / 60, info->time % 60));
    trap_Cvar_Set("ui_scoreCaptures", va("%i", info->captures));
    if (redBlue) {
        trap_Cvar_Set("ui_scoreAccuracy2", va("%i%%", info->accuracy));
        trap_Cvar_Set("ui_scoreImpressives2", va("%i", info->impressives));
        trap_Cvar_Set("ui_scoreExcellents2", va("%i", info->excellents));
        trap_Cvar_Set("ui_scoreDefends2", va("%i", info->defends));
        trap_Cvar_Set("ui_scoreAssists2", va("%i", info->assists));
        trap_Cvar_Set("ui_scoreGauntlets2", va("%i", info->gauntlets));
        trap_Cvar_Set("ui_scoreScore2", va("%i", info->score));
        trap_Cvar_Set("ui_scorePerfect2", va("%i", info->perfects));
        trap_Cvar_Set("ui_scoreTeam2", va("%i to %i", info->redScore, info->blueScore));
        trap_Cvar_Set("ui_scoreBase2", va("%i", info->baseScore));
        trap_Cvar_Set("ui_scoreTimeBonus2", va("%i", info->timeBonus));
        trap_Cvar_Set("ui_scoreSkillBonus2", va("%i", info->skillBonus));
        trap_Cvar_Set("ui_scoreShutoutBonus2", va("%i", info->shutoutBonus));
        trap_Cvar_Set("ui_scoreTime2", va("%02i:%02i", info->time / 60, info->time % 60));
        trap_Cvar_Set("ui_scoreCaptures2", va("%i", info->captures));
    }
}

/*
===============
UI_SPPostgameMenu_f

uix86.dll UI_SPPostgameMenu_f @0x10002350: load games/<map>_<gametype>.game (if present) and
publish it to the ui_score* cvars. Called when the menu gametype selection changes.
===============
*/
static void UI_SPPostgameMenu_f(const char* mapname, int gameType) {
    postGameInfo_t info;
    char fileName[MAX_QPATH];
    fileHandle_t f;
    int size;

    memset(&info, 0, sizeof(info));
    Com_sprintf(fileName, sizeof(fileName), "games/%s_%i.game", mapname, gameType);
    if (trap_FS_FOpenFile(fileName, &f, FS_READ) >= 0) {
        size = 0;
        trap_FS_Read(&size, sizeof(int), f);
        if (size == sizeof(postGameInfo_t)) {
            trap_FS_Read(&info, sizeof(postGameInfo_t), f);
        }
        trap_FS_FCloseFile(f);
    }
    UI_SetScoreBoardCvars(&info, qfalse);
}

static qboolean UI_GameType_HandleKey(int flags, float* special, int key, qboolean resetMap) {
    int select = UI_SelectForKey(key);
    if (select != 0) {
        int oldCount = UI_MapCountByGameType(qtrue);

        // hard coded mess here
        if (select < 0) {
            ui_gameType.integer--;
            if (ui_gameType.integer == 2) {
                ui_gameType.integer = 1;
            } else if (ui_gameType.integer < 2) {
                ui_gameType.integer = uiInfo.numGameTypes - 1;
            }
        } else {
            ui_gameType.integer++;
            if (ui_gameType.integer >= uiInfo.numGameTypes) {
                ui_gameType.integer = 1;
            } else if (ui_gameType.integer == 2) {
                ui_gameType.integer = 3;
            }
        }

        trap_Cvar_SetValue("ui_gameType", ui_gameType.integer);
        UI_SetCapFragLimits(qtrue);
        // uix86.dll UI_HandleGameType @0x1000a110: refresh the postgame scoreboard cvars for the
        // newly selected map/gametype. The binary sets no ui_Q3Model here.
        UI_SPPostgameMenu_f(uiInfo.mapList[ui_currentMap.integer].mapLoadName,
                            uiInfo.gameTypes[ui_gameType.integer].gtEnum);
        if (resetMap && oldCount != UI_MapCountByGameType(qtrue)) {
            trap_Cvar_SetValue("ui_currentMap", 0);
            Menu_SetFeederSelection(NULL, FEEDER_MAPS, 0, NULL);
        }
        return qtrue;
    }
    return qfalse;
}

static qboolean UI_NetGameType_HandleKey(int flags, float* special, int key) {
    int select = UI_SelectForKey(key);
    if (select != 0) {
        ui_netGameType.integer += select;

        // [QL] uix86.dll UI_HandleNetGameType @0x1000a250: wrap, then skip past the gametype
        // enums 6/7/8 (1FCTF/Obelisk/Harvester) which the net picker does not offer. The binary
        // always increments on a skip (even when cycling backward), then re-wraps.
        while (1) {
            int gtEnum;
            if (ui_netGameType.integer < 0) {
                ui_netGameType.integer = uiInfo.numGameTypes - 1;
            } else if (ui_netGameType.integer >= uiInfo.numGameTypes) {
                ui_netGameType.integer = 0;
            }
            gtEnum = uiInfo.gameTypes[ui_netGameType.integer].gtEnum;
            if (gtEnum != 6 && gtEnum != 7 && gtEnum != 8) {
                break;
            }
            ui_netGameType.integer++;
        }

        trap_Cvar_SetValue("ui_netGametype", ui_netGameType.integer);
        trap_Cvar_SetValue("ui_actualNetGametype", uiInfo.gameTypes[ui_netGameType.integer].gtEnum);
        trap_Cvar_SetValue("ui_currentNetMap", 0);
        UI_MapCountByGameType(qfalse);
        Menu_SetFeederSelection(NULL, FEEDER_ALLMAPS, 0, NULL);
        return qtrue;
    }
    return qfalse;
}

static qboolean UI_JoinGameType_HandleKey(int flags, float* special, int key) {
    int select = UI_SelectForKey(key);
    if (select != 0) {
        ui_joinGameType.integer += select;

        if (ui_joinGameType.integer < 0) {
            ui_joinGameType.integer = uiInfo.numJoinGameTypes - 1;
        } else if (ui_joinGameType.integer >= uiInfo.numJoinGameTypes) {
            ui_joinGameType.integer = 0;
        }

        trap_Cvar_SetValue("ui_joinGametype", ui_joinGameType.integer);
        // [QL] uix86.dll UI_HandleJoinGameType @0x1000a300 rebuilds the display list
        // after switching the join filter.
        UI_BuildServerDisplayList(qtrue);
        return qtrue;
    }
    return qfalse;
}

static qboolean UI_OpponentName_HandleKey(int flags, float* special, int key) {
    int select = UI_SelectForKey(key);
    if (select != 0) {
        if (select < 0) {
            UI_PriorOpponent();
        } else {
            UI_NextOpponent();
        }
        return qtrue;
    }
    return qfalse;
}

static qboolean UI_BotName_HandleKey(int flags, float* special, int key) {
    int select = UI_SelectForKey(key);
    if (select != 0) {
        // [QL] uix86.dll UI_HandleBotSelection @0x1000a570: cycle botIndex against a single
        // UI_GetNumBots() bound (the +2/+1 slack covers the two synthetic list entries), with
        // no g_gametype>=GT_TEAM characterList branch.
        int numBots = UI_GetNumBots();

        uiInfo.botIndex += select;

        if (uiInfo.botIndex >= numBots + 2) {
            uiInfo.botIndex = 0;
        } else if (uiInfo.botIndex < 0) {
            uiInfo.botIndex = numBots + 1;
        }
        return qtrue;
    }
    return qfalse;
}

static qboolean UI_RedBlue_HandleKey(int flags, float* special, int key) {
    int select = UI_SelectForKey(key);
    if (select != 0) {
        uiInfo.redBlue ^= 1;
        return qtrue;
    }
    return qfalse;
}

static qboolean UI_Crosshair_HandleKey(int flags, float* special, int key) {
    int select = UI_SelectForKey(key);
    if (select != 0) {
        uiInfo.currentCrosshair += select;

        if (uiInfo.currentCrosshair >= NUM_CROSSHAIRS) {
            uiInfo.currentCrosshair = 0;
        } else if (uiInfo.currentCrosshair < 0) {
            uiInfo.currentCrosshair = NUM_CROSSHAIRS - 1;
        }
        trap_Cvar_SetValue("cg_drawCrosshair", uiInfo.currentCrosshair);
        return qtrue;
    }
    return qfalse;
}

static qboolean UI_SelectedPlayer_HandleKey(int flags, float* special, int key) {
    int select = UI_SelectForKey(key);
    if (select != 0) {
        int selected;

        UI_BuildPlayerList();
        if (!uiInfo.teamLeader) {
            return qfalse;
        }
        selected = trap_Cvar_VariableValue("cg_selectedPlayer");

        selected += select;

        if (selected > uiInfo.myTeamCount) {
            selected = 0;
        } else if (selected < 0) {
            selected = uiInfo.myTeamCount;
        }

        if (selected == uiInfo.myTeamCount) {
            trap_Cvar_Set("cg_selectedPlayerName", "Everyone");
        } else {
            trap_Cvar_Set("cg_selectedPlayerName", uiInfo.teamNames[selected]);
        }
        trap_Cvar_SetValue("cg_selectedPlayer", selected);
    }
    return qfalse;
}

// [QL] Crosshair colour cycle for the UI_CROSSHAIR_COLOR picker widget. Ported
// from uix86.dll UI_HandleCrosshairColor @0x1000a790: MOUSE1/ENTER/JOY1 advance,
// MOUSE2 goes back, wrapping over 1..26. Only reached when cg_crosshairHealth == 0.
static qboolean UI_CrosshairColor_HandleKey(int flags, float* special, int key) {
    int select = UI_SelectForKey(key);
    if (select != 0) {
        int color = (int)trap_Cvar_VariableValue("cg_crosshairColor");
        color += select;
        if (color >= 27) {
            color = 1;
        } else if (color < 1) {
            color = 26;
        }
        trap_Cvar_SetValue("cg_crosshairColor", color);
        return qtrue;
    }
    return qfalse;
}

// [QL] Single-player skill picker (owner-draw UI_SKILL 517). Ported from uix86.dll
// UI_HandleSkill @0x1000a390: only MOUSE1/MOUSE2/ENTER/JOY1 respond, MOUSE2 steps down and
// everything else up, wrapping over 1..5. Deliberately not the UI_SelectForKey path.
static qboolean UI_Skill_HandleKey(int flags, float *special, int key) {
    int skill;

    if (key != K_MOUSE1 && key != K_MOUSE2 && key != K_ENTER && key != K_JOY1) {
        return qfalse;
    }
    skill = (int)trap_Cvar_VariableValue("g_spSkill");
    if (key == K_MOUSE2) {
        skill--;
    } else {
        skill++;
    }
    if (skill < 1) {
        skill = 5;
    } else if (skill > 5) {
        skill = 1;
    }
    trap_Cvar_Set("g_spSkill", va("%i", skill));
    return qtrue;
}

// [QL] Server-browser source picker (owner-draw UI_NETSOURCE 518). Ported from uix86.dll
// UI_HandleNetSource @0x1000a420: cycle ui_netSource over Local/Internet/Favorites while
// skipping AS_MPLAYER (1) - forward past 1 jumps to 2, back past 1 drops to 0 - then rebuild
// the display list and refresh unless the new source is AS_GLOBAL (2, the master list).
static qboolean UI_NetSource_HandleKey(int flags, float *special, int key) {
    qboolean skipped = qfalse;

    if (key == K_MOUSE1 || key == K_ENTER || key == K_JOY1) {
        ui_netSource.integer++;
        if (ui_netSource.integer == 1) {
            ui_netSource.integer = 2;
            skipped = qtrue;
        }
    } else if (key == K_MOUSE2) {
        ui_netSource.integer--;
        if (ui_netSource.integer == 1) {
            ui_netSource.integer = 0;
            skipped = qtrue;
        }
    } else {
        return qfalse;
    }

    if (!skipped) {
        if (ui_netSource.integer >= 4) {
            ui_netSource.integer = 0;
        } else if (ui_netSource.integer < 0) {
            ui_netSource.integer = 3;
        }
    }

    UI_BuildServerDisplayList(qtrue);
    if (ui_netSource.integer != AS_GLOBAL) {
        UI_StartServerRefresh(qtrue);
    }
    trap_Cvar_Set("ui_netSource", va("%d", ui_netSource.integer));
    return qtrue;
}

// [QL] Server-browser mod filter picker (owner-draw UI_NETFILTER 520). Ported from uix86.dll
// UI_HandleServerFilter @0x1000a4f0: MOUSE1/ENTER/JOY1 forward, MOUSE2 back, wrap over 0..6,
// rebuild the display list on every accepted key.
static qboolean UI_ServerFilter_HandleKey(int flags, float *special, int key) {
    if (key == K_MOUSE1 || key == K_ENTER || key == K_JOY1) {
        ui_serverFilterType.integer++;
    } else if (key == K_MOUSE2) {
        ui_serverFilterType.integer--;
    } else {
        return qfalse;
    }
    if (ui_serverFilterType.integer > 6) {
        ui_serverFilterType.integer = 0;
    } else if (ui_serverFilterType.integer < 0) {
        ui_serverFilterType.integer = 6;
    }
    UI_BuildServerDisplayList(qtrue);
    return qtrue;
}

static qboolean UI_OwnerDrawHandleKey(int ownerDraw, int flags, float* special, int key) {
    switch (ownerDraw) {
        case UI_HANDICAP:
            return UI_Handicap_HandleKey(flags, special, key);
            break;
        case UI_GAMETYPE:
            return UI_GameType_HandleKey(flags, special, key, qtrue);
            break;
        case UI_NETGAMETYPE:
            return UI_NetGameType_HandleKey(flags, special, key);
            break;
        case UI_JOINGAMETYPE:
            return UI_JoinGameType_HandleKey(flags, special, key);
            break;
        case UI_OPPONENT_NAME:
            UI_OpponentName_HandleKey(flags, special, key);
            break;
        case UI_BOTNAME:
            return UI_BotName_HandleKey(flags, special, key);
            break;
        case UI_REDBLUE:
            UI_RedBlue_HandleKey(flags, special, key);
            break;
        case UI_CROSSHAIR:
            UI_Crosshair_HandleKey(flags, special, key);
            break;
        case UI_CROSSHAIR_COLOR:
            // [QL] Colour cycling only when not in health-coloured mode.
            if (trap_Cvar_VariableValue("cg_crosshairHealth") == 0) {
                return UI_CrosshairColor_HandleKey(flags, special, key);
            }
            break;
        case UI_SELECTEDPLAYER:
            UI_SelectedPlayer_HandleKey(flags, special, key);
            break;
        case UI_BOTSKILL: {
            int select = UI_SelectForKey(key);
            if (select != 0) {
                uiInfo.skillIndex += select;
                if (uiInfo.skillIndex >= numSkillLevels) {
                    uiInfo.skillIndex = 0;
                } else if (uiInfo.skillIndex < 0) {
                    uiInfo.skillIndex = numSkillLevels - 1;
                }
                return qtrue;
            }
            break;
        }
        case UI_SKILL:
            return UI_Skill_HandleKey(flags, special, key);
            break;
        case UI_NETSOURCE:
            return UI_NetSource_HandleKey(flags, special, key);
            break;
        case UI_NETFILTER:
            return UI_ServerFilter_HandleKey(flags, special, key);
            break;
        default:
            break;
    }

    return qfalse;
}

static float UI_GetValue(int ownerDraw) {
    return 0;
}

/*
===============
UI_LoadMods
===============
*/
static void UI_LoadMods(void) {
    int numdirs;
    char dirlist[2048];
    char* dirptr;
    char* descptr;
    int i;
    int dirlen;

    uiInfo.modCount = 0;
    numdirs = trap_FS_GetFileList("$modlist", "", dirlist, sizeof(dirlist));
    dirptr = dirlist;
    for (i = 0; i < numdirs; i++) {
        dirlen = strlen(dirptr) + 1;
        descptr = dirptr + dirlen;
        uiInfo.modList[uiInfo.modCount].modName = String_Alloc(dirptr);
        uiInfo.modList[uiInfo.modCount].modDescr = String_Alloc(descptr);
        dirptr += dirlen + strlen(descptr) + 1;
        uiInfo.modCount++;
        if (uiInfo.modCount >= MAX_MODS) {
            break;
        }
    }
}

/*
===============
UI_LoadTeams
===============
*/
static void UI_LoadTeams(void) {
    char teamList[4096];
    char* teamName;
    int i, len, count;

    count = trap_FS_GetFileList("", "team", teamList, 4096);

    if (count) {
        teamName = teamList;
        for (i = 0; i < count; i++) {
            len = strlen(teamName);
            UI_ParseTeamInfo(teamName);
            teamName += len + 1;
        }
    }
}

/*
===============
UI_LoadMovies
===============
*/
static void UI_LoadMovies(void) {
    char movielist[4096];
    char* moviename;
    int i, len;

    uiInfo.movieCount = trap_FS_GetFileList("video", "roq", movielist, 4096);

    if (uiInfo.movieCount) {
        if (uiInfo.movieCount > MAX_MOVIES) {
            uiInfo.movieCount = MAX_MOVIES;
        }
        moviename = movielist;
        for (i = 0; i < uiInfo.movieCount; i++) {
            len = strlen(moviename);
            if (!Q_stricmp(moviename + len - 4, ".roq")) {
                moviename[len - 4] = '\0';
            }
            Q_strupr(moviename);
            uiInfo.movieList[i] = String_Alloc(moviename);
            moviename += len + 1;
        }
    }
}

#define NAMEBUFSIZE (MAX_DEMOS * 32)

/*
===============
UI_LoadDemos
===============
*/
static void UI_LoadDemos(void) {
    char demolist[NAMEBUFSIZE];
    char demoExt[32];
    char demoSuffix[32];
    char* demoname;
    int i, len;
    int protocol;

    // [QL] uix86.dll UI_LoadDemos @0x1000acf0: read ONLY "protocol", extension "dm_%d"
    // (no leading dot), cap the count at 256, strip a trailing ".dm_<proto>" suffix and
    // upper-case each demo name.
    protocol = (int)trap_Cvar_VariableValue("protocol");

    Com_sprintf(demoExt, sizeof(demoExt), "dm_%d", protocol);
    uiInfo.demoCount = trap_FS_GetFileList("demos", demoExt, demolist, ARRAY_LEN(demolist));
    Com_sprintf(demoSuffix, sizeof(demoSuffix), ".dm_%d", protocol);

    if (uiInfo.demoCount > 256)
        uiInfo.demoCount = 256;

    demoname = demolist;
    for (i = 0; i < uiInfo.demoCount; i++) {
        len = strlen(demoname);
        // strip a trailing ".dm_<proto>" if present
        if (len >= (int)strlen(demoSuffix)
            && Q_stricmp(demoname + len - strlen(demoSuffix), demoSuffix) == 0) {
            demoname[len - strlen(demoSuffix)] = '\0';
        }
        Q_strupr(demoname);
        uiInfo.demoList[i] = String_Alloc(demoname);
        demoname += len + 1;
    }
}

static void UI_Update(const char* name) {
    int val = trap_Cvar_VariableValue(name);

    if (Q_stricmp(name, "ui_SetName") == 0) {
        trap_Cvar_Set("name", UI_Cvar_VariableString("ui_Name"));
    } else if (Q_stricmp(name, "ui_setRate") == 0) {
        float rate = trap_Cvar_VariableValue("rate");
        if (rate >= 5000) {
            trap_Cvar_Set("cl_maxpackets", "30");
            trap_Cvar_Set("cl_packetdup", "1");
        } else if (rate >= 4000) {
            trap_Cvar_Set("cl_maxpackets", "15");
            trap_Cvar_Set("cl_packetdup", "2");  // favor less prediction errors when there's packet loss
        } else {
            trap_Cvar_Set("cl_maxpackets", "15");
            trap_Cvar_Set("cl_packetdup", "1");  // favor lower bandwidth
        }
    } else if (Q_stricmp(name, "ui_GetName") == 0) {
        trap_Cvar_Set("ui_Name", UI_Cvar_VariableString("name"));
    } else if (Q_stricmp(name, "r_colorbits") == 0) {
        // [QL] uix86.dll UI_RunMenuScript update @0x1000ae50: plain Cvar_Set string writes;
        // colorbits 32 only sets r_depthbits (no r_stencilbits write).
        switch (val) {
            case 0:
                trap_Cvar_Set("r_depthbits", "0");
                trap_Cvar_Set("r_stencilbits", "0");
                break;
            case 16:
                trap_Cvar_Set("r_depthbits", "16");
                trap_Cvar_Set("r_stencilbits", "0");
                break;
            case 32:
                trap_Cvar_Set("r_depthbits", "24");
                break;
        }
    } else if (Q_stricmp(name, "ui_mousePitch") == 0) {
        // [QL] literal m_pitch strings, not SetValue
        if (val == 0) {
            trap_Cvar_Set("m_pitch", "0.022");
        } else {
            trap_Cvar_Set("m_pitch", "-0.022");
        }
    }
}

// [QL] uix86.dll UI_GetGameTypeString @0x10001000: gametype enum -> short name.
static const char* UI_GetGameTypeString(int gametype) {
    switch (gametype) {
        case 0: return "ffa";
        case 1: return "duel";
        case 2: return "race";
        case 3: return "tdm";
        case 4: return "ca";
        case 5: return "ctf";
        case 6: return "oneflag";
        case 8: return "har";
        case 9: return "ft";
        case 10: return "dom";
        case 11: return "ad";
        case 12: return "rr";
        default: return "";
    }
}

static void UI_RunMenuScript(char** args) {
    const char *name, *name2;
    char buff[1024];

    if (String_Parse(args, &name)) {
        if (Q_stricmp(name, "StartServer") == 0) {
            int i, clients, count, g, v;
            float skill;
            char botBuff[1024];
            trap_Cvar_Set("cg_thirdPerson", "0");
            trap_Cvar_Set("cg_cameraOrbit", "0");
            trap_Cvar_Set("ui_singlePlayerActive", "0");
            trap_Cvar_SetValue("dedicated", Com_Clamp(0, 2, ui_dedicated.integer));
            // [QL] StartServer remaps the menu selection through uiInfo.gameTypes[] because the
            // displayed list is non-contiguous (it skips the 1FCTF/Obelisk/Harvester gtEnums), then
            // clamps to [0, GT_MAX_GAME_TYPE]. Matches UI_RunScript StartServer (uix86.dll
            // 0x1000b0e0) and mirrors the ui_actualNetGametype lookup above.
            trap_Cvar_SetValue("g_gametype", Com_Clamp(0, GT_MAX_GAME_TYPE, uiInfo.gameTypes[ui_netGameType.integer].gtEnum));
            trap_Cvar_Set("g_redTeam", UI_Cvar_VariableString("ui_teamName"));
            trap_Cvar_Set("g_blueTeam", UI_Cvar_VariableString("ui_opponentName"));
            trap_Cmd_ExecuteText(EXEC_APPEND, va("wait ; wait ; map %s\n", uiInfo.mapList[ui_currentNetMap.integer].mapLoadName));
            skill = trap_Cvar_VariableValue("g_spSkill");
            // [QL] raise sv_maxclients to fit the occupied bot slots, then addbot each one.
            // Matches uix86.dll StartServer @0x1000b0e0: count non-empty ui_blueteam%i /
            // ui_redteam%i slots (i=1..5), sv_maxclients = max(count|8, current).
            clients = (int)trap_Cvar_VariableValue("sv_maxclients");
            count = 0;
            for (i = 1; i <= 5; i++) {
                if ((int)trap_Cvar_VariableValue(va("ui_blueteam%i", i)) >= 0) {
                    count++;
                }
                if ((int)trap_Cvar_VariableValue(va("ui_redteam%i", i)) >= 0) {
                    count++;
                }
            }
            if (count == 0) {
                count = 8;
            }
            if (count < clients) {
                count = clients;
            }
            trap_Cvar_Set("sv_maxclients", va("%d", count));
            // [QL] non-team gametypes take the bot from the bot-info list (Sarge fallback) with no
            // team argument; team gametypes take characterList[slot].base with a Blue/Red team.
            g = (int)trap_Cvar_VariableValue("g_gametype");
            for (i = 1; i <= 5; i++) {
                v = (int)trap_Cvar_VariableValue(va("ui_blueteam%i", i));
                if (v > 1) {
                    if (g < GT_TEAM) {
                        Com_sprintf(botBuff, sizeof(botBuff), "addbot %s %f \n", UI_GetBotNameByNumber(v - 2), skill);
                    } else {
                        Com_sprintf(botBuff, sizeof(botBuff), "addbot %s %f %s\n", uiInfo.characterList[v].base, skill, "Blue");
                    }
                    trap_Cmd_ExecuteText(EXEC_APPEND, botBuff);
                }
                v = (int)trap_Cvar_VariableValue(va("ui_redteam%i", i));
                if (v > 1) {
                    if (g < GT_TEAM) {
                        Com_sprintf(botBuff, sizeof(botBuff), "addbot %s %f \n", UI_GetBotNameByNumber(v - 2), skill);
                    } else {
                        Com_sprintf(botBuff, sizeof(botBuff), "addbot %s %f %s\n", uiInfo.characterList[v].base, skill, "Red");
                    }
                    trap_Cmd_ExecuteText(EXEC_APPEND, botBuff);
                }
            }
        } else if (Q_stricmp(name, "resetDefaults") == 0) {
            trap_Cmd_ExecuteText(EXEC_APPEND, "exec default.cfg\n");
            trap_Cmd_ExecuteText(EXEC_APPEND, "cvar_restart\n");
            Controls_SetDefaults();
            // [QL] uix86.dll resetDefaults @0x1000b0e0 keeps the intro suppressed
            trap_Cvar_Set("com_introPlayed", "1");
            trap_Cmd_ExecuteText(EXEC_APPEND, "vid_restart\n");
        } else if (Q_stricmp(name, "loadArenas") == 0) {
            UI_LoadArenasIntoMapList();
            UI_MapCountByGameType(qfalse);
            Menu_SetFeederSelection(NULL, FEEDER_ALLMAPS, 0, "createserver");
        } else if (Q_stricmp(name, "updateCallvoteMapPreview") == 0) {
            // [QL] Re-filter map list by callvote gametype and reset selection
            UI_MapCountByCallvoteGameType();
            Menu_SetFeederSelection(NULL, FEEDER_CVMAPS, 0, "ingame_callvote");
        } else if (Q_stricmp(name, "saveControls") == 0) {
            Controls_SetConfig(qtrue);
        } else if (Q_stricmp(name, "loadControls") == 0) {
            Controls_GetConfig();
        } else if (Q_stricmp(name, "clearError") == 0) {
            trap_Cvar_Set("com_errorMessage", "");
        } else if (Q_stricmp(name, "loadGameInfo") == 0) {
            UI_ParseGameInfo("gameinfo.txt");
        } else if (Q_stricmp(name, "LoadDemos") == 0) {
            UI_LoadDemos();
        } else if (Q_stricmp(name, "LoadMovies") == 0) {
            UI_LoadMovies();
        } else if (Q_stricmp(name, "LoadMods") == 0) {
            UI_LoadMods();
        } else if (Q_stricmp(name, "RunMod") == 0) {
            trap_Cvar_Set("fs_game", uiInfo.modList[uiInfo.modIndex].modName);
            trap_Cmd_ExecuteText(EXEC_APPEND, "vid_restart\n");
        } else if (Q_stricmp(name, "RunDemo") == 0) {
            trap_Cmd_ExecuteText(EXEC_APPEND, va("demo %s\n", uiInfo.demoList[uiInfo.demoIndex]));
        } else if (Q_stricmp(name, "closeJoin") == 0) {
            if (uiInfo.serverStatus.refreshActive) {
                // [QL] cancel an in-progress refresh instead of leaving the menu
                UI_ServerRefreshComplete();  // == UI_StopServerRefresh
                uiInfo.serverStatus.nextDisplayRefresh = 0;
                uiInfo.nextServerStatusRefresh = 0;
                uiInfo.nextFindPlayerRefresh = 0;
            } else {
                Menus_CloseByName("joinserver");
                Menus_OpenByName("main");
            }
        } else if (Q_stricmp(name, "UpdateFilter") == 0) {
            // [QL] rebuild the display list with the current filter, then reselect the top row
            if (ui_netSource.integer == AS_LOCAL) {
                UI_StartServerRefresh(qtrue);
            }
            UI_BuildServerDisplayList(qtrue);
            UI_FeederSelection(FEEDER_SERVERS, 0);
        } else if (Q_stricmp(name, "RefreshServers") == 0) {
            UI_StartServerRefresh(qtrue);
            UI_BuildServerDisplayList(qtrue);
        } else if (Q_stricmp(name, "RefreshFilter") == 0) {
            UI_StartServerRefresh(qfalse);
            UI_BuildServerDisplayList(qtrue);
        } else if (Q_stricmp(name, "StopRefresh") == 0) {
            UI_ServerRefreshComplete();  // == UI_StopServerRefresh
            uiInfo.serverStatus.nextDisplayRefresh = 0;
            uiInfo.nextServerStatusRefresh = 0;
            uiInfo.nextFindPlayerRefresh = 0;
        } else if (Q_stricmp(name, "ServerSort") == 0) {
            int sortColumn;
            if (Int_Parse(args, &sortColumn)) {
                // if sorting on the same column again, flip the direction
                if (sortColumn == uiInfo.serverStatus.sortKey) {
                    uiInfo.serverStatus.sortDir = !uiInfo.serverStatus.sortDir;
                }
                UI_ServersSort(sortColumn, qtrue);
            }
        } else if (Q_stricmp(name, "FindPlayer") == 0) {
            UI_BuildFindPlayerList(qtrue);
            // clear the displayed server-status info
            uiInfo.serverStatusInfo.numLines = 0;
            Menu_SetFeederSelection(NULL, FEEDER_FINDPLAYER, 0, NULL);
        } else if (Q_stricmp(name, "ServerStatus") == 0) {
            trap_LAN_GetServerAddressString(ui_netSource.integer,
                uiInfo.serverStatus.displayServers[uiInfo.serverStatus.currentServer],
                uiInfo.serverStatusAddress, sizeof(uiInfo.serverStatusAddress));
            UI_BuildServerStatus(qtrue);
        } else if (Q_stricmp(name, "FoundPlayerServerStatus") == 0) {
            Q_strncpyz(uiInfo.serverStatusAddress,
                uiInfo.foundPlayerServerAddresses[uiInfo.currentFoundPlayerServer],
                sizeof(uiInfo.serverStatusAddress));
            UI_BuildServerStatus(qtrue);
            Menu_SetFeederSelection(NULL, FEEDER_FINDPLAYER, 0, NULL);
        } else if (Q_stricmp(name, "addFavorite") == 0) {
            if (ui_netSource.integer != AS_FAVORITES) {
                char favName[MAX_NAME_LENGTH];
                char favAddr[MAX_NAME_LENGTH];
                int res;
                char info[MAX_STRING_CHARS];
                trap_LAN_GetServerInfo(ui_netSource.integer,
                    uiInfo.serverStatus.displayServers[uiInfo.serverStatus.currentServer],
                    info, sizeof(info));
                favName[0] = favAddr[0] = '\0';
                Q_strncpyz(favName, Info_ValueForKey(info, "hostname"), sizeof(favName));
                Q_strncpyz(favAddr, Info_ValueForKey(info, "addr"), sizeof(favAddr));
                if (strlen(favName) > 0 && strlen(favAddr) > 0) {
                    res = trap_LAN_AddServer(AS_FAVORITES, favName, favAddr);
                    if (res == 0) {
                        Com_Printf("Favorite already in list\n");
                    } else if (res == -1) {
                        Com_Printf("Favorite list full\n");
                    } else {
                        Com_Printf("Added favorite server %s\n", favAddr);
                    }
                }
            }
        } else if (Q_stricmp(name, "deleteFavorite") == 0) {
            if (ui_netSource.integer == AS_FAVORITES) {
                char favAddr[MAX_NAME_LENGTH];
                char info[MAX_STRING_CHARS];
                trap_LAN_GetServerInfo(ui_netSource.integer,
                    uiInfo.serverStatus.displayServers[uiInfo.serverStatus.currentServer],
                    info, sizeof(info));
                favAddr[0] = '\0';
                Q_strncpyz(favAddr, Info_ValueForKey(info, "addr"), sizeof(favAddr));
                if (strlen(favAddr) > 0) {
                    trap_LAN_RemoveServer(AS_FAVORITES, favAddr);
                }
            }
        } else if (Q_stricmp(name, "createFavorite") == 0) {
            if (ui_netSource.integer == AS_FAVORITES) {
                char favName[MAX_NAME_LENGTH];
                char favAddr[MAX_NAME_LENGTH];
                int res;
                favName[0] = favAddr[0] = '\0';
                Q_strncpyz(favName, UI_Cvar_VariableString("ui_favoriteName"), sizeof(favName));
                Q_strncpyz(favAddr, UI_Cvar_VariableString("ui_favoriteAddress"), sizeof(favAddr));
                if (strlen(favName) > 0 && strlen(favAddr) > 0) {
                    res = trap_LAN_AddServer(AS_FAVORITES, favName, favAddr);
                    if (res == 0) {
                        Com_Printf("Favorite already in list\n");
                    } else if (res == -1) {
                        Com_Printf("Favorite list full\n");
                    } else {
                        Com_Printf("Added favorite server %s\n", favAddr);
                    }
                }
            }
        } else if (Q_stricmp(name, "JoinServer") == 0) {
            trap_Cvar_Set("cg_thirdPerson", "0");
            trap_Cvar_Set("cg_cameraOrbit", "0");
            trap_Cvar_Set("ui_singlePlayerActive", "0");
            if (uiInfo.serverStatus.currentServer >= 0 && uiInfo.serverStatus.currentServer < uiInfo.serverStatus.numDisplayServers) {
                // [QL fix] resolve the selected server's address into buff before connecting
                trap_LAN_GetServerAddressString(ui_netSource.integer,
                    uiInfo.serverStatus.displayServers[uiInfo.serverStatus.currentServer],
                    buff, sizeof(buff));
                trap_Cmd_ExecuteText(EXEC_APPEND, va("connect %s\n", buff));
            }
        } else if (Q_stricmp(name, "FoundPlayerJoinServer") == 0) {
            if (uiInfo.currentFoundPlayerServer >= 0 && uiInfo.currentFoundPlayerServer < uiInfo.numFoundPlayerServers) {
                trap_Cmd_ExecuteText(EXEC_APPEND, va("connect %s\n", uiInfo.foundPlayerServerAddresses[uiInfo.currentFoundPlayerServer]));
            }
        } else if (Q_stricmp(name, "Quit") == 0) {
            trap_Cmd_ExecuteText(EXEC_NOW, "quit");
        } else if (Q_stricmp(name, "Controls") == 0) {
            trap_Cvar_Set("cl_paused", "1");
            trap_Key_SetCatcher(KEYCATCH_UI);
            Menus_CloseAll();
            Menus_ActivateByName("setup_menu2");
        } else if (Q_stricmp(name, "Leave") == 0) {
            trap_Cmd_ExecuteText(EXEC_APPEND, "disconnect\n");
            trap_Key_SetCatcher(KEYCATCH_UI);
            Menus_CloseAll();
            Menus_ActivateByName("main");
        } else if (Q_stricmp(name, "closeingame") == 0) {
            trap_Key_SetCatcher(trap_Key_GetCatcher() & ~KEYCATCH_UI);
            trap_Key_ClearStates();
            trap_Cvar_Set("cl_paused", "0");
            Menus_CloseAll();
        } else if (Q_stricmp(name, "voteMap") == 0) {
            if (ui_currentNetMap.integer >= 0 && ui_currentNetMap.integer < uiInfo.mapCount) {
                // [QL] uix86.dll UI_RunScript voteMap @0x1000b0e0 appends the gametype short
                // name from ui_actualNetGametype (empty when -1).
                trap_Cmd_ExecuteText(EXEC_APPEND, va("callvote map %s %s\n",
                    uiInfo.mapList[ui_currentNetMap.integer].mapLoadName,
                    (ui_actualNetGameType.integer == -1) ? "" : UI_GetGameTypeString(ui_actualNetGameType.integer)));
            }
        } else if (Q_stricmp(name, "voteKick") == 0) {
            if (uiInfo.playerIndex >= 0 && uiInfo.playerIndex < uiInfo.playerCount) {
                trap_Cmd_ExecuteText(EXEC_APPEND, va("callvote kick %s\n", uiInfo.playerNames[uiInfo.playerIndex]));
            }
        } else if (Q_stricmp(name, "voteGame") == 0) {
            if (ui_netGameType.integer >= 0 && ui_netGameType.integer < uiInfo.numGameTypes) {
                trap_Cmd_ExecuteText(EXEC_APPEND, va("callvote g_gametype %i\n", uiInfo.gameTypes[ui_netGameType.integer].gtEnum));
            }
        } else if (Q_stricmp(name, "addBot") == 0) {
            if (trap_Cvar_VariableValue("g_gametype") >= GT_TEAM) {
                // [QL] .base holds the model name (see Character_Parse); .name is
                // now the skin, so use base as the bot character identifier.
                trap_Cmd_ExecuteText(EXEC_APPEND, va("addbot %s %i %s\n", uiInfo.characterList[uiInfo.botIndex].base, uiInfo.skillIndex + 1, (uiInfo.redBlue == 0) ? "Red" : "Blue"));
            } else {
                trap_Cmd_ExecuteText(EXEC_APPEND, va("addbot %s %i %s\n", UI_GetBotNameByNumber(uiInfo.botIndex), uiInfo.skillIndex + 1, (uiInfo.redBlue == 0) ? "Red" : "Blue"));
            }
        } else if (Q_stricmp(name, "orders") == 0) {
            const char* orders;
            if (String_Parse(args, &orders)) {
                int selectedPlayer = trap_Cvar_VariableValue("cg_selectedPlayer");
                if (selectedPlayer < uiInfo.myTeamCount) {
                    Com_sprintf(buff, sizeof(buff), orders, uiInfo.teamClientNums[selectedPlayer]);
                    trap_Cmd_ExecuteText(EXEC_APPEND, buff);
                    trap_Cmd_ExecuteText(EXEC_APPEND, "\n");
                } else {
                    int i;
                    for (i = 0; i < uiInfo.myTeamCount; i++) {
                        if (uiInfo.playerNumber == uiInfo.teamClientNums[i]) {
                            continue;
                        }
                        Com_sprintf(buff, sizeof(buff), orders, uiInfo.teamClientNums[i]);
                        trap_Cmd_ExecuteText(EXEC_APPEND, buff);
                        trap_Cmd_ExecuteText(EXEC_APPEND, "\n");
                    }
                }
                trap_Key_SetCatcher(trap_Key_GetCatcher() & ~KEYCATCH_UI);
                trap_Key_ClearStates();
                trap_Cvar_Set("cl_paused", "0");
                Menus_CloseAll();
            }
        } else if (Q_stricmp(name, "glCustom") == 0) {
            trap_Cvar_Set("ui_glCustom", "4");
        } else if (Q_stricmp(name, "update") == 0) {
            if (String_Parse(args, &name2)) {
                UI_Update(name2);
            }
        } else if (Q_stricmp(name, "setPbClStatus") == 0) {
            int stat;
            if (Int_Parse(args, &stat))
                trap_SetPbClStatus(stat);
        } else if (Q_stricmp(name, "clearComError") == 0) {
			trap_Cvar_Set("com_errorMessage", "");
        // [QL] Player action scripts - binary-verified from uix86.dll UI_RunScript
        } else if (Q_stricmp(name, "clientViewProfile") == 0) {
            if (uiInfo.playerIndex >= 0 && uiInfo.playerIndex < uiInfo.playerCount) {
                trap_Cmd_ExecuteText(EXEC_APPEND, va("clientviewprofile %i\n",
                    uiInfo.teamClientNums[uiInfo.playerIndex]));
            }
        } else if (Q_stricmp(name, "clientFriendInvite") == 0) {
            if (uiInfo.playerIndex >= 0 && uiInfo.playerIndex < uiInfo.playerCount) {
                trap_Cmd_ExecuteText(EXEC_APPEND, va("clientfriendinvite %i\n",
                    uiInfo.teamClientNums[uiInfo.playerIndex]));
            }
        } else if (Q_stricmp(name, "clientMutePlayer") == 0) {
            if (uiInfo.playerIndex >= 0 && uiInfo.playerIndex < uiInfo.playerCount) {
                trap_Cmd_ExecuteText(EXEC_APPEND, va("clientmute %i\n",
                    uiInfo.teamClientNums[uiInfo.playerIndex]));
            }
        } else if (Q_stricmp(name, "modPlayer") == 0) {
            if (uiInfo.playerIndex >= 0 && uiInfo.playerIndex < uiInfo.playerCount) {
                trap_Cmd_ExecuteText(EXEC_APPEND, va("addmod %i\n",
                    uiInfo.teamClientNums[uiInfo.playerIndex]));
            }
        } else if (Q_stricmp(name, "adminPlayer") == 0) {
            if (uiInfo.playerIndex >= 0 && uiInfo.playerIndex < uiInfo.playerCount) {
                trap_Cmd_ExecuteText(EXEC_APPEND, va("addadmin %i\n",
                    uiInfo.teamClientNums[uiInfo.playerIndex]));
            }
        } else if (Q_stricmp(name, "deopPlayer") == 0) {
            if (uiInfo.playerIndex >= 0 && uiInfo.playerIndex < uiInfo.playerCount) {
                trap_Cmd_ExecuteText(EXEC_APPEND, va("demote %i\n",
                    uiInfo.teamClientNums[uiInfo.playerIndex]));
            }
        } else if (Q_stricmp(name, "putred") == 0) {
            if (uiInfo.playerIndex >= 0 && uiInfo.playerIndex < uiInfo.playerCount) {
                trap_Cmd_ExecuteText(EXEC_APPEND, va("put %i r\n",
                    uiInfo.teamClientNums[uiInfo.playerIndex]));
            }
        } else if (Q_stricmp(name, "putblue") == 0) {
            if (uiInfo.playerIndex >= 0 && uiInfo.playerIndex < uiInfo.playerCount) {
                trap_Cmd_ExecuteText(EXEC_APPEND, va("put %i b\n",
                    uiInfo.teamClientNums[uiInfo.playerIndex]));
            }
        } else if (Q_stricmp(name, "putspec") == 0) {
            if (uiInfo.playerIndex >= 0 && uiInfo.playerIndex < uiInfo.playerCount) {
                trap_Cmd_ExecuteText(EXEC_APPEND, va("put %i s\n",
                    uiInfo.teamClientNums[uiInfo.playerIndex]));
            }
        } else if (Q_stricmp(name, "mutePlayer") == 0) {
            if (uiInfo.playerIndex >= 0 && uiInfo.playerIndex < uiInfo.playerCount) {
                trap_Cmd_ExecuteText(EXEC_APPEND, va("mute %i\n",
                    uiInfo.teamClientNums[uiInfo.playerIndex]));
            }
        } else if (Q_stricmp(name, "unmutePlayer") == 0) {
            if (uiInfo.playerIndex >= 0 && uiInfo.playerIndex < uiInfo.playerCount) {
                trap_Cmd_ExecuteText(EXEC_APPEND, va("unmute %i\n",
                    uiInfo.teamClientNums[uiInfo.playerIndex]));
            }
        } else if (Q_stricmp(name, "tempbanPlayer") == 0) {
            if (uiInfo.playerIndex >= 0 && uiInfo.playerIndex < uiInfo.playerCount) {
                trap_Cmd_ExecuteText(EXEC_APPEND, va("tempban %i\n",
                    uiInfo.teamClientNums[uiInfo.playerIndex]));
            }
        } else if (Q_stricmp(name, "banPlayer") == 0) {
            if (uiInfo.playerIndex >= 0 && uiInfo.playerIndex < uiInfo.playerCount) {
                trap_Cmd_ExecuteText(EXEC_APPEND, va("ban %i\n",
                    uiInfo.teamClientNums[uiInfo.playerIndex]));
            }
        } else if (Q_stricmp(name, "kickPlayer") == 0) {
            if (uiInfo.playerIndex >= 0 && uiInfo.playerIndex < uiInfo.playerCount) {
                trap_Cmd_ExecuteText(EXEC_APPEND, va("clientkick %i\n",
                    uiInfo.teamClientNums[uiInfo.playerIndex]));
            }
        } else if (Q_stricmp(name, "teamModelChanged") == 0) {
            // [QL] Force team model preview to refresh
            updateModel = qtrue;
        } else if (Q_stricmp(name, "enemyModelChanged") == 0) {
            // [QL] Force enemy model preview to refresh
            updateModel = qtrue;
        } else if (Q_stricmp(name, "teamColorDefaults") == 0) {
            // [QL] Reset team color sliders to defaults
            trap_Cvar_Set("ui_teamHeadColor", "96");
            trap_Cvar_Set("ui_teamUpperColor", "23");
            trap_Cvar_Set("ui_teamLowerColor", "23");
        } else if (Q_stricmp(name, "enemyColorDefaults") == 0) {
            // [QL] Reset enemy color sliders to defaults
            trap_Cvar_Set("ui_enemyHeadColor", "27");
            trap_Cvar_Set("ui_enemyUpperColor", "2");
            trap_Cvar_Set("ui_enemyLowerColor", "2");
        } else {
            Com_Printf("unknown UI script %s\n", name);
        }
    }
}

// Empty in stock ioquake3 too. No ui menu owner-draw consumes DC->getTeamColor;
// the team-tinted drawing lives in cgame, which has its own CG_GetTeamColor.
// Not a porting gap - left as-is so the DC table stays complete.
static void UI_GetTeamColor(vec4_t* color) {
}

/*
==================
UI_MapCountByGameType
==================
*/
static int UI_MapCountByGameType(qboolean singlePlayer) {
    int i, c, game;
    char info[MAX_STRING_CHARS];
    c = 0;
    // [QL] uix86.dll UI_FilterMapsEx @0x1000d3c0: source the gametype from
    // CS_SERVERINFO (not the menu dropdown), clamp negatives to 0, no GT_TEAM->GT_FFA
    // remap. For the MAPS feeder (singlePlayer) require the extra typeBits & 4 bit.
    game = 0;
    if (trap_GetConfigString(CS_SERVERINFO, info, sizeof(info))) {
        game = atoi(Info_ValueForKey(info, "gametype"));
        if (game < 0) {
            game = 0;
        }
    }

    for (i = 0; i < uiInfo.mapCount; i++) {
        uiInfo.mapList[i].active = qfalse;
        if ((uiInfo.mapList[i].typeBits & (1 << game))
            && (!singlePlayer || (uiInfo.mapList[i].typeBits & 4))) {
            c++;
            uiInfo.mapList[i].active = qtrue;
        }
    }
    return c;
}

/*
==================
UI_MapCountByCallvoteGameType
[QL] uix86.dll UI_FilterMaps @0x1000d300: source the gametype from CS_SERVERINFO
(clamp negatives to 0), then if the ui_actualNetGametype override is not -1 use it.
No GT_TEAM->GT_FFA remap and no typeBits & 4 requirement.
==================
*/
static int UI_MapCountByCallvoteGameType(void) {
    int i, c, game;
    char info[MAX_STRING_CHARS];
    c = 0;
    game = 0;
    if (trap_GetConfigString(CS_SERVERINFO, info, sizeof(info))) {
        game = atoi(Info_ValueForKey(info, "gametype"));
        if (game < 0) {
            game = 0;
        }
    }
    if (ui_actualNetGameType.integer != -1) {
        game = ui_actualNetGameType.integer;
    }
    for (i = 0; i < uiInfo.mapCount; i++) {
        uiInfo.mapList[i].active = qfalse;
        if (uiInfo.mapList[i].typeBits & (1 << game)) {
            c++;
            uiInfo.mapList[i].active = qtrue;
        }
    }
    return c;
}

qboolean UI_hasSkinForBase(const char* base, const char* team) {
    char test[MAX_QPATH];

    Com_sprintf(test, sizeof(test), "models/players/%s/%s/lower_default.skin", base, team);

    if (trap_FS_FOpenFile(test, NULL, FS_READ)) {
        return qtrue;
    }
    Com_sprintf(test, sizeof(test), "models/players/characters/%s/%s/lower_default.skin", base, team);

    if (trap_FS_FOpenFile(test, NULL, FS_READ)) {
        return qtrue;
    }
    return qfalse;
}

/*
==================
stristr
==================
*/
static char* stristr(char* str, char* charset) {
    int i;

    while (*str) {
        for (i = 0; charset[i] && str[i]; i++) {
            if (toupper(charset[i]) != toupper(str[i]))
                break;
        }
        if (!charset[i])
            return str;
        str++;
    }
    return NULL;
}

// ============================================================================
// [QL] Server browser
//
// Ported from uix86.dll (build 1069). Verified against Ghidra @ port 8195:
//   UI_InsertServerIntoDisplayList  0x1000d630
//   UI_RemoveServerFromDisplayList  0x1000d670
//   UI_BinaryServerInsert           0x1000d6c0   (QL-variant search form)
//   UI_BuildServerDisplayList       0x1000d740
//   UI_SortServerStatusInfo         0x1000da60
//   UI_GetServerStatusInfo          0x1000db60
//   UI_BuildFindPlayerList          0x1000deb0
//   UI_BuildServerStatus            0x1000e3b0   (Ghidra label "UI_FeederSelection")
//   UI_ServerRefreshComplete        0x100118f0   (== missionpack UI_StopServerRefresh)
//   UI_DoServerRefresh              0x10011970
//   UI_StartServerRefresh           0x10011a30
// ============================================================================

static int QDECL UI_ServerSortCompare(const void *arg1, const void *arg2) {
    return trap_LAN_CompareServers(ui_netSource.integer, uiInfo.serverStatus.sortKey,
                                   uiInfo.serverStatus.sortDir,
                                   *(const int *)arg1, *(const int *)arg2);
}

static void UI_ServersSort(int column, qboolean force) {
    if (!force && uiInfo.serverStatus.sortKey == column) {
        return;
    }
    uiInfo.serverStatus.sortKey = column;
    qsort(&uiInfo.serverStatus.displayServers[0], uiInfo.serverStatus.numDisplayServers,
          sizeof(int), UI_ServerSortCompare);
}

static void UI_InsertServerIntoDisplayList(int num, int position) {
    int i;

    if (position < 0 || position > uiInfo.serverStatus.numDisplayServers) {
        return;
    }
    uiInfo.serverStatus.numDisplayServers++;
    for (i = uiInfo.serverStatus.numDisplayServers; i > position; i--) {
        uiInfo.serverStatus.displayServers[i] = uiInfo.serverStatus.displayServers[i - 1];
    }
    uiInfo.serverStatus.displayServers[position] = num;
}

static void UI_RemoveServerFromDisplayList(int num) {
    int i, j;

    for (i = 0; i < uiInfo.serverStatus.numDisplayServers; i++) {
        if (uiInfo.serverStatus.displayServers[i] == num) {
            uiInfo.serverStatus.numDisplayServers--;
            for (j = i; j < uiInfo.serverStatus.numDisplayServers; j++) {
                uiInfo.serverStatus.displayServers[j] = uiInfo.serverStatus.displayServers[j + 1];
            }
            return;
        }
    }
}

// [QL] Binary-search insert. QL uses an alternate (functionally equivalent) form to
// stock missionpack: unconditional 'len -= mid', 'offset += mid' inside the loop,
// loop while 'mid > 0', then a deferred 'offset++' when the last compare was "greater".
static void UI_BinaryServerInsert(int num) {
    int mid, offset, res, len;

    len = uiInfo.serverStatus.numDisplayServers;
    offset = 0;
    if (len <= 0) {
        UI_InsertServerIntoDisplayList(num, 0);
        return;
    }

    res = 0;
    do {
        mid = len >> 1;
        res = trap_LAN_CompareServers(ui_netSource.integer, uiInfo.serverStatus.sortKey,
                                      uiInfo.serverStatus.sortDir, num,
                                      uiInfo.serverStatus.displayServers[offset + mid]);
        if (res == 0) {
            UI_InsertServerIntoDisplayList(num, offset + mid);
            return;
        }
        len -= mid;
        if (res == 1) {
            offset += mid;
        }
    } while (mid > 0);

    if (res == 1) {
        offset++;
    }
    UI_InsertServerIntoDisplayList(num, offset);
}

// [QL] Builds the filtered/sorted display list. Default motd is the unchanged
// "Welcome to Team Arena!". force==2 rebuilds without resetting the master query.
static void UI_BuildServerDisplayList(int force) {
    int i, count, clients, maxClients, ping, game, len;
    char info[MAX_STRING_CHARS];
    static int numinvisible;

    if (!force && uiInfo.uiDC.realTime <= uiInfo.serverStatus.nextDisplayRefresh) {
        return;
    }
    if (force == 2) {
        force = 0;
    }

    trap_Cvar_VariableStringBuffer("cl_motdString", uiInfo.serverStatus.motd,
                                   sizeof(uiInfo.serverStatus.motd));
    len = strlen(uiInfo.serverStatus.motd);
    if (len == 0) {
        strcpy(uiInfo.serverStatus.motd, "Welcome to Team Arena!");
        len = strlen(uiInfo.serverStatus.motd);
    }
    if (len != uiInfo.serverStatus.motdLen) {
        uiInfo.serverStatus.motdLen = len;
        uiInfo.serverStatus.motdWidth = -1;
    }

    if (force) {
        numinvisible = 0;
        uiInfo.serverStatus.numDisplayServers = 0;
        uiInfo.serverStatus.numPlayersOnServers = 0;
        Menu_SetFeederSelection(NULL, FEEDER_SERVERS, 0, NULL);
        trap_LAN_MarkServerVisible(ui_netSource.integer, -1, qtrue);
    }

    count = trap_LAN_GetServerCount(ui_netSource.integer);
    if (count == -1 || (ui_netSource.integer == AS_LOCAL && count == 0)) {
        uiInfo.serverStatus.numDisplayServers = 0;
        uiInfo.serverStatus.numPlayersOnServers = 0;
        uiInfo.serverStatus.nextDisplayRefresh = uiInfo.uiDC.realTime + 500;
        return;
    }

    for (i = 0; i < count; i++) {
        if (!trap_LAN_ServerIsVisible(ui_netSource.integer, i)) {
            continue;
        }
        ping = trap_LAN_GetServerPing(ui_netSource.integer, i);
        if (ping > 0 || ui_netSource.integer == AS_FAVORITES) {
            trap_LAN_GetServerInfo(ui_netSource.integer, i, info, sizeof(info));

            clients = atoi(Info_ValueForKey(info, "clients"));
            uiInfo.serverStatus.numPlayersOnServers += clients;

            if (ui_browserShowEmpty.integer == 0 && clients == 0) {
                trap_LAN_MarkServerVisible(ui_netSource.integer, i, qfalse);
                continue;
            }
            if (ui_browserShowFull.integer == 0) {
                maxClients = atoi(Info_ValueForKey(info, "sv_maxclients"));
                if (clients == maxClients) {
                    trap_LAN_MarkServerVisible(ui_netSource.integer, i, qfalse);
                    continue;
                }
            }
            if (uiInfo.joinGameTypes[ui_joinGameType.integer].gtEnum != -1) {
                game = atoi(Info_ValueForKey(info, "gametype"));
                if (game != uiInfo.joinGameTypes[ui_joinGameType.integer].gtEnum) {
                    trap_LAN_MarkServerVisible(ui_netSource.integer, i, qfalse);
                    continue;
                }
            }
            // [QL] uix86.dll UI_BuildServerDisplayList @0x1000d740 guards on
            // ui_serverFilterType > 0 only (the 7-entry table needs no upper bound)
            if (ui_serverFilterType.integer > 0) {
                if (Q_stricmp(Info_ValueForKey(info, "game"),
                              serverFilters[ui_serverFilterType.integer].basedir) != 0) {
                    trap_LAN_MarkServerVisible(ui_netSource.integer, i, qfalse);
                    continue;
                }
            }
            // never add a favourite twice
            if (ui_netSource.integer == AS_FAVORITES) {
                UI_RemoveServerFromDisplayList(i);
            }
            UI_BinaryServerInsert(i);
            if (ping > 0) {
                trap_LAN_MarkServerVisible(ui_netSource.integer, i, qfalse);
                numinvisible++;
            }
        }
    }

    uiInfo.serverStatus.refreshtime = uiInfo.uiDC.realTime;
}

// [QL] Promote a fixed set of cvar keys to the top of the status-detail list
// and relabel them (sortKeys/sortKeyNames verified from uix86.dll @ 0x1002af48).
static void UI_SortServerStatusInfo(serverStatusInfo_t *info) {
    int i, j, index;
    char *tmp1, *tmp2;
    static const char *sortKeys[] = {
        "sv_hostname", "Address", "gamename", "g_gametype", "mapname",
        "version", "protocol", "timelimit", "fraglimit", NULL
    };
    static const char *sortKeyNames[] = {
        "Name", "", "Game name", "Game type", "Map",
        "", "", "", "", NULL
    };

    index = 0;
    for (i = 0; sortKeys[i]; i++) {
        // [QL] uix86.dll UI_SortServerStatusInfo @0x1000da60: inner scan starts at 0
        for (j = 0; j < info->numLines; j++) {
            if (info->lines[j][1] && *info->lines[j][1] == '\0'
                && info->lines[j][0] && !Q_stricmp(info->lines[j][0], sortKeys[i])) {
                tmp1 = info->lines[index][0];
                tmp2 = info->lines[index][3];
                info->lines[index][0] = info->lines[j][0];
                info->lines[index][3] = info->lines[j][3];
                info->lines[j][0] = tmp1;
                info->lines[j][3] = tmp2;
                if (strlen(sortKeyNames[i])) {
                    info->lines[index][0] = (char *)sortKeyNames[i];
                }
                index++;
            }
        }
    }
}

static qboolean UI_GetServerStatusInfo(const char *serverAddress, serverStatusInfo_t *info) {
    char *p, *score, *ping, *name;
    int i, len;

    if (!info) {
        trap_LAN_ServerStatus(serverAddress, NULL, 0);
        return qfalse;
    }
    memset(info, 0, sizeof(*info));
    if (trap_LAN_ServerStatus(serverAddress, info->text, sizeof(info->text))) {
        Q_strncpyz(info->address, serverAddress, sizeof(info->address));
        p = info->text;
        info->numLines = 0;
        info->lines[info->numLines][0] = "Address";
        info->lines[info->numLines][1] = "";
        info->lines[info->numLines][2] = "";
        info->lines[info->numLines][3] = info->address;
        info->numLines++;
        // cvar key/value pairs
        while (p && *p) {
            p = strchr(p, '\\');
            if (!p) break;
            *p++ = '\0';
            if (*p == '\\') break;
            info->lines[info->numLines][0] = p;
            info->lines[info->numLines][1] = "";
            info->lines[info->numLines][2] = "";
            p = strchr(p, '\\');
            if (!p) break;
            *p++ = '\0';
            info->lines[info->numLines][3] = p;
            info->numLines++;
            if (info->numLines >= MAX_SERVERSTATUS_LINES) break;
        }
        // player list
        if (info->numLines < MAX_SERVERSTATUS_LINES - 3) {
            info->lines[info->numLines][0] = "";
            info->lines[info->numLines][1] = "";
            info->lines[info->numLines][2] = "";
            info->lines[info->numLines][3] = "";
            info->numLines++;
            info->lines[info->numLines][0] = "num";
            info->lines[info->numLines][1] = "score";
            info->lines[info->numLines][2] = "ping";
            info->lines[info->numLines][3] = "name";
            info->numLines++;
            i = 0;
            len = 0;
            while (p && *p) {
                if (*p == '\\') *p++ = '\0';
                if (!p) break;
                score = p;
                p = strchr(p, ' ');
                if (!p) break;
                *p++ = '\0';
                ping = p;
                p = strchr(p, ' ');
                if (!p) break;
                *p++ = '\0';
                name = p;
                Com_sprintf(&info->pings[len], sizeof(info->pings) - len, "%d", i);
                info->lines[info->numLines][0] = &info->pings[len];
                len += strlen(&info->pings[len]) + 1;
                info->lines[info->numLines][1] = score;
                info->lines[info->numLines][2] = ping;
                info->lines[info->numLines][3] = name;
                info->numLines++;
                if (info->numLines >= MAX_SERVERSTATUS_LINES) break;
                p = strchr(p, '\\');
                if (!p) break;
                *p++ = '\0';
                i++;
            }
        }
        UI_SortServerStatusInfo(info);
        return qtrue;
    }
    return qfalse;
}

// [QL] 0x1000e3b0 (Ghidra label "UI_FeederSelection"): really UI_BuildServerStatus.
// _UI_Refresh calls this each frame with force==qfalse.
static void UI_BuildServerStatus(qboolean force) {
    if (uiInfo.nextFindPlayerRefresh) {
        return;
    }
    if (!force) {
        if (uiInfo.nextServerStatusRefresh == 0
            || uiInfo.uiDC.realTime < uiInfo.nextServerStatusRefresh) {
            return;
        }
    } else {
        Menu_SetFeederSelection(NULL, FEEDER_SERVERSTATUS, 0, NULL);
        uiInfo.serverStatusInfo.numLines = 0;
        trap_LAN_ServerStatus(NULL, NULL, 0);
    }
    // [QL] uix86.dll UI_BuildServerStatus @0x1000e3b0 bound: currentServer <= numDisplayServers
    if (uiInfo.serverStatus.currentServer >= 0
        && uiInfo.serverStatus.currentServer <= uiInfo.serverStatus.numDisplayServers
        && uiInfo.serverStatus.numDisplayServers != 0) {
        if (UI_GetServerStatusInfo(uiInfo.serverStatusAddress, &uiInfo.serverStatusInfo)) {
            uiInfo.nextServerStatusRefresh = 0;
            UI_GetServerStatusInfo(uiInfo.serverStatusAddress, NULL);
        } else {
            uiInfo.nextServerStatusRefresh = uiInfo.uiDC.realTime + 500;
        }
    }
}

static void UI_BuildFindPlayerList(qboolean force) {
    static int numFound, numTimeOuts;
    int i, j, resend;
    serverStatusInfo_t info;
    char name[MAX_NAME_LENGTH + 2];
    char infoString[MAX_STRING_CHARS];
    pendingServer_t *pendingServer;

    if (!force) {
        if (!uiInfo.nextFindPlayerRefresh || uiInfo.nextFindPlayerRefresh > uiInfo.uiDC.realTime) {
            return;
        }
    } else {
        memset(&uiInfo.pendingServerStatus, 0, sizeof(uiInfo.pendingServerStatus));
        uiInfo.numFoundPlayerServers = 0;
        uiInfo.currentFoundPlayerServer = 0;
        trap_Cvar_VariableStringBuffer("ui_findPlayer", uiInfo.findPlayerName,
                                       sizeof(uiInfo.findPlayerName));
        Q_CleanStr(uiInfo.findPlayerName);
        if (!strlen(uiInfo.findPlayerName)) {
            uiInfo.nextFindPlayerRefresh = 0;
            return;
        }
        // resend at half the server-status timeout
        resend = ui_serverStatusTimeOut.integer / 2 - 10;
        if (resend < 50) {
            resend = 50;
        }
        trap_Cvar_Set("cl_serverStatusResendTime", va("%d", resend));
        trap_LAN_ServerStatus(NULL, NULL, 0);
        uiInfo.numFoundPlayerServers = 1;
        // [QL] uix86.dll UI_BuildFindPlayerList @0x1000deb0 writes at the numFoundPlayerServers
        // slot directly (no -1).
        Com_sprintf(uiInfo.foundPlayerServerNames[uiInfo.numFoundPlayerServers],
                    sizeof(uiInfo.foundPlayerServerNames[0]), "searching %d...",
                    uiInfo.pendingServerStatus.num);
        numFound = 0;
        numTimeOuts++;
    }
    for (i = 0; i < MAX_SERVERSTATUSREQUESTS; i++) {
        pendingServer = &uiInfo.pendingServerStatus.server[i];
        if (pendingServer->valid) {
            if (UI_GetServerStatusInfo(pendingServer->adrstr, &info)) {
                numFound++;
                for (j = 0; j < info.numLines; j++) {
                    // [QL] guard on the ping column [2], not the name column [3]
                    if (!info.lines[j][2] || !*info.lines[j][2]) {
                        continue;
                    }
                    Q_strncpyz(name, info.lines[j][3], sizeof(name));
                    Q_CleanStr(name);
                    if (stristr(name, uiInfo.findPlayerName)) {
                        if (uiInfo.numFoundPlayerServers < MAX_FOUNDPLAYER_SERVERS - 1) {
                            Q_strncpyz(uiInfo.foundPlayerServerAddresses[uiInfo.numFoundPlayerServers],
                                       pendingServer->adrstr,
                                       sizeof(uiInfo.foundPlayerServerAddresses[0]));
                            Q_strncpyz(uiInfo.foundPlayerServerNames[uiInfo.numFoundPlayerServers],
                                       pendingServer->name,
                                       sizeof(uiInfo.foundPlayerServerNames[0]));
                            uiInfo.numFoundPlayerServers++;
                        } else {
                            uiInfo.pendingServerStatus.num = uiInfo.serverStatus.numDisplayServers;
                            break;
                        }
                    }
                }
                Com_sprintf(uiInfo.foundPlayerServerNames[uiInfo.numFoundPlayerServers],
                            sizeof(uiInfo.foundPlayerServerNames[0]), "searching %d/%d...",
                            uiInfo.pendingServerStatus.num, numFound);
                pendingServer->valid = qfalse;
            }
            if (!pendingServer->valid
                || pendingServer->startTime < uiInfo.uiDC.realTime - ui_serverStatusTimeOut.integer) {
                if (pendingServer->valid) {
                    numTimeOuts++;
                }
                trap_LAN_ServerStatus(pendingServer->adrstr, NULL, 0);
                pendingServer->valid = qfalse;
                if (uiInfo.pendingServerStatus.num < uiInfo.serverStatus.numDisplayServers) {
                    pendingServer->startTime = uiInfo.uiDC.realTime;
                    trap_LAN_GetServerAddressString(ui_netSource.integer,
                        uiInfo.serverStatus.displayServers[uiInfo.pendingServerStatus.num],
                        pendingServer->adrstr, sizeof(pendingServer->adrstr));
                    trap_LAN_GetServerInfo(ui_netSource.integer,
                        uiInfo.serverStatus.displayServers[uiInfo.pendingServerStatus.num],
                        infoString, sizeof(infoString));
                    Q_strncpyz(pendingServer->name, Info_ValueForKey(infoString, "hostname"),
                               sizeof(pendingServer->name));
                    pendingServer->valid = qtrue;
                    uiInfo.pendingServerStatus.num++;
                    Com_sprintf(uiInfo.foundPlayerServerNames[uiInfo.numFoundPlayerServers - 1],
                                sizeof(uiInfo.foundPlayerServerNames[0]), "searching %d/%d...",
                                uiInfo.pendingServerStatus.num, numFound);
                }
            }
        }
    }
    for (i = 0; i < MAX_SERVERSTATUSREQUESTS; i++) {
        if (uiInfo.pendingServerStatus.server[i].valid) {
            break;
        }
    }
    if (i < MAX_SERVERSTATUSREQUESTS
        || uiInfo.pendingServerStatus.num < uiInfo.serverStatus.numDisplayServers) {
        uiInfo.nextFindPlayerRefresh = uiInfo.uiDC.realTime + 25;
        return;
    }
    if (!uiInfo.numFoundPlayerServers) {
        Com_sprintf(uiInfo.foundPlayerServerNames[0], sizeof(uiInfo.foundPlayerServerNames[0]),
                    "no servers found");
    } else {
        Com_sprintf(uiInfo.foundPlayerServerNames[uiInfo.numFoundPlayerServers],
                    sizeof(uiInfo.foundPlayerServerNames[0]),
                    "%d server%s found with player %s",
                    uiInfo.numFoundPlayerServers - 1,
                    (uiInfo.numFoundPlayerServers == 2) ? "" : "s", uiInfo.findPlayerName);
    }
    uiInfo.nextFindPlayerRefresh = 0;
    UI_FeederSelection(FEEDER_FINDPLAYER, uiInfo.currentFoundPlayerServer);
}

// [QL] 0x100118f0: QL name UI_ServerRefreshComplete == missionpack UI_StopServerRefresh.
static void UI_ServerRefreshComplete(void) {
    int count;

    if (!uiInfo.serverStatus.refreshActive) {
        return;
    }
    uiInfo.serverStatus.refreshActive = qfalse;
    Com_Printf("%d servers listed in browser with %d players.\n",
               uiInfo.serverStatus.numDisplayServers,
               uiInfo.serverStatus.numPlayersOnServers);
    count = trap_LAN_GetServerCount(ui_netSource.integer);
    if (count - uiInfo.serverStatus.numDisplayServers > 0) {
        Com_Printf("%d servers not listed due to packet loss or pings higher than %d\n",
                   count - uiInfo.serverStatus.numDisplayServers,
                   (int)trap_Cvar_VariableValue("cl_maxPing"));
    }
}

static void UI_DoServerRefresh(void) {
    qboolean wait = qfalse;

    if (!uiInfo.serverStatus.refreshActive) {
        return;
    }
    if (ui_netSource.integer != AS_FAVORITES) {
        if (ui_netSource.integer == AS_LOCAL) {
            if (!trap_LAN_GetServerCount(ui_netSource.integer)) {
                wait = qtrue;
            }
        } else {
            if (trap_LAN_GetServerCount(ui_netSource.integer) < 0) {
                wait = qtrue;
            }
        }
    }

    if (uiInfo.uiDC.realTime < uiInfo.serverStatus.refreshtime && wait) {
        return;
    }

    if (trap_LAN_UpdateVisiblePings(ui_netSource.integer)) {
        uiInfo.serverStatus.refreshtime = uiInfo.uiDC.realTime + 1000;
    } else if (!wait) {
        UI_BuildServerDisplayList(2);
        UI_ServerRefreshComplete();
    }
    UI_BuildServerDisplayList(qfalse);
}

// [QL] AS_GLOBAL uses master index 0, AS_MPLAYER master index 1 (same as missionpack).
// The !full path inlines missionpack's UI_UpdatePendingPings.
static void UI_StartServerRefresh(qboolean full) {
    int i;
    char *ptr;
    qtime_t q;

    trap_RealTime(&q);
    trap_Cvar_Set(va("ui_lastServerRefresh_%i", ui_netSource.integer),
                  va("%s-%i, %i at %i:%i", monthAbbrev[q.tm_mon], q.tm_mday,
                     1900 + q.tm_year, q.tm_hour, q.tm_min));

    if (!full) {
        trap_LAN_ResetPings(ui_netSource.integer);
        uiInfo.serverStatus.refreshActive = qtrue;
        uiInfo.serverStatus.refreshtime = uiInfo.uiDC.realTime + 1000;
        return;
    }

    uiInfo.serverStatus.refreshActive = qtrue;
    uiInfo.serverStatus.nextDisplayRefresh = uiInfo.uiDC.realTime + 1000;
    uiInfo.serverStatus.numDisplayServers = 0;
    uiInfo.serverStatus.numPlayersOnServers = 0;
    trap_LAN_MarkServerVisible(ui_netSource.integer, -1, qtrue);
    trap_LAN_ResetPings(ui_netSource.integer);

    if (ui_netSource.integer == AS_LOCAL) {
        trap_Cmd_ExecuteText(EXEC_APPEND, "localservers\n");
        uiInfo.serverStatus.refreshtime = uiInfo.uiDC.realTime + 1000;
        return;
    }

    uiInfo.serverStatus.refreshtime = uiInfo.uiDC.realTime + 5000;
    if (ui_netSource.integer == AS_GLOBAL || ui_netSource.integer == AS_MPLAYER) {
        i = (ui_netSource.integer == AS_GLOBAL) ? 0 : 1;
        ptr = UI_Cvar_VariableString("debug_protocol");
        if (strlen(ptr)) {
            trap_Cmd_ExecuteText(EXEC_APPEND, va("globalservers %d %s full empty\n", i, ptr));
        } else {
            trap_Cmd_ExecuteText(EXEC_APPEND, va("globalservers %d %d full empty\n", i,
                                 (int)trap_Cvar_VariableValue("protocol")));
        }
    }
}

/*
==================
UI_PlayerModelExists

[QL] uix86.dll @0x1000d490. A parsed character is real only when its skin file
models/players/<base>/lower_<skin>.skin exists on disk. QL passes a NULL file
handle to trap_FS_FOpenFile, so the engine reports the file length without
opening it; a nonzero length means the skin is present. index >= MAX_HEADS
returns qfalse. (base = model dir, name = skin, per Character_Parse.)
==================
*/
static qboolean UI_PlayerModelExists(int index) {
    char path[1024];

    if (index < 0 || index >= MAX_HEADS) {
        return qfalse;
    }

    Com_sprintf(path, sizeof(path), "models/players/%s/lower_%s.skin",
                uiInfo.characterList[index].base, uiInfo.characterList[index].name);

    return (trap_FS_FOpenFile(path, NULL, FS_READ) != 0) ? qtrue : qfalse;
}

/*
==================
UI_BuildPlayerModelList

[QL] uix86.dll @0x1000d530. Walks the single parsed character list and marks
each model .active once its skin file is confirmed on disk. The flag is cached:
a model already active is not re-validated. Returns the active count. Both the
HEADS and Q3HEADS feeders draw from this one list (UI_FeederCount @0x1000e470
passes 0 for FEEDER_HEADS, 1 for FEEDER_Q3HEADS). When excludeSkins is 1 the
six team/sport skins are skipped so they never appear in the Q3 model picker.
==================
*/
static int UI_BuildPlayerModelList(int excludeSkins) {
    int i;
    int count;
    const char* skin;

    count = 0;
    for (i = 0; i < uiInfo.characterCount; i++) {
        if (excludeSkins == 1) {
            skin = uiInfo.characterList[i].name;
            if (skin && (Q_stricmp(skin, "blue") == 0 ||
                         Q_stricmp(skin, "bright") == 0 ||
                         Q_stricmp(skin, "red") == 0 ||
                         Q_stricmp(skin, "sport") == 0 ||
                         Q_stricmp(skin, "sport_blue") == 0 ||
                         Q_stricmp(skin, "sport_red") == 0)) {
                continue;
            }
        }

        if (!uiInfo.characterList[i].active) {
            if (!UI_PlayerModelExists(i)) {
                continue;
            }
            uiInfo.characterList[i].active = qtrue;
        }
        count++;
    }
    return count;
}

/*
==================
UI_FeederCount
==================
*/
static int UI_FeederCount(float feederID) {
    // [QL] uix86.dll UI_FeederCount @0x1000e470: FEEDER_HEADS and FEEDER_Q3HEADS
    // share one disk-validated player-model list; HEADS keeps every valid skin,
    // Q3HEADS drops the team/sport skins (arg 1).
    if (feederID == FEEDER_HEADS) {
        return UI_BuildPlayerModelList(0);
    } else if (feederID == FEEDER_Q3HEADS) {
        return UI_BuildPlayerModelList(1);
    } else if (feederID == FEEDER_CINEMATICS) {
        return uiInfo.movieCount;
    } else if (feederID == FEEDER_MAPS || feederID == FEEDER_ALLMAPS) {
        return UI_MapCountByGameType(feederID == FEEDER_MAPS ? qtrue : qfalse);
    } else if (feederID == FEEDER_CVMAPS) {
        return UI_MapCountByCallvoteGameType();
    } else if (feederID == FEEDER_FINDPLAYER) {
        return uiInfo.numFoundPlayerServers;
    } else if (feederID == FEEDER_PLAYER_LIST) {
        if (uiInfo.uiDC.realTime > uiInfo.playerRefresh) {
            uiInfo.playerRefresh = uiInfo.uiDC.realTime + 3000;
            UI_BuildPlayerList();
        }
        return uiInfo.playerCount;
    } else if (feederID == FEEDER_TEAM_LIST) {
        if (uiInfo.uiDC.realTime > uiInfo.playerRefresh) {
            uiInfo.playerRefresh = uiInfo.uiDC.realTime + 3000;
            UI_BuildPlayerList();
        }
        return uiInfo.myTeamCount;
    } else if (feederID == FEEDER_MODS) {
        return uiInfo.modCount;
    } else if (feederID == FEEDER_DEMOS) {
        return uiInfo.demoCount;
    } else if (feederID == FEEDER_SERVERS) {
        return uiInfo.serverStatus.numDisplayServers;
    }
    return 0;
}

static const char* UI_SelectedMap(int index, int* actual) {
    int i, c;
    c = 0;
    *actual = 0;
    for (i = 0; i < uiInfo.mapCount; i++) {
        if (uiInfo.mapList[i].active) {
            if (c == index) {
                *actual = i;
                return uiInfo.mapList[i].mapName;
            } else {
                c++;
            }
        }
    }
    return "";
}

static const char* UI_SelectedHead(int index, int* actual) {
    int i, c;
    c = 0;
    *actual = 0;
    for (i = 0; i < uiInfo.characterCount; i++) {
        if (uiInfo.characterList[i].active) {
            if (c == index) {
                *actual = i;
                return uiInfo.characterList[i].name;
            } else {
                c++;
            }
        }
    }
    return "";
}

static const char* UI_FeederItemText(float feederID, int index, int column, qhandle_t* handle) {
    static char info[MAX_STRING_CHARS];
    static char hostname[1024];
    static char clientBuff[32];
    static int lastColumn = -1;
    static int lastTime = 0;
    // [QL] browser "Game" column names, indexed by the "gametype" info value
    // (verified from uix86.dll @ 0x1002ae48).
    static const char *browserGameTypes[8] = {
        "FFA", "TOURNAMENT", "SP", "TEAM DM", "CTF", "1FCTF", "OVERLOAD", "HARVESTER"
    };
    *handle = -1;
    if (feederID == FEEDER_HEADS || feederID == FEEDER_Q3HEADS) {
        // [QL] uix86.dll UI_FeederItemText @0x1000e640: both head feeders walk
        // the one active player-model list and return the skin name.
        int actual;
        return UI_SelectedHead(index, &actual);
    } else if (feederID == FEEDER_MAPS || feederID == FEEDER_ALLMAPS || feederID == FEEDER_CVMAPS) {
        int actual;
        return UI_SelectedMap(index, &actual);
    } else if (feederID == FEEDER_FINDPLAYER) {
        if (index >= 0 && index < uiInfo.numFoundPlayerServers) {
            return uiInfo.foundPlayerServerNames[index];
        }
    } else if (feederID == FEEDER_PLAYER_LIST) {
        if (index >= 0 && index < uiInfo.playerCount) {
            return uiInfo.playerNames[index];
        }
    } else if (feederID == FEEDER_TEAM_LIST) {
        if (index >= 0 && index < uiInfo.myTeamCount) {
            return uiInfo.teamNames[index];
        }
    } else if (feederID == FEEDER_MODS) {
        if (index >= 0 && index < uiInfo.modCount) {
            if (uiInfo.modList[index].modDescr && *uiInfo.modList[index].modDescr) {
                return uiInfo.modList[index].modDescr;
            } else {
                return uiInfo.modList[index].modName;
            }
        }
    } else if (feederID == FEEDER_CINEMATICS) {
        if (index >= 0 && index < uiInfo.movieCount) {
            return uiInfo.movieList[index];
        }
    } else if (feederID == FEEDER_DEMOS) {
        if (index >= 0 && index < uiInfo.demoCount) {
            return uiInfo.demoList[index];
        }
    } else if (feederID == FEEDER_SERVERS) {
        if (index >= 0 && index < uiInfo.serverStatus.numDisplayServers) {
            int ping, game;
            if (lastColumn != column || lastTime > uiInfo.uiDC.realTime + 5000) {
                trap_LAN_GetServerInfo(ui_netSource.integer,
                    uiInfo.serverStatus.displayServers[index], info, sizeof(info));
                lastColumn = column;
                lastTime = uiInfo.uiDC.realTime;
            }
            ping = atoi(Info_ValueForKey(info, "ping"));
            switch (column) {
            case SORT_HOST:
                if (ping <= 0) {
                    return Info_ValueForKey(info, "addr");
                }
                if (ui_netSource.integer == AS_LOCAL) {
                    Com_sprintf(hostname, sizeof(hostname), "%s [%s]",
                                Info_ValueForKey(info, "hostname"),
                                netnames[atoi(Info_ValueForKey(info, "nettype"))]);
                    return hostname;
                }
                Com_sprintf(hostname, sizeof(hostname), "%s", Info_ValueForKey(info, "hostname"));
                return hostname;
            case SORT_MAP:
                return Info_ValueForKey(info, "mapname");
            case SORT_CLIENTS:
                Com_sprintf(clientBuff, sizeof(clientBuff), "%s (%s)",
                            Info_ValueForKey(info, "clients"),
                            Info_ValueForKey(info, "sv_maxclients"));
                return clientBuff;
            case SORT_GAME:
                game = atoi(Info_ValueForKey(info, "gametype"));
                if (game >= 0 && game < 8) {
                    return browserGameTypes[game];
                }
                return "Unknown";
            case SORT_PING:
                if (ping <= 0) {
                    return "...";  // [QL] deviation: stock missionpack uses "--"
                }
                return Info_ValueForKey(info, "ping");
            }
        }
    } else if (feederID == FEEDER_SERVERSTATUS) {
        if (index >= 0 && index < uiInfo.serverStatusInfo.numLines
            && column >= 0 && column < 4) {
            return uiInfo.serverStatusInfo.lines[index][column];
        }
    }
    return "";
}

static qhandle_t UI_FeederItemImage(float feederID, int index) {
    if (feederID == FEEDER_HEADS || feederID == FEEDER_Q3HEADS) {
        // [QL] both head feeders index the one player-model list; the head icon
        // is lazily registered from characterList[].imageName.
        int actual;
        UI_SelectedHead(index, &actual);
        index = actual;
        if (index >= 0 && index < uiInfo.characterCount) {
            if (uiInfo.characterList[index].headImage == -1) {
                uiInfo.characterList[index].headImage = trap_R_RegisterShaderNoMip(uiInfo.characterList[index].imageName);
            }
            return uiInfo.characterList[index].headImage;
        }
    } else if (feederID == FEEDER_ALLMAPS || feederID == FEEDER_MAPS || feederID == FEEDER_CVMAPS) {
        int actual;
        UI_SelectedMap(index, &actual);
        index = actual;
        if (index >= 0 && index < uiInfo.mapCount) {
            if (uiInfo.mapList[index].levelShot == -1) {
                uiInfo.mapList[index].levelShot = trap_R_RegisterShaderNoMip(uiInfo.mapList[index].imageName);
            }
            return uiInfo.mapList[index].levelShot;
        }
    }
    return 0;
}

static void UI_FeederSelection(float feederID, int index) {
    static char info[MAX_STRING_CHARS];
    if (feederID == FEEDER_HEADS || feederID == FEEDER_Q3HEADS) {
        int actual;
        UI_SelectedHead(index, &actual);
        index = actual;
        if (index >= 0 && index < uiInfo.characterCount) {
            // [QL] uix86.dll UI_FeederSelection @0x1000eba0: both head feeders
            // index the one player-model list. The "default" skin selects the
            // bare model name, any other skin uses "<model>/<skin>" so
            // UI_PlayerInfo_SetModel loads the correct skin variant. (base =
            // model, name = skin per Character_Parse.)
            if (Q_stricmp(uiInfo.characterList[index].name, "default") == 0) {
                trap_Cvar_Set("model", uiInfo.characterList[index].base);
                trap_Cvar_Set("headmodel", uiInfo.characterList[index].base);
            } else {
                trap_Cvar_Set("model", va("%s/%s", uiInfo.characterList[index].base, uiInfo.characterList[index].name));
                trap_Cvar_Set("headmodel", va("%s/%s", uiInfo.characterList[index].base, uiInfo.characterList[index].name));
            }
            updateModel = qtrue;
        }
    } else if (feederID == FEEDER_MAPS || feederID == FEEDER_ALLMAPS || feederID == FEEDER_CVMAPS) {
        int actual, map;
        map = (feederID == FEEDER_ALLMAPS || feederID == FEEDER_CVMAPS) ? ui_currentNetMap.integer : ui_currentMap.integer;
        if (uiInfo.mapList[map].cinematic >= 0) {
            trap_CIN_StopCinematic(uiInfo.mapList[map].cinematic);
            uiInfo.mapList[map].cinematic = -1;
        }
        UI_SelectedMap(index, &actual);
        trap_Cvar_Set("ui_mapIndex", va("%d", index));
        ui_mapIndex.integer = index;

        if (feederID == FEEDER_MAPS) {
            ui_currentMap.integer = actual;
            trap_Cvar_Set("ui_currentMap", va("%d", actual));
            uiInfo.mapList[ui_currentMap.integer].cinematic = trap_CIN_PlayCinematic(va("%s.roq", uiInfo.mapList[ui_currentMap.integer].mapLoadName), 0, 0, 0, 0, (CIN_loop | CIN_silent));
            trap_Cvar_Set("ui_opponentModel", uiInfo.mapList[ui_currentMap.integer].opponentName);
            updateOpponentModel = qtrue;
        } else {
            ui_currentNetMap.integer = actual;
            trap_Cvar_Set("ui_currentNetMap", va("%d", actual));
            uiInfo.mapList[ui_currentNetMap.integer].cinematic = trap_CIN_PlayCinematic(va("%s.roq", uiInfo.mapList[ui_currentNetMap.integer].mapLoadName), 0, 0, 0, 0, (CIN_loop | CIN_silent));
        }
    } else if (feederID == FEEDER_SERVERS) {
        const char* mapName;
        uiInfo.serverStatus.currentServer = index;
        trap_LAN_GetServerInfo(ui_netSource.integer,
            uiInfo.serverStatus.displayServers[index], info, sizeof(info));
        uiInfo.serverStatus.currentServerPreview = trap_R_RegisterShaderNoMip(
            va("levelshots/%s", Info_ValueForKey(info, "mapname")));
        if (uiInfo.serverStatus.currentServerCinematic >= 0) {
            trap_CIN_StopCinematic(uiInfo.serverStatus.currentServerCinematic);
            uiInfo.serverStatus.currentServerCinematic = -1;
        }
        mapName = Info_ValueForKey(info, "mapname");
        if (mapName && *mapName) {
            uiInfo.serverStatus.currentServerCinematic = trap_CIN_PlayCinematic(
                va("%s.roq", mapName), 0, 0, 0, 0, (CIN_loop | CIN_silent));
        }
    } else if (feederID == FEEDER_SERVERSTATUS) {
        //
    } else if (feederID == FEEDER_FINDPLAYER) {
        uiInfo.currentFoundPlayerServer = index;
        //
        if (index < uiInfo.numFoundPlayerServers - 1) {
            // build a new server status for this server
            Q_strncpyz(uiInfo.serverStatusAddress, uiInfo.foundPlayerServerAddresses[uiInfo.currentFoundPlayerServer], sizeof(uiInfo.serverStatusAddress));
            Menu_SetFeederSelection(NULL, FEEDER_SERVERSTATUS, 0, NULL);
        }
    } else if (feederID == FEEDER_PLAYER_LIST) {
        uiInfo.playerIndex = index;
    } else if (feederID == FEEDER_TEAM_LIST) {
        uiInfo.teamIndex = index;
    } else if (feederID == FEEDER_MODS) {
        uiInfo.modIndex = index;
    } else if (feederID == FEEDER_CINEMATICS) {
        uiInfo.movieIndex = index;
        if (uiInfo.previewMovie >= 0) {
            trap_CIN_StopCinematic(uiInfo.previewMovie);
        }
        uiInfo.previewMovie = -1;
    } else if (feederID == FEEDER_DEMOS) {
        uiInfo.demoIndex = index;
    }
}

static qboolean Team_Parse(char** p) {
    char* token;
    const char* tempStr;
    int i;

    token = COM_ParseExt(p, qtrue);

    if (token[0] != '{') {
        return qfalse;
    }

    while (1) {
        token = COM_ParseExt(p, qtrue);

        if (Q_stricmp(token, "}") == 0) {
            return qtrue;
        }

        if (!token[0]) {
            return qfalse;
        }

        if (token[0] == '{') {
            if (uiInfo.teamCount == MAX_TEAMS) {
                uiInfo.teamCount--;
                Com_Printf("Too many teams, last team replaced!\n");
            }

            // seven tokens per line, team name and icon, and 5 team member names
            if (!String_Parse(p, &uiInfo.teamList[uiInfo.teamCount].teamName) || !String_Parse(p, &tempStr)) {
                return qfalse;
            }

            uiInfo.teamList[uiInfo.teamCount].imageName = tempStr;
            uiInfo.teamList[uiInfo.teamCount].teamIcon = trap_R_RegisterShaderNoMip(uiInfo.teamList[uiInfo.teamCount].imageName);
            uiInfo.teamList[uiInfo.teamCount].teamIcon_Metal = trap_R_RegisterShaderNoMip(va("%s_metal", uiInfo.teamList[uiInfo.teamCount].imageName));
            uiInfo.teamList[uiInfo.teamCount].teamIcon_Name = trap_R_RegisterShaderNoMip(va("%s_name", uiInfo.teamList[uiInfo.teamCount].imageName));

            uiInfo.teamList[uiInfo.teamCount].cinematic = -1;

            for (i = 0; i < TEAM_MEMBERS; i++) {
                uiInfo.teamList[uiInfo.teamCount].teamMembers[i] = NULL;
                if (!String_Parse(p, &uiInfo.teamList[uiInfo.teamCount].teamMembers[i])) {
                    return qfalse;
                }
            }

            Com_Printf("Loaded team %s with team icon %s.\n", uiInfo.teamList[uiInfo.teamCount].teamName, tempStr);
            uiInfo.teamCount++;

            token = COM_ParseExt(p, qtrue);
            if (token[0] != '}') {
                return qfalse;
            }
        }
    }

    return qfalse;
}

static qboolean Character_Parse(char** p) {
    char* token;

    token = COM_ParseExt(p, qtrue);

    if (token[0] != '{') {
        return qfalse;
    }

    while (1) {
        token = COM_ParseExt(p, qtrue);

        if (Q_stricmp(token, "}") == 0) {
            return qtrue;
        }

        if (!token[0]) {
            return qfalse;
        }

        if (token[0] == '{') {
            if (uiInfo.characterCount == MAX_HEADS) {
                uiInfo.characterCount--;
                Com_Printf("Too many characters, last character replaced!\n");
            }

            // [QL] Each entry in ui/teaminfo.txt's "characters" block is a
            // { "model" "skin" } pair (e.g. { "anarki" "sport_red" }). uix86.dll
            // UI_ParseGameInfo @0x1000f140 stores the model in "base", the skin
            // in "name", and builds the icon as
            // models/players/<model>/icon_<skin>.tga. (Q3's { name sex } form is
            // gone; the "James"/"Janet" fallbacks do not apply to QL assets.)
            if (!String_Parse(p, &uiInfo.characterList[uiInfo.characterCount].base) || !String_Parse(p, &uiInfo.characterList[uiInfo.characterCount].name)) {
                return qfalse;
            }

            uiInfo.characterList[uiInfo.characterCount].headImage = -1;
            uiInfo.characterList[uiInfo.characterCount].imageName = String_Alloc(va("models/players/%s/icon_%s.tga", uiInfo.characterList[uiInfo.characterCount].base, uiInfo.characterList[uiInfo.characterCount].name));

            Com_Printf("Loaded character model %s skin %s.\n", uiInfo.characterList[uiInfo.characterCount].base, uiInfo.characterList[uiInfo.characterCount].name);
            uiInfo.characterCount++;

            token = COM_ParseExt(p, qtrue);
            if (token[0] != '}') {
                return qfalse;
            }
        }
    }

    return qfalse;
}

static qboolean Alias_Parse(char** p) {
    char* token;

    token = COM_ParseExt(p, qtrue);

    if (token[0] != '{') {
        return qfalse;
    }

    while (1) {
        token = COM_ParseExt(p, qtrue);

        if (Q_stricmp(token, "}") == 0) {
            return qtrue;
        }

        if (!token[0]) {
            return qfalse;
        }

        if (token[0] == '{') {
            if (uiInfo.aliasCount == MAX_ALIASES) {
                uiInfo.aliasCount--;
                Com_Printf("Too many aliases, last alias replaced!\n");
            }

            // three tokens per line, character name, bot alias, and preferred action a - all purpose, d - defense, o - offense
            if (!String_Parse(p, &uiInfo.aliasList[uiInfo.aliasCount].name) || !String_Parse(p, &uiInfo.aliasList[uiInfo.aliasCount].ai) || !String_Parse(p, &uiInfo.aliasList[uiInfo.aliasCount].action)) {
                return qfalse;
            }

            Com_Printf("Loaded character alias %s using character ai %s.\n", uiInfo.aliasList[uiInfo.aliasCount].name, uiInfo.aliasList[uiInfo.aliasCount].ai);
            uiInfo.aliasCount++;

            token = COM_ParseExt(p, qtrue);
            if (token[0] != '}') {
                return qfalse;
            }
        }
    }

    return qfalse;
}

// mode
// 0 - high level parsing
// 1 - team parsing
// 2 - character parsing
static void UI_ParseTeamInfo(const char* teamFile) {
    char* token;
    char* p;
    char* buff = NULL;
    // static int mode = 0; TTimo: unused

    buff = GetMenuBuffer(teamFile);
    if (!buff) {
        return;
    }

    p = buff;

    while (1) {
        token = COM_ParseExt(&p, qtrue);
        if (!token[0] || token[0] == '}') {
            break;
        }

        if (Q_stricmp(token, "}") == 0) {
            break;
        }

        if (Q_stricmp(token, "teams") == 0) {
            if (Team_Parse(&p)) {
                continue;
            } else {
                break;
            }
        }

        if (Q_stricmp(token, "characters") == 0) {
            Character_Parse(&p);
        }

        if (Q_stricmp(token, "aliases") == 0) {
            Alias_Parse(&p);
        }
    }
}

static qboolean GameType_Parse(char** p, qboolean join) {
    char* token;

    token = COM_ParseExt(p, qtrue);

    if (token[0] != '{') {
        return qfalse;
    }

    if (join) {
        uiInfo.numJoinGameTypes = 0;
    } else {
        uiInfo.numGameTypes = 0;
    }

    while (1) {
        token = COM_ParseExt(p, qtrue);

        if (Q_stricmp(token, "}") == 0) {
            return qtrue;
        }

        if (!token[0]) {
            return qfalse;
        }

        if (token[0] == '{') {
            // two tokens per line, gametype name and number
            if (join) {
                if (!String_Parse(p, &uiInfo.joinGameTypes[uiInfo.numJoinGameTypes].gameType) || !Int_Parse(p, &uiInfo.joinGameTypes[uiInfo.numJoinGameTypes].gtEnum)) {
                    return qfalse;
                }
            } else {
                if (!String_Parse(p, &uiInfo.gameTypes[uiInfo.numGameTypes].gameType) || !Int_Parse(p, &uiInfo.gameTypes[uiInfo.numGameTypes].gtEnum)) {
                    return qfalse;
                }
            }

            if (join) {
                if (uiInfo.numJoinGameTypes < MAX_GAMETYPES) {
                    uiInfo.numJoinGameTypes++;
                } else {
                    Com_Printf("Too many net game types, last one replace!\n");
                }
            } else {
                if (uiInfo.numGameTypes < MAX_GAMETYPES) {
                    uiInfo.numGameTypes++;
                } else {
                    Com_Printf("Too many game types, last one replace!\n");
                }
            }

            token = COM_ParseExt(p, qtrue);
            if (token[0] != '}') {
                return qfalse;
            }
        }
    }
    return qfalse;
}

static qboolean MapList_Parse(char** p) {
    char* token;

    token = COM_ParseExt(p, qtrue);

    if (token[0] != '{') {
        return qfalse;
    }

    uiInfo.mapCount = 0;

    while (1) {
        token = COM_ParseExt(p, qtrue);

        if (Q_stricmp(token, "}") == 0) {
            return qtrue;
        }

        if (!token[0]) {
            return qfalse;
        }

        if (token[0] == '{') {
            if (!String_Parse(p, &uiInfo.mapList[uiInfo.mapCount].mapName) || !String_Parse(p, &uiInfo.mapList[uiInfo.mapCount].mapLoadName) || !Int_Parse(p, &uiInfo.mapList[uiInfo.mapCount].teamMembers)) {
                return qfalse;
            }

            if (!String_Parse(p, &uiInfo.mapList[uiInfo.mapCount].opponentName)) {
                return qfalse;
            }

            uiInfo.mapList[uiInfo.mapCount].typeBits = 0;

            while (1) {
                token = COM_ParseExt(p, qtrue);
                if (token[0] >= '0' && token[0] <= '9') {
                    uiInfo.mapList[uiInfo.mapCount].typeBits |= (1 << (token[0] - 0x030));
                    if (!Int_Parse(p, &uiInfo.mapList[uiInfo.mapCount].timeToBeat[token[0] - 0x30])) {
                        return qfalse;
                    }
                } else {
                    break;
                }
            }

            uiInfo.mapList[uiInfo.mapCount].cinematic = -1;
            uiInfo.mapList[uiInfo.mapCount].levelShot = trap_R_RegisterShaderNoMip(va("levelshots/preview/%s", uiInfo.mapList[uiInfo.mapCount].mapLoadName));

            if (uiInfo.mapCount < MAX_MAPS) {
                uiInfo.mapCount++;
            } else {
                Com_Printf("Too many maps, last one replaced!\n");
            }
        }
    }
    return qfalse;
}

static void UI_ParseGameInfo(const char* teamFile) {
    char* token;
    char* p;
    char* buff = NULL;
    // int mode = 0; TTimo: unused

    buff = GetMenuBuffer(teamFile);
    if (!buff) {
        return;
    }

    p = buff;

    while (1) {
        token = COM_ParseExt(&p, qtrue);
        if (!token[0] || token[0] == '}') {
            break;
        }

        if (Q_stricmp(token, "}") == 0) {
            break;
        }

        if (Q_stricmp(token, "gametypes") == 0) {
            if (GameType_Parse(&p, qfalse)) {
                continue;
            } else {
                break;
            }
        }

        if (Q_stricmp(token, "joingametypes") == 0) {
            if (GameType_Parse(&p, qtrue)) {
                continue;
            } else {
                break;
            }
        }

        if (Q_stricmp(token, "maps") == 0) {
            // start a new menu
            MapList_Parse(&p);
        }
    }
}

static void UI_Pause(qboolean b) {
    if (b) {
        // pause the game and set the ui keycatcher
        trap_Cvar_Set("cl_paused", "1");
        trap_Key_SetCatcher(KEYCATCH_UI);
    } else {
        // unpause the game and clear the ui keycatcher
        trap_Key_SetCatcher(trap_Key_GetCatcher() & ~KEYCATCH_UI);
        trap_Key_ClearStates();
        trap_Cvar_Set("cl_paused", "0");
    }
}

static int UI_PlayCinematic(const char* name, float x, float y, float w, float h) {
    return trap_CIN_PlayCinematic(name, x, y, w, h, (CIN_loop | CIN_silent));
}

static void UI_StopCinematic(int handle) {
    if (handle >= 0) {
        trap_CIN_StopCinematic(handle);
    } else {
        handle = abs(handle);
        if (handle == UI_MAPCINEMATIC) {
            if (uiInfo.mapList[ui_currentMap.integer].cinematic >= 0) {
                trap_CIN_StopCinematic(uiInfo.mapList[ui_currentMap.integer].cinematic);
                uiInfo.mapList[ui_currentMap.integer].cinematic = -1;
            }
        } else if (handle == UI_NETMAPCINEMATIC) {
            if (uiInfo.serverStatus.currentServerCinematic >= 0) {
                trap_CIN_StopCinematic(uiInfo.serverStatus.currentServerCinematic);
                uiInfo.serverStatus.currentServerCinematic = -1;
            }
        } else if (handle == UI_CLANCINEMATIC) {
            int i = UI_TeamIndexFromName(UI_Cvar_VariableString("ui_teamName"));
            if (i >= 0 && i < uiInfo.teamCount) {
                if (uiInfo.teamList[i].cinematic >= 0) {
                    trap_CIN_StopCinematic(uiInfo.teamList[i].cinematic);
                    uiInfo.teamList[i].cinematic = -1;
                }
            }
        }
    }
}

static void UI_DrawCinematic(int handle, float x, float y, float w, float h) {
    // adjust coords to get correct placement in wide screen
    UI_AdjustFrom640(&x, &y, &w, &h);

    // CIN_SetExtents takes stretched 640x480 virtualized coords
    x *= SCREEN_WIDTH / (float)uiInfo.uiDC.glconfig.vidWidth;
    w *= SCREEN_WIDTH / (float)uiInfo.uiDC.glconfig.vidWidth;
    y *= SCREEN_HEIGHT / (float)uiInfo.uiDC.glconfig.vidHeight;
    h *= SCREEN_HEIGHT / (float)uiInfo.uiDC.glconfig.vidHeight;

    trap_CIN_SetExtents(handle, x, y, w, h);
    trap_CIN_DrawCinematic(handle);
}

static void UI_RunCinematicFrame(int handle) {
    trap_CIN_RunCinematic(handle);
}

/*
=================
PlayerModel_BuildList
=================
*/
static void UI_BuildQ3Model_List(void) {
    int numdirs;
    int numfiles;
    char dirlist[2048];
    char filelist[2048];
    char skinname[MAX_QPATH];
    char scratch[256];
    char* dirptr;
    char* fileptr;
    int i;
    int j, k, dirty;
    int dirlen;
    int filelen;

    uiInfo.q3HeadCount = 0;

    // iterate directory of all player models
    numdirs = trap_FS_GetFileList("models/players", "/", dirlist, 2048);
    dirptr = dirlist;
    for (i = 0; i < numdirs && uiInfo.q3HeadCount < MAX_PLAYERMODELS; i++, dirptr += dirlen + 1) {
        dirlen = strlen(dirptr);

        if (dirlen && dirptr[dirlen - 1] == '/')
            dirptr[dirlen - 1] = '\0';

        if (!strcmp(dirptr, ".") || !strcmp(dirptr, ".."))
            continue;

        // iterate all skin files in directory
        numfiles = trap_FS_GetFileList(va("models/players/%s", dirptr), "tga", filelist, 2048);
        fileptr = filelist;
        for (j = 0; j < numfiles && uiInfo.q3HeadCount < MAX_PLAYERMODELS; j++, fileptr += filelen + 1) {
            filelen = strlen(fileptr);

            COM_StripExtension(fileptr, skinname, sizeof(skinname));

            // look for icon_????
            if (Q_stricmpn(skinname, "icon_", 5) == 0 && !(Q_stricmp(skinname, "icon_blue") == 0 || Q_stricmp(skinname, "icon_red") == 0)) {
                if (Q_stricmp(skinname, "icon_default") == 0) {
                    Com_sprintf(scratch, sizeof(scratch), "%s", dirptr);
                } else {
                    Com_sprintf(scratch, sizeof(scratch), "%s/%s", dirptr, skinname + 5);
                }
                dirty = 0;
                for (k = 0; k < uiInfo.q3HeadCount; k++) {
                    if (!Q_stricmp(scratch, uiInfo.q3HeadNames[uiInfo.q3HeadCount])) {
                        dirty = 1;
                        break;
                    }
                }
                if (!dirty) {
                    Com_sprintf(uiInfo.q3HeadNames[uiInfo.q3HeadCount], sizeof(uiInfo.q3HeadNames[uiInfo.q3HeadCount]), "%s", scratch);
                    uiInfo.q3HeadIcons[uiInfo.q3HeadCount++] = trap_R_RegisterShaderNoMip(va("models/players/%s/%s", dirptr, skinname));
                }
            }
        }
    }
}

/*
=================
UI_Init
=================
*/
void _UI_Init(qboolean inGameLoad) {
    const char* menuSet;

    UI_RegisterCvars();
    UI_InitMemory();

    // [QL] Publish which iobin.pk3 this is. The pak01 stamp in the main menu is
    // baked into the menu text at package time and so only identifies the menus;
    // the game modules live in a separate pak and can be a different build
    // entirely - which is exactly the case that needs identifying when a fix
    // appears not to have landed. This comes from the module's own compile, so
    // it cannot disagree with the code that is running.
    trap_Cvar_Set("ui_iobinBuild", "iobin " PRODUCT_VERSION);

    // cache redundant calulations
    trap_GetGlconfig(&uiInfo.uiDC.glconfig);

    trap_Cvar_Set("ui_videomode", va("%dx%d", uiInfo.uiDC.glconfig.vidWidth, uiInfo.uiDC.glconfig.vidHeight));

    // for 640x480 virtualized screen
    uiInfo.uiDC.yscale = uiInfo.uiDC.glconfig.vidHeight * (1.0 / (float)SCREEN_HEIGHT);
    uiInfo.uiDC.xscale = uiInfo.uiDC.glconfig.vidWidth * (1.0 / (float)SCREEN_WIDTH);
    if (uiInfo.uiDC.glconfig.vidWidth * SCREEN_HEIGHT > uiInfo.uiDC.glconfig.vidHeight * SCREEN_WIDTH) {
        // wide screen
        uiInfo.uiDC.bias = 0.5 * (uiInfo.uiDC.glconfig.vidWidth - (uiInfo.uiDC.glconfig.vidHeight * ((float)SCREEN_WIDTH / (float)SCREEN_HEIGHT)));
        uiInfo.uiDC.xscale = uiInfo.uiDC.yscale;
    } else {
        // no wide screen
        uiInfo.uiDC.bias = 0;
    }

    uiInfo.uiDC.registerShaderNoMip = &trap_R_RegisterShaderNoMip;
    uiInfo.uiDC.setColor = &UI_SetColor;
    uiInfo.uiDC.drawHandlePic = &UI_DrawHandlePic;
    uiInfo.uiDC.drawStretchPic = &trap_R_DrawStretchPic;
    uiInfo.uiDC.drawText = &UI_DrawText_DC;
    uiInfo.uiDC.textWidth = &UI_TextWidth_DC;
    uiInfo.uiDC.textHeight = &UI_TextHeight_DC;
    uiInfo.uiDC.registerModel = &trap_R_RegisterModel;
    uiInfo.uiDC.modelBounds = &trap_R_ModelBounds;
    uiInfo.uiDC.fillRect = &UI_FillRect;
    uiInfo.uiDC.drawRect = &_UI_DrawRect;
    uiInfo.uiDC.drawSides = &_UI_DrawSides;
    uiInfo.uiDC.drawTopBottom = &_UI_DrawTopBottom;
    uiInfo.uiDC.clearScene = &trap_R_ClearScene;
    uiInfo.uiDC.addRefEntityToScene = &trap_R_AddRefEntityToScene;
    uiInfo.uiDC.renderScene = &trap_R_RenderScene;
    uiInfo.uiDC.registerFont = &trap_R_RegisterFont;
    uiInfo.uiDC.ownerDrawItem = &UI_OwnerDraw_DC;
    uiInfo.uiDC.getValue = &UI_GetValue;
    uiInfo.uiDC.ownerDrawVisible = &UI_OwnerDrawVisible;
    uiInfo.uiDC.runScript = &UI_RunMenuScript;
    uiInfo.uiDC.getTeamColor = &UI_GetTeamColor;
    uiInfo.uiDC.setCVar = trap_Cvar_Set;
    uiInfo.uiDC.getCVarString = trap_Cvar_VariableStringBuffer;
    uiInfo.uiDC.getCVarValue = trap_Cvar_VariableValue;
    uiInfo.uiDC.drawTextWithCursor = &UI_DrawTextWithCursor_DC;
    uiInfo.uiDC.setOverstrikeMode = &trap_Key_SetOverstrikeMode;
    uiInfo.uiDC.getOverstrikeMode = &trap_Key_GetOverstrikeMode;
    uiInfo.uiDC.startLocalSound = &trap_S_StartLocalSound;
    uiInfo.uiDC.ownerDrawHandleKey = &UI_OwnerDrawHandleKey;
    uiInfo.uiDC.feederCount = &UI_FeederCount;
    uiInfo.uiDC.feederItemImage = &UI_FeederItemImage;
    uiInfo.uiDC.feederItemText = &UI_FeederItemText;
    uiInfo.uiDC.feederSelection = &UI_FeederSelection;
    uiInfo.uiDC.setBinding = &trap_Key_SetBinding;
    uiInfo.uiDC.getBindingBuf = &trap_Key_GetBindingBuf;
    uiInfo.uiDC.keynumToStringBuf = &trap_Key_KeynumToStringBuf;
    uiInfo.uiDC.executeText = &trap_Cmd_ExecuteText;
    uiInfo.uiDC.Error = &Com_Error;
    uiInfo.uiDC.Print = &Com_Printf;
    uiInfo.uiDC.Pause = &UI_Pause;
    uiInfo.uiDC.ownerDrawWidth = &UI_OwnerDrawWidth;
    uiInfo.uiDC.registerSound = &trap_S_RegisterSound;
    uiInfo.uiDC.startBackgroundTrack = &trap_S_StartBackgroundTrack;
    uiInfo.uiDC.stopBackgroundTrack = &trap_S_StopBackgroundTrack;
    uiInfo.uiDC.playCinematic = &UI_PlayCinematic;
    uiInfo.uiDC.stopCinematic = &UI_StopCinematic;
    uiInfo.uiDC.drawCinematic = &UI_DrawCinematic;
    uiInfo.uiDC.runCinematicFrame = &UI_RunCinematicFrame;

    Init_Display(&uiInfo.uiDC);

    String_Init();

    uiInfo.uiDC.cursor = trap_R_RegisterShaderNoMip("menu/art/3_cursor2");
    uiInfo.uiDC.whiteShader = trap_R_RegisterShaderNoMip("white");

    AssetCache();

    uiInfo.teamCount = 0;
    uiInfo.characterCount = 0;
    uiInfo.aliasCount = 0;

    UI_ParseTeamInfo("ui/teaminfo.txt");
    UI_LoadTeams();
    UI_LoadArenas();

    menuSet = UI_Cvar_VariableString("ui_menuFiles");
    if (menuSet == NULL || menuSet[0] == '\0') {
        menuSet = "ui/menus.txt";
    }

    UI_LoadMenus(menuSet, qtrue);
    UI_LoadMenus("ui/ingame.txt", qfalse);
    // [QL] ours, after Quake Live's - see ui/io_ingame.txt
    UI_LoadExtraMenus("ui/io_ingame.txt");

    Menus_CloseAll();

    UI_BuildQ3Model_List();
    UI_LoadBots();

    // sets defaults for ui temp cvars
    uiInfo.effectsColor = gamecodetoui[(int)trap_Cvar_VariableValue("color1") - 1];
    uiInfo.currentCrosshair = (int)trap_Cvar_VariableValue("cg_drawCrosshair") % NUM_CROSSHAIRS;
    if (uiInfo.currentCrosshair < 0) {
        uiInfo.currentCrosshair = 0;
    }
    trap_Cvar_Set("ui_mousePitch", (trap_Cvar_VariableValue("m_pitch") >= 0) ? "0" : "1");

    uiInfo.serverStatus.currentServerCinematic = -1;
    uiInfo.previewMovie = -1;

    trap_Cvar_Register(NULL, "debug_protocol", "", 0);

    trap_Cvar_Set("ui_actualNetGametype", va("%d", ui_netGameType.integer));
}

/*
=================
UI_KeyEvent
=================
*/
void _UI_KeyEvent(int key, qboolean down) {
    if (Menu_Count() > 0) {
        menuDef_t* menu = Menu_GetFocused();
        if (menu) {
            if (key == K_ESCAPE && down && !Menus_AnyFullScreenVisible()) {
                Menus_CloseAll();
            } else {
                Menu_HandleKey(menu, key, down);
            }
        } else {
            trap_Key_SetCatcher(trap_Key_GetCatcher() & ~KEYCATCH_UI);
            trap_Key_ClearStates();
            trap_Cvar_Set("cl_paused", "0");
        }
    }
}

/*
=================
UI_MouseEvent
=================
*/
void _UI_MouseEvent(int dx, int dy) {
    int bias;

    // convert X bias to 640 coords
    bias = uiInfo.uiDC.bias / uiInfo.uiDC.xscale;

    // update mouse screen position
    uiInfo.uiDC.cursorx += dx;
    if (uiInfo.uiDC.cursorx < -bias)
        uiInfo.uiDC.cursorx = -bias;
    else if (uiInfo.uiDC.cursorx > SCREEN_WIDTH + bias)
        uiInfo.uiDC.cursorx = SCREEN_WIDTH + bias;

    uiInfo.uiDC.cursory += dy;
    if (uiInfo.uiDC.cursory < 0)
        uiInfo.uiDC.cursory = 0;
    else if (uiInfo.uiDC.cursory > SCREEN_HEIGHT)
        uiInfo.uiDC.cursory = SCREEN_HEIGHT;

    if (Menu_Count() > 0) {
        Display_MouseMove(NULL, uiInfo.uiDC.cursorx, uiInfo.uiDC.cursory);
    }
}

void UI_LoadNonIngame(void) {
    const char* menuSet = UI_Cvar_VariableString("ui_menuFiles");
    if (menuSet == NULL || menuSet[0] == '\0') {
        menuSet = "ui/menus.txt";
    }
    UI_LoadMenus(menuSet, qfalse);
    uiInfo.inGameLoad = qfalse;
}

void _UI_SetActiveMenu(uiMenuCommand_t menu) {
    char buf[256];

    // this should be the ONLY way the menu system is brought up
    // ensure minimum menu data is cached
    if (Menu_Count() > 0) {
        vec3_t v;
        v[0] = v[1] = v[2] = 0;
        switch (menu) {
            case UIMENU_BAD_CD_KEY:
                // [Q3 remnant] the enum value is kept for numbering only; QL
                // has no CD key menu, so there is nothing to activate.
                return;
            case UIMENU_NONE:
                trap_Key_SetCatcher(trap_Key_GetCatcher() & ~KEYCATCH_UI);
                trap_Key_ClearStates();
                Menus_CloseAll();
                trap_Cvar_Set("cl_paused", "0");
                return;
            case UIMENU_MAIN:
                trap_Key_SetCatcher(KEYCATCH_UI);
                // [QL] We are out of a game, so say so. Quake Live's own menus
                // gate items on this: main_options' BACK button and demo.menu's
                // both carry cvarTest "ui_mainmenu" / showCvar { "1" }, because
                // in-game those panels are reached through the ingame nav and
                // must not offer a way back to the main menu.
                //
                // cgame sets it to "0" when a map loads (cg_main.c) and nothing
                // ever set it back, so after the first time you joined a game
                // the OPTIONS panel lost its BACK button for the rest of the
                // session - visibly gone, and unclickable where it should be,
                // because the item genuinely was not there. A fresh launch was
                // fine, which is what made this look like it came and went.
                trap_Cvar_Set("ui_mainmenu", "1");
                // [QL] Update UI state from cvars (binary-verified)
                uiInfo.effectsColor = gamecodetoui[(int)trap_Cvar_VariableValue("color1") - 1];
                uiInfo.currentCrosshair = (int)trap_Cvar_VariableValue("cg_drawCrosshair");
                if (uiInfo.inGameLoad) {
                    UI_LoadNonIngame();
                }
                Menus_CloseAll();
                Menus_ActivateByName("main");
                trap_Cvar_VariableStringBuffer("com_errorMessage", buf, sizeof(buf));
                // [QL] Play menu music
                trap_S_StartBackgroundTrack("music/fla_mp05", NULL);
                if (strlen(buf)) {
                    Menus_ActivateByName("error_popmenu");
                }
                return;
            case UIMENU_MAIN_OPTIONS:
                trap_Cvar_Set("cl_paused", "1");
                trap_Key_SetCatcher(KEYCATCH_UI);
                UI_BuildPlayerList();
                Menus_CloseAll();
                Menus_ActivateByName("main_options");
                return;
            case UIMENU_TEAM:
                trap_Key_SetCatcher(KEYCATCH_UI);
                // [QL] uix86.dll _UI_SetActiveMenu opens "joingame_menu", not "team"
                Menus_ActivateByName("joingame_menu");
                return;
            case UIMENU_POSTGAME:
                trap_Cvar_Set("sv_killserver", "1");
                trap_Key_SetCatcher(KEYCATCH_UI);
                if (uiInfo.inGameLoad) {
                    UI_LoadNonIngame();
                }
                Menus_CloseAll();
                Menus_ActivateByName("endofgame");
                return;
            case UIMENU_INGAME:
                // [QL] Update UI state from cvars (binary-verified)
                uiInfo.effectsColor = gamecodetoui[(int)trap_Cvar_VariableValue("color1") - 1];
                uiInfo.currentCrosshair = (int)trap_Cvar_VariableValue("cg_drawCrosshair");
                trap_Cvar_Set("ui_mousePitch", (trap_Cvar_VariableValue("m_pitch") >= 0) ? "0" : "1");
                trap_Cvar_Set("ui_cvGameType", "-1");
                trap_Cvar_Set("cl_paused", "1");
                trap_Key_SetCatcher(KEYCATCH_UI);
                UI_BuildPlayerList();
                Menus_CloseAll();
                /*
                [QL] The team buttons.

                Without these the in-game menu offers no way to change team at
                all - a player who does not know to type "cmd team r" into the
                console is stuck wherever they were put. Quake Live has them on
                this page; ours is a panel of our own (ui/io_teamselect.menu)
                because pak00 is not ours to edit.

                Activated *first*, not last, and the distinction matters. What it
                paints on top of is decided by load order - Menu_PaintAll walks
                the array, ours is loaded after Quake Live's ingame set, so it
                paints last either way. What activation order decides is which
                menu ends up with WINDOW_HASFOCUS, and that is the one that
                answers keyboard input including onESC. Leaving focus on the page
                itself keeps Escape and the tabs behaving exactly as before;
                clicks still reach our panel, because Display_CaptureItem picks
                the topmost visible menu under the cursor.
                */
                Menus_ActivateByName("io_teamselect");
                Menus_ActivateByName("ingame");
                Menus_ActivateByName("ingame_about");
                return;
        }
    }
}

qboolean _UI_IsFullscreen(void) {
    return Menus_AnyFullScreenVisible();
}

/*
=================
UI_CheckActiveMenu

[QL] vmMain export UI_CHECK_ACTIVE_MENU. Returns qtrue if ANY loaded menu is
currently visible. Verified from uix86.dll @ 0x100103c0: it tests window.flags
bit 0x4. QL's window flags match Q3/ioquakelive ordering (HASFOCUS=0x2,
VISIBLE=0x4), proven behaviourally because QL's Menus_ActivateByName clears
bit 0x2 from de-focused menus (i.e. WINDOW_HASFOCUS), and Menu_PostParse tests
bit 0x4 as the visible check. So bit 0x4 here is WINDOW_VISIBLE, NOT
WINDOW_HASFOCUS, and this is distinct from the fullscreen check the stock export
was aliased to (which additionally requires menu->fullScreen).
=================
*/
qboolean UI_CheckActiveMenu(void) {
    // The Menus[]/menuCount array is private to ui_shared.c; the visible-scan
    // lives there as Menus_AnyVisible() (sibling of Menus_AnyFullScreenVisible).
    return Menus_AnyVisible();
}

/*
=================
UI_WalkMenus

[QL] vmMain export UI_WALK_MENUS. The engine passes a callback function pointer
in arg0; the UI lazily loads the arena/map list, then invokes the callback once
per map with that map's load name. Verified from uix86.dll @ 0x10003930.
=================
*/
void UI_WalkMenus(void (*callback)(const char* mapLoadName)) {
    int i;

    if (uiInfo.mapCount < 1) {
        UI_LoadArenas();
    }

    for (i = 0; i < uiInfo.mapCount; i++) {
        if (uiInfo.mapList[i].mapLoadName != NULL) {
            callback(uiInfo.mapList[i].mapLoadName);
        }
    }
}

static connstate_t lastConnState;
static char lastLoadingText[MAX_INFO_VALUE];

static void UI_ReadableSize(char* buf, int bufsize, int value) {
    if (value > 1024 * 1024 * 1024) {  // gigs
        Com_sprintf(buf, bufsize, "%d", value / (1024 * 1024 * 1024));
        Com_sprintf(buf + strlen(buf), bufsize - strlen(buf), ".%02d GB",
                    (value % (1024 * 1024 * 1024)) * 100 / (1024 * 1024 * 1024));
    } else if (value > 1024 * 1024) {  // megs
        Com_sprintf(buf, bufsize, "%d", value / (1024 * 1024));
        Com_sprintf(buf + strlen(buf), bufsize - strlen(buf), ".%02d MB",
                    (value % (1024 * 1024)) * 100 / (1024 * 1024));
    } else if (value > 1024) {  // kilos
        Com_sprintf(buf, bufsize, "%d KB", value / 1024);
    } else {  // bytes
        Com_sprintf(buf, bufsize, "%d bytes", value);
    }
}

// Assumes time is in msec
static void UI_PrintTime(char* buf, int bufsize, int time) {
    time /= 1000;  // change to seconds

    if (time > 3600) {  // in the hours range
        Com_sprintf(buf, bufsize, "%d hr %d min", time / 3600, (time % 3600) / 60);
    } else if (time > 60) {  // mins
        Com_sprintf(buf, bufsize, "%d min %d sec", time / 60, time % 60);
    } else {  // secs
        Com_sprintf(buf, bufsize, "%d sec", time);
    }
}

void Text_PaintCenter(float x, float y, float scale, vec4_t color, const char* text, float adjust) {
    int len = Text_Width(text, scale, 0);
    Text_Paint(x - len / 2, y, scale, color, text, 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);
}

void Text_PaintCenter_AutoWrapped(float x, float y, float xmax, float ystep, float scale, vec4_t color, const char* str, float adjust) {
    int width;
    char *s1, *s2, *s3;
    char c_bcp;
    char buf[1024];

    if (!str || str[0] == '\0')
        return;

    Q_strncpyz(buf, str, sizeof(buf));
    s1 = s2 = s3 = buf;

    while (1) {
        do {
            s3++;
        } while (*s3 != ' ' && *s3 != '\0');
        c_bcp = *s3;
        *s3 = '\0';
        width = Text_Width(s1, scale, 0);
        *s3 = c_bcp;
        if (width > xmax) {
            if (s1 == s2) {
                // fuck, don't have a clean cut, we'll overflow
                s2 = s3;
            }
            *s2 = '\0';
            Text_PaintCenter(x, y, scale, color, s1, adjust);
            y += ystep;
            if (c_bcp == '\0') {
                // that was the last word
                // we could start a new loop, but that wouldn't be much use
                // even if the word is too long, we would overflow it (see above)
                // so just print it now if needed
                s2++;
                if (*s2 != '\0')  // if we are printing an overflowing line we have s2 == s3
                    Text_PaintCenter(x, y, scale, color, s2, adjust);
                break;
            }
            s2++;
            s1 = s2;
            s3 = s2;
        } else {
            s2 = s3;
            if (c_bcp == '\0')  // we reached the end
            {
                Text_PaintCenter(x, y, scale, color, s1, adjust);
                break;
            }
        }
    }
}

static void UI_DisplayDownloadInfo(const char* downloadName, float centerPoint, float yStart, float scale) {
    static char dlText[] = "Downloading:";
    static char etaText[] = "Estimated time left:";
    static char xferText[] = "Transfer rate:";

    int downloadSize, downloadCount, downloadTime;
    char dlSizeBuf[64], totalSizeBuf[64], xferRateBuf[64], dlTimeBuf[64];
    int xferRate;
    int leftWidth;
    const char* s;

    downloadSize = trap_Cvar_VariableValue("cl_downloadSize");
    downloadCount = trap_Cvar_VariableValue("cl_downloadCount");
    downloadTime = trap_Cvar_VariableValue("cl_downloadTime");

    leftWidth = 320;

    UI_SetColor(colorWhite);
    Text_PaintCenter(centerPoint, yStart + 112, scale, colorWhite, dlText, 0);
    Text_PaintCenter(centerPoint, yStart + 192, scale, colorWhite, etaText, 0);
    Text_PaintCenter(centerPoint, yStart + 248, scale, colorWhite, xferText, 0);

    if (downloadSize > 0) {
        s = va("%s (%d%%)", downloadName,
               (int)((float)downloadCount * 100.0f / downloadSize));
    } else {
        s = downloadName;
    }

    Text_PaintCenter(centerPoint, yStart + 136, scale, colorWhite, s, 0);

    UI_ReadableSize(dlSizeBuf, sizeof dlSizeBuf, downloadCount);
    UI_ReadableSize(totalSizeBuf, sizeof totalSizeBuf, downloadSize);

    if (downloadCount < 4096 || !downloadTime) {
        Text_PaintCenter(leftWidth, yStart + 216, scale, colorWhite, "estimating", 0);
        Text_PaintCenter(leftWidth, yStart + 160, scale, colorWhite, va("(%s of %s copied)", dlSizeBuf, totalSizeBuf), 0);
    } else {
        if ((uiInfo.uiDC.realTime - downloadTime) / 1000) {
            xferRate = downloadCount / ((uiInfo.uiDC.realTime - downloadTime) / 1000);
        } else {
            xferRate = 0;
        }
        UI_ReadableSize(xferRateBuf, sizeof xferRateBuf, xferRate);

        // Extrapolate estimated completion time
        if (downloadSize && xferRate) {
            int n = downloadSize / xferRate;  // estimated time for entire d/l in secs

            // We do it in K (/1024) because we'd overflow around 4MB
            UI_PrintTime(dlTimeBuf, sizeof dlTimeBuf,
                         (n - (((downloadCount / 1024) * n) / (downloadSize / 1024))) * 1000);

            Text_PaintCenter(leftWidth, yStart + 216, scale, colorWhite, dlTimeBuf, 0);
            Text_PaintCenter(leftWidth, yStart + 160, scale, colorWhite, va("(%s of %s copied)", dlSizeBuf, totalSizeBuf), 0);
        } else {
            Text_PaintCenter(leftWidth, yStart + 216, scale, colorWhite, "estimating", 0);
            if (downloadSize) {
                Text_PaintCenter(leftWidth, yStart + 160, scale, colorWhite, va("(%s of %s copied)", dlSizeBuf, totalSizeBuf), 0);
            } else {
                Text_PaintCenter(leftWidth, yStart + 160, scale, colorWhite, va("(%s copied)", dlSizeBuf), 0);
            }
        }

        if (xferRate) {
            Text_PaintCenter(leftWidth, yStart + 272, scale, colorWhite, va("%s/Sec", xferRateBuf), 0);
        }
    }
}

/*
========================
UI_DrawConnectScreen

This will also be overlaid on the cgame info screen during loading
to prevent it from blinking away too rapidly on local or lan games.
========================
*/
void UI_DrawConnectScreen(qboolean overlay) {
    char* s;
    uiClientState_t cstate;
    char info[MAX_INFO_VALUE];
    char text[256];
    float centerPoint, yStart, scale;

    menuDef_t* menu = Menus_FindByName("Connect");

    if (!overlay && menu) {
        Menu_Paint(menu, qtrue);
    }

    if (!overlay) {
        centerPoint = 320;
        yStart = 130;
        scale = 0.5f;
    } else {
        return;
    }

    // see what information we should display
    trap_GetClientState(&cstate);

    info[0] = '\0';
    if (trap_GetConfigString(CS_SERVERINFO, info, sizeof(info))) {
        Text_PaintCenter(centerPoint, yStart, scale, colorWhite, va("Loading %s", Info_ValueForKey(info, "mapname")), 0);
    }

    if (!Q_stricmp(cstate.servername, "localhost")) {
        Text_PaintCenter(centerPoint, yStart + 48, scale, colorWhite, "Starting up...", ITEM_TEXTSTYLE_SHADOWEDMORE);
    } else {
        Com_sprintf(text, sizeof(text), "Connecting to %s", cstate.servername);
        Text_PaintCenter(centerPoint, yStart + 48, scale, colorWhite, text, ITEM_TEXTSTYLE_SHADOWEDMORE);
    }

    // display global MOTD at bottom
    Text_PaintCenter(centerPoint, 600, scale, colorWhite, Info_ValueForKey(cstate.updateInfoString, "motd"), 0);
    // print any server info (server full, bad version, etc)
    if (cstate.connState < CA_CONNECTED) {
        Text_PaintCenter_AutoWrapped(centerPoint, yStart + 176, 630, 20, scale, colorWhite, cstate.messageString, 0);
    }

    if (lastConnState > cstate.connState) {
        lastLoadingText[0] = '\0';
    }
    lastConnState = cstate.connState;

    switch (cstate.connState) {
        case CA_CONNECTING:
            s = va("Awaiting connection...%i", cstate.connectPacketCount);
            break;
        case CA_CHALLENGING:
            s = va("Awaiting challenge...%i", cstate.connectPacketCount);
            break;
        case CA_CONNECTED: {
            char downloadName[MAX_INFO_VALUE];

            trap_Cvar_VariableStringBuffer("cl_downloadName", downloadName, sizeof(downloadName));
            if (*downloadName) {
                UI_DisplayDownloadInfo(downloadName, centerPoint, yStart, scale);
                return;
            }
        }
            s = "Awaiting gamestate...";
            break;
        case CA_LOADING:
            s = "Game initialising...";
            break;
        case CA_PRIMED:
            s = "Awaiting initial frame...";
            break;
        default:
            return;
    }

    if (Q_stricmp(cstate.servername, "localhost")) {
        Text_PaintCenter(centerPoint, yStart + 80, scale, colorWhite, s, 0);
    }

    // password required / connection rejected information goes here
}

/*
================
cvars
================
*/

typedef struct {
    vmCvar_t* vmCvar;
    char* cvarName;
    char* defaultString;
    int cvarFlags;
    // uix86.dll: per-cvar change hook, invoked from UI_UpdateCvars when modificationCount changes
    void (*update)(void);
} cvarTable_t;

vmCvar_t ui_ffa_fraglimit;
vmCvar_t ui_ffa_timelimit;

vmCvar_t ui_tourney_fraglimit;
vmCvar_t ui_tourney_timelimit;

vmCvar_t ui_team_fraglimit;
vmCvar_t ui_team_timelimit;
vmCvar_t ui_team_friendly;

vmCvar_t ui_ctf_capturelimit;
vmCvar_t ui_ctf_timelimit;
vmCvar_t ui_ctf_friendly;

vmCvar_t ui_arenasFile;
vmCvar_t ui_botsFile;
vmCvar_t ui_spAwards;
vmCvar_t ui_spVideos;
vmCvar_t ui_spSkill;

vmCvar_t ui_spSelection;

vmCvar_t ui_brassTime;
vmCvar_t ui_drawCrosshair;
vmCvar_t ui_drawCrosshairNames;
vmCvar_t ui_marks;

vmCvar_t ui_redteam;
vmCvar_t ui_blueteam;
vmCvar_t ui_teamName;
vmCvar_t ui_dedicated;
vmCvar_t ui_gameType;
vmCvar_t ui_netGameType;
vmCvar_t ui_actualNetGameType;
vmCvar_t ui_joinGameType;
vmCvar_t ui_netSource;
vmCvar_t ui_serverFilterType;
vmCvar_t ui_opponentName;
vmCvar_t ui_menuFiles;
vmCvar_t ui_currentMap;
vmCvar_t ui_currentNetMap;
vmCvar_t ui_mapIndex;
vmCvar_t ui_currentOpponent;
vmCvar_t ui_selectedPlayer;
vmCvar_t ui_selectedPlayerName;
vmCvar_t ui_scoreAccuracy;
vmCvar_t ui_scoreImpressives;
vmCvar_t ui_scoreExcellents;
vmCvar_t ui_scoreCaptures;
vmCvar_t ui_scoreDefends;
vmCvar_t ui_scoreAssists;
vmCvar_t ui_scoreGauntlets;
vmCvar_t ui_scoreScore;
vmCvar_t ui_scorePerfect;
vmCvar_t ui_scoreTeam;
vmCvar_t ui_scoreBase;
vmCvar_t ui_scoreTimeBonus;
vmCvar_t ui_scoreSkillBonus;
vmCvar_t ui_scoreShutoutBonus;
vmCvar_t ui_scoreTime;
vmCvar_t ui_captureLimit;
vmCvar_t ui_fragLimit;
vmCvar_t ui_smallFont;
vmCvar_t ui_bigFont;
vmCvar_t ui_findPlayer;
vmCvar_t ui_hudFiles;
vmCvar_t ui_recordSPDemo;
vmCvar_t ui_realCaptureLimit;
vmCvar_t ui_realWarmUp;
vmCvar_t ui_serverStatusTimeOut;

// [QL additions] - player 2 duel scores
vmCvar_t ui_scoreAccuracy2;
vmCvar_t ui_scoreImpressives2;
vmCvar_t ui_scoreExcellents2;
vmCvar_t ui_scoreDefends2;
vmCvar_t ui_scoreAssists2;
vmCvar_t ui_scoreGauntlets2;
vmCvar_t ui_scoreScore2;
vmCvar_t ui_scorePerfect2;
vmCvar_t ui_scoreTeam2;
vmCvar_t ui_scoreBase2;
vmCvar_t ui_scoreTimeBonus2;
vmCvar_t ui_scoreSkillBonus2;
vmCvar_t ui_scoreShutoutBonus2;
vmCvar_t ui_scoreTime2;
vmCvar_t ui_scoreCaptures2;

// [QL additions] - model/skin customization
vmCvar_t ui_forceTeamModel;
vmCvar_t ui_forceTeamSkin;
vmCvar_t ui_forceEnemyModel;
vmCvar_t ui_forceEnemySkin;
vmCvar_t ui_forceTeamModelBright;
vmCvar_t ui_forceEnemyModelBright;
vmCvar_t ui_teamColor;
vmCvar_t ui_enemyColor;
vmCvar_t ui_teamHeadColor;
vmCvar_t ui_teamUpperColor;
vmCvar_t ui_teamLowerColor;
vmCvar_t ui_enemyHeadColor;
vmCvar_t ui_enemyUpperColor;
vmCvar_t ui_enemyLowerColor;

// [QL additions] - game settings
vmCvar_t ui_doWarmup;
vmCvar_t ui_warmup;
vmCvar_t ui_pure;
vmCvar_t ui_friendlyFire;
vmCvar_t ui_cvGameType;
vmCvar_t ui_matchStartTime;
vmCvar_t ui_saveCaptureLimit;
vmCvar_t ui_saveFragLimit;
vmCvar_t ui_votestring;
vmCvar_t ui_intermission;

// [QL additions] - browser
vmCvar_t ui_browserSortKey;
vmCvar_t ui_browserShowFull;
vmCvar_t ui_browserShowEmpty;
vmCvar_t ui_browserMaster;
vmCvar_t ui_browserGameType;

// [QL additions] - screen effects
vmCvar_t ui_screenDamage;
vmCvar_t ui_screenDamage_Team;
vmCvar_t ui_bloomPreset;

// [QL additions] - misc
vmCvar_t ui_version;
vmCvar_t ui_gibs;
vmCvar_t ui_announcer;
vmCvar_t ui_mainmenu;
vmCvar_t ui_currentTier;
vmCvar_t ui_opponentModel;
vmCvar_t ui_mousePitch;
vmCvar_t ui_favoriteName;
vmCvar_t ui_favoriteAddress;


// [QL] missing UI cvars from binary parity audit (3)
vmCvar_t ui_cg_announcer;
vmCvar_t ui_cdkeychecked;
vmCvar_t ui_singlePlayerActive;
static cvarTable_t cvarTable[] = {
    {&ui_ffa_fraglimit, "ui_ffa_fraglimit", "20", CVAR_ARCHIVE},
    {&ui_ffa_timelimit, "ui_ffa_timelimit", "0", CVAR_ARCHIVE},

    {&ui_tourney_fraglimit, "ui_tourney_fraglimit", "0", CVAR_ARCHIVE},
    {&ui_tourney_timelimit, "ui_tourney_timelimit", "15", CVAR_ARCHIVE},

    {&ui_team_fraglimit, "ui_team_fraglimit", "0", CVAR_ARCHIVE},
    {&ui_team_timelimit, "ui_team_timelimit", "20", CVAR_ARCHIVE},
    {&ui_team_friendly, "ui_team_friendly", "1", CVAR_ARCHIVE},

    {&ui_ctf_capturelimit, "ui_ctf_capturelimit", "8", CVAR_ARCHIVE},
    {&ui_ctf_timelimit, "ui_ctf_timelimit", "30", CVAR_ARCHIVE},
    {&ui_ctf_friendly, "ui_ctf_friendly", "0", CVAR_ARCHIVE},

    {&ui_arenasFile, "g_arenasFile", "", CVAR_ROM | CVAR_INIT},
    {&ui_botsFile, "g_botsFile", "", CVAR_ROM | CVAR_INIT},

    {&ui_brassTime, "cg_brassTime", "2500", CVAR_ARCHIVE},
    {&ui_drawCrosshair, "cg_drawCrosshair", "2", CVAR_ARCHIVE},
    {&ui_drawCrosshairNames, "cg_enemyCrosshairNames", "1", CVAR_ARCHIVE},
    {&ui_marks, "cg_marks", "1", CVAR_ARCHIVE},

    {&ui_debug, "ui_debug", "0", CVAR_TEMP},
    // [QL] traces every menu open/close/action to the console (ui_shared.c Menu_Trace)
    {&ui_debugMenus, "ui_debugMenus", "0", CVAR_TEMP},
    {&ui_initialized, "ui_initialized", "0", CVAR_TEMP},
    {&ui_dedicated, "ui_dedicated", "0", CVAR_ARCHIVE},
    {&ui_gameType, "ui_gametype", "3", CVAR_ARCHIVE},
    {&ui_joinGameType, "ui_joinGametype", "0", CVAR_ARCHIVE},
    {&ui_netGameType, "ui_netGametype", "3", CVAR_ARCHIVE},
    {&ui_actualNetGameType, "ui_actualNetGametype", "3", CVAR_ARCHIVE},
    {&ui_blueteam, "ui_blueteam", "Stroggs", CVAR_ARCHIVE},
    {&ui_redteam, "ui_redteam", "Pagans", CVAR_ARCHIVE},
    {&ui_netSource, "ui_netSource", "0", CVAR_ARCHIVE},
    {&ui_menuFiles, "ui_menuFiles", "ui/menus.txt", CVAR_ARCHIVE},
    {&ui_currentMap, "ui_currentMap", "0", CVAR_ARCHIVE},
    {&ui_currentNetMap, "ui_currentNetMap", "0", CVAR_ARCHIVE},
    {&ui_mapIndex, "ui_mapIndex", "0", CVAR_ARCHIVE},
    {&ui_currentOpponent, "ui_currentOpponent", "0", CVAR_ARCHIVE},
    {&ui_selectedPlayer, "cg_selectedPlayer", "0", CVAR_ARCHIVE},
    {&ui_selectedPlayerName, "cg_selectedPlayerName", "", CVAR_ARCHIVE},
    {&ui_scoreAccuracy, "ui_scoreAccuracy", "0", CVAR_ARCHIVE},
    {&ui_scoreImpressives, "ui_scoreImpressives", "0", CVAR_ARCHIVE},
    {&ui_scoreExcellents, "ui_scoreExcellents", "0", CVAR_ARCHIVE},
    {&ui_scoreCaptures, "ui_scoreCaptures", "0", CVAR_ARCHIVE},
    {&ui_scoreDefends, "ui_scoreDefends", "0", CVAR_ARCHIVE},
    {&ui_scoreAssists, "ui_scoreAssists", "0", CVAR_ARCHIVE},
    {&ui_scoreGauntlets, "ui_scoreGauntlets", "0", CVAR_ARCHIVE},
    {&ui_scoreScore, "ui_scoreScore", "0", CVAR_ARCHIVE},
    {&ui_scorePerfect, "ui_scorePerfect", "0", CVAR_ARCHIVE},
    {&ui_scoreTeam, "ui_scoreTeam", "0 to 0", CVAR_ARCHIVE},
    {&ui_scoreBase, "ui_scoreBase", "0", CVAR_ARCHIVE},
    {&ui_scoreTime, "ui_scoreTime", "00:00", CVAR_ARCHIVE},
    {&ui_scoreTimeBonus, "ui_scoreTimeBonus", "0", CVAR_ARCHIVE},
    {&ui_scoreSkillBonus, "ui_scoreSkillBonus", "0", CVAR_ARCHIVE},
    {&ui_scoreShutoutBonus, "ui_scoreShutoutBonus", "0", CVAR_ARCHIVE},
    {&ui_fragLimit, "ui_fragLimit", "10", 0},
    {&ui_captureLimit, "ui_captureLimit", "5", 0},
    {&ui_smallFont, "ui_smallFont", "0.25", CVAR_REPLICATE | CVAR_ARCHIVE},
    {&ui_bigFont, "ui_bigFont", "0.4", CVAR_REPLICATE | CVAR_ARCHIVE},
    {&ui_findPlayer, "ui_findPlayer", "Sarge", CVAR_ARCHIVE},
    {&ui_hudFiles, "cg_hudFiles", "ui/hud.txt", CVAR_ARCHIVE},
    {&ui_recordSPDemo, "ui_recordSPDemo", "0", CVAR_ARCHIVE},
    {&ui_realWarmUp, "g_warmup", "10", CVAR_ARCHIVE},
    {&ui_realCaptureLimit, "capturelimit", "8", CVAR_NORESTART | CVAR_SERVERINFO | CVAR_ARCHIVE},
    {&ui_serverStatusTimeOut, "ui_serverStatusTimeOut", "7000", CVAR_ARCHIVE},

    // [QL additions] - player 2 duel scores
    {&ui_scoreAccuracy2, "ui_scoreAccuracy2", "0", CVAR_ARCHIVE},
    {&ui_scoreImpressives2, "ui_scoreImpressives2", "0", CVAR_ARCHIVE},
    {&ui_scoreExcellents2, "ui_scoreExcellents2", "0", CVAR_ARCHIVE},
    {&ui_scoreDefends2, "ui_scoreDefends2", "0", CVAR_ARCHIVE},
    {&ui_scoreAssists2, "ui_scoreAssists2", "0", CVAR_ARCHIVE},
    {&ui_scoreGauntlets2, "ui_scoreGauntlets2", "0", CVAR_ARCHIVE},
    {&ui_scoreScore2, "ui_scoreScore2", "0", CVAR_ARCHIVE},
    {&ui_scorePerfect2, "ui_scorePerfect2", "0", CVAR_ARCHIVE},
    {&ui_scoreTeam2, "ui_scoreTeam2", "0 to 0", CVAR_ARCHIVE},
    {&ui_scoreBase2, "ui_scoreBase2", "0", CVAR_ARCHIVE},
    {&ui_scoreTimeBonus2, "ui_scoreTimeBonus2", "0", CVAR_ARCHIVE},
    {&ui_scoreSkillBonus2, "ui_scoreSkillBonus2", "0", CVAR_ARCHIVE},
    {&ui_scoreShutoutBonus2, "ui_scoreShutoutBonus2", "0", CVAR_ARCHIVE},
    {&ui_scoreTime2, "ui_scoreTime2", "00:00", CVAR_ARCHIVE},
    {&ui_scoreCaptures2, "ui_scoreCaptures2", "0", CVAR_ARCHIVE},

    // [QL additions] - model/skin customization
    {&ui_forceTeamModel, "ui_forceTeamModel", "", CVAR_TEMP},
    {&ui_forceTeamSkin, "ui_forceTeamSkin", "", CVAR_TEMP},
    {&ui_forceEnemyModel, "ui_forceEnemyModel", "", CVAR_TEMP},
    {&ui_forceEnemySkin, "ui_forceEnemySkin", "", CVAR_TEMP},
    {&ui_forceTeamModelBright, "ui_forceTeamModelBright", "0", CVAR_ROM},
    {&ui_forceEnemyModelBright, "ui_forceEnemyModelBright", "0", CVAR_ROM},
    {&ui_teamColor, "ui_teamColor", "0", CVAR_ARCHIVE},
    {&ui_enemyColor, "ui_enemyColor", "0", CVAR_ARCHIVE},
    {&ui_teamHeadColor, "ui_teamHeadColor", "96", CVAR_ARCHIVE},
    {&ui_teamUpperColor, "ui_teamUpperColor", "96", CVAR_ARCHIVE},
    {&ui_teamLowerColor, "ui_teamLowerColor", "96", CVAR_ARCHIVE},
    {&ui_enemyHeadColor, "ui_enemyHeadColor", "27", CVAR_ARCHIVE},
    {&ui_enemyUpperColor, "ui_enemyUpperColor", "27", CVAR_ARCHIVE},
    {&ui_enemyLowerColor, "ui_enemyLowerColor", "27", CVAR_ARCHIVE},

    // [QL additions] - game settings
    {&ui_doWarmup, "ui_doWarmup", "0", CVAR_ARCHIVE},
    {&ui_warmup, "ui_warmup", "0", CVAR_ARCHIVE},
    {&ui_pure, "ui_pure", "1", CVAR_ARCHIVE},
    {&ui_friendlyFire, "ui_friendlyFire", "1", CVAR_ARCHIVE},
    {&ui_cvGameType, "ui_cvGameType", "-1", 0},
    {&ui_matchStartTime, "ui_matchStartTime", "0", CVAR_ROM},
    {&ui_saveCaptureLimit, "ui_saveCaptureLimit", "5", CVAR_ARCHIVE},
    {&ui_saveFragLimit, "ui_saveFragLimit", "10", CVAR_ARCHIVE},
    {&ui_votestring, "ui_votestring", "", CVAR_TEMP},
    {&ui_intermission, "ui_intermission", "0", CVAR_ROM},

    // [QL additions] - browser
    {&ui_browserSortKey, "ui_browserSortKey", "4", CVAR_ARCHIVE},
    {&ui_browserShowFull, "ui_browserShowFull", "1", CVAR_ARCHIVE},
    {&ui_browserShowEmpty, "ui_browserShowEmpty", "1", CVAR_ARCHIVE},
    {&ui_browserMaster, "ui_browserMaster", "0", CVAR_ARCHIVE},
    {&ui_browserGameType, "ui_browserGameType", "0", CVAR_ARCHIVE},

    // [QL additions] - screen effects
    {&ui_screenDamage, "ui_screenDamage", "0", CVAR_ARCHIVE},
    {&ui_screenDamage_Team, "ui_screenDamage_Team", "0", CVAR_ARCHIVE},
    {&ui_bloomPreset, "ui_bloomPreset", "Default", CVAR_ARCHIVE},

    // [QL additions] - misc
    {&ui_version, "ui_version", "1069 win-x86 May 25 2016 15:18:22", CVAR_ROM},  // [QL] _UI_Init 0x1000fab0 build banner
    {&ui_gibs, "ui_gibs", "1", CVAR_ROM},
    {&ui_announcer, "ui_announcer", "1", CVAR_ARCHIVE},
    {&ui_mainmenu, "ui_mainmenu", "1", CVAR_ROM},
    {&ui_currentTier, "ui_currentTier", "0", CVAR_ARCHIVE},
    {&ui_opponentModel, "ui_opponentModel", "sarge", CVAR_ARCHIVE},
    {&ui_mousePitch, "ui_mousePitch", "0", CVAR_ARCHIVE},
    {&ui_favoriteName, "ui_favoriteName", "", CVAR_ARCHIVE},
    {&ui_favoriteAddress, "ui_favoriteAddress", "", CVAR_ARCHIVE},

    // [QL additions] - server favorites (16 slots)
    {NULL, "server1", "", CVAR_ARCHIVE},
    {NULL, "server2", "", CVAR_ARCHIVE},
    {NULL, "server3", "", CVAR_ARCHIVE},
    {NULL, "server4", "", CVAR_ARCHIVE},
    {NULL, "server5", "", CVAR_ARCHIVE},
    {NULL, "server6", "", CVAR_ARCHIVE},
    {NULL, "server7", "", CVAR_ARCHIVE},
    {NULL, "server8", "", CVAR_ARCHIVE},
    {NULL, "server9", "", CVAR_ARCHIVE},
    {NULL, "server10", "", CVAR_ARCHIVE},
    {NULL, "server11", "", CVAR_ARCHIVE},
    {NULL, "server12", "", CVAR_ARCHIVE},
    {NULL, "server13", "", CVAR_ARCHIVE},
    {NULL, "server14", "", CVAR_ARCHIVE},
    {NULL, "server15", "", CVAR_ARCHIVE},
    {NULL, "server16", "", CVAR_ARCHIVE},

    // [QL additions] - team bot names
    {NULL, "ui_blueteam1", "0", CVAR_ARCHIVE},
    {NULL, "ui_blueteam2", "0", CVAR_ARCHIVE},
    {NULL, "ui_blueteam3", "0", CVAR_ARCHIVE},
    {NULL, "ui_blueteam4", "0", CVAR_ARCHIVE},
    {NULL, "ui_blueteam5", "0", CVAR_ARCHIVE},
    {NULL, "ui_redteam1", "0", CVAR_ARCHIVE},
    {NULL, "ui_redteam2", "0", CVAR_ARCHIVE},
    {NULL, "ui_redteam3", "0", CVAR_ARCHIVE},
    {NULL, "ui_redteam4", "0", CVAR_ARCHIVE},
    {NULL, "ui_redteam5", "0", CVAR_ARCHIVE},

    // [QL additions] - server browser refresh timestamps
    {NULL, "ui_lastServerRefresh_0", "", CVAR_ARCHIVE},
    {NULL, "ui_lastServerRefresh_1", "", CVAR_ARCHIVE},
    {NULL, "ui_lastServerRefresh_2", "", CVAR_ARCHIVE},
    {NULL, "ui_lastServerRefresh_3", "", CVAR_ARCHIVE},

    // [QL additions] - misc Q3 cvars in binary table
    {NULL, "ui_teamName", "Pagans", CVAR_ARCHIVE},
    {NULL, "ui_new", "0", CVAR_TEMP},
    {NULL, "ui_priv", "0", CVAR_ROM},
    {NULL, "g_spAwards", "", CVAR_ROM | CVAR_ARCHIVE},
    {NULL, "g_spSkill", "2", CVAR_ARCHIVE},
    {NULL, "g_spScores1", "", CVAR_ROM | CVAR_ARCHIVE},
    {NULL, "g_spScores2", "", CVAR_ROM | CVAR_ARCHIVE},
    {NULL, "g_spScores3", "", CVAR_ROM | CVAR_ARCHIVE},
    {NULL, "g_spScores4", "", CVAR_ROM | CVAR_ARCHIVE},
    {NULL, "g_spScores5", "", CVAR_ROM | CVAR_ARCHIVE},
    {NULL, "g_spVideos", "", CVAR_ROM | CVAR_ARCHIVE},
    {NULL, "ui_spSelection", "", CVAR_ROM},

    {NULL, "ui_videomode", "", CVAR_ROM},
    {NULL, "g_localTeamPref", "", 0},
    // [QL] missing UI cvars from binary parity audit
    {&ui_cg_announcer, "cg_announcer", "1", CVAR_TEMP, UI_OnAnnouncerChanged},
    {&ui_cdkeychecked, "ui_cdkeychecked", "0", CVAR_ROM},
    {&ui_opponentName, "ui_opponentName", "Stroggs", CVAR_ARCHIVE},
    {&ui_singlePlayerActive, "ui_singlePlayerActive", "0", 0},
};

static int cvarTableSize = ARRAY_LEN(cvarTable);

/*
=================
UI_CheckModelBright

uix86.dll UI_CheckModelBright @0x10011510: seed a *Bright ROM cvar from the current model/skin.
Lowercases the first 0x40 chars of each string, then sets targetCvar to "1" when the skin
contains "bright", or the model contains "bright" and the skin is empty; otherwise "0".
=================
*/
static void UI_CheckModelBright(const char* modelCvar, const char* skinCvar, const char* targetCvar) {
    char model[1024];
    char skin[1024];
    int i;

    trap_Cvar_VariableStringBuffer(modelCvar, model, sizeof(model));
    trap_Cvar_VariableStringBuffer(skinCvar, skin, sizeof(skin));

    for (i = 0; i < 0x40 && model[i]; i++) {
        model[i] = (char)tolower(model[i]);
    }
    for (i = 0; i < 0x40 && skin[i]; i++) {
        skin[i] = (char)tolower(skin[i]);
    }

    if (strstr(skin, "bright") || (strstr(model, "bright") && skin[0] == '\0')) {
        trap_Cvar_Set(targetCvar, "1");
    } else {
        trap_Cvar_Set(targetCvar, "0");
    }
}

/*
=================
UI_OnAnnouncerChanged

uix86.dll UI_OnAnnouncerChanged @0x10011690: preview the selected announcer voice and mirror
cg_announcer into ui_announcer.
=================
*/
static void UI_OnAnnouncerChanged(void) {
    sfxHandle_t s;

    if (ui_cg_announcer.integer == 2) {
        s = trap_S_RegisterSound("sound/misc/vo_evil.ogg", 7);
    } else if (ui_cg_announcer.integer == 3) {
        s = trap_S_RegisterSound("sound/misc/vo_female.ogg", 7);
    } else {
        s = trap_S_RegisterSound("sound/misc/vo_default.ogg", 7);
    }
    trap_S_StartLocalSound(s, CHAN_ANNOUNCER);
    trap_Cvar_Set("ui_announcer", va("%i", ui_cg_announcer.integer));
}

/*
=================
UI_RegisterCvars
=================
*/
void UI_RegisterCvars(void) {
    int i;
    cvarTable_t* cv;

    for (i = 0, cv = cvarTable; i < cvarTableSize; i++, cv++) {
        trap_Cvar_Register(cv->vmCvar, cv->cvarName, cv->defaultString, cv->cvarFlags);
    }

    // uix86.dll UI_RegisterCvars @0x10011730: seed the *Bright ROM cvars from current model/skin
    UI_CheckModelBright("cg_forceTeamModel", "cg_forceTeamSkin", "ui_forceTeamModelBright");
    UI_CheckModelBright("cg_forceEnemyModel", "cg_forceEnemySkin", "ui_forceEnemyModelBright");
}

/*
=================
UI_UpdateCvars
=================
*/
void UI_UpdateCvars(void) {
    int i;
    cvarTable_t* cv;
    int oldMod;

    for (i = 0, cv = cvarTable; i < cvarTableSize; i++, cv++) {
        if (!cv->vmCvar) {
            continue;
        }

        oldMod = cv->vmCvar->modificationCount;
        trap_Cvar_Update(cv->vmCvar);
        // uix86.dll UI_UpdateCvars @: fire the change hook only when the cvar changed
        // (oldMod != 0 skips the very first update where modificationCount was still 0).
        if (cv->update && oldMod != 0 && oldMod != cv->vmCvar->modificationCount) {
            cv->update();
        }
    }
}
