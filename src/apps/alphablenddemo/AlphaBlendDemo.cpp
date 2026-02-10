/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */
#include <Application.h>
#include <LayoutBuilder.h>
#include <Messenger.h>
#include <OS.h>
#include <Slider.h>
#include <String.h>
#include <View.h>
#include <Window.h>
#include <interface/WindowInfo.h>

#include <string.h>

#include <private/app/ServerProtocol.h>
#include <inttypes.h>

namespace {

const char* kAppSignature = "application/x-vnd.haiku-AlphaBlendDemo";
const uint32 kMsgAlphaChanged = 'alch';
const uint32 kMsgOffsetChanged = 'alof';
const uint32 kMsgWindowAlphaChanged = 'alwa';
const int32 kNullWindowToken = -1;

class AlphaBlendView : public BView {
public:
	AlphaBlendView()
		:
		BView("alpha blend view",
			B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_TRANSPARENT_BACKGROUND),
		fAlpha(0.6f),
		fBackgroundOffset(0.0f)
	{
		SetViewColor(B_TRANSPARENT_COLOR);
	}

	void SetAlpha(float alpha)
	{
		fAlpha = alpha;
		Invalidate();
	}

	void SetBackgroundOffset(float offset)
	{
		fBackgroundOffset = offset;
		Invalidate();
	}

	void Draw(BRect updateRect) override
	{
		BRect bounds = Bounds();
		SetDrawingMode(B_OP_COPY);
		SetHighColor(B_TRANSPARENT_COLOR);
		FillRect(updateRect);

		BRect checkerBounds = bounds;
		checkerBounds.top += fBackgroundOffset;
		if (checkerBounds.IsValid())
			_DrawCheckerboard(checkerBounds);

		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);

		if (fBackgroundOffset > 0.0f) {
			BRect gap(bounds.left, bounds.top, bounds.right,
				bounds.top + fBackgroundOffset - 1.0f);
			SetDrawingMode(B_OP_COPY);
			SetHighColor(B_TRANSPARENT_COLOR);
			FillRect(gap);
			SetDrawingMode(B_OP_ALPHA);
			SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);
		}

		BRect backdrop(bounds.left + 20, bounds.top + 20,
			bounds.right - 20, bounds.top + 140);
		SetHighColor(60, 60, 60, (uint8)(fAlpha * 180.0f));
		FillRoundRect(backdrop, 10, 10);

		BRect leftRect(bounds.left + 40, bounds.top + 40,
			bounds.left + bounds.Width() * 0.6f, bounds.top + bounds.Height() * 0.6f);
		BRect rightRect(bounds.left + bounds.Width() * 0.35f, bounds.top + 80,
			bounds.right - 40, bounds.bottom - 80);

		SetHighColor(255, 80, 80, (uint8)(fAlpha * 255.0f));
		FillRoundRect(leftRect, 12, 12);

		SetHighColor(80, 140, 255, (uint8)(fAlpha * 255.0f));
		FillRoundRect(rightRect, 12, 12);

		SetHighColor(30, 30, 30, 255);
		SetDrawingMode(B_OP_OVER);
		DrawString("Drag the slider to adjust alpha blending (shapes + background).",
			BPoint(bounds.left + 16, bounds.bottom - 16));
	}

private:
	void _DrawCheckerboard(const BRect& bounds)
	{
		const float cell = 24.0f;
		const rgb_color light = {220, 220, 220, 255};
		const rgb_color dark = {200, 200, 200, 255};

		int32 startRow = (int32)(bounds.top / cell);
		int32 startCol = (int32)(bounds.left / cell);
		for (float y = bounds.top; y < bounds.bottom; y += cell, startRow++) {
			int32 col = startCol;
			for (float x = bounds.left; x < bounds.right; x += cell, col++) {
				bool isDark = ((startRow + col) % 2) == 0;
				SetHighColor(isDark ? dark : light);
				FillRect(BRect(x, y, x + cell - 1, y + cell - 1));
			}
		}
	}

	float fAlpha;
	float fBackgroundOffset;
};

class AlphaBlendWindow : public BWindow {
public:
	AlphaBlendWindow()
		:
		BWindow(BRect(100, 100, 700, 520), "Alpha Blend Demo",
			B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE),
		fBlendView(new AlphaBlendView()),
		fSlider(new BSlider("alpha slider", "Alpha: 60%", new BMessage(kMsgAlphaChanged),
			0, 100, B_HORIZONTAL)),
		fOffsetSlider(new BSlider("offset slider", "Background offset: 0",
			new BMessage(kMsgOffsetChanged), 0, 200, B_HORIZONTAL)),
		fWindowAlphaSlider(new BSlider("window alpha slider", "Window alpha: 100%",
			new BMessage(kMsgWindowAlphaChanged), 0, 100, B_HORIZONTAL)),
		fWindowToken(kNullWindowToken)
	{
		fSlider->SetValue(60);
		fSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
		fSlider->SetHashMarkCount(6);

		fOffsetSlider->SetValue(0);
		fOffsetSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
		fOffsetSlider->SetHashMarkCount(5);

		fWindowAlphaSlider->SetValue(85);
		fWindowAlphaSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
		fWindowAlphaSlider->SetHashMarkCount(6);
		fWindowAlphaSlider->SetLabel("Window alpha: 85%");

		BLayoutBuilder::Group<>(this, B_VERTICAL, 10)
			.SetInsets(10, 10, 10, 10)
			.Add(fBlendView)
			.Add(fSlider)
			.Add(fOffsetSlider)
			.Add(fWindowAlphaSlider);
	}

	void MessageReceived(BMessage* message) override
	{
		if (message->what == kMsgAlphaChanged) {
			int32 value = fSlider->Value();
			float alpha = value / 100.0f;
			fBlendView->SetAlpha(alpha);
			BString label;
			label.SetToFormat("Alpha: %" B_PRId32 "%%", value);
			fSlider->SetLabel(label.String());
			return;
		}
		if (message->what == kMsgOffsetChanged) {
			int32 value = fOffsetSlider->Value();
			fBlendView->SetBackgroundOffset((float)value);
			BString label;
			label.SetToFormat("Background offset: %" B_PRId32, value);
			fOffsetSlider->SetLabel(label.String());
			return;
		}
		if (message->what == kMsgWindowAlphaChanged) {
			int32 value = fWindowAlphaSlider->Value();
			_SetWindowAlpha(value / 100.0f);
			BString label;
			label.SetToFormat("Window alpha: %" B_PRId32 "%%", value);
			fWindowAlphaSlider->SetLabel(label.String());
			return;
		}

		BWindow::MessageReceived(message);
	}

	void ApplyInitialWindowAlpha()
	{
		_SetWindowAlpha(fWindowAlphaSlider->Value() / 100.0f);
	}

private:
	void _SetWindowAlpha(float alpha)
	{
		if (fWindowToken == kNullWindowToken)
			fWindowToken = _ResolveWindowToken();
		if (fWindowToken == kNullWindowToken)
			return;

		BMessenger messenger("application/x-vnd.Haiku-app_server");
		if (!messenger.IsValid())
			return;

		BMessage message(AS_INTERNAL_SET_WINDOW_ALPHA);
		message.AddInt32("window", fWindowToken);
		message.AddFloat("alpha", alpha);
		messenger.SendMessage(&message);
	}

	int32 _ResolveWindowToken() const
	{
		team_info info;
		if (get_team_info(B_CURRENT_TEAM, &info) != B_OK)
			return kNullWindowToken;

		int32 count = 0;
		int32* tokens = get_token_list(info.team, &count);
		if (tokens == NULL)
			return kNullWindowToken;

		int32 resolved = kNullWindowToken;
		for (int32 i = 0; i < count; i++) {
			client_window_info* windowInfo = get_window_info(tokens[i]);
			if (windowInfo != NULL && windowInfo->name[0] != '\0'
				&& strcmp(windowInfo->name, Title()) == 0) {
				resolved = tokens[i];
				free(windowInfo);
				break;
			}
			free(windowInfo);
		}

		free(tokens);
		return resolved;
	}

	AlphaBlendView* fBlendView;
	BSlider* fSlider;
	BSlider* fOffsetSlider;
	BSlider* fWindowAlphaSlider;
	int32 fWindowToken;
};

class AlphaBlendApp : public BApplication {
public:
	AlphaBlendApp()
		:
		BApplication(kAppSignature)
	{
	}

	void ReadyToRun() override
	{
		AlphaBlendWindow* window = new AlphaBlendWindow();
		window->Show();
		window->ApplyInitialWindowAlpha();
	}
};

} // namespace


int
main()
{
	AlphaBlendApp app;
	app.Run();
	return 0;
}
