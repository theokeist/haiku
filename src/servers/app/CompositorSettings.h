/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */
#ifndef COMPOSITOR_SETTINGS_H
#define COMPOSITOR_SETTINGS_H

#include <SupportDefs.h>

class BPath;

struct CompositorSettings {
	bool		enable_compositor;
	bool		enable_animations;
	bool		enable_blur;
	bool		enable_translucency;
	int32		target_fps;
	int32		log_level;
	bool		debug_controls;
	bool		force_blur_all;
	float		force_opacity;
	bool		force_opacity_only_opaque;
	bool		show_overlay;
	bool		log_timings;
	bool		stress_invalidate;

				CompositorSettings();

	void		SetDefaults();
	status_t	LoadFromSettingsFile();

	static	status_t	SettingsPath(BPath& path);
};

#endif // COMPOSITOR_SETTINGS_H
