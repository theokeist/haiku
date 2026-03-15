/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */
#include "CompositorSettings.h"

#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <Path.h>

namespace {

static const char* kCompositorSettingsDir = "system/app_server";
static const char* kCompositorSettingsFile = "compositor_settings";

static const char* kEnableCompositorKey = "enable_compositor";
static const char* kEnableAnimationsKey = "enable_animations";
static const char* kEnableBlurKey = "enable_blur";
static const char* kEnableTranslucencyKey = "enable_translucency";
static const char* kTargetFpsKey = "target_fps";
static const char* kLogLevelKey = "log_level";
static const char* kDebugControlsKey = "debug_controls";
static const char* kForceBlurAllKey = "force_blur_all";
static const char* kForceOpacityKey = "force_opacity";
static const char* kForceOpacityOnlyOpaqueKey = "force_opacity_only_opaque";
static const char* kSystemAlphaKey = "system_alpha";
static const char* kShowOverlayKey = "show_overlay";
static const char* kLogTimingsKey = "log_timings";
static const char* kStressInvalidateKey = "stress_invalidate";
static const char* kEnableTitleBlurPolicyKey = "enable_title_blur_policy";
static const char* kEnableFloatingUntitledBlurPolicyKey
	= "enable_floating_untitled_blur_policy";
static const char* kBlurPolicyTokensKey = "blur_policy_tokens";

} // namespace


CompositorSettings::CompositorSettings()
{
	SetDefaults();
}


void
CompositorSettings::SetDefaults()
{
	enable_compositor = true;
	enable_animations = true;
	enable_blur = false;
	enable_translucency = false;
	target_fps = 60;
	log_level = 0;
	debug_controls = false;
	force_blur_all = false;
	force_opacity = -1.0f;
	force_opacity_only_opaque = true;
	system_alpha = 0.85f;
	show_overlay = false;
	log_timings = false;
	stress_invalidate = false;
	true_shadows = false;
	enable_title_blur_policy = true;
	enable_floating_untitled_blur_policy = true;
	blur_policy_tokens = "deskbar,notification,notify";
}


status_t
CompositorSettings::LoadFromSettingsFile()
{
	SetDefaults();

	BPath path;
	status_t status = SettingsPath(path);
	if (status != B_OK)
		return status;

	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return file.InitCheck();

	BMessage settings;
	status = settings.Unflatten(&file);
	if (status != B_OK)
		return status;

	enable_compositor = settings.GetBool(kEnableCompositorKey,
		enable_compositor);
	enable_animations = settings.GetBool(kEnableAnimationsKey,
		enable_animations);
	enable_blur = settings.GetBool(kEnableBlurKey, enable_blur);
	enable_translucency = settings.GetBool(kEnableTranslucencyKey,
		enable_translucency);
	target_fps = settings.GetInt32(kTargetFpsKey, target_fps);
	log_level = settings.GetInt32(kLogLevelKey, log_level);
	debug_controls = settings.GetBool(kDebugControlsKey, debug_controls);
	force_blur_all = settings.GetBool(kForceBlurAllKey, force_blur_all);
	force_opacity = settings.GetFloat(kForceOpacityKey, force_opacity);
	force_opacity_only_opaque = settings.GetBool(kForceOpacityOnlyOpaqueKey,
		force_opacity_only_opaque);
	system_alpha = settings.GetFloat(kSystemAlphaKey, system_alpha);
	show_overlay = settings.GetBool(kShowOverlayKey, show_overlay);
	log_timings = settings.GetBool(kLogTimingsKey, log_timings);
	stress_invalidate = settings.GetBool(kStressInvalidateKey,
		stress_invalidate);
	true_shadows = settings.GetBool("true_shadows", true_shadows);
	enable_title_blur_policy = settings.GetBool(kEnableTitleBlurPolicyKey,
		enable_title_blur_policy);
	enable_floating_untitled_blur_policy = settings.GetBool(
		kEnableFloatingUntitledBlurPolicyKey,
		enable_floating_untitled_blur_policy);
	blur_policy_tokens = settings.GetString(kBlurPolicyTokensKey,
		blur_policy_tokens);

	if (target_fps <= 0)
		target_fps = 60;

	if (log_level < 0)
		log_level = 0;

	if (force_opacity > 1.0f)
		force_opacity = 1.0f;
	if (force_opacity < 0.0f && force_opacity != -1.0f)
		force_opacity = -1.0f;

	if (system_alpha > 1.0f)
		system_alpha = 1.0f;
	if (system_alpha < 0.0f)
		system_alpha = 0.0f;

	return B_OK;
}


status_t
CompositorSettings::SettingsPath(BPath& path)
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;

	status = path.Append(kCompositorSettingsDir);
	if (status != B_OK)
		return status;

	status = create_directory(path.Path(), 0755);
	if (status != B_OK)
		return status;

	return path.Append(kCompositorSettingsFile);
}
