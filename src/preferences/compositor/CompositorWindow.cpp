/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */
#include "CompositorWindow.h"

#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <File.h>
#include <FindDirectory.h>
#include <LayoutBuilder.h>
#include <Messenger.h>
#include <Message.h>
#include <Path.h>
#include <Slider.h>

#include <private/app/ServerProtocol.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Compositor"

namespace {

const int32 kMinFps = 30;
const int32 kMaxFps = 144;
const int32 kMinOpacity = 20;
const int32 kMaxOpacity = 100;

const int32 kMsgSettingsChanged = 'scng';
const int32 kMsgApplySettings = 'aply';

const char* kSettingsDir = "system/app_server";
const char* kSettingsFile = "compositor_settings";

const char* kEnableCompositorKey = "enable_compositor";
const char* kEnableAnimationsKey = "enable_animations";
const char* kEnableBlurKey = "enable_blur";
const char* kEnableTranslucencyKey = "enable_translucency";
const char* kTargetFpsKey = "target_fps";
const char* kLogLevelKey = "log_level";
const char* kDebugControlsKey = "debug_controls";
const char* kForceBlurAllKey = "force_blur_all";
const char* kForceOpacityKey = "force_opacity";
const char* kForceOpacityOnlyOpaqueKey = "force_opacity_only_opaque";
const char* kShowOverlayKey = "show_overlay";
const char* kLogTimingsKey = "log_timings";
const char* kStressInvalidateKey = "stress_invalidate";

} // namespace


CompositorWindow::CompositorWindow()
	:	BWindow(BRect(0, 0, 360, 260), B_TRANSLATE_SYSTEM_NAME("Compositor"),
			B_TITLED_WINDOW,
			B_AUTO_UPDATE_SIZE_LIMITS | B_NOT_ZOOMABLE | B_ASYNCHRONOUS_CONTROLS),
	fAnimationsCheckBox(NULL),
	fBlurCheckBox(NULL),
	fTranslucencyCheckBox(NULL),
	fLoggingCheckBox(NULL),
	fDebugControlsCheckBox(NULL),
	fForceBlurAllCheckBox(NULL),
	fForceOpacityCheckBox(NULL),
	fForceOpacityOnlyOpaqueCheckBox(NULL),
	fShowOverlayCheckBox(NULL),
	fLogTimingsCheckBox(NULL),
	fStressInvalidateCheckBox(NULL),
	fForceOpacitySlider(NULL),
	fFpsSlider(NULL),
	fApplyButton(NULL)
{
	fAnimationsCheckBox = new BCheckBox("animations",
		B_TRANSLATE("Enable animations"),
		new BMessage(kMsgSettingsChanged));
	fBlurCheckBox = new BCheckBox("blur",
		B_TRANSLATE("Enable blur for system windows"),
		new BMessage(kMsgSettingsChanged));
	fTranslucencyCheckBox = new BCheckBox("translucency",
		B_TRANSLATE("Enable translucency for normal windows"),
		new BMessage(kMsgSettingsChanged));
	fLoggingCheckBox = new BCheckBox("logging",
		B_TRANSLATE("Enable compositor logging"),
		new BMessage(kMsgSettingsChanged));
	fDebugControlsCheckBox = new BCheckBox("debug_controls",
		B_TRANSLATE("Enable debug controls"),
		new BMessage(kMsgSettingsChanged));
	fForceBlurAllCheckBox = new BCheckBox("force_blur_all",
		B_TRANSLATE("Force blur-behind for all windows"),
		new BMessage(kMsgSettingsChanged));
	fForceOpacityCheckBox = new BCheckBox("force_opacity",
		B_TRANSLATE("Force opacity override"),
		new BMessage(kMsgSettingsChanged));
	fForceOpacityOnlyOpaqueCheckBox = new BCheckBox("force_opacity_only_opaque",
		B_TRANSLATE("Only apply forced opacity to opaque windows"),
		new BMessage(kMsgSettingsChanged));
	fShowOverlayCheckBox = new BCheckBox("show_overlay",
		B_TRANSLATE("Show compositor debug overlay"),
		new BMessage(kMsgSettingsChanged));
	fLogTimingsCheckBox = new BCheckBox("log_timings",
		B_TRANSLATE("Log blur/compositor timings"),
		new BMessage(kMsgSettingsChanged));
	fStressInvalidateCheckBox = new BCheckBox("stress_invalidate",
		B_TRANSLATE("Stress invalidate (disable blur cache)"),
		new BMessage(kMsgSettingsChanged));
	fForceOpacitySlider = new BSlider("force_opacity_value",
		B_TRANSLATE("Forced opacity:"), new BMessage(kMsgSettingsChanged),
		kMinOpacity, kMaxOpacity, B_HORIZONTAL);
	fForceOpacitySlider->SetLimitLabels("0.2", "1.0");
	fForceOpacitySlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fForceOpacitySlider->SetHashMarkCount(5);
	fForceOpacitySlider->SetModificationMessage(new BMessage(kMsgSettingsChanged));

	fFpsSlider = new BSlider("fps", B_TRANSLATE("Target frame rate:"),
		new BMessage(kMsgSettingsChanged), kMinFps, kMaxFps, B_HORIZONTAL);
	fFpsSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fFpsSlider->SetHashMarkCount(7);
	fFpsSlider->SetLimitLabels("30", "144");
	fFpsSlider->SetModificationMessage(new BMessage(kMsgSettingsChanged));

	fApplyButton = new BButton(B_TRANSLATE("Apply"),
		new BMessage(kMsgApplySettings));
	fApplyButton->SetEnabled(false);

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fAnimationsCheckBox)
		.Add(fBlurCheckBox)
		.Add(fTranslucencyCheckBox)
		.Add(fLoggingCheckBox)
		.Add(fDebugControlsCheckBox)
		.Add(fForceBlurAllCheckBox)
		.Add(fForceOpacityCheckBox)
		.Add(fForceOpacityOnlyOpaqueCheckBox)
		.Add(fForceOpacitySlider)
		.Add(fShowOverlayCheckBox)
		.Add(fLogTimingsCheckBox)
		.Add(fStressInvalidateCheckBox)
		.Add(fFpsSlider)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.AddGlue()
			.Add(fApplyButton)
		.End();

	_LoadSettings();
	_UpdateControls();
	_UpdateApplyState();
	CenterOnScreen();
}


bool
CompositorWindow::QuitRequested()
{
	be_app->PostMessage(B_QUIT_REQUESTED);
	return true;
}


void
CompositorWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSettingsChanged:
			fForceOpacitySlider->SetEnabled(
				fForceOpacityCheckBox->Value() == B_CONTROL_ON);
			_UpdateApplyState();
			break;
		case kMsgApplySettings:
			_SaveSettings();
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
CompositorWindow::_LoadSettings()
{
	_SetDefaults(fCurrent);
	fSaved = fCurrent;

	BPath path;
	if (_SettingsPath(path) != B_OK)
		return;

	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	BMessage settings;
	if (settings.Unflatten(&file) != B_OK)
		return;

	fCurrent.enableCompositor = settings.GetBool(kEnableCompositorKey,
		fCurrent.enableCompositor);
	fCurrent.enableAnimations = settings.GetBool(kEnableAnimationsKey,
		fCurrent.enableAnimations);
	fCurrent.enableBlur = settings.GetBool(kEnableBlurKey,
		fCurrent.enableBlur);
	fCurrent.enableTranslucency = settings.GetBool(kEnableTranslucencyKey,
		fCurrent.enableTranslucency);
	fCurrent.targetFps = settings.GetInt32(kTargetFpsKey, fCurrent.targetFps);
	fCurrent.logLevel = settings.GetInt32(kLogLevelKey, fCurrent.logLevel);
	fCurrent.debugControls = settings.GetBool(kDebugControlsKey,
		fCurrent.debugControls);
	fCurrent.forceBlurAll = settings.GetBool(kForceBlurAllKey,
		fCurrent.forceBlurAll);
	fCurrent.forceOpacity = settings.GetFloat(kForceOpacityKey,
		fCurrent.forceOpacity);
	fCurrent.forceOpacityOnlyOpaque = settings.GetBool(kForceOpacityOnlyOpaqueKey,
		fCurrent.forceOpacityOnlyOpaque);
	fCurrent.showOverlay = settings.GetBool(kShowOverlayKey,
		fCurrent.showOverlay);
	fCurrent.logTimings = settings.GetBool(kLogTimingsKey,
		fCurrent.logTimings);
	fCurrent.stressInvalidate = settings.GetBool(kStressInvalidateKey,
		fCurrent.stressInvalidate);

	if (fCurrent.targetFps <= 0)
		fCurrent.targetFps = 60;
	if (fCurrent.targetFps < kMinFps)
		fCurrent.targetFps = kMinFps;
	if (fCurrent.targetFps > kMaxFps)
		fCurrent.targetFps = kMaxFps;
	if (fCurrent.forceOpacity > 1.0f)
		fCurrent.forceOpacity = 1.0f;
	if (fCurrent.forceOpacity < 0.0f && fCurrent.forceOpacity != -1.0f)
		fCurrent.forceOpacity = -1.0f;

	fSaved = fCurrent;
}


void
CompositorWindow::_SaveSettings()
{
	fCurrent.enableCompositor = true;
	fCurrent.enableAnimations = fAnimationsCheckBox->Value() == B_CONTROL_ON;
	fCurrent.enableBlur = fBlurCheckBox->Value() == B_CONTROL_ON;
	fCurrent.enableTranslucency
		= fTranslucencyCheckBox->Value() == B_CONTROL_ON;
	fCurrent.targetFps = fFpsSlider->Value();
	fCurrent.logLevel = fLoggingCheckBox->Value() == B_CONTROL_ON ? 1 : 0;
	fCurrent.debugControls
		= fDebugControlsCheckBox->Value() == B_CONTROL_ON;
	fCurrent.forceBlurAll = fForceBlurAllCheckBox->Value() == B_CONTROL_ON;
	fCurrent.forceOpacity = fForceOpacityCheckBox->Value() == B_CONTROL_ON
		? (float)fForceOpacitySlider->Value() / 100.0f : -1.0f;
	fCurrent.forceOpacityOnlyOpaque
		= fForceOpacityOnlyOpaqueCheckBox->Value() == B_CONTROL_ON;
	fCurrent.showOverlay = fShowOverlayCheckBox->Value() == B_CONTROL_ON;
	fCurrent.logTimings = fLogTimingsCheckBox->Value() == B_CONTROL_ON;
	fCurrent.stressInvalidate
		= fStressInvalidateCheckBox->Value() == B_CONTROL_ON;

	BPath path;
	if (_SettingsPath(path) != B_OK)
		return;

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return;

	BMessage settings;
	settings.AddBool(kEnableCompositorKey, fCurrent.enableCompositor);
	settings.AddBool(kEnableAnimationsKey, fCurrent.enableAnimations);
	settings.AddBool(kEnableBlurKey, fCurrent.enableBlur);
	settings.AddBool(kEnableTranslucencyKey, fCurrent.enableTranslucency);
	settings.AddInt32(kTargetFpsKey, fCurrent.targetFps);
	settings.AddInt32(kLogLevelKey, fCurrent.logLevel);
	settings.AddBool(kDebugControlsKey, fCurrent.debugControls);
	settings.AddBool(kForceBlurAllKey, fCurrent.forceBlurAll);
	settings.AddFloat(kForceOpacityKey, fCurrent.forceOpacity);
	settings.AddBool(kForceOpacityOnlyOpaqueKey,
		fCurrent.forceOpacityOnlyOpaque);
	settings.AddBool(kShowOverlayKey, fCurrent.showOverlay);
	settings.AddBool(kLogTimingsKey, fCurrent.logTimings);
	settings.AddBool(kStressInvalidateKey, fCurrent.stressInvalidate);

	if (settings.Flatten(&file) != B_OK)
		return;

	fSaved = fCurrent;
	_UpdateControls();
	_UpdateApplyState();
	_SendDebugOptionsMessage();
	_SendReloadMessage();
}


void
CompositorWindow::_UpdateControls()
{
	fAnimationsCheckBox->SetValue(
		fSaved.enableAnimations ? B_CONTROL_ON : B_CONTROL_OFF);
	fBlurCheckBox->SetValue(
		fSaved.enableBlur ? B_CONTROL_ON : B_CONTROL_OFF);
	fTranslucencyCheckBox->SetValue(
		fSaved.enableTranslucency ? B_CONTROL_ON : B_CONTROL_OFF);
	fLoggingCheckBox->SetValue(
		fSaved.logLevel > 0 ? B_CONTROL_ON : B_CONTROL_OFF);
	fDebugControlsCheckBox->SetValue(
		fSaved.debugControls ? B_CONTROL_ON : B_CONTROL_OFF);
	fForceBlurAllCheckBox->SetValue(
		fSaved.forceBlurAll ? B_CONTROL_ON : B_CONTROL_OFF);
	fForceOpacityCheckBox->SetValue(
		fSaved.forceOpacity >= 0.0f ? B_CONTROL_ON : B_CONTROL_OFF);
	fForceOpacityOnlyOpaqueCheckBox->SetValue(
		fSaved.forceOpacityOnlyOpaque ? B_CONTROL_ON : B_CONTROL_OFF);
	fShowOverlayCheckBox->SetValue(
		fSaved.showOverlay ? B_CONTROL_ON : B_CONTROL_OFF);
	fLogTimingsCheckBox->SetValue(
		fSaved.logTimings ? B_CONTROL_ON : B_CONTROL_OFF);
	fStressInvalidateCheckBox->SetValue(
		fSaved.stressInvalidate ? B_CONTROL_ON : B_CONTROL_OFF);
	fForceOpacitySlider->SetValue((int32)((fSaved.forceOpacity >= 0.0f
		? fSaved.forceOpacity : 0.85f) * 100.0f));
	fForceOpacitySlider->SetEnabled(fSaved.forceOpacity >= 0.0f);
	fFpsSlider->SetValue(fSaved.targetFps);
}


void
CompositorWindow::_UpdateApplyState()
{
	fApplyButton->SetEnabled(_IsDirty());
}


void
CompositorWindow::_SendReloadMessage()
{
	BMessenger messenger("application/x-vnd.Haiku-app_server");
	if (!messenger.IsValid())
		return;

	BMessage reload(AS_INTERNAL_RELOAD_COMPOSITOR_SETTINGS);
	messenger.SendMessage(&reload);
}



void
CompositorWindow::_SendDebugOptionsMessage()
{
	BMessenger messenger("application/x-vnd.Haiku-app_server");
	if (!messenger.IsValid())
		return;

	BMessage debug(AS_INTERNAL_SET_COMPOSITOR_DEBUG_OPTIONS);
	debug.AddBool("force_blur_all", fSaved.forceBlurAll);
	debug.AddFloat("force_opacity", fSaved.forceOpacity);
	debug.AddBool("force_opacity_only_opaque",
		fSaved.forceOpacityOnlyOpaque);
	debug.AddBool("show_overlay", fSaved.showOverlay);
	debug.AddBool("log_timings", fSaved.logTimings);
	debug.AddBool("stress_invalidate", fSaved.stressInvalidate);

	BMessage reply;
	messenger.SendMessage(&debug, &reply);
}


void
CompositorWindow::_SetDefaults(Settings& settings)
{
	settings.enableCompositor = true;
	settings.enableAnimations = true;
	settings.enableBlur = false;
	settings.enableTranslucency = false;
	settings.targetFps = 60;
	settings.logLevel = 0;
	settings.debugControls = false;
	settings.forceBlurAll = false;
	settings.forceOpacity = -1.0f;
	settings.forceOpacityOnlyOpaque = true;
	settings.showOverlay = false;
	settings.logTimings = false;
	settings.stressInvalidate = false;
}


bool
CompositorWindow::_IsDirty() const
{
	bool animations = fAnimationsCheckBox->Value() == B_CONTROL_ON;
	bool blur = fBlurCheckBox->Value() == B_CONTROL_ON;
	bool translucency = fTranslucencyCheckBox->Value() == B_CONTROL_ON;
	bool logging = fLoggingCheckBox->Value() == B_CONTROL_ON;
	bool debugControls = fDebugControlsCheckBox->Value() == B_CONTROL_ON;
	bool forceBlurAll = fForceBlurAllCheckBox->Value() == B_CONTROL_ON;
	bool forceOpacityEnabled = fForceOpacityCheckBox->Value() == B_CONTROL_ON;
	float forceOpacity = forceOpacityEnabled
		? (float)fForceOpacitySlider->Value() / 100.0f : -1.0f;
	bool forceOpacityOnlyOpaque
		= fForceOpacityOnlyOpaqueCheckBox->Value() == B_CONTROL_ON;
	bool showOverlay = fShowOverlayCheckBox->Value() == B_CONTROL_ON;
	bool logTimings = fLogTimingsCheckBox->Value() == B_CONTROL_ON;
	bool stressInvalidate = fStressInvalidateCheckBox->Value() == B_CONTROL_ON;
	int32 fps = fFpsSlider->Value();

	return fSaved.enableAnimations != animations
		|| fSaved.enableBlur != blur
		|| fSaved.enableTranslucency != translucency
		|| (fSaved.logLevel > 0) != logging
		|| fSaved.debugControls != debugControls
		|| fSaved.forceBlurAll != forceBlurAll
		|| fSaved.forceOpacity != forceOpacity
		|| fSaved.forceOpacityOnlyOpaque != forceOpacityOnlyOpaque
		|| fSaved.showOverlay != showOverlay
		|| fSaved.logTimings != logTimings
		|| fSaved.stressInvalidate != stressInvalidate
		|| fSaved.targetFps != fps;
}


status_t
CompositorWindow::_SettingsPath(BPath& path) const
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;

	status = path.Append(kSettingsDir);
	if (status != B_OK)
		return status;

	status = create_directory(path.Path(), 0755);
	if (status != B_OK)
		return status;

	return path.Append(kSettingsFile);
}
