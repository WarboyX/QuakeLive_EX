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

#ifndef __APPLE__
#error This file is for Mac OS X only. You probably should not compile it.
#endif

// Please note that this file is just some Mac-specific bits. Most of the
// Mac OS X code is shared with other Unix platforms in sys_unix.c ...
//
// Compiled with -fobjc-arc (see DO_OBJCC / DO_DED_OBJCC in Makefile).
// No manual retain/release/autorelease - they are a compile error under ARC.

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"
#include "sys_local.h"

#include <errno.h>

#import <Carbon/Carbon.h>
#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

/*
==============
Sys_Dialog

Display an OS X dialog box
==============
*/
dialogResult_t Sys_Dialog( dialogType_t type, const char *message, const char *title )
{
	dialogResult_t result = DR_OK;
	NSAlert *alert = [NSAlert new];

	[alert setMessageText: [NSString stringWithUTF8String: title]];
	[alert setInformativeText: [NSString stringWithUTF8String: message]];

	if( type == DT_ERROR )
		[alert setAlertStyle: NSAlertStyleCritical];
	else
		[alert setAlertStyle: NSAlertStyleWarning];

	switch( type )
	{
		default:
			[alert runModal];
			result = DR_OK;
			break;

		case DT_YES_NO:
			[alert addButtonWithTitle: @"Yes"];
			[alert addButtonWithTitle: @"No"];
			switch( [alert runModal] )
			{
				default:
				case NSAlertFirstButtonReturn: result = DR_YES; break;
				case NSAlertSecondButtonReturn: result = DR_NO; break;
			}
			break;

		case DT_OK_CANCEL:
			[alert addButtonWithTitle: @"OK"];
			[alert addButtonWithTitle: @"Cancel"];

			switch( [alert runModal] )
			{
				default:
				case NSAlertFirstButtonReturn: result = DR_OK; break;
				case NSAlertSecondButtonReturn: result = DR_CANCEL; break;
			}
			break;
	}

	return result;
}

/*
=================
Sys_StripAppBundle

Discovers if passed dir is suffixed with the directory structure of a Mac OS X
.app bundle. If it is, the .app directory structure is stripped off the end and
the result is returned. If not, dir is returned untouched.
=================
*/
char *Sys_StripAppBundle( char *dir )
{
	static char cwd[MAX_OSPATH];

	Q_strncpyz(cwd, dir, sizeof(cwd));
	if(strcmp(Sys_Basename(cwd), "MacOS"))
		return dir;
	Q_strncpyz(cwd, Sys_Dirname(cwd), sizeof(cwd));
	if(strcmp(Sys_Basename(cwd), "Contents"))
		return dir;
	Q_strncpyz(cwd, Sys_Dirname(cwd), sizeof(cwd));
	if(!strstr(Sys_Basename(cwd), ".app"))
		return dir;
	Q_strncpyz(cwd, Sys_Dirname(cwd), sizeof(cwd));
	return cwd;
}

/*
=================
Sys_DefaultAppPath

On macOS, return Contents/Resources so fs_apppath points to the standard
bundle resource location. Bundled pk3s (iobin.pk3, pak01.pk3) are placed
there at build time and are found regardless of App Translocation.
=================
*/
char *Sys_DefaultAppPath( void )
{
	static char resourcePath[MAX_OSPATH];
	NSString *path = [[NSBundle mainBundle] resourcePath];
	if (path && [path length] > 0) {
		Q_strncpyz(resourcePath, [path UTF8String], sizeof(resourcePath));
		return resourcePath;
	}
	return Sys_BinaryPath();
}

#ifndef DEDICATED
/*
=================
QLPak0CancelHelper

Cancel flag for Sys_LocatePak0's progress window. The GCD worker polls
->cancelled once per 256 KB chunk; the Cancel button / Escape key sets it.
=================
*/
@interface QLPak0CancelHelper : NSObject
{
@public
	BOOL cancelled;
}
- (void)onCancelClicked:(id)sender;
@end

@implementation QLPak0CancelHelper
- (void)onCancelClicked:(id)sender
{
	(void)sender;
	cancelled = YES;
}
@end

/*
=================
Sys_LocatePak0

NSOpenPanel to locate pak00.pk3 and copy it into destDir/baseq3/.
Copy runs on a GCD background queue; NSModalSession keeps the progress
window live on the main thread. Returns qtrue on success.
=================
*/
qboolean Sys_LocatePak0( const char *destDir )
{
	if (!NSApp) {
		[NSApplication sharedApplication];
		[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
	}
	// activateIgnoringOtherApps: is deprecated on macOS 14+
	if (@available(macOS 14.0, *)) {
		[NSApp activate];
	} else {
		[NSApp activateIgnoringOtherApps:YES];
	}

	NSOpenPanel *panel = [NSOpenPanel openPanel];
	[panel setTitle:@"Locate pak00.pk3"];
	[panel setMessage:@"Quake Live game data not found.\n"
	                   "Please select pak00.pk3 from your Quake Live installation."];
	[panel setCanChooseFiles:YES];
	[panel setCanChooseDirectories:NO];
	[panel setAllowsMultipleSelection:NO];
	if (@available(macOS 11.0, *)) {
		[panel setAllowedContentTypes:
		    @[[UTType typeWithFilenameExtension:@"pk3"]]];
	} else {
		[panel setAllowedFileTypes:@[@"pk3"]];
	}

	if ([panel runModal] != NSModalResponseOK)
		return qfalse;

	NSString *srcPath = [[panel URL] path];

	char pak00Dest[MAX_OSPATH];
	Com_sprintf(pak00Dest, sizeof(pak00Dest), "%s/%s", destDir, BASEGAME_DIR);
	Sys_Mkdir(pak00Dest);
	Com_sprintf(pak00Dest, sizeof(pak00Dest), "%s/%s/pak00.pk3", destDir, BASEGAME_DIR);
	// Blocks can't capture C arrays; pointer is valid while the modal runs.
	const char *pak00DestPath = pak00Dest;

	NSDictionary *attrs = [[NSFileManager defaultManager]
	    attributesOfItemAtPath:srcPath error:nil];
	long long fileSize = attrs ? [[attrs objectForKey:NSFileSize] longLongValue] : 0;

	NSWindow *progressWin = [[NSWindow alloc]
	    initWithContentRect:NSMakeRect(0, 0, 400, 110)
	    styleMask:NSWindowStyleMaskTitled
	    backing:NSBackingStoreBuffered
	    defer:NO];
	// ARC: must be NO or -close double-releases the window.
	[progressWin setReleasedWhenClosed:NO];
	[progressWin setTitle:@"Quake Live - First Time Setup"];

	NSProgressIndicator *bar = [[NSProgressIndicator alloc]
	    initWithFrame:NSMakeRect(20, 65, 360, 20)];
	[bar setStyle:NSProgressIndicatorStyleBar];
	[bar setIndeterminate:(fileSize == 0)];
	[bar setMinValue:0.0];
	[bar setMaxValue:100.0];
	[bar setDoubleValue:0.0];
	[[progressWin contentView] addSubview:bar];

	NSTextField *label = [[NSTextField alloc]
	    initWithFrame:NSMakeRect(20, 42, 360, 18)];
	[label setBezeled:NO];
	[label setDrawsBackground:NO];
	[label setEditable:NO];
	[label setStringValue:@"Copying pak00.pk3..."];
	[[progressWin contentView] addSubview:label];

	QLPak0CancelHelper *cancelHelper = [[QLPak0CancelHelper alloc] init];
	NSButton *cancelBtn = [NSButton buttonWithTitle:@"Cancel"
	                                         target:cancelHelper
	                                         action:@selector(onCancelClicked:)];
	[cancelBtn setFrame:NSMakeRect(300, 8, 80, 28)];
	[cancelBtn setKeyEquivalent:@"\033"];
	[[progressWin contentView] addSubview:cancelBtn];

	[progressWin center];
	[progressWin makeKeyAndOrderFront:nil];
	if (fileSize == 0)
		[bar startAnimation:nil];

	__block qboolean copyOk = qfalse;
	__block BOOL copyDone = NO;
	__block int copyErrno = 0;

	NSModalSession session = [NSApp beginModalSessionForWindow:progressWin];

	dispatch_async(
	    dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{

		FILE *fin = fopen([srcPath UTF8String], "rb");
		if (!fin) {
			copyErrno = errno;
			dispatch_async(dispatch_get_main_queue(), ^{ copyDone = YES; });
			return;
		}
		FILE *fout = fopen(pak00DestPath, "wb");
		if (!fout) {
			copyErrno = errno;
			fclose(fin);
			dispatch_async(dispatch_get_main_queue(), ^{ copyDone = YES; });
			return;
		}

		char buf[256 * 1024];   // 256 KB chunks
		long long copied = 0;
		int lastPct = -1;
		qboolean ok = qtrue;
		size_t nread;

		while ((nread = fread(buf, 1, sizeof(buf), fin)) > 0) {
			if (cancelHelper->cancelled) {
				ok = qfalse;
				break;
			}
			if (fwrite(buf, 1, nread, fout) != nread) {
				copyErrno = errno;
				ok = qfalse;
				break;
			}
			copied += (long long)nread;
			if (fileSize > 0) {
				// Only dispatch when percent changes (~100 updates vs ~3600).
				int pct = (int)((double)copied / (double)fileSize * 100.0);
				if (pct != lastPct) {
					lastPct = pct;
					long long snap = copied;
					dispatch_async(dispatch_get_main_queue(), ^{
						[bar setDoubleValue:(double)pct];
						[label setStringValue:[NSString stringWithFormat:
						    @"Copying pak00.pk3... %d%%  (%.0f / %.0f MB)",
						    pct,
						    (double)snap / (1024.0 * 1024.0),
						    (double)fileSize / (1024.0 * 1024.0)]];
					});
				}
			}
		}
		if (ok && ferror(fin)) {
			copyErrno = errno;
			ok = qfalse;
		}

		fclose(fin);
		fclose(fout);
		copyOk = ok;

		dispatch_async(dispatch_get_main_queue(), ^{
			copyDone = YES;
		});
	});

	while (!copyDone) {
		[NSApp runModalSession:session];
		[[NSRunLoop currentRunLoop]
		    runMode:NSDefaultRunLoopMode
		    beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
	}
	[NSApp endModalSession:session];

	BOOL userCancelled = cancelHelper->cancelled;

	[progressWin orderOut:nil];

	if (userCancelled) {
		remove(pak00Dest);
		Com_Printf("pak00.pk3 copy cancelled by user\n");
		return qfalse;
	}

	if (!copyOk) {
		remove(pak00Dest);
		char errMsg[256];
		if (copyErrno != 0) {
			Com_sprintf(errMsg, sizeof(errMsg),
			    "Failed to copy pak00.pk3:\n%s", strerror(copyErrno));
		} else {
			Q_strncpyz(errMsg, "Failed to copy pak00.pk3.", sizeof(errMsg));
		}
		Sys_Dialog(DT_ERROR, errMsg, "Quake Live");
		return qfalse;
	}

	Com_Printf("pak00.pk3 copied to %s\n", pak00Dest);
	return qtrue;
}
#endif // !DEDICATED
