/*
 * Copyright 2001-2016, Haiku, Inc.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Axel Dörfler, axeld@pinc-software.de
 *		Stephan Aßmus <superstippi@gmx.de>
 * 		Christian Packmann
 */


#include "AppServer.h"

#include <syslog.h>

#include <AutoDeleter.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <LaunchRoster.h>
#include <PortLink.h>
#include <RosterPrivate.h>
#include <TokenSpace.h>

#include "BitmapManager.h"
#include "Desktop.h"
#include "GlobalFontManager.h"
#include "InputManager.h"
#include "ScreenManager.h"
#include "ServerProtocol.h"
#include "ServerWindow.h"
#include "Window.h"

static const int32 kMsgAlphaDebugPoll = 'adpl';
static const int32 kMsgCompositorDebugPoll = 'cdpl';


//#define DEBUG_SERVER
#ifdef DEBUG_SERVER
#	include <stdio.h>
#	define STRACE(x) printf x
#else
#	define STRACE(x) ;
#endif


// Globals
port_id gAppServerPort;
BTokenSpace gTokenSpace;
uint32 gAppServerSIMDFlags = 0;


/*!	\brief Constructor

	This loads the default fonts, allocates all the major global variables,
	spawns the main housekeeping threads, loads user preferences for the UI
	and decorator, and allocates various locks.
*/
AppServer::AppServer(status_t* status)
	:
	SERVER_BASE("application/x-vnd.Haiku-app_server", "picasso", -1, false,
		status),
	fDesktopLock("AppServerDesktopLock"),
	fAlphaDebugRunner(NULL),
	fCompositorDebugRunner(NULL),
	fAlphaDebugEnabled(false),
	fAlphaDebugSettingsMTime(0),
	fCompositorDebugOptions(),
	fCompositorDebugSettingsMTime(0)
{
	openlog("app_server", 0, LOG_DAEMON);

	gInputManager = new InputManager();

	// Create the font server and scan the proper directories.
	gFontManager = new GlobalFontManager;
	if (gFontManager->InitCheck() != B_OK)
		debugger("font manager could not be initialized!");

	gFontManager->Run();

	gScreenManager = new ScreenManager();
	gScreenManager->Run();

	// Create the bitmap allocator. Object declared in BitmapManager.cpp
	gBitmapManager = new BitmapManager();

#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
#if 0
	// This is not presently needed, as app_server is launched from the login session.
	// TODO: check the attached displays, and launch login session for them
	BMessage data;
	data.AddString("name", "app_server");
	data.AddInt32("session", 0);
	BLaunchRoster().Target("login", data);
#endif

	// Inform the registrar we've (re)started.
	BMessage request(kMsgAppServerStarted);
	BRoster::Private().SendTo(&request, NULL, false);
#endif

	_LoadCompositorSettings();
	_UpdateAlphaDebugSetting(true);
	fAlphaDebugRunner = new(std::nothrow) BMessageRunner(BMessenger(this),
		new BMessage(kMsgAlphaDebugPoll), 1000000);
	_UpdateCompositorDebugSetting(true);
	fCompositorDebugRunner = new(std::nothrow) BMessageRunner(BMessenger(this),
		new BMessage(kMsgCompositorDebugPoll), 1000000);
}


/*!	\brief Destructor
	Reached only when the server is asked to shut down in Test mode.
*/
AppServer::~AppServer()
{
	delete fAlphaDebugRunner;
	delete fCompositorDebugRunner;
	delete gBitmapManager;

	gScreenManager->Lock();
	gScreenManager->Quit();

	gFontManager->Lock();
	gFontManager->Quit();

	closelog();
}


void
AppServer::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgAlphaDebugPoll:
			_UpdateAlphaDebugSetting(false);
			break;

		case kMsgCompositorDebugPoll:
			_UpdateCompositorDebugSetting(false);
			break;

		case AS_INTERNAL_SET_WINDOW_ALPHA:
		{
			// Per-window alpha override entrypoint used by internal tooling.
			// Dispatch through Desktop to reuse window-locking/ownership checks.
			int32 windowToken = message->GetInt32("window", B_NULL_TOKEN);
			float alpha = message->GetFloat("alpha", 1.0f);

			if (windowToken != B_NULL_TOKEN) {
				BAutolock locker(fDesktopLock);
				for (int32 i = 0; i < fDesktops.CountItems(); i++) {
					Desktop* desktop = fDesktops.ItemAt(i);
					if (desktop != NULL && desktop->SetWindowAlpha(windowToken, alpha))
						break;
				}
			}
			break;
		}

		case AS_INTERNAL_SET_ALPHA_DEBUG:
		{
			// Global alpha-debug switch propagated to all desktops/windows.
			bool enabled = message->GetBool("enabled", false);
			if (enabled != fAlphaDebugEnabled) {
				fAlphaDebugEnabled = enabled;
				_ApplyAlphaDebugSetting(enabled);
			}
			break;
		}
		case AS_PRIVATE_SET_WINDOW_EFFECTS:
		{
			BMessage reply;
			status_t status = B_BAD_VALUE;
			int32 windowToken = message->GetInt32("window", B_NULL_TOKEN);
			bool animate = message->GetBool("animate", false);
			bigtime_t duration = message->GetInt64("duration", 150000);

			float alpha = 1.0f;
			bool blurEnabled = false;
			float blurRadius = 0.0f;
			bool hasAlpha = message->FindFloat("alpha", &alpha) == B_OK;
			bool hasBlur = message->FindBool("blur", &blurEnabled) == B_OK;
			bool hasBlurRadius = message->FindFloat("blur_radius", &blurRadius)
				== B_OK;

			if (hasAlpha || hasBlur || hasBlurRadius) {
				BAutolock tokenLocker(BPrivate::gDefaultTokens);
				ServerWindow* window = NULL;
				if (windowToken != B_NULL_TOKEN
					&& BPrivate::gDefaultTokens.GetToken(windowToken,
						B_SERVER_TOKEN, (void**)&window) == B_OK
					&& window != NULL
					&& window->Window() != NULL) {
					Window* effectsWindow = window->Window();
					if (hasAlpha)
						effectsWindow->SetAlpha(alpha, animate, duration);
					if (hasBlur)
						effectsWindow->SetBlurEnabled(blurEnabled);
					if (hasBlurRadius)
						effectsWindow->SetBlurRadius(blurRadius);

					reply.AddFloat("alpha", effectsWindow->Alpha());
					reply.AddBool("blur", effectsWindow->BlurEnabled());
					reply.AddFloat("blur_radius", effectsWindow->BlurRadius());
					reply.AddInt64("duration",
						effectsWindow->AlphaAnimationDuration());
					status = B_OK;
				}
			}

			reply.what = status;
			message->SendReply(&reply);
			break;
		}
		case AS_INTERNAL_SET_COMPOSITOR_DEBUG_OPTIONS:
		{
			fCompositorSettings.force_blur_all
				= message->GetBool("force_blur_all",
					fCompositorSettings.force_blur_all);
			fCompositorSettings.force_opacity = message->GetFloat("force_opacity",
				fCompositorSettings.force_opacity);
			fCompositorSettings.force_opacity_only_opaque
				= message->GetBool("force_opacity_only_opaque",
					fCompositorSettings.force_opacity_only_opaque);
			fCompositorSettings.show_overlay = message->GetBool("show_overlay",
				fCompositorSettings.show_overlay);
			fCompositorSettings.log_timings = message->GetBool("log_timings",
				fCompositorSettings.log_timings);
			fCompositorSettings.stress_invalidate
				= message->GetBool("stress_invalidate",
					fCompositorSettings.stress_invalidate);

			if (fCompositorSettings.force_opacity > 1.0f)
				fCompositorSettings.force_opacity = 1.0f;
			if (fCompositorSettings.force_opacity < 0.0f
				&& fCompositorSettings.force_opacity != -1.0f) {
				fCompositorSettings.force_opacity = -1.0f;
			}

			_ApplyCompositorSettings();
			_InvalidateAllDesktops();

			BMessage reply(B_OK);
			reply.AddBool("force_blur_all", fCompositorSettings.force_blur_all);
			reply.AddFloat("force_opacity", fCompositorSettings.force_opacity);
			reply.AddBool("force_opacity_only_opaque",
				fCompositorSettings.force_opacity_only_opaque);
			reply.AddBool("show_overlay", fCompositorSettings.show_overlay);
			reply.AddBool("log_timings", fCompositorSettings.log_timings);
			reply.AddBool("stress_invalidate",
				fCompositorSettings.stress_invalidate);
			message->SendReply(&reply);
			break;
		}
		case AS_INTERNAL_RELOAD_COMPOSITOR_SETTINGS:
			_LoadCompositorSettings();
			_InvalidateAllDesktops();
			break;

		case AS_GET_DESKTOP:
		{
			Desktop* desktop = NULL;

			int32 userID = message->GetInt32("user", 0);
			int32 version = message->GetInt32("version", 0);
			const char* targetScreen = message->GetString("target");

			if (version != AS_PROTOCOL_VERSION) {
				syslog(LOG_ERR, "Application for user %" B_PRId32 " does not "
					"support the current server protocol (%" B_PRId32 ").\n",
					userID, version);
			} else {
				desktop = _FindDesktop(userID, targetScreen);
				if (desktop == NULL) {
					// we need to create a new desktop object for this user
					// TODO: test if the user exists on the system
					// TODO: maybe have a separate AS_START_DESKTOP_SESSION for
					// authorizing the user
					desktop = _CreateDesktop(userID, targetScreen);
				}
			}

			BMessage reply;
			if (desktop != NULL)
				reply.AddInt32("port", desktop->MessagePort());
			else
				reply.what = (uint32)B_ERROR;

			message->SendReply(&reply);
			break;
		}

		default:
			// We don't allow application scripting
			STRACE(("AppServer received unexpected code %" B_PRId32 "\n",
				message->what));
			break;
	}
}


void
AppServer::_LoadCompositorSettings()
{
	fCompositorSettings.LoadFromSettingsFile();
	_ApplyCompositorSettings();
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status < B_OK)
		return status;

	status = path.Append("system/app_server");
	if (status < B_OK)
		return status;

	status = create_directory(path.Path(), 0755);
	if (status < B_OK && status != B_FILE_EXISTS)
		return status;

	return path.Append("alpha_debug");
}


void
AppServer::_UpdateAlphaDebugSetting(bool force)
{
	BPath path;
	if (_AlphaDebugSettingsPath(path) != B_OK)
		return;

	BEntry entry(path.Path());
	time_t modified = 0;
	bool enabled = false;
	if (entry.Exists()) {
		entry.GetModificationTime(&modified);
		if (!force && modified == fAlphaDebugSettingsMTime)
			return;

		BFile file(path.Path(), B_READ_ONLY);
		BMessage settings;
		if (file.InitCheck() == B_OK && settings.Unflatten(&file) == B_OK)
			enabled = settings.GetBool("enabled", false);
	} else if (!force && fAlphaDebugSettingsMTime == 0 && !fAlphaDebugEnabled) {
		return;
	}

	fAlphaDebugSettingsMTime = modified;
	if (enabled == fAlphaDebugEnabled && !force)
		return;

	fAlphaDebugEnabled = enabled;
	_ApplyAlphaDebugSetting(enabled);
}


void
AppServer::_ApplyCompositorSettings()
{
	BAutolock locker(fDesktopLock);
	for (int32 i = 0; i < fDesktops.CountItems(); i++) {
		Desktop* desktop = fDesktops.ItemAt(i);
		if (desktop != NULL)
			desktop->ApplyCompositorSettings(fCompositorSettings);
	}
}


void
AppServer::_InvalidateAllDesktops(){
			desktop->SetAlphaDebugEnabled(enabled);
	}



status_t
AppServer::_CompositorDebugSettingsPath(BPath& path) const
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status < B_OK)
		return status;

	status = path.Append("system/app_server");
	if (status < B_OK)
		return status;

	status = create_directory(path.Path(), 0755);
	if (status < B_OK && status != B_FILE_EXISTS)
		return status;

	return path.Append("compositor_debug");
}


void
AppServer::_UpdateCompositorDebugSetting(bool force)
{
	BPath path;
	if (_CompositorDebugSettingsPath(path) != B_OK)
		return;

	BEntry entry(path.Path());
	time_t modified = 0;
	CompositorDebugOptions options;
	if (entry.Exists()) {
		entry.GetModificationTime(&modified);
		if (!force && modified == fCompositorDebugSettingsMTime)
			return;

		BFile file(path.Path(), B_READ_ONLY);
		BMessage settings;
		if (file.InitCheck() == B_OK && settings.Unflatten(&file) == B_OK) {
			options.forceBlurAll = settings.GetBool("forceBlurAll", false);
			options.forceOpacity = settings.GetFloat("forceOpacity", -1.0f);
			options.showOverlay = settings.GetBool("showOverlay", false);
			options.logTimings = settings.GetBool("logTimings", false);
			options.stressInvalidate = settings.GetBool("stressInvalidate", false);
		}
	} else if (!force && fCompositorDebugSettingsMTime == 0
		&& !fCompositorDebugOptions.forceBlurAll
		&& fCompositorDebugOptions.forceOpacity < 0.0f
		&& !fCompositorDebugOptions.showOverlay
		&& !fCompositorDebugOptions.logTimings
		&& !fCompositorDebugOptions.stressInvalidate) {
		return;
	}

	fCompositorDebugSettingsMTime = modified;

	if (!force
		&& options.forceBlurAll == fCompositorDebugOptions.forceBlurAll
		&& options.forceOpacity == fCompositorDebugOptions.forceOpacity
		&& options.showOverlay == fCompositorDebugOptions.showOverlay
		&& options.logTimings == fCompositorDebugOptions.logTimings
		&& options.stressInvalidate == fCompositorDebugOptions.stressInvalidate) {
		return;
	}

	fCompositorDebugOptions = options;
	_ApplyCompositorDebugSetting(options);
}


void
AppServer::_ApplyCompositorDebugSetting(const CompositorDebugOptions& options)
{
	BAutolock locker(fDesktopLock);
	for (int32 i = 0; i < fDesktops.CountItems(); i++) {
		Desktop* desktop = fDesktops.ItemAt(i);
		if (desktop != NULL) {
			desktop->SetCompositorDebugOptions(options);
			desktop->Redraw();
		}
	}
}


bool
AppServer::QuitRequested()
{
#if TEST_MODE
	while (fDesktops.CountItems() > 0) {
		Desktop *desktop = fDesktops.RemoveItemAt(0);

		thread_id thread = desktop->Thread();
		desktop->PostMessage(B_QUIT_REQUESTED);

		// we just wait for the desktop to kill itself
		status_t status;
		wait_for_thread(thread, &status);
	}

	delete this;
	exit(0);

	return SERVER_BASE::QuitRequested();
#else
	return false;
#endif

}


/*!	\brief Creates a desktop object for an authorized user
*/
Desktop*
AppServer::_CreateDesktop(uid_t userID, const char* targetScreen)
{
	BAutolock locker(fDesktopLock);
	ObjectDeleter<Desktop> desktop;
	try {
		desktop.SetTo(new Desktop(userID, targetScreen));

		status_t status = desktop->Init();
		if (status == B_OK)
			status = desktop->Run();
		// Apply current global debug state to new desktops at creation time so
		// they don't wait for the periodic settings poll to match server state.
		if (status == B_OK)
			desktop->SetAlphaDebugEnabled(fAlphaDebugEnabled);
		if (status == B_OK)
			desktop->SetCompositorDebugOptions(fCompositorDebugOptions);
		if (status == B_OK && !fDesktops.AddItem(desktop.Get()))
			status = B_NO_MEMORY;

		if (status != B_OK) {
			syslog(LOG_ERR, "Cannot initialize Desktop object: %s\n",
				strerror(status));
			return NULL;
		}
	} catch (...) {
		// there is obviously no memory left
		return NULL;
	}

	Desktop* created = desktop.Detach();
	if (created != NULL)
		created->ApplyCompositorSettings(fCompositorSettings);
	return created;
}


/*!	\brief Finds the desktop object that belongs to a certain user
*/
Desktop*
AppServer::_FindDesktop(uid_t userID, const char* targetScreen)
{
	BAutolock locker(fDesktopLock);

	for (int32 i = 0; i < fDesktops.CountItems(); i++) {
		Desktop* desktop = fDesktops.ItemAt(i);

		if (desktop->UserID() == userID
			&& ((desktop->TargetScreen() == NULL && targetScreen == NULL)
				|| (desktop->TargetScreen() != NULL && targetScreen != NULL
					&& strcmp(desktop->TargetScreen(), targetScreen) == 0))) {
			return desktop;
		}
	}

	return NULL;
}


//	#pragma mark -


int
main(int argc, char** argv)
{
	srand(real_time_clock_usecs());

	status_t status;
	AppServer* server = new AppServer(&status);
	if (status == B_OK)
		server->Run();

	return status == B_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
