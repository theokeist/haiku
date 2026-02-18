/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */
#ifndef COMPOSITOR_WINDOW_H
#define COMPOSITOR_WINDOW_H

#include <Path.h>
#include <String.h>
#include <Window.h>

class BButton;
class BCheckBox;
class BSlider;
class BTextControl;

class CompositorWindow : public BWindow {
public:
						CompositorWindow();
	bool				QuitRequested() override;
	void				MessageReceived(BMessage* message) override;

private:
	struct Settings {
		bool		enableCompositor;
		bool		enableAnimations;
		bool		enableBlur;
		bool		enableTranslucency;
		int32		targetFps;
		int32		logLevel;
		bool		debugControls;
		bool		forceBlurAll;
		float		forceOpacity;
		bool		forceOpacityOnlyOpaque;
		bool		showOverlay;
		bool		logTimings;
		bool		stressInvalidate;
		bool		enableTitleBlurPolicy;
		bool		enableFloatingUntitledBlurPolicy;
		BString		blurPolicyTokens;
		BString		forceOpacityColor;
	};

	void				_LoadSettings();
	void				_SaveSettings();
	void				_UpdateControls();
	void				_UpdateApplyState();
	void				_SendReloadMessage();
	void				_SendDebugOptionsMessage();
	void				_SetDefaults(Settings& settings);
	bool				_IsDirty() const;
	status_t			_SettingsPath(BPath& path) const;

	Settings			fCurrent;
	Settings			fSaved;

	BCheckBox* 			fAnimationsCheckBox;
	BCheckBox* 			fBlurCheckBox;
	BCheckBox* 			fTranslucencyCheckBox;
	BCheckBox* 			fLoggingCheckBox;
	BCheckBox* 			fDebugControlsCheckBox;
	BCheckBox* 			fForceBlurAllCheckBox;
	BCheckBox* 			fForceOpacityCheckBox;
	BCheckBox* 			fForceOpacityOnlyOpaqueCheckBox;
	BCheckBox* 			fShowOverlayCheckBox;
	BCheckBox* 			fLogTimingsCheckBox;
	BCheckBox* 			fStressInvalidateCheckBox;
	BCheckBox*			fTitleBlurPolicyCheckBox;
	BCheckBox*			fFloatingUntitledBlurPolicyCheckBox;
	BTextControl*		fBlurPolicyTokensControl;
	BTextControl*		fForceOpacityColorControl;
	BSlider*			fForceOpacitySlider;
	BSlider*			fFpsSlider;
	BButton*			fApplyButton;
};

#endif // COMPOSITOR_WINDOW_H
