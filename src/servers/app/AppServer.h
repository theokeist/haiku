/*
 * Copyright 2001-2015, Haiku, Inc.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Axel Dörfler, axeld@pinc-software.de
 */
#ifndef	APP_SERVER_H
#define	APP_SERVER_H


#include <Application.h>
#include <List.h>
#include <Locker.h>
#include <ObjectList.h>
#include <OS.h>
#include <Path.h>
#include <String.h>
#include <Window.h>
#include <MessageRunner.h>

#include "MessageLooper.h"
#include "CompositorSettings.h"
#include "CompositorDebugOptions.h"
#include "ServerConfig.h"


#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
#	include <Server.h>
#	define SERVER_BASE BServer
#else
#	include "TestServerLoopAdapter.h"
#	define SERVER_BASE TestServerLoopAdapter
#endif


class ServerApp;
class BitmapManager;
class Desktop;


class AppServer : public SERVER_BASE {
public:
								AppServer(status_t* status);
	virtual						~AppServer();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			Desktop*			_CreateDesktop(uid_t userID,
									const char* targetScreen);
	virtual	Desktop*			_FindDesktop(uid_t userID,
									const char* targetScreen);

			void				_LaunchInputServer();
			void				_LoadCompositorSettings();
			void				_ApplyCompositorSettings();
			void				_InvalidateAllDesktops();
			void				_UpdateAlphaDebugSetting(bool force);
			status_t			_AlphaDebugSettingsPath(BPath& path) const;
			void				_ApplyAlphaDebugSetting(bool enabled);
			void				_UpdateCompositorDebugSetting(bool force);
			status_t			_CompositorDebugSettingsPath(BPath& path) const;
			void				_ApplyCompositorDebugSetting(
									const CompositorDebugOptions& options);

private:
			BObjectList<Desktop> fDesktops;
			BLocker				fDesktopLock;
			CompositorSettings	fCompositorSettings;
			BMessageRunner*		fAlphaDebugRunner;
			BMessageRunner*		fCompositorDebugRunner;
			bool				fAlphaDebugEnabled;
			time_t				fAlphaDebugSettingsMTime;
			CompositorDebugOptions	fCompositorDebugOptions;
			time_t				fCompositorDebugSettingsMTime;
};


extern BitmapManager *gBitmapManager;
extern port_id gAppServerPort;


#endif	/* APP_SERVER_H */
