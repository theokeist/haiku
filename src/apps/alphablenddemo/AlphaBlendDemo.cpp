/*
 * Copyright 2026, Haiku.
 * Distributed under the terms of the MIT License.
 */
#include <Application.h>
#include <LayoutBuilder.h>
#include <Slider.h>
#include <String.h>
#include <View.h>
#include <Window.h>
#include <inttypes.h>

namespace {

const char* kAppSignature = "application/x-vnd.haiku-AlphaBlendDemo";
const uint32 kMsgAlphaChanged = 'alch';

class AlphaBlendView : public BView {
public:
	AlphaBlendView()
		:
		BView("alpha blend view", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE),
		fAlpha(0.6f)
	{
		SetViewColor(ui_color(B_PANEL_BACKGROUND_COLOR));
	}

	void SetAlpha(float alpha)
	{
		fAlpha = alpha;
		Invalidate();
	}

	void Draw(BRect updateRect) override
	{
		BRect bounds = Bounds();
		SetLowColor(ui_color(B_PANEL_BACKGROUND_COLOR));
		FillRect(updateRect, B_SOLID_LOW);

		_DrawCheckerboard(bounds);

		SetDrawingMode(B_OP_ALPHA);
		SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_OVERLAY);

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

		for (float y = bounds.top; y < bounds.bottom; y += cell) {
			for (float x = bounds.left; x < bounds.right; x += cell) {
				bool isDark = ((int)(x / cell) + (int)(y / cell)) % 2 == 0;
				SetHighColor(isDark ? dark : light);
				FillRect(BRect(x, y, x + cell - 1, y + cell - 1));
			}
		}
	}

	float fAlpha;
};

class AlphaBlendWindow : public BWindow {
public:
	AlphaBlendWindow()
		:
		BWindow(BRect(100, 100, 700, 520), "Alpha Blend Demo",
			B_TITLED_WINDOW, B_QUIT_ON_WINDOW_CLOSE),
		fBlendView(new AlphaBlendView()),
		fSlider(new BSlider("alpha slider", "Alpha: 60%", new BMessage(kMsgAlphaChanged),
			0, 100, B_HORIZONTAL))
	{
		fSlider->SetValue(60);
		fSlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
		fSlider->SetHashMarkCount(6);

		BLayoutBuilder::Group<>(this, B_VERTICAL, 10)
			.SetInsets(10, 10, 10, 10)
			.Add(fBlendView)
			.Add(fSlider);
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

		BWindow::MessageReceived(message);
	}

private:
	AlphaBlendView* fBlendView;
	BSlider* fSlider;
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
