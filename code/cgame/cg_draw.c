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
//
// cg_draw.c -- draw all of the graphical elements during
// active (after loading) gameplay

#include "cg_local.h"

#include "../ui/ui_shared.h"

// used for scoreboard
extern displayContextDef_t cgDC;
menuDef_t* menuScoreboard = NULL;
menuDef_t* menuEndScoreboard = NULL;

int sortedTeamPlayers[TEAM_MAXOVERLAY];
int numSortedTeamPlayers;

char systemChat[256];
char teamChat1[256];
char teamChat2[256];

/*
=================
CG_KeyNameForCommand

Resolves a command binding (e.g. "+attack") to its display key name (e.g. "MOUSE1").
If no key is bound, writes the fallback string "???" into buf.
=================
*/
void CG_KeyNameForCommand(const char* command, char* buf, int buflen) {
	int key;

	key = trap_Key_GetKey(command);
	if (key >= 0) {
		trap_Key_KeynumToStringBuf(key, buf, buflen);
	} else {
		Q_strncpyz(buf, "???", buflen);
	}
}

// [QL] Text is rendered by the engine glyph atlas (fontstash/stb_truetype) via
// trap_R_Font_DrawString / trap_R_Font_TextExtents.  The legacy per-glyph
// fontInfo_t path (FreeType) is gone; these functions convert the game's
// 640x480 virtual coordinates and text scale into the screen-pixel coordinates
// and pixel font size the engine expects, matching the QL binary.

// Font index convention (QL engine / menudef): 0 = FONT_DEFAULT (handelgothic,
// "normal"), 1 = FONT_SANS (notosans), 2 = FONT_MONO (droidsansmono).
static int CG_EngineFont(int fontIndex) {
	if (fontIndex >= 0 && fontIndex < 3)
		return fontIndex;
	return 0;
}

// Map a legacy fontInfo_t* back to a font index for the _Font variants.
static int CG_IndexForFontPtr(const fontInfo_t* font) {
	int i;
	for (i = 0; i < 3; i++) {
		if (font == &cgDC.Assets.extraFonts[i])
			return i;
	}
	return 0;
}

// Pixel font size for a text scale (QL: screenFontScale = (vidHeight/768)*96).
static float CG_FontPixelSize(float scale) {
	return (((float)cgs.glconfig.vidHeight / 768.0f) * 96.0f) * scale;
}

// Horizontal scale mapping 640-space x to screen pixels, honouring widescreen.
static float CG_TextXScale(void) {
	int ws = cg_currentWidescreen;
	if (cgs.widescreenBias > 0.0f && (ws != 0 || (trap_Key_GetCatcher() & KEYCATCH_CGAME)))
		return (cgs.glconfig.vidHeight * 4.0f / 3.0f) / 640.0f;
	return cgs.screenXScale;
}

// Convert 640x480 virtual coords to screen pixels with widescreen bias
// (matches QL cg_drawtools.c CG_Text_Paint).
static void CG_TextToScreen(float* x, float* y) {
	float xscale = CG_TextXScale();
	float xbias = 0.0f;
	int ws = cg_currentWidescreen;
	if (cgs.widescreenBias > 0.0f && (ws != 0 || (trap_Key_GetCatcher() & KEYCATCH_CGAME))) {
		if (ws == 2 || (trap_Key_GetCatcher() & KEYCATCH_CGAME))
			xbias = cgs.widescreenBias;
		else if (ws == 3)
			xbias = cgs.widescreenBias * 2.0f;
	}
	if (x)
		*x = *x * xscale + xbias;
	if (y)
		*y = *y * cgs.screenYScale;
}

// Core painter: virtual coords + scale -> screen pixels, engine glyph atlas.
// A shadow style draws the whole string offset in solid black first.
static void CG_PaintText(float x, float y, int fontIndex, float scale, const vec4_t color,
                         const char* text, int limit, int style) {
	float sx = x, sy = y;
	float size = CG_FontPixelSize(scale);
	int efont = CG_EngineFont(fontIndex);
	int lim = (limit > 0) ? limit : -1;

	if (!text || !*text)
		return;

	CG_TextToScreen(&sx, &sy);

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
static void CG_MeasureText(const char* text, float scale, int fontIndex, int limit, int* w640, int* h640) {
	int wpx = 0, hpx = 0;
	float xscale, yscale;
	float size = CG_FontPixelSize(scale);
	int lim = (limit > 0) ? limit : -1;

	trap_R_Font_TextExtents(text, 0, lim, size, CG_EngineFont(fontIndex), NULL, NULL, &wpx, &hpx);

	xscale = CG_TextXScale();
	if (xscale <= 0.0f)
		xscale = 1.0f;
	yscale = cgs.screenYScale;
	if (yscale <= 0.0f)
		yscale = 1.0f;

	if (w640)
		*w640 = (int)((float)wpx / xscale);
	if (h640)
		*h640 = (int)((float)hpx / yscale);
}

// [QL] Font-pointer variants - used by DC wrappers.
float CG_Text_Width_Font(const char* text, float scale, int limit, fontInfo_t* font) {
	int w = 0;
	CG_MeasureText(text, scale, CG_IndexForFontPtr(font), limit, &w, NULL);
	return (float)w;
}

float CG_Text_Height_Font(const char* text, float scale, int limit, fontInfo_t* font) {
	int h = 0;
	CG_MeasureText(text, scale, CG_IndexForFontPtr(font), limit, NULL, &h);
	return (float)h;
}

void CG_Text_Paint_Font(float x, float y, float scale, vec4_t color, const char* text, float adjust, int limit, int style, fontInfo_t* font) {
	(void)adjust;
	CG_PaintText(x, y, CG_IndexForFontPtr(font), scale, color, text, limit, style);
}

// [QL] DC wrapper functions - pass the font index straight to the engine.
void CG_DrawText_DC(float x, float y, float scale, vec4_t color, const char* text,
					float adjust, int limit, int style, int fontIndex) {
	(void)adjust;
	CG_PaintText(x, y, fontIndex, scale, color, text, limit, style);
}

float CG_TextWidth_DC(const char* text, float scale, int limit, int fontIndex) {
	int w = 0;
	CG_MeasureText(text, scale, fontIndex, limit, &w, NULL);
	return (float)w;
}

float CG_TextHeight_DC(const char* text, float scale, int limit, int fontIndex) {
	int h = 0;
	CG_MeasureText(text, scale, fontIndex, limit, NULL, &h);
	return (float)h;
}

void CG_DrawTextWithCursor_DC(float x, float y, float scale, vec4_t color, const char* text,
							  int cursorPos, char cursor, int limit, int style, int fontIndex) {
	(void)cursorPos;
	(void)cursor;
	CG_PaintText(x, y, fontIndex, scale, color, text, limit, style);
}

// [QL] CG_DrawText - matching binary's 0x10008440 in cgamex86.dll. Renders text
// through the engine glyph atlas at screen coordinates with a full-string shadow
// pass; the widescreen coordinate transform lives in CG_PaintText/CG_TextToScreen.
void CG_DrawText(float x, float y, int fontIndex, float scale, vec4_t color,
                 const char *text, float adjust, int maxChars, int textStyle) {
	(void)adjust;
	CG_PaintText(x, y, fontIndex, scale, color, text, maxChars, textStyle);
}

int CG_DrawTextWidth(const char *text, float scale, int limit, int fontIndex) {
	int w = 0;
	CG_MeasureText(text, scale, fontIndex, limit, &w, NULL);
	return w;
}

// Scale-based versions - used by ~90 direct call sites in cg_newdraw.c, cg_info.c,
// etc.  These use the default font (index 0), matching QL's CG_Text_Paint.
int CG_Text_Width(const char* text, float scale, int limit) {
	int w = 0;
	CG_MeasureText(text, scale, 0, limit, &w, NULL);
	return w;
}

int CG_Text_Height(const char* text, float scale, int limit) {
	int h = 0;
	CG_MeasureText(text, scale, 0, limit, NULL, &h);
	return h;
}

void CG_Text_PaintChar(float x, float y, float width, float height, float scale, float s, float t, float s2, float t2, qhandle_t hShader) {
	float w, h;
	w = width * scale;
	h = height * scale;
	CG_AdjustFrom640(&x, &y, &w, &h);
	trap_R_DrawStretchPic(x, y, w, h, s, t, s2, t2, hShader);
}

void CG_Text_Paint(float x, float y, float scale, vec4_t color, const char* text, float adjust, int limit, int style) {
	(void)adjust;
	// QL: CG_Text_Paint uses the default font (index 0 = handelgothic).
	CG_PaintText(x, y, 0, scale, color, text, limit, style);
}

/*
================
CG_Draw3DModel

================
*/
void CG_Draw3DModel(float x, float y, float w, float h, qhandle_t model, qhandle_t skin, vec3_t origin, vec3_t angles) {
	refdef_t refdef;
	refEntity_t ent;

	if (!cg_draw3dIcons.integer || !cg_drawIcons.integer) {
		return;
	}

	CG_AdjustFrom640(&x, &y, &w, &h);

	memset(&refdef, 0, sizeof(refdef));

	memset(&ent, 0, sizeof(ent));
	AnglesToAxis(angles, ent.axis);
	VectorCopy(origin, ent.origin);
	ent.hModel = model;
	ent.customSkin = skin;
	ent.renderfx = RF_NOSHADOW;  // no stencil shadows

	refdef.rdflags = RDF_NOWORLDMODEL;

	AxisClear(refdef.viewaxis);

	refdef.fov_x = 30;
	refdef.fov_y = 30;

	refdef.x = x;
	refdef.y = y;
	refdef.width = w;
	refdef.height = h;

	refdef.time = cg.time;

	trap_R_ClearScene();
	trap_R_AddRefEntityToScene(&ent);
	trap_R_RenderScene(&refdef);
}

/*
================
CG_DrawHead

Used for both the status bar and the scoreboard
================
*/
void CG_DrawHead(float x, float y, float w, float h, int clientNum, vec3_t headAngles) {
	clipHandle_t cm;
	clientInfo_t* ci;
	float len;
	vec3_t origin;
	vec3_t mins, maxs;

	ci = &cgs.clientinfo[clientNum];

	if (cg_draw3dIcons.integer) {
		cm = ci->headModel;
		if (!cm) {
			return;
		}

		// offset the origin y and z to center the head
		trap_R_ModelBounds(cm, mins, maxs);

		origin[2] = -0.5 * (mins[2] + maxs[2]);
		origin[1] = 0.5 * (mins[1] + maxs[1]);

		// calculate distance so the head nearly fills the box
		// assume heads are taller than wide
		len = 0.7 * (maxs[2] - mins[2]);
		origin[0] = len / 0.268;  // len / tan( fov/2 )

		// allow per-model tweaking
		VectorAdd(origin, ci->headOffset, origin);

		CG_Draw3DModel(x, y, w, h, ci->headModel, ci->headSkin, origin, headAngles);
	} else if (cg_drawIcons.integer) {
		CG_DrawPic(x, y, w, h, ci->modelIcon);
	}

	// if they are deferred, draw a cross out
	if (ci->deferred) {
		CG_DrawPic(x, y, w, h, cgs.media.deferShader);
	}
}

/*
================
CG_DrawFlagModel

Used for both the status bar and the scoreboard
================
*/
void CG_DrawFlagModel(float x, float y, float w, float h, int team, qboolean force2D) {
	qhandle_t cm;
	float len;
	vec3_t origin, angles;
	vec3_t mins, maxs;
	qhandle_t handle;

	if (!force2D && cg_draw3dIcons.integer) {
		VectorClear(angles);

		cm = cgs.media.redFlagModel;

		// offset the origin y and z to center the flag
		trap_R_ModelBounds(cm, mins, maxs);

		origin[2] = -0.5 * (mins[2] + maxs[2]);
		origin[1] = 0.5 * (mins[1] + maxs[1]);

		// calculate distance so the flag nearly fills the box
		// assume heads are taller than wide
		len = 0.5 * (maxs[2] - mins[2]);
		origin[0] = len / 0.268;  // len / tan( fov/2 )

		angles[YAW] = 60 * sin(cg.time / 2000.0);
		;

		if (team == TEAM_RED) {
			handle = cgs.media.redFlagModel;
		} else if (team == TEAM_BLUE) {
			handle = cgs.media.blueFlagModel;
		} else if (team == TEAM_FREE) {
			handle = cgs.media.neutralFlagModel;
		} else {
			return;
		}
		CG_Draw3DModel(x, y, w, h, handle, 0, origin, angles);
	} else if (cg_drawIcons.integer) {
		gitem_t* item;

		if (team == TEAM_RED) {
			item = BG_FindItemForPowerup(PW_REDFLAG);
		} else if (team == TEAM_BLUE) {
			item = BG_FindItemForPowerup(PW_BLUEFLAG);
		} else if (team == TEAM_FREE) {
			item = BG_FindItemForPowerup(PW_NEUTRALFLAG);
		} else {
			return;
		}
		if (item) {
			CG_DrawPic(x, y, w, h, cg_items[ITEM_INDEX(item)].icon);
		}
	}
}

/*
================
CG_DrawTeamBackground

================
*/
void CG_DrawTeamBackground(int x, int y, int w, int h, float alpha, int team) {
	vec4_t hcolor;

	hcolor[3] = alpha;
	if (team == TEAM_RED) {
		hcolor[0] = 1;
		hcolor[1] = 0;
		hcolor[2] = 0;
	} else if (team == TEAM_BLUE) {
		hcolor[0] = 0;
		hcolor[1] = 0;
		hcolor[2] = 1;
	} else {
		return;
	}
	trap_R_SetColor(hcolor);
	CG_DrawPic(x, y, w, h, cgs.media.teamStatusBar);
	trap_R_SetColor(NULL);
}

/*
===========================================================================================

  UPPER RIGHT CORNER

===========================================================================================
*/

/*
================
CG_DrawAttacker

================
*/
static float CG_DrawAttacker(float y) {
	int t;
	float size;
	vec3_t angles;
	const char* info;
	const char* name;
	int clientNum;

	if (cg.predictedPlayerState.stats[STAT_HEALTH] <= 0) {
		return y;
	}

	if (!cg.attackerTime) {
		return y;
	}

	clientNum = cg.predictedPlayerState.persistant[PERS_ATTACKER];
	if (clientNum < 0 || clientNum >= MAX_CLIENTS || clientNum == cg.snap->ps.clientNum) {
		return y;
	}

	if (!cgs.clientinfo[clientNum].infoValid) {
		cg.attackerTime = 0;
		return y;
	}

	t = cg.time - cg.attackerTime;
	if (t > ATTACKER_HEAD_TIME) {
		cg.attackerTime = 0;
		return y;
	}

	size = ICON_SIZE * 1.25;

	angles[PITCH] = 0;
	angles[YAW] = 180;
	angles[ROLL] = 0;
	CG_DrawHead(SCREEN_WIDTH - size, y, size, size, clientNum, angles);

	info = CG_ConfigString(CS_PLAYERS + clientNum);
	name = Info_ValueForKey(info, "n");
	y += size;
	{
		vec4_t halfAlpha = { 1, 1, 1, 0.5f };
		float tw = CG_Text_Width(name, 0.3f, 0);
		CG_Text_Paint(SCREEN_WIDTH - tw, y + 14, 0.3f, halfAlpha, name, 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);
	}

	return y + BIGCHAR_HEIGHT + 2;
}

/*
==================
CG_DrawSnapshot
==================
*/
static float CG_DrawSnapshot(float y) {
	char* s;
	int w;

	s = va("time:%i snap:%i cmd:%i", cg.snap->serverTime,
		   cg.latestSnapshotNum, cgs.serverCommandSequence);
	w = CG_Text_Width(s, 0.3f, 0);

	CG_Text_Paint(635 - w, y + 14, 0.3f, colorWhite, s, 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);

	return y + BIGCHAR_HEIGHT + 4;
}

/*
==================
CG_DrawFPS
==================
*/
#define FPS_FRAMES 4
static float CG_DrawFPS(float y) {
	char* s;
	int w;
	static int previousTimes[FPS_FRAMES];
	static int index;
	int i, total;
	int fps;
	static int previous;
	int t, frameTime;

	// don't use serverTime, because that will be drifting to
	// correct for internet lag changes, timescales, timedemos, etc
	t = trap_Milliseconds();
	frameTime = t - previous;
	previous = t;

	previousTimes[index % FPS_FRAMES] = frameTime;
	index++;
	if (index > FPS_FRAMES) {
		// average multiple frames together to smooth changes out a bit
		total = 0;
		for (i = 0; i < FPS_FRAMES; i++) {
			total += previousTimes[i];
		}
		if (!total) {
			total = 1;
		}
		fps = 1000 * FPS_FRAMES / total;

		s = va("%ifps", fps);
		w = CG_Text_Width(s, 0.3f, 0);

		CG_Text_Paint(635 - w, y + 14, 0.3f, colorWhite, s, 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);
	}

	return y + BIGCHAR_HEIGHT + 4;
}

/*
=================
CG_DrawTimer
=================
*/
static float CG_DrawTimer(float y) {
	char* s;
	int w;
	int mins, seconds, tens;
	int msec;

	msec = cg.time - cgs.levelStartTime;

	seconds = msec / 1000;
	mins = seconds / 60;
	seconds -= mins * 60;
	tens = seconds / 10;
	seconds -= tens * 10;

	s = va("%i:%i%i", mins, tens, seconds);
	w = CG_Text_Width(s, 0.3f, 0);

	CG_Text_Paint(635 - w, y + 14, 0.3f, colorWhite, s, 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);

	return y + BIGCHAR_HEIGHT + 4;
}

/*
=================
CG_DrawTeamOverlay
=================
*/

static float CG_DrawTeamOverlay(float y, qboolean right, qboolean upper) {
	int x, w, h, xx;
	int i, j, len;
	const char* p;
	vec4_t hcolor;
	int pwidth, lwidth;
	int plyrs;
	char st[16];
	clientInfo_t* ci;
	gitem_t* item;
	int ret_y, count;

	if (!cg_drawTeamOverlay.integer) {
		return y;
	}

	if (cg.snap->ps.persistant[PERS_TEAM] != TEAM_RED && cg.snap->ps.persistant[PERS_TEAM] != TEAM_BLUE) {
		return y;  // Not on any team
	}

	plyrs = 0;

	// max player name width
	pwidth = 0;
	count = (numSortedTeamPlayers > 8) ? 8 : numSortedTeamPlayers;
	for (i = 0; i < count; i++) {
		ci = cgs.clientinfo + sortedTeamPlayers[i];
		if (ci->infoValid && ci->team == cg.snap->ps.persistant[PERS_TEAM]) {
			plyrs++;
			len = CG_DrawStrlen(ci->name);
			if (len > pwidth)
				pwidth = len;
		}
	}

	if (!plyrs)
		return y;

	if (pwidth > TEAM_OVERLAY_MAXNAME_WIDTH)
		pwidth = TEAM_OVERLAY_MAXNAME_WIDTH;

	// max location name width
	lwidth = 0;
	for (i = 1; i < MAX_LOCATIONS; i++) {
		p = CG_ConfigString(CS_LOCATIONS + i);
		if (p && *p) {
			len = CG_DrawStrlen(p);
			if (len > lwidth)
				lwidth = len;
		}
	}

	if (lwidth > TEAM_OVERLAY_MAXLOCATION_WIDTH)
		lwidth = TEAM_OVERLAY_MAXLOCATION_WIDTH;

	w = (pwidth + lwidth + 4 + 7) * TINYCHAR_WIDTH;

	if (right)
		x = 640 - w;
	else
		x = 0;

	h = plyrs * TINYCHAR_HEIGHT;

	if (upper) {
		ret_y = y + h;
	} else {
		y -= h;
		ret_y = y;
	}

	if (cg.snap->ps.persistant[PERS_TEAM] == TEAM_RED) {
		hcolor[0] = 1.0f;
		hcolor[1] = 0.0f;
		hcolor[2] = 0.0f;
		hcolor[3] = 0.33f;
	} else {  // if ( cg.snap->ps.persistant[PERS_TEAM] == TEAM_BLUE )
		hcolor[0] = 0.0f;
		hcolor[1] = 0.0f;
		hcolor[2] = 1.0f;
		hcolor[3] = 0.33f;
	}
	trap_R_SetColor(hcolor);
	CG_DrawPic(x, y, w, h, cgs.media.teamStatusBar);
	trap_R_SetColor(NULL);

	for (i = 0; i < count; i++) {
		ci = cgs.clientinfo + sortedTeamPlayers[i];
		if (ci->infoValid && ci->team == cg.snap->ps.persistant[PERS_TEAM]) {
			/*
			[QL] A frozen teammate is the one thing this overlay exists to tell
			you in Freeze Tag - they are stood still somewhere waiting for
			someone to walk over, and nothing else on screen says so. The whole
			row goes ice blue so it reads at a glance without having to find the
			name, and the health/armour pair below is replaced with FROZEN,
			since a statue's numbers are 0/0 and mean nothing.
			*/
			if (ci->frozen) {
				hcolor[0] = 0.55f;
				hcolor[1] = 0.85f;
				hcolor[2] = 1.0f;
				hcolor[3] = 1.0f;
			} else {
				hcolor[0] = hcolor[1] = hcolor[2] = hcolor[3] = 1.0;
			}

			xx = x + TINYCHAR_WIDTH;

			CG_DrawStringExt(xx, y,
							 ci->name, hcolor, qfalse, qfalse,
							 TINYCHAR_WIDTH, TINYCHAR_HEIGHT, TEAM_OVERLAY_MAXNAME_WIDTH);

			if (lwidth) {
				/*
				[QL] A statue's location is FROZEN.

				This column is the only text field on the row, so it is where a
				word belongs - and where a frozen player is matters far less than
				that they are frozen and need someone to come and get them.
				"unknown" is what it read otherwise: most maps carry no
				target_location entities, so ci->location is 0 and
				CS_LOCATIONS + 0 is empty. Quake Live's own overlay shows
				"unknown" on those maps too, so that fallback is not ours to
				fix - it is just not worth a column here.

				Health and armour keep their real numbers, which for a statue is
				0 0, rather than being replaced by the word.
				*/
				if (ci->frozen) {
					p = "FROZEN";
				} else {
					p = CG_ConfigString(CS_LOCATIONS + ci->location);
					if (!p || !*p)
						p = "unknown";
				}
				//				len = CG_DrawStrlen(p);
				//				if (len > lwidth)
				//					len = lwidth;

				//				xx = x + TINYCHAR_WIDTH * 2 + TINYCHAR_WIDTH * pwidth +
				//					((lwidth/2 - len/2) * TINYCHAR_WIDTH);
				xx = x + TINYCHAR_WIDTH * 2 + TINYCHAR_WIDTH * pwidth;
				CG_DrawStringExt(xx, y,
								 p, hcolor, qfalse, qfalse, TINYCHAR_WIDTH, TINYCHAR_HEIGHT,
								 TEAM_OVERLAY_MAXLOCATION_WIDTH);
			}

			// [QL] the ice colour stays on a frozen row: CG_GetColorForHealth
			// would paint a statue critical-red off its zero health, and red is
			// already what a dying teammate looks like
			if (!ci->frozen) {
				CG_GetColorForHealth(ci->health, ci->armor, hcolor);
			}
			Com_sprintf(st, sizeof(st), "%3i %3i", ci->health, ci->armor);

			xx = x + TINYCHAR_WIDTH * 3 +
				TINYCHAR_WIDTH * pwidth + TINYCHAR_WIDTH * lwidth;

			CG_DrawStringExt(xx, y,
							 st, hcolor, qfalse, qfalse,
							 TINYCHAR_WIDTH, TINYCHAR_HEIGHT, 0);

			// draw weapon icon
			xx += TINYCHAR_WIDTH * 3;

			/*
			[QL] Not for a statue.

			The icon sits three characters into a field sized for "%3i %3i",
			which lands it in the gap between health and armour. FROZEN is six
			characters with no gap, so the icon was drawn straight over the Z.

			Skipping it is the right fix rather than moving the text, because
			there is nothing to draw either way: PM_Weapon sets ps.weapon to
			WP_NONE while health is <= 0, and a frozen player's health is 0, so
			curWeapon is WP_NONE and the icon falls through to deferShader - the
			red no-entry circle that was sitting on the word. What weapon a
			statue is holding is not information anyone needs.
			*/
			if (!ci->frozen) {
				if (CG_WeaponInfo(ci->curWeapon)->weaponIcon) {
					CG_DrawPic(xx, y, TINYCHAR_WIDTH, TINYCHAR_HEIGHT,
							   CG_WeaponInfo(ci->curWeapon)->weaponIcon);
				} else {
					CG_DrawPic(xx, y, TINYCHAR_WIDTH, TINYCHAR_HEIGHT,
							   cgs.media.deferShader);
				}
			}

			// Draw powerup icons
			if (right) {
				xx = x;
			} else {
				xx = x + w - TINYCHAR_WIDTH;
			}
			for (j = 0; j <= PW_NUM_POWERUPS; j++) {
				if (ci->powerups & (1 << j)) {
					item = BG_FindItemForPowerup(j);

					if (item) {
						CG_DrawPic(xx, y, TINYCHAR_WIDTH, TINYCHAR_HEIGHT,
								   trap_R_RegisterShader(item->icon));
						if (right) {
							xx -= TINYCHAR_WIDTH;
						} else {
							xx += TINYCHAR_WIDTH;
						}
					}
				}
			}

			y += TINYCHAR_HEIGHT;
		}
	}

	return ret_y;
	// #endif
}

/*
=====================
CG_DrawSpeedometer

[QL] Draws the speedometer bar graph and text.
Modes: 1 = icon+bars+text (right), 2 = bars+text (center), 3 = text only (center)
Mode 4 is handled by the menu ownerdraw (cg_newdraw.c), not here.
=====================
*/
static void CG_DrawSpeedometer(void) {
	int mode = cg_speedometer.integer;
	int i, idx;
	float rectX, rectY, rectW, rectH;
	float barScale, barWidth, halfHeight;
	float speed, barHeight, barX, barY, fullBarH, overflowH, totalH;
	int graphX, graphY;
	int baseSpeed;
	const char* text;

	if (!cg.snap) return;

	baseSpeed = cg.snap->ps.speed;
	if (baseSpeed <= 0) baseSpeed = 320;

	// set positions based on mode
	if (mode < 2) {
		// mode 1: right-aligned, icon + bars + text
		rectX = 592.0f;
		rectY = 384.0f;
		rectW = 48.0f;
		rectH = 48.0f;
		graphX = 592;
		graphY = 384;
		cg.speedBarColor1[3] = 1.0f;
		cg.speedBarColor2[3] = 1.0f;
		CG_SetWidescreen(WIDESCREEN_RIGHT);
	} else {
		// modes 2-3: centered
		rectX = 256.0f;
		graphY = cg_crosshairY.integer / 2 + 241;
		rectY = (float)graphY;
		rectW = 128.0f;
		rectH = 32.0f;
		graphX = 256;
		cg.speedBarColor1[3] = 0.75f;
		cg.speedBarColor2[3] = 0.75f;
		CG_SetWidescreen(WIDESCREEN_CENTER);
	}

	// adjust coordinates to screen space
	CG_AdjustFrom640(&rectX, &rectY, &rectW, &rectH);

	halfHeight = rectH * 0.5f;
	barScale = (rectH - 5.0f) / ((float)baseSpeed * 3.0f);
	barWidth = rectW / (float)SPEED_HISTORY_SIZE;

	// mode 1: draw background (lagometer shader, same as binary)
	if (mode < 2) {
		trap_R_SetColor(NULL);
		CG_DrawPic((float)graphX, (float)graphY, 48.0f, 48.0f, cgs.media.lagometerShader);
	}

	// modes 1-2: draw bar graph
	if (mode < 3 && cg.speedHistoryCount > 0) {
		idx = cg.speedHistoryIndex + 1;
		if (idx > SPEED_HISTORY_SIZE - 1) idx = 0;

		for (i = 0; i < cg.speedHistoryCount; i++) {
			speed = cg.speedHistory[idx];
			if (speed > 0.0f) {
				barHeight = barScale * speed;
				if (barHeight > halfHeight) barHeight = halfHeight;

				// lower half bar (green)
				trap_R_SetColor(cg.speedBarColor1);
				barX = rectX + barWidth * (float)i;
				barY = (rectY + rectH) - barHeight;
				trap_R_DrawStretchPic(barX, barY, barWidth, barHeight,
									  0, 0, 0, 0, cgs.media.whiteShader);

				// overflow bar (yellow) - when speed exceeds half
				fullBarH = barScale * speed;
				if (fullBarH > halfHeight) {
					overflowH = fullBarH - barHeight;
					if (overflowH > halfHeight) overflowH = halfHeight;
					totalH = fullBarH;
					if (totalH > rectH) totalH = rectH;
					trap_R_SetColor(cg.speedBarColor2);
					barY = (rectY + rectH) - totalH;
					trap_R_DrawStretchPic(barX, barY, barWidth, overflowH,
										  0, 0, 0, 0, cgs.media.whiteShader);
				}
			}

			idx++;
			if (idx > SPEED_HISTORY_SIZE - 1) idx = 0;
		}
	}

	// text: draw current speed value below the bar area
	{
		int speedInt = (int)cg.speedHistory[cg.speedHistoryIndex];
		int textX, textY;
		float tw;

		text = va("%d", speedInt);
		tw = CG_Text_Width(text, 0.175f, 0);

		if (mode < 2) {
			// mode 1: right-aligned, text below 48px bar area
			CG_SetWidescreen(WIDESCREEN_RIGHT);
			textX = graphX + 2;
			textY = graphY + 8;
		} else {
			// modes 2-3: centered text below 32px bar area
			textX = 320 - (int)(tw / 2);
			textY = graphY + 32 + 1;
		}

		CG_Text_Paint((float)textX, (float)textY,
					  0.15f, colorWhite, text, 0, 0, ITEM_TEXTSTYLE_SHADOWED);
	}

	trap_R_SetColor(NULL);
	CG_SetWidescreen(WIDESCREEN_STRETCH);
}

/*
=====================
CG_DrawUpperRight

=====================
*/
static void CG_DrawUpperRight(stereoFrame_t stereoFrame) {
	float y;

	y = 0;

	if (cgs.gametype >= GT_TEAM && cg_drawTeamOverlay.integer == 1) {
		y = CG_DrawTeamOverlay(y, qtrue, qtrue);
	}
	if (cg_drawSnapshot.integer) {
		y = CG_DrawSnapshot(y);
	}
	if (cg_drawFPS.integer && (stereoFrame == STEREO_CENTER || stereoFrame == STEREO_RIGHT)) {
		y = CG_DrawFPS(y);
	}
	if (cg_drawTimer.integer) {
		y = CG_DrawTimer(y);
	}
	if (cg_drawAttacker.integer) {
		CG_DrawAttacker(y);
	}
	// [QL] speedometer - bar graph + text drawn for modes 1-3
	// (mode 4 is text-only via menu ownerdraw, not drawn here)
	if (cg_speedometer.integer > 0 && cg_speedometer.integer < 4) {
		CG_DrawSpeedometer();
	}
}

/*
===========================================================================================

  LOWER RIGHT CORNER

===========================================================================================
*/

/*
===================
CG_DrawReward
===================
*/
static void CG_DrawReward(void) {
	float* color;
	int i, count;
	float x, y;
	char buf[32];

	if (!cg_drawRewards.integer) {
		return;
	}

	color = CG_FadeColor(cg.rewardTime, REWARD_TIME);
	if (!color) {
		if (cg.rewardStack > 0) {
			for (i = 0; i < cg.rewardStack; i++) {
				cg.rewardSound[i] = cg.rewardSound[i + 1];
				cg.rewardShader[i] = cg.rewardShader[i + 1];
				cg.rewardCount[i] = cg.rewardCount[i + 1];
			}
			cg.rewardTime = cg.time;
			cg.rewardStack--;
			color = CG_FadeColor(cg.rewardTime, REWARD_TIME);
			trap_S_StartLocalSound(cg.rewardSound[0], CHAN_ANNOUNCER);
		} else {
			return;
		}
	}

	trap_R_SetColor(color);

	if (cg.rewardCount[0] >= 10) {
		y = 56;
		x = 320 - ICON_SIZE / 2;
		CG_DrawPic(x, y, ICON_SIZE - 4, ICON_SIZE - 4, cg.rewardShader[0]);
		Com_sprintf(buf, sizeof(buf), "%d", cg.rewardCount[0]);
		{
			float tw = CG_Text_Width(buf, 0.22f, 0);
			CG_Text_Paint((SCREEN_WIDTH - tw) / 2, y + ICON_SIZE + 12, 0.22f, color, buf, 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);
		}
	} else {
		count = cg.rewardCount[0];

		y = 56;
		x = 320 - count * ICON_SIZE / 2;
		for (i = 0; i < count; i++) {
			CG_DrawPic(x, y, ICON_SIZE - 4, ICON_SIZE - 4, cg.rewardShader[0]);
			x += ICON_SIZE;
		}
	}
	trap_R_SetColor(NULL);
}

/*
===============================================================================

LAGOMETER

===============================================================================
*/

#define LAG_SAMPLES 128

typedef struct {
	int frameSamples[LAG_SAMPLES];
	int frameCount;
	int snapshotFlags[LAG_SAMPLES];
	int snapshotSamples[LAG_SAMPLES];
	int snapshotCount;
} lagometer_t;

lagometer_t lagometer;

/*
==============
CG_AddLagometerFrameInfo

Adds the current interpolate / extrapolate bar for this frame
==============
*/
void CG_AddLagometerFrameInfo(void) {
	int offset;

	offset = cg.time - cg.latestSnapshotTime;
	lagometer.frameSamples[lagometer.frameCount & (LAG_SAMPLES - 1)] = offset;
	lagometer.frameCount++;
}

/*
==============
CG_AddLagometerSnapshotInfo

Each time a snapshot is received, log its ping time and
the number of snapshots that were dropped before it.

Pass NULL for a dropped packet.
==============
*/
void CG_AddLagometerSnapshotInfo(snapshot_t* snap) {
	// dropped packet
	if (!snap) {
		lagometer.snapshotSamples[lagometer.snapshotCount & (LAG_SAMPLES - 1)] = -1;
		lagometer.snapshotCount++;
		return;
	}

	// add this snapshot's info
	lagometer.snapshotSamples[lagometer.snapshotCount & (LAG_SAMPLES - 1)] = snap->ping;
	lagometer.snapshotFlags[lagometer.snapshotCount & (LAG_SAMPLES - 1)] = snap->snapFlags;
	lagometer.snapshotCount++;
}

/*
==============
CG_DrawDisconnect

Should we draw something differnet for long lag vs no packets?
==============
*/
static void CG_DrawDisconnect(void) {
	float x, y;
	int cmdNum;
	usercmd_t cmd;
	const char* s;
	int w;

	// draw the phone jack if we are completely past our buffers
	cmdNum = trap_GetCurrentCmdNumber() - CMD_BACKUP + 1;
	trap_GetUserCmd(cmdNum, &cmd);
	if (cmd.serverTime <= cg.snap->ps.commandTime || cmd.serverTime > cg.time) {  // special check for map_restart
		return;
	}

	// also add text in center of screen
	s = "Connection Interrupted";
	w = CG_Text_Width(s, 0.5f, 0);
	CG_Text_Paint(320 - w / 2, 116, 0.5f, colorWhite, s, 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);

	// blink the icon
	if ((cg.time >> 9) & 1) {
		return;
	}

	x = SCREEN_WIDTH - 48;
	y = SCREEN_HEIGHT - 144;

	CG_DrawPic(x, y, 48, 48, trap_R_RegisterShader("gfx/2d/net.jpg"));
}

#define MAX_LAGOMETER_PING 900
#define MAX_LAGOMETER_RANGE 300

/*
==============
CG_DrawLagometer
==============
*/
static void CG_DrawLagometer(void) {
	int a, x, y, i;
	float v;
	float ax, ay, aw, ah, mid, range;
	int color;
	float vscale;

	if (!cg_lagometer.integer || cg.demoPlayback) {
		CG_DrawDisconnect();
		return;
	}

	//
	// draw the graph
	//
	x = SCREEN_WIDTH - 48;
	y = SCREEN_HEIGHT - 144;

	trap_R_SetColor(NULL);
	CG_DrawPic(x, y, 48, 48, cgs.media.lagometerShader);

	ax = x;
	ay = y;
	aw = 48;
	ah = 48;
	CG_AdjustFrom640(&ax, &ay, &aw, &ah);

	color = -1;
	range = ah / 3;
	mid = ay + range;

	vscale = range / MAX_LAGOMETER_RANGE;

	// draw the frame interpoalte / extrapolate graph
	for (a = 0; a < aw; a++) {
		i = (lagometer.frameCount - 1 - a) & (LAG_SAMPLES - 1);
		v = lagometer.frameSamples[i];
		v *= vscale;
		if (v > 0) {
			if (color != 1) {
				color = 1;
				trap_R_SetColor(g_color_table[ColorIndex(COLOR_YELLOW)]);
			}
			if (v > range) {
				v = range;
			}
			trap_R_DrawStretchPic(ax + aw - a, mid - v, 1, v, 0, 0, 0, 0, cgs.media.whiteShader);
		} else if (v < 0) {
			if (color != 2) {
				color = 2;
				trap_R_SetColor(g_color_table[ColorIndex(COLOR_BLUE)]);
			}
			v = -v;
			if (v > range) {
				v = range;
			}
			trap_R_DrawStretchPic(ax + aw - a, mid, 1, v, 0, 0, 0, 0, cgs.media.whiteShader);
		}
	}

	// draw the snapshot latency / drop graph
	range = ah / 2;
	vscale = range / MAX_LAGOMETER_PING;

	for (a = 0; a < aw; a++) {
		i = (lagometer.snapshotCount - 1 - a) & (LAG_SAMPLES - 1);
		v = lagometer.snapshotSamples[i];
		if (v > 0) {
			if (lagometer.snapshotFlags[i] & SNAPFLAG_RATE_DELAYED) {
				if (color != 5) {
					color = 5;  // YELLOW for rate delay
					trap_R_SetColor(g_color_table[ColorIndex(COLOR_YELLOW)]);
				}
			} else {
				if (color != 3) {
					color = 3;
					trap_R_SetColor(g_color_table[ColorIndex(COLOR_GREEN)]);
				}
			}
			v = v * vscale;
			if (v > range) {
				v = range;
			}
			trap_R_DrawStretchPic(ax + aw - a, ay + ah - v, 1, v, 0, 0, 0, 0, cgs.media.whiteShader);
		} else if (v < 0) {
			if (color != 4) {
				color = 4;  // RED for dropped snapshots
				trap_R_SetColor(g_color_table[ColorIndex(COLOR_RED)]);
			}
			trap_R_DrawStretchPic(ax + aw - a, ay + ah - range, 1, range, 0, 0, 0, 0, cgs.media.whiteShader);
		}
	}

	trap_R_SetColor(NULL);

	if (cg_nopredict.integer) {
		CG_Text_Paint(x, y + 14, 0.3f, colorWhite, "snc", 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);
	}

	// [QL] draw ping number when cg_lagometer == 2
	if (cg_lagometer.integer == 2 && cg.snap) {
		static int lastPing;
		const char* text;
		int textX, textY;

		if (cg.snap->ping) {
			lastPing = cg.snap->ping;
		}
		text = va("%d", lastPing);
		textX = x + 2;
		textY = y + 8;
		CG_Text_Paint((float)textX, (float)textY,
					  0.15f, colorWhite, text, 0, 0, ITEM_TEXTSTYLE_SHADOWED);
	}

	CG_DrawDisconnect();
}

/*
===============================================================================

CENTER PRINTING

===============================================================================
*/

/*
==============
CG_CenterPrint

Called for important messages that should stay in the center of the screen
for a few moments
==============
*/
void CG_CenterPrint(const char* str, int y, int charWidth) {
	char* s;

	Q_strncpyz(cg.centerPrint, str, sizeof(cg.centerPrint));

	cg.centerPrintTime = cg.time;
	cg.centerPrintY = y;
	cg.centerPrintCharWidth = charWidth;

	// count the number of lines for centering
	cg.centerPrintLines = 1;
	s = cg.centerPrint;
	while (*s) {
		if (*s == '\n')
			cg.centerPrintLines++;
		s++;
	}
}

/*
===================
CG_DrawCenterString
===================
*/
static void CG_DrawCenterString(void) {
	char* start;
	int l;
	int x, y, w;

	int h;

	float* color;

	if (!cg.centerPrintTime) {
		return;
	}

	color = CG_FadeColor(cg.centerPrintTime, 1000 * cg_centertime.value);
	if (!color) {
		return;
	}

	trap_R_SetColor(color);

	start = cg.centerPrint;

	y = cg.centerPrintY - cg.centerPrintLines * BIGCHAR_HEIGHT / 2;

	while (1) {
		char linebuffer[1024];

		for (l = 0; l < 50; l++) {
			if (!start[l] || start[l] == '\n') {
				break;
			}
			linebuffer[l] = start[l];
		}
		linebuffer[l] = 0;

		w = CG_Text_Width(linebuffer, 0.5, 0);
		h = CG_Text_Height(linebuffer, 0.5, 0);
		x = (SCREEN_WIDTH - w) / 2;
		CG_Text_Paint(x, y + h, 0.5, color, linebuffer, 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);
		y += h + 6;

		while (*start && (*start != '\n')) {
			start++;
		}
		if (!*start) {
			break;
		}
		start++;
	}

	trap_R_SetColor(NULL);
}

/*
================================================================================

CROSSHAIR

================================================================================
*/

/*
=================
CG_DrawCrosshair
=================
*/
static void CG_DrawCrosshair(void) {
	float w, h;
	qhandle_t hShader;
	float f;
	float x, y;
	int ca;

	if (!cg_drawCrosshair.integer) {
		return;
	}

	if (cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR) {
		return;
	}

	if (cg.renderingThirdPerson) {
		return;
	}

	// set color based on health
	if (cg_crosshairHealth.integer) {
		vec4_t hcolor;

		CG_ColorForHealth(hcolor);
		trap_R_SetColor(hcolor);
	} else {
		trap_R_SetColor(NULL);
	}

	w = h = cg_crosshairSize.value;

	// pulse the size of the crosshair when picking up items
	f = cg.time - cg.itemPickupBlendTime;
	if (f > 0 && f < ITEM_BLOB_TIME) {
		f /= ITEM_BLOB_TIME;
		w *= (1 + f);
		h *= (1 + f);
	}

	x = cg_crosshairX.integer;
	y = cg_crosshairY.integer;
	CG_AdjustFrom640(&x, &y, &w, &h);

	ca = cg_drawCrosshair.integer;
	if (ca < 0) {
		ca = 0;
	}
	hShader = cgs.media.crosshairShader[ca % NUM_CROSSHAIRS];

	trap_R_DrawStretchPic(x + cg.refdef.x + 0.5 * (cg.refdef.width - w),
						  y + cg.refdef.y + 0.5 * (cg.refdef.height - h),
						  w, h, 0, 0, 1, 1, hShader);

	trap_R_SetColor(NULL);
}

/*
=================
CG_DrawCrosshair3D
=================
*/
static void CG_DrawCrosshair3D(void) {
	float w;
	qhandle_t hShader;
	float f;
	int ca;

	trace_t trace;
	vec3_t endpos;
	float stereoSep, zProj, maxdist, xmax;
	char rendererinfos[128];
	refEntity_t ent;

	if (!cg_drawCrosshair.integer) {
		return;
	}

	if (cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR) {
		return;
	}

	if (cg.renderingThirdPerson) {
		return;
	}

	w = cg_crosshairSize.value;

	// pulse the size of the crosshair when picking up items
	f = cg.time - cg.itemPickupBlendTime;
	if (f > 0 && f < ITEM_BLOB_TIME) {
		f /= ITEM_BLOB_TIME;
		w *= (1 + f);
	}

	ca = cg_drawCrosshair.integer;
	if (ca < 0) {
		ca = 0;
	}
	hShader = cgs.media.crosshairShader[ca % NUM_CROSSHAIRS];

	// Use a different method rendering the crosshair so players don't see two of them when
	// focusing their eyes at distant objects with high stereo separation
	// We are going to trace to the next shootable object and place the crosshair in front of it.

	// first get all the important renderer information
	trap_Cvar_VariableStringBuffer("r_zProj", rendererinfos, sizeof(rendererinfos));
	zProj = atof(rendererinfos);
	trap_Cvar_VariableStringBuffer("r_stereoSeparation", rendererinfos, sizeof(rendererinfos));
	stereoSep = zProj / atof(rendererinfos);

	xmax = zProj * tan(cg.refdef.fov_x * M_PI / 360.0f);

	// let the trace run through until a change in stereo separation of the crosshair becomes less than one pixel.
	maxdist = cgs.glconfig.vidWidth * stereoSep * zProj / (2 * xmax);
	VectorMA(cg.refdef.vieworg, maxdist, cg.refdef.viewaxis[0], endpos);
	CG_Trace(&trace, cg.refdef.vieworg, NULL, NULL, endpos, 0, MASK_SHOT);

	memset(&ent, 0, sizeof(ent));
	ent.reType = RT_SPRITE;
	ent.renderfx = RF_DEPTHHACK | RF_CROSSHAIR;

	VectorCopy(trace.endpos, ent.origin);

	// scale the crosshair so it appears the same size for all distances
	ent.radius = w / 640 * xmax * trace.fraction * maxdist / zProj;
	ent.customShader = hShader;

	trap_R_AddRefEntityToScene(&ent);
}

/*
=================
CG_ScanForCrosshairEntity
=================
*/
static void CG_ScanForCrosshairEntity(void) {
	trace_t trace;
	vec3_t start, end;
	int content;

	VectorCopy(cg.refdef.vieworg, start);
	VectorMA(start, 131072, cg.refdef.viewaxis[0], end);

	CG_Trace(&trace, start, vec3_origin, vec3_origin, end,
			 cg.snap->ps.clientNum, CONTENTS_SOLID | CONTENTS_BODY);
	if (trace.entityNum >= MAX_CLIENTS) {
		return;
	}

	// if the player is in fog, don't show it
	content = CG_PointContents(trace.endpos, 0);
	if (content & CONTENTS_FOG) {
		return;
	}

	// if the player is invisible, don't show it
	if (cg_entities[trace.entityNum].currentState.powerups & (1 << PW_INVIS)) {
		return;
	}

	// update the fade timer
	cg.crosshairClientNum = trace.entityNum;
	cg.crosshairClientTime = cg.time;
}

/*
=====================
CG_DrawCrosshairNames
=====================
*/
static void CG_DrawCrosshairNames(void) {
	float* color;
	char* name;
	float w;

	if (!cg_drawCrosshair.integer) {
		return;
	}
	if (!cg_drawCrosshairNames.integer) {
		return;
	}
	if (cg.renderingThirdPerson) {
		return;
	}

	// scan the known entities to see if the crosshair is sighted on one
	CG_ScanForCrosshairEntity();

	// draw the name of the player being looked at
	color = CG_FadeColor(cg.crosshairClientTime, 1000);
	if (!color) {
		trap_R_SetColor(NULL);
		return;
	}

	name = cgs.clientinfo[cg.crosshairClientNum].name;
	color[3] *= 0.5f;
	w = CG_Text_Width(name, 0.3f, 0);
	CG_Text_Paint(320 - w / 2, 190, 0.3f, color, name, 0, 0, ITEM_TEXTSTYLE_SHADOWED);
	trap_R_SetColor(NULL);
}

//==============================================================================

/*
=================
CG_DrawSpectator

[QL] Rewritten to match binary (cgamex86.dll 0x10034d70).
- CA/AD eliminated players: "Round In Progress" centered at y=60
- Pure spectators: "SPECTATOR MODE" + "Press mouse button 1..." header,
  then left-aligned "waiting to play" (duel) or "press ESC..." (team)
=================
*/
static void CG_DrawSpectator(void) {
	float scale, w, y;
	const char* s;
	vec4_t grayColor = {0.73f, 0.73f, 0.73f, 0.7f};
	char attackKey[32], menuKey[32];

	// [QL] CA/AD: show "Round In Progress" for eliminated players (on a team but spectating)
	if ((cgs.gametype == GT_CA || cgs.gametype == GT_AD) &&
		cg.snap->ps.pm_type == PM_SPECTATOR &&
		cg.snap->ps.persistant[PERS_TEAM] != TEAM_SPECTATOR) {
		scale = 0.35f;
		s = "Round In Progress";
		CG_SetWidescreen(WIDESCREEN_CENTER);
		w = CG_Text_Width(s, scale, 0);
		CG_Text_Paint(320 - w / 2, 60, scale, colorWhite, s, 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);
		return;
	}

	CG_KeyNameForCommand("+attack", attackKey, sizeof(attackKey));
	CG_KeyNameForCommand("togglemenu", menuKey, sizeof(menuKey));

	// "SPECTATOR MODE" header
	scale = 0.22f;
	s = "SPECTATOR MODE";
	w = CG_Text_Width(s, scale, 0);
	CG_Text_Paint(320 - w / 2, 440, scale, colorWhite, s, 0, 0, 0);

	// "Press <key> to cycle through players"
	y = 452;
	s = va("Press %s to cycle through players", attackKey);
	w = CG_Text_Width(s, 0.18f, 0);
	CG_Text_Paint(320 - w / 2, y, 0.18f, grayColor, s, 0, 0, 0);

	// Bottom hints - left-aligned, skip when following a player
	if (!(cg.snap->ps.pm_flags & PMF_FOLLOW)) {
		CG_SetWidescreen(WIDESCREEN_LEFT);
		if (cgs.gametype == GT_DUEL) {
			CG_Text_Paint(20, 461, 0.28f, colorWhite, "waiting to play", 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);
		} else if (cgs.gametype >= GT_TEAM) {
			s = va("press %s and use the JOIN buttons", menuKey);
			CG_Text_Paint(20, 453, 0.28f, colorWhite, s, 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);
			CG_Text_Paint(20, 470, 0.28f, colorWhite, "to enter the game", 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);
		}
	}
}

/*
=================
CG_DrawVote
=================
*/
static void CG_DrawVote(void) {
	char* s;
	int sec;

	// [QL] team-kill complaint prompt (binary CG_DrawVote 0x1000dc00). Only in team
	// gametypes (>= GT_TEAM, excluding RR); replaces the vote line while pending.
	if (cgs.gametype >= GT_TEAM && cgs.gametype != GT_RR &&
	    cg_complaintWarning.integer && cg.complaintEndTime > cg.time && !cg.demoPlayback) {
		if (cg.complaintClient >= 0 && cg.complaintClient < MAX_CLIENTS) {
			char yesKey[32], noKey[32];
			int csec = (cg.complaintEndTime - cg.time) / 1000;
			if (csec < 0) {
				csec = 0;
			}
			CG_KeyNameForCommand("vote yes", yesKey, sizeof(yesKey));
			CG_KeyNameForCommand("vote no", noKey, sizeof(noKey));
			s = va("File complaint against %s for team-killing?", cgs.clientinfo[cg.complaintClient].name);
			CG_Text_Paint(4, 300, 0.22f, colorYellow, s, 0, 0, ITEM_TEXTSTYLE_SHADOWED);
			s = va("Press '%s' for Yes, or '%s' for No (%is)", yesKey, noKey, csec);
			CG_Text_Paint(8, 312, 0.22f, colorWhite, s, 0, 0, ITEM_TEXTSTYLE_SHADOWED);
			return;
		}
		// negative status codes (binary strings, incl. the "Comlaints" typo)
		s = NULL;
		switch (cg.complaintClient) {
			case -1: s = "Your complaint has been filed."; break;
			case -2: s = "Your complaint has been dismissed."; break;
			case -3: s = "Comlaints cannot be filed against server admins."; break;
			case -4: s = "You received friendly fire from a server admin."; break;
			default: break;
		}
		if (s) {
			CG_Text_Paint(3, 300, 0.22f, colorWhite, s, 0, 0, ITEM_TEXTSTYLE_SHADOWED);
			return;
		}
	}

	if (!cgs.voteTime) {
		return;
	}

	// play a talk beep whenever it is modified
	if (cgs.voteModified) {
		cgs.voteModified = qfalse;
		trap_S_StartLocalSound(cgs.media.talkSound, CHAN_LOCAL_SOUND);
	}

	// [QL] Vote countdown: binary uses (voteTime - cg.time + 30000) / 1000
	sec = (cgs.voteTime - cg.time + VOTE_TIME) / 1000;
	if (sec < 0) {
		sec = 0;
	}
	// [QL] Show key bindings in vote text, Y=300/312 (binary-verified)
	s = va("VOTE(%is):%s yes:%i no:%i", sec, cgs.voteString, cgs.voteYes, cgs.voteNo);
	CG_Text_Paint(4, 300, 0.22f, colorYellow, s, 0, 0, ITEM_TEXTSTYLE_SHADOWED);
	{
		char menuKey[32];
		CG_KeyNameForCommand("togglemenu", menuKey, sizeof(menuKey));
		s = va("or press %s then click Vote", menuKey);
	}
	CG_Text_Paint(8, 312, 0.22f, colorWhite, s, 0, 0, ITEM_TEXTSTYLE_SHADOWED);
}

/*
=================
CG_DrawTeamVote
=================
*/
static void CG_DrawTeamVote(void) {
	char* s;
	int sec, cs_offset;

	if (cgs.clientinfo[cg.clientNum].team == TEAM_RED)
		cs_offset = 0;
	else if (cgs.clientinfo[cg.clientNum].team == TEAM_BLUE)
		cs_offset = 1;
	else
		return;

	if (!cgs.teamVoteTime[cs_offset]) {
		return;
	}

	// play a talk beep whenever it is modified
	if (cgs.teamVoteModified[cs_offset]) {
		cgs.teamVoteModified[cs_offset] = qfalse;
		trap_S_StartLocalSound(cgs.media.talkSound, CHAN_LOCAL_SOUND);
	}

	sec = (VOTE_TIME - (cg.time - cgs.teamVoteTime[cs_offset])) / 1000;
	if (sec < 0) {
		sec = 0;
	}
	s = va("TEAMVOTE(%is):%s yes:%i no:%i", sec, cgs.teamVoteString[cs_offset],
		   cgs.teamVoteYes[cs_offset], cgs.teamVoteNo[cs_offset]);
	CG_Text_Paint(4, 324, 0.22f, colorYellow, s, 0, 0, ITEM_TEXTSTYLE_SHADOWED);
}

/*
[QL] Drop the scoreboard's player list clear of its column headers.

Item_ListBox_Paint draws the first row at rect.y + 1. Quake Live's scoreboard
menus put the PLAYER / SCORE / K/D / THAWS labels *inside* the top of the list
rect - in ingame_scoreboard_ft.menu the labels sit at y 170 and the list box is
"rect 73 165 284 130" - so row one is painted over the header band.

Quake Live's own list box evidently starts its content below that. Rather than
change Item_ListBox_Paint, which every list in the game shares (server browser,
demo list, map list), this nudges only the scoreboard feeders: move the top down
by one element height and take the same amount off the height, so the bottom
edge and the scroll bar stay where the menu put them.

Applied once per menu. Menus_FindByName hands back the same menuDef every time,
so without the guard a gametype change would stack a second offset.
*/
static void CG_OffsetScoreboardList(menuDef_t *menu) {
	static const menuDef_t *adjusted[16];
	static int numAdjusted;
	int i;

	if (!menu) {
		return;
	}
	for (i = 0; i < numAdjusted; i++) {
		if (adjusted[i] == menu) {
			return;
		}
	}
	if (numAdjusted < (int)ARRAY_LEN(adjusted)) {
		adjusted[numAdjusted++] = menu;
	}

	for (i = 0; i < menu->itemCount; i++) {
		itemDef_t *item = menu->items[i];
		listBoxDef_t *listPtr;
		float shift;

		if (item->special != FEEDER_SCOREBOARD &&
			item->special != FEEDER_ENDSCOREBOARD &&
			item->special != FEEDER_REDTEAM_LIST &&
			item->special != FEEDER_BLUETEAM_LIST) {
			continue;
		}

		listPtr = (listBoxDef_t *)item->typeData;
		if (!listPtr || listPtr->elementHeight <= 0) {
			continue;
		}

		/*
		[QL] How far down is a matter of taste and of the menu being drawn, and
		I cannot see the result from here - one element height overshot and left
		a visible gap. So it is a cvar rather than another guess: raise
		cg_scoreboardListOffset to push the rows down, lower it to bring them up,
		0 to leave the menu's own geometry alone.
		*/
		shift = cg_scoreboardListOffset.value;
		if (shift < 0.0f) {
			shift = 0.0f;
		}
		if (shift >= item->window.rect.h) {
			continue;   // nothing left to show
		}

		// [QL] the left-hand team's bar belongs on the outside, so the two
		// lists mirror each other instead of both crowding the middle
		if (item->special == FEEDER_REDTEAM_LIST) {
			item->window.flags |= WINDOW_LB_LEFTSCROLL;
		}

		item->window.rect.y += shift;
		item->window.rect.h -= shift;
	}
}

// [QL] Set both in-game and end-of-game scoreboard menus by gametype
void CG_SetEndScoreboardMenu(void) {
	switch (cgs.gametype) {
	case GT_FFA:        menuScoreboard = Menus_FindByName("score_menu_ffa");         menuEndScoreboard = Menus_FindByName("endscore_menu_ffa"); break;
	case GT_DUEL:       menuScoreboard = Menus_FindByName("score_menu_duel");        menuEndScoreboard = Menus_FindByName("endscore_menu_duel"); break;
	case GT_RACE:       menuScoreboard = Menus_FindByName("score_menu_race");        menuEndScoreboard = Menus_FindByName("endscore_menu_race"); break;
	case GT_RR:         menuScoreboard = Menus_FindByName("score_menu_rr");          menuEndScoreboard = Menus_FindByName("endscore_menu_rr"); break;
	case GT_TEAM:       menuScoreboard = Menus_FindByName("teamscore_menu_tdm");     menuEndScoreboard = Menus_FindByName("endteamscore_menu_tdm"); break;
	case GT_CA:         menuScoreboard = Menus_FindByName("teamscore_menu_ca");      menuEndScoreboard = Menus_FindByName("endteamscore_menu_ca"); break;
	case GT_CTF:        menuScoreboard = Menus_FindByName("teamscore_menu_ctf");     menuEndScoreboard = Menus_FindByName("endteamscore_menu_ctf"); break;
	case GT_1FCTF:      menuScoreboard = Menus_FindByName("teamscore_menu_1fctf");   menuEndScoreboard = Menus_FindByName("endteamscore_menu_1fctf"); break;
	case GT_HARVESTER:  menuScoreboard = Menus_FindByName("teamscore_menu_har");     menuEndScoreboard = Menus_FindByName("endteamscore_menu_har"); break;
	case GT_FREEZE:     menuScoreboard = Menus_FindByName("teamscore_menu_ft");      menuEndScoreboard = Menus_FindByName("endteamscore_menu_ft"); break;
	case GT_DOMINATION: menuScoreboard = Menus_FindByName("teamscore_menu_dom");     menuEndScoreboard = Menus_FindByName("endteamscore_menu_dom"); break;
	case GT_AD:         menuScoreboard = Menus_FindByName("teamscore_menu_ad");      menuEndScoreboard = Menus_FindByName("endteamscore_menu_ad"); break;
	default:            menuScoreboard = Menus_FindByName("teamscore_menu");         menuEndScoreboard = Menus_FindByName("endteamscore_menu"); break;
	}

	CG_OffsetScoreboardList(menuScoreboard);
	CG_OffsetScoreboardList(menuEndScoreboard);
}

/*
[QL] Keep the highlight and the visible page on the local player.

Run every frame the scoreboard is drawn, not once when it opens. The list
re-sorts as scores change, so a row index recorded at open drifts onto whoever
happens to be standing there a few frags later - which is how the highlight
ended up on another player's line and the view opened on a page the local player
was not on. Scrolling by hand takes the view back (cg.scoreboardScrolled); the
highlight keeps following.

CG_SetScoreSelection cannot do this job: it goes through
Menu_SetFeederSelection, which resets startPos whenever the index is zero, so a
player in first place could never scroll away.
*/
static void CG_TrackLocalPlayerOnScoreboard(menuDef_t *menu) {
	int i, index = -1, teamIndex = 0;
	int feeder;

	if (!cg.snap) {
		return;
	}

	// The team scoreboards split the players across two lists, so the row a
	// player sits on is their position within their own team's list, not their
	// position in cg.scores.
	for (i = 0; i < cg.numScores; i++) {
		if (cg.scores[i].client != cg.snap->ps.clientNum) {
			continue;
		}
		index = i;
		break;
	}
	if (index < 0) {
		return;
	}
	for (i = 0; i < index; i++) {
		if (cg.scores[i].team == cg.scores[index].team) {
			teamIndex++;
		}
	}

	if (cg.scores[index].team == TEAM_RED) {
		feeder = FEEDER_REDTEAM_LIST;
	} else if (cg.scores[index].team == TEAM_BLUE) {
		feeder = FEEDER_BLUETEAM_LIST;
	} else {
		feeder = FEEDER_SCOREBOARD;
		teamIndex = index;
	}

	Menu_SetFeederCursor(menu, feeder, teamIndex);
	if (feeder == FEEDER_SCOREBOARD) {
		Menu_SetFeederCursor(menu, FEEDER_ENDSCOREBOARD, teamIndex);
	}

	// Leave the view where the player put it once they have scrolled.
	if (cg.scoreboardScrolled) {
		return;
	}
	Menu_ShowFeederIndex(menu, feeder, teamIndex);
	if (feeder == FEEDER_SCOREBOARD) {
		Menu_ShowFeederIndex(menu, FEEDER_ENDSCOREBOARD, teamIndex);
	}
}

/*
[QL] Tell the client whether the scoreboard is on screen.

CL_ScoreboardScrollKey needs to know, so that it only takes the wheel and the
page keys away from their bindings while there is a scoreboard for them to
scroll. Written every frame rather than on change: a stuck value would cost the
player their weapon switch, and one Cvar_Set of an unchanged string per frame is
not worth being clever about.
*/
static void CG_PublishScoreboardState(qboolean showing) {
	trap_Cvar_Set("cg_scoreboardActive", showing ? "1" : "0");
}

/*
[QL] Ask the server for scores again while the board is being held.

CG_ScoresDown_f sends "score" on the key press and nothing after it, so a
scoreboard held open showed whatever arrived at the moment it was opened. On a
sixty-player instagib server the numbers are visibly stale within a second, and
holding TAB to watch the match is precisely when they need to move.

Same two-second throttle CG_ScoresDown_f already applies, so this costs no more
than tapping TAB does - it just stops the display freezing while the key is down.
*/
#define SCOREBOARD_REFRESH_TIME 2000

static void CG_RefreshScoreboard(void) {
	if (cg.predictedPlayerState.pm_type == PM_INTERMISSION) {
		return;   // the final scores are not going to change
	}
	if (cg.scoresRequestTime + SCOREBOARD_REFRESH_TIME >= cg.time) {
		return;
	}
	cg.scoresRequestTime = cg.time;
	trap_SendClientCommand("score");
}

static qboolean CG_DrawScoreboardMenu(void) {
	static qboolean firstTime = qtrue;

	if (menuScoreboard) {
		menuScoreboard->window.flags &= ~WINDOW_FORCED;
	}
	if (cg_paused.integer) {
		cg.deferredPlayerLoading = 0;
		firstTime = qtrue;
		return qfalse;
	}

	// don't draw scoreboard during death while warmup up
	if (cg.warmup && !cg.showScores) {
		return qfalse;
	}

	/*
	[QL] A frozen player is not a dead one.

	The scoreboard shows itself automatically while dead, and a Freeze Tag
	statue sits at zero health for its whole life - so it kept popping up
	mid-round. PM_FREEZE is its own pm_type, but there is a frame or two around
	the death where pm_type is still PM_DEAD and PW_FREEZE is already set, which
	is the "sometimes" in the report; the powerup is the reliable test.
	*/
	if (cg.showScores ||
		(cg.predictedPlayerState.pm_type == PM_DEAD &&
		 !cg.predictedPlayerState.powerups[PW_FREEZE]) ||
		cg.predictedPlayerState.pm_type == PM_INTERMISSION) {
	} else {
		if (!CG_FadeColor(cg.scoreFadeTime, FADE_TIME)) {
			// next time scoreboard comes up, don't print killer
			cg.deferredPlayerLoading = 0;
			cg.killerName[0] = 0;
			firstTime = qtrue;
			return qfalse;
		}
	}

	if (menuScoreboard == NULL) {
		CG_SetEndScoreboardMenu();
	}

	// [QL] Use end scoreboard during intermission, in-game scoreboard otherwise
	{
		menuDef_t *activeMenu;
		if (cg.predictedPlayerState.pm_type == PM_INTERMISSION && menuEndScoreboard) {
			activeMenu = menuEndScoreboard;
		} else {
			activeMenu = menuScoreboard;
		}

		if (activeMenu) {
			if (firstTime) {
				cg.scoreboardScrolled = qfalse;
				firstTime = qfalse;
			}
			CG_RefreshScoreboard();
			CG_SetScoreSelection(NULL);
			CG_TrackLocalPlayerOnScoreboard(activeMenu);
			Menu_Paint(activeMenu, qtrue);
		}
	}

	// load any models that have been deferred
	if (++cg.deferredPlayerLoading > 10) {
		CG_LoadDeferredPlayers();
	}

	return qtrue;
}

static qboolean CG_DrawScoreboard(void) {
	qboolean showing = CG_DrawScoreboardMenu();

	CG_PublishScoreboardState(showing);
	return showing;
}

/*
=================
CG_DrawIntermission
=================
*/
static void CG_DrawIntermission(void) {
	cg.scoreFadeTime = cg.time;
	cg.scoreBoardShowing = CG_DrawScoreboard();

	/*
	[QL] Draw the cursor while cgame holds the mouse.

	The ui module draws its own (UI_DrawHandlePic with Assets.cursor) but cgame
	never drew one, so once cgame took the key catcher at intermission the mouse
	moved and hovered - audibly, since items play a sound on mouse-enter - with
	nothing on screen to show where it was.

	cgs.media.cursor is already registered in CG_RegisterGraphics; it just had
	no drawer. Same 32x32 at the same -16 offset the ui uses, so the hotspot
	lands in the middle of the pointer and matches the menus.
	*/
	if (cgs.eventHandling != CGAME_EVENT_NONE) {
		// Prefer whatever the loaded HUD declared in its assets block - that is
		// the cursor the menus themselves use, and it is parsed into
		// cgDC.Assets.cursor by CG_ParseMenu. cgs.media.cursor is the fallback.
		qhandle_t cursor = cgDC.Assets.cursor ? cgDC.Assets.cursor : cgs.media.cursor;

		if (cursor) {
			CG_SetWidescreen(WIDESCREEN_STRETCH);
			CG_DrawPic(cgs.cursorX - 16, cgs.cursorY - 16, 32, 32, cursor);
		}
	}
}

/*
=================
CG_DrawFollow
=================
*/
static qboolean CG_DrawFollow(void) {
	// [QL] Team-specific colors for followed player name (binary-verified)
	static vec4_t teamColorFree = { 1.0f, 1.0f, 1.0f, 1.0f };
	static vec4_t teamColorRed  = { 1.0f, 0.5f, 0.5f, 1.0f };
	static vec4_t teamColorBlue = { 0.5f, 0.75f, 1.0f, 1.0f };
	static vec4_t teamColorSpec = { 0.85f, 0.85f, 0.85f, 1.0f };
	const char* name;
	float *nameColor;

	if (!(cg.snap->ps.pm_flags & PMF_FOLLOW)) {
		return qfalse;
	}

	{
		float w = CG_Text_Width("following", 0.4f, 0);
		CG_Text_Paint(320 - w / 2, 38, 0.4f, colorWhite, "following", 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);
	}

	name = cgs.clientinfo[cg.snap->ps.clientNum].name;

	switch (cgs.clientinfo[cg.snap->ps.clientNum].team) {
	case TEAM_RED:       nameColor = teamColorRed; break;
	case TEAM_BLUE:      nameColor = teamColorBlue; break;
	case TEAM_SPECTATOR: nameColor = teamColorSpec; break;
	default:             nameColor = teamColorFree; break;
	}

	{
		float w = CG_Text_Width(name, 0.6f, 0);
		CG_Text_Paint(320 - w / 2, 70, 0.6f, nameColor, name, 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);
	}

	return qtrue;
}

/*
=================
CG_DrawAmmoWarning
=================
*/
static void CG_DrawAmmoWarning(void) {
	const char* s;

	if (cg_drawAmmoWarning.integer == 0) {
		return;
	}

	if (!cg.lowAmmoWarning) {
		return;
	}

	if (cg.lowAmmoWarning == 2) {
		s = "OUT OF AMMO";
	} else {
		s = "LOW AMMO WARNING";
	}
	{
		float tw = CG_Text_Width(s, 0.4f, 0);
		CG_Text_Paint(320 - tw / 2, 78, 0.4f, colorWhite, s, 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);
	}
}

/*
=================
CG_DrawProxWarning
=================
*/
static void CG_DrawProxWarning(void) {
	char s[32];
	static int proxTime;
	int proxTick;

	if (!(cg.snap->ps.eFlags & EF_TICKING)) {
		proxTime = 0;
		return;
	}

	if (proxTime == 0) {
		proxTime = cg.time;
	}

	proxTick = 10 - ((cg.time - proxTime) / 1000);

	if (proxTick > 0 && proxTick <= 5) {
		Com_sprintf(s, sizeof(s), "INTERNAL COMBUSTION IN: %i", proxTick);
	} else {
		Com_sprintf(s, sizeof(s), "YOU HAVE BEEN MINED");
	}

	{
		float tw = CG_Text_Width(s, 0.4f, 0);
		CG_Text_Paint(320 - tw / 2, 96, 0.4f, g_color_table[ColorIndex(COLOR_RED)], s, 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);
	}
}

/*
=================
CG_DrawWarmupCountdown

[QL] Draw the countdown display during COUNT_DOWN phase.
Binary: FUN_1000ec00 in cgamex86.dll
=================
*/
static void CG_DrawWarmupCountdown(int gt) {
	int w;
	const char* s;
	const char* header = "";
	clientInfo_t* ci1;
	clientInfo_t* ci2;
	int i;

	// Gametype header line
	if (gt == GT_DUEL) {
		// Duel: show "X vs Y"
		ci1 = NULL;
		ci2 = NULL;
		for (i = 0; i < cgs.maxclients; i++) {
			if (cgs.clientinfo[i].infoValid && cgs.clientinfo[i].team == TEAM_FREE) {
				if (!ci1) {
					ci1 = &cgs.clientinfo[i];
				} else {
					ci2 = &cgs.clientinfo[i];
				}
			}
		}
		if (ci1 && ci2) {
			header = va("%s vs %s", ci1->name, ci2->name);
		}
	} else if (gt >= 0 && gt <= GT_RR) {
		// Round-based gametypes with active rounds: "Round Begins in"
		switch (gt) {
		case GT_CA:
		case GT_FREEZE:
		case GT_AD:
		case GT_RR:
			header = "Round Begins in";
			break;
		default:
			header = gametypeDisplayNames[gt];
			break;
		}
	} else {
		header = "Unknown Gametype";
	}

	w = CG_Text_Width(header, 0.6f, 0);
	CG_Text_Paint(320 - w / 2, 90, 0.6f, colorWhite, header, 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);

	// Countdown number
	switch (gt) {
	case GT_CA:
	case GT_FREEZE:
	case GT_AD:
	case GT_RR:
		// Round-based: bare number
		s = va("%i", cg.warmupCount);
		break;
	default:
		s = va("Starts in: %i", cg.warmupCount);
		break;
	}

	w = CG_Text_Width(s, 0.45f, 0);
	CG_Text_Paint(320 - w / 2, 125, 0.45f, colorWhite, s, 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);
}

/*
=================
CG_DrawWarmupMessages

[QL] Full warmup display: countdown tick sounds, PRE_GAME waiting messages,
and countdown display. Replaces the old CG_DrawWarmup.
Binary: CG_DrawWarmupMessages in cgamex86.dll
=================
*/
static void CG_DrawWarmup(void) {
	int sec;
	int i;
	int w;
	int gt;
	int teamCounts[4];
	const char* line1;
	const char* line2;

	// Handle countdown tick and sounds
	if (cg.warmup == 0) {
		if (cg.warmupCount == -1) {
			return;  // no warmup active
		}
		// warmup just ended, fall through to clear
	} else if (cg.warmup == -1) {
		cg.warmupCount = -1;
	} else if (cg.warmup > 0) {
		// Countdown active - calculate seconds remaining
		sec = (cg.warmup - cg.time + 1000) / 1000;
		if (sec < 0) {
			sec = 0;
		}

		if (sec != cg.warmupCount) {
			cg.warmupCount = sec;
			// Play countdown sounds
			switch (sec) {
			case 1:
				trap_S_StartLocalSound(cgs.media.count1Sound, CHAN_ANNOUNCER);
				break;
			case 2:
				trap_S_StartLocalSound(cgs.media.count2Sound, CHAN_ANNOUNCER);
				break;
			case 3:
				trap_S_StartLocalSound(cgs.media.count3Sound, CHAN_ANNOUNCER);
				break;
			}
		}
	}

	if (cg.showScores) {
		return;
	}

	// Determine effective gametype for display
	gt = (cg.warmupGametype >= 0) ? cg.warmupGametype : cgs.gametype;

	// Countdown display
	if (cg.warmupCount > 0) {
		CG_DrawWarmupCountdown(gt);
		return;
	}

	// PRE_GAME display (warmup == -1)
	if (cg.warmup >= 0) {
		return;
	}

	// Count players per team
	memset(teamCounts, 0, sizeof(teamCounts));
	for (i = 0; i < cgs.maxclients; i++) {
		if (cgs.clientinfo[i].infoValid) {
			teamCounts[cgs.clientinfo[i].team]++;
		}
	}

	line1 = NULL;
	line2 = NULL;

	if (gt == GT_RR) {
		// Red Rover
		if (cgs.teamSizeMin > 0 &&
			(teamCounts[TEAM_RED] < cgs.teamSizeMin || teamCounts[TEAM_BLUE] < cgs.teamSizeMin)) {
			line1 = "The match will begin";
			line2 = "when more players join.";
		} else {
			line1 = "The match will begin";
			line2 = "when more players are ready.";
		}
	} else if (gt < GT_TEAM) {
		// FFA / Duel / Race
		if (gt == GT_DUEL && teamCounts[TEAM_FREE] > 2) {
			line1 = "The match will begin when";
			line2 = "fewer players are in the match.";
		} else if (teamCounts[TEAM_FREE] < 2) {
			line1 = "The match will begin";
			line2 = "when more players join.";
		} else {
			line1 = "The match will begin";
			line2 = "when more players are ready.";
		}
	} else {
		// Team games (TDM, CA, CTF, 1FCTF, Overload, Harvester, FT, Dom, AD)
		if (teamCounts[TEAM_RED] < cgs.teamSizeMin) {
			if (teamCounts[TEAM_BLUE] < cgs.teamSizeMin) {
				// Both teams need players
				line1 = "Waiting for more players.";
				line2 = va("The match requires %i player%s per team.",
					cgs.teamSizeMin, cgs.teamSizeMin != 1 ? "s" : "");
			} else {
				// Only red needs players
				int need = cgs.teamSizeMin - teamCounts[TEAM_RED];
				line1 = va("Waiting for %i more player%s", need, need != 1 ? "s" : "");
				line2 = va("to join the %s.", "Red Team");
			}
		} else if (teamCounts[TEAM_BLUE] < cgs.teamSizeMin) {
			// Only blue needs players
			int need = cgs.teamSizeMin - teamCounts[TEAM_BLUE];
			line1 = va("Waiting for %i more player%s", need, need != 1 ? "s" : "");
			line2 = va("to join the %s.", "Blue Team");
		} else if (cgs.teamForceBalance &&
				   (teamCounts[TEAM_RED] > teamCounts[TEAM_BLUE] + 1 ||
				    teamCounts[TEAM_BLUE] > teamCounts[TEAM_RED] + 1)) {
			line1 = "The teams must be balanced";
			line2 = "before the match can begin.";
		} else {
			line1 = "The match will begin";
			line2 = "when more players are ready.";
		}
	}

	if (line1) {
		w = CG_Text_Width(line1, 0.35f, 0);
		CG_Text_Paint(320 - w / 2, 88, 0.35f, colorWhite, line1, 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);
	}
	if (line2) {
		w = CG_Text_Width(line2, 0.35f, 0);
		CG_Text_Paint(320 - w / 2, 108, 0.35f, colorWhite, line2, 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);
	}
}

//==================================================================================
/*
=================
CG_DrawTimedMenus
=================
*/
void CG_DrawTimedMenus(void) {
	if (cg.voiceTime) {
		int t = cg.time - cg.voiceTime;
		if (t > 2500) {
			Menus_CloseByName("voiceMenu");
			trap_Cvar_Set("cl_conXOffset", "0");
			cg.voiceTime = 0;
		}
	}
}

/*
=================
CG_DrawTimeout

// Address: 0x1000ef30
[QL] Pause/timeout overlay. Shows "Match Paused" during an indefinite pause, or
"Match resuming in ^5N^7 seconds" once a timein countdown is running. Plays the pause
klaxon once when a pause begins. Called every frame from CG_Draw2D; self-gates on
cgs.freezeEnd (CS_PAUSE_START_TIME).
=================
*/
static void CG_DrawTimeout(void) {
	static int lastPause;     // cgs.freezeEnd of the pause we last announced
	static int lastCountSec;  // last resume-countdown second we voiced
	const char* s;
	float w;
	int sec;

	if (!cgs.freezeEnd) {
		lastPause = 0;
		return;
	}

	// announce a newly-started pause once (klaxon)
	if (lastPause != cgs.freezeEnd) {
		lastPause = cgs.freezeEnd;
		lastCountSec = -1;
		trap_S_StartLocalSound(cgs.media.pauseSound, CHAN_ANNOUNCER);
	}

	if (cgs.pauseEnd == 0) {
		s = "Match Paused";
	} else {
		sec = (cgs.pauseEnd - cg.time) / 1000;

		// [QL] resume-countdown announcer, once per second as the timein countdown ticks.
		// The binary calls CG_PlayMatchStateSound(secondsRemaining): 5 = prepare_to_fight,
		// 3 = "two", 2 = "one", 1 = "fight".
		if (sec != lastCountSec) {
			lastCountSec = sec;
			switch (sec) {
			case 5: trap_S_StartLocalSound(cgs.media.countPrepareSound, CHAN_ANNOUNCER); break;
			case 3: trap_S_StartLocalSound(cgs.media.count2Sound, CHAN_ANNOUNCER); break;
			case 2: trap_S_StartLocalSound(cgs.media.count1Sound, CHAN_ANNOUNCER); break;
			case 1: trap_S_StartLocalSound(cgs.media.countFightSound, CHAN_ANNOUNCER); break;
			}
		}

		if (sec <= 0) {
			return;  // countdown finished; server is about to resume
		}
		s = va("Match resuming in ^5%d^7 seconds", sec);
	}

	w = CG_Text_Width(s, 0.5f, 0);
	CG_Text_Paint(320 - w / 2, 128, 0.5f, colorWhite, s, 0, 0, ITEM_TEXTSTYLE_SHADOWEDMORE);
}

/*
=================
CG_Draw2D
=================
*/
static void CG_Draw2D(stereoFrame_t stereoFrame) {
	if (cgs.orderPending && cg.time > cgs.orderTime) {
		CG_CheckOrderPending();
	}

	// if we are taking a levelshot for the menu, don't draw anything
	if (cg.levelShot) {
		return;
	}

	if (cg_draw2D.integer == 0) {
		return;
	}

	if (cg.snap->ps.pm_type == PM_INTERMISSION) {
		CG_DrawIntermission();
		return;
	}

	// [QL] shared floating-effect pool: damage numbers, player outlines, freeze/flag
	// glows and head float-sprites, all projected to screen (binary CG_DrawDamagePlums
	// called from CG_Draw2D @ 0x10010e9e).
	CG_SetWidescreen(WIDESCREEN_STRETCH);
	CG_DrawFloatingEffects();

	/*
		if (cg.cameraMode) {
			return;
		}
	*/
	if (cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR ||
		((cgs.gametype == GT_CA || cgs.gametype == GT_AD) &&
		 cg.snap->ps.pm_type == PM_SPECTATOR)) {
		CG_SetWidescreen(WIDESCREEN_CENTER);
		CG_DrawSpectator();
		CG_SetWidescreen(WIDESCREEN_STRETCH);

		if (stereoFrame == STEREO_CENTER)
			CG_DrawCrosshair();

		CG_SetWidescreen(WIDESCREEN_CENTER);
		CG_DrawCrosshairNames();
		CG_SetWidescreen(WIDESCREEN_STRETCH);
	} else {
		// don't draw any status if dead or the scoreboard is being explicitly shown
		if (!cg.showScores && cg.snap->ps.stats[STAT_HEALTH] > 0) {
			if (cg_drawStatus.integer) {
				Menu_PaintAll();
				CG_DrawTimedMenus();
			}

			CG_SetWidescreen(WIDESCREEN_CENTER);
			CG_DrawAmmoWarning();
			CG_DrawProxWarning();
			CG_SetWidescreen(WIDESCREEN_STRETCH);

			if (stereoFrame == STEREO_CENTER)
				CG_DrawCrosshair();

			CG_SetWidescreen(WIDESCREEN_CENTER);
			CG_DrawCrosshairNames();
			CG_DrawWeaponBar();  // [QL] handles all modes (Left/Right/Centered/Classic/No)
			CG_DrawReward();
			CG_SetWidescreen(WIDESCREEN_STRETCH);
		}
	}

	CG_SetWidescreen(WIDESCREEN_LEFT);
	CG_DrawVote();
	CG_SetWidescreen(WIDESCREEN_RIGHT);
	CG_DrawTeamVote();
	CG_SetWidescreen(WIDESCREEN_RIGHT);
	CG_DrawLagometer();
	CG_SetWidescreen(WIDESCREEN_STRETCH);

	if (!cg_paused.integer) {
		CG_SetWidescreen(WIDESCREEN_RIGHT);
		CG_DrawUpperRight(stereoFrame);
		CG_SetWidescreen(WIDESCREEN_STRETCH);
	}

	CG_SetWidescreen(WIDESCREEN_CENTER);
	if (!CG_DrawFollow()) {
		CG_DrawWarmup();
	}
	CG_DrawTimeout();  // [QL] pause/timeout overlay
	CG_SetWidescreen(WIDESCREEN_STRETCH);

	// [QL] race timer is drawn by menu system via CG_DrawRaceTimes ownerdraw
	// (removed hardcoded duplicate here)

	// [QL] draw chat overlay
	CG_DrawChat();

	// don't draw center string if scoreboard is up
	cg.scoreBoardShowing = CG_DrawScoreboard();
	if (!cg.scoreBoardShowing) {
		CG_SetWidescreen(WIDESCREEN_CENTER);
		CG_DrawCenterString();
		CG_SetWidescreen(WIDESCREEN_STRETCH);
	}
}

/*
=====================
CG_DrawActive

CG_DrawFullScreenColor

QL binary: draws a fullscreen vignette overlay when cg_vignette is enabled (vmCvar 0x10A63960)
=====================
*/
static void CG_DrawFullScreenColor(void) {
	if (cg_vignette.integer) {
		CG_DrawPic(0, 0, 640, 480, cgs.media.vignetteShader);
	}
}

/*
=====================
Perform all drawing needed to completely fill the screen
=====================
*/
void CG_DrawActive(stereoFrame_t stereoView) {
	// optionally draw the info screen instead
	if (!cg.snap) {
		CG_DrawInformation();
		return;
	}

	// optionally draw the tournement scoreboard instead
	if (cg.snap->ps.persistant[PERS_TEAM] == TEAM_SPECTATOR &&
		(cg.snap->ps.pm_flags & PMF_SCOREBOARD)) {
		CG_DrawTourneyScoreboard();
		return;
	}

	// QL binary: cg_zoomOutOnDeath.integer resets zoom on death/freeze/intermission (vmCvar 0x10A61200)
	if (((cg.predictedPlayerState.pm_type == PM_DEAD ||
	      cg.predictedPlayerState.pm_type == PM_FREEZE) && cg_zoomOutOnDeath.integer) ||
	    cg.predictedPlayerState.pm_type == PM_INTERMISSION) {
		cg.zoomed = qfalse;
	}

	// clear around the rendered view if sized down
	CG_TileClear();

	if (stereoView != STEREO_CENTER)
		CG_DrawCrosshair3D();

	// draw 3D view
	trap_R_RenderScene(&cg.refdef);

	// draw status bar and other floating elements
	CG_Draw2D(stereoView);

	// QL binary: vignette overlay drawn after HUD
	if (!cg.renderingThirdPerson) {
		CG_DrawFullScreenColor();
	}
}
