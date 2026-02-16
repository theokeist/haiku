/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */

#include <Application.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <Messenger.h>
#include <Slider.h>
#include <StringView.h>
#include <Window.h>

#include <private/app/ServerProtocol.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "WindowEffectsDemo"


extern "C" status_t _safe_get_server_token_(const BLooper*, int32*);

namespace {

const int32 kMsgUpdateEffects = 'efct';
const int32 kMsgToggleAnimate = 'anim';

} // namespace


class WindowEffectsWindow : public BWindow {
public:
	WindowEffectsWindow();
	void MessageReceived(BMessage* message) override;

private:
	void _EnsureServerToken();
	void _SendEffects();
	void _UpdateDurationState();

	int32 fServerToken;
	BSlider* fAlphaSlider;
	BCheckBox* fBlurCheckBox;
	BSlider* fBlurRadiusSlider;
	BCheckBox* fAnimateCheckBox;
	BSlider* fDurationSlider;
	BMessenger fAppServer;
};


WindowEffectsWindow::WindowEffectsWindow()
	:
	BWindow(BRect(0, 0, 420, 260), B_TRANSLATE_SYSTEM_NAME("Window Effects"),
		B_TITLED_WINDOW, B_AUTO_UPDATE_SIZE_LIMITS),
	fServerToken(B_NULL_TOKEN),
	fAlphaSlider(NULL),
	fBlurCheckBox(NULL),
	fBlurRadiusSlider(NULL),
	fAnimateCheckBox(NULL),
	fDurationSlider(NULL),
	fAppServer("application/x-vnd.Haiku-app_server")
{
	fAlphaSlider = new BSlider("alpha", B_TRANSLATE("Alpha"),
		new BMessage(kMsgUpdateEffects), 0, 100, B_HORIZONTAL);
	fAlphaSlider->SetLimitLabels("0", "1.0");
	fAlphaSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fAlphaSlider->SetHashMarkCount(5);
	fAlphaSlider->SetValue(100);
	fAlphaSlider->SetModificationMessage(new BMessage(kMsgUpdateEffects));

	fBlurCheckBox = new BCheckBox("blur", B_TRANSLATE("Enable blur"),
		new BMessage(kMsgUpdateEffects));

	fBlurRadiusSlider = new BSlider("radius",
		B_TRANSLATE("Blur radius"), new BMessage(kMsgUpdateEffects),
		0, 20, B_HORIZONTAL);
	fBlurRadiusSlider->SetLimitLabels("0", "20");
	fBlurRadiusSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fBlurRadiusSlider->SetHashMarkCount(5);
	fBlurRadiusSlider->SetValue(6);
	fBlurRadiusSlider->SetModificationMessage(
		new BMessage(kMsgUpdateEffects));

	fAnimateCheckBox = new BCheckBox("animate",
		B_TRANSLATE("Animate alpha changes"),
		new BMessage(kMsgToggleAnimate));

	fDurationSlider = new BSlider("duration",
		B_TRANSLATE("Duration (ms)"), new BMessage(kMsgUpdateEffects),
		50, 500, B_HORIZONTAL);
	fDurationSlider->SetLimitLabels("50", "500");
	fDurationSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fDurationSlider->SetHashMarkCount(5);
	fDurationSlider->SetValue(150);
	fDurationSlider->SetModificationMessage(
		new BMessage(kMsgUpdateEffects));

	BLayoutBuilder::Group<>(this, B_VERTICAL, B_USE_DEFAULT_SPACING)
		.SetInsets(B_USE_WINDOW_SPACING)
		.Add(fAlphaSlider)
		.Add(fBlurCheckBox)
		.Add(fBlurRadiusSlider)
		.Add(fAnimateCheckBox)
		.Add(fDurationSlider)
		.Add(new BStringView("hint",
			B_TRANSLATE("Changes are applied to this window.")));

	_UpdateDurationState();
	CenterOnScreen();
}


void
WindowEffectsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgToggleAnimate:
			_UpdateDurationState();
			_SendEffects();
			break;
		case kMsgUpdateEffects:
			_SendEffects();
			break;
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


void
WindowEffectsWindow::_EnsureServerToken()
{
	if (fServerToken != B_NULL_TOKEN)
		return;
	_safe_get_server_token_(this, &fServerToken);
}


void
WindowEffectsWindow::_SendEffects()
{
	_EnsureServerToken();
	if (fServerToken == B_NULL_TOKEN || !fAppServer.IsValid())
		return;

	float alpha = fAlphaSlider->Value() / 100.0f;
	bool blurEnabled = fBlurCheckBox->Value() == B_CONTROL_ON;
	float blurRadius = fBlurRadiusSlider->Value();
	bool animate = fAnimateCheckBox->Value() == B_CONTROL_ON;
	bigtime_t duration = (bigtime_t)fDurationSlider->Value() * 1000;

	BMessage message(AS_PRIVATE_SET_WINDOW_EFFECTS);
	message.AddInt32("window", fServerToken);
	message.AddFloat("alpha", alpha);
	message.AddBool("blur", blurEnabled);
	message.AddFloat("blur_radius", blurRadius);
	if (animate) {
		message.AddBool("animate", true);
		message.AddInt64("duration", duration);
	}

	fAppServer.SendMessage(&message);
}


void
WindowEffectsWindow::_UpdateDurationState()
{
	bool enabled = fAnimateCheckBox->Value() == B_CONTROL_ON;
	fDurationSlider->SetEnabled(enabled);
}


class WindowEffectsApp : public BApplication {
public:
	WindowEffectsApp()
		:
		BApplication("application/x-vnd.Haiku-WindowEffectsDemo")
	{
	}

	void ReadyToRun() override
	{
		WindowEffectsWindow* window = new WindowEffectsWindow();
		window->Show();
	}
};


int
main()
{
	WindowEffectsApp app;
	app.Run();
	return 0;
}
