/*
 *  Copyright 2010-2020 Haiku, Inc. All rights reserved.
 *  Distributed under the terms of the MIT license.
 *
 *	Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Alexander von Gluck <kallisti5@unixzen.com>
 *		John Scipione <jscipione@gmail.com>
 *		Ryan Leavengood <leavengood@gmail.com>
 */


#include "LookAndFeelSettingsView.h"

#include <stdio.h>
#include <stdlib.h>

#include <inttypes.h>

#include <Alert.h>
#include <Alignment.h>
#include <AppFileInfo.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <InterfaceDefs.h>
#include <InterfacePrivate.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <PathFinder.h>
#include <PopUpMenu.h>
#include <ScrollBar.h>
#include <Messenger.h>
#include <StringView.h>
#include <Size.h>
#include <Slider.h>
#include <SpaceLayoutItem.h>
#include <StringView.h>
#include <TextView.h>

#include "AppearanceWindow.h"
#include "FakeScrollBar.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DecorSettingsView"
	// This was not renamed to keep from breaking translations


static const int32 kMsgSetDecor = 'deco';
static const int32 kMsgDecorInfo = 'idec';

static const int32 kMsgSetControlLook = 'ctlk';
static const int32 kMsgControlLookInfo = 'iclk';

static const int32 kMsgDoubleScrollBarArrows = 'dsba';

static const int32 kMsgArrowStyleSingle = 'mass';
static const int32 kMsgArrowStyleDouble = 'masd';
static const int32 kMsgAlphaDebugEnabled = 'adbg';
static const int32 kMsgCompositorEffectChanged = 'cech';
static const int32 kMsgSetCompositorDebugOptions = 'cDbg';

static const bool kDefaultDoubleScrollBarArrowsSetting = false;


//	#pragma mark - LookAndFeelSettingsView


LookAndFeelSettingsView::LookAndFeelSettingsView(const char* name)
	:
	BView(name, 0),
	fDecorInfoButton(NULL),
	fDecorMenuField(NULL),
	fDecorMenu(NULL),
	fControlLookInfoButton(NULL),
	fControlLookMenuField(NULL),
	fControlLookMenu(NULL),
	fArrowStyleSingle(NULL),
	fArrowStyleDouble(NULL),
	fAlphaDebugCheckBox(NULL),
	fForceEffectsAllCheckBox(NULL),
	fShowOverlayCheckBox(NULL),
	fLogTimingsCheckBox(NULL),
	fStressInvalidateCheckBox(NULL),
	fForceOpacitySlider(NULL),
	fSavedDecor(NULL),
	fCurrentDecor(NULL),
	fSavedControlLook(NULL),
	fCurrentControlLook(NULL),
	fSavedDoubleArrowsValue(_DoubleScrollBarArrows()),
	fSavedAlphaDebugEnabled(false),
	fCurrentAlphaDebugEnabled(false),
	fSavedForceEffectsAll(false),
	fSavedShowOverlay(false),
	fSavedLogTimings(false),
	fSavedStressInvalidate(false),
	fSavedForceOpacity(-1.0f),
	fForceEffectsAll(false),
	fShowOverlay(false),
	fLogTimings(false),
	fStressInvalidate(false),
	fForceOpacity(-1.0f)
{
	fCurrentDecor = fDecorUtility.CurrentDecorator()->ShortcutName();
	fSavedDecor = fCurrentDecor;
	fCurrentAlphaDebugEnabled = _AlphaDebugEnabled();
	fSavedAlphaDebugEnabled = fCurrentAlphaDebugEnabled;
	_LoadCompositorEffectsSettings();
	fSavedForceEffectsAll = fForceEffectsAll;
	fSavedShowOverlay = fShowOverlay;
	fSavedLogTimings = fLogTimings;
	fSavedStressInvalidate = fStressInvalidate;
	fSavedForceOpacity = fForceOpacity;

	// Decorator menu
	_BuildDecorMenu();
	fDecorMenuField = new BMenuField("decorator",
		B_TRANSLATE("Decorator:"), fDecorMenu);

	fDecorInfoButton = new BButton(B_TRANSLATE("About"),
		new BMessage(kMsgDecorInfo));

	BPrivate::get_control_look(fCurrentControlLook);
	fSavedControlLook = fCurrentControlLook;

	// ControlLook menu
	_BuildControlLookMenu();
	fControlLookMenuField = new BMenuField("controllook",
		B_TRANSLATE("ControlLook:"), fControlLookMenu);
	fControlLookMenuField->SetToolTip(
		B_TRANSLATE("No effect on running applications"));

	fControlLookInfoButton = new BButton(B_TRANSLATE("About"),
		new BMessage(kMsgControlLookInfo));

	// scroll bar arrow style
	BBox* arrowStyleBox = new BBox("arrow style");
	arrowStyleBox->SetLabel(B_TRANSLATE("Arrow style"));

	fArrowStyleSingle = new FakeScrollBar(true, false,
		new BMessage(kMsgArrowStyleSingle));
	fArrowStyleDouble = new FakeScrollBar(true, true,
		new BMessage(kMsgArrowStyleDouble));
	fAlphaDebugCheckBox = new BCheckBox("alpha_debug_controls",
		B_TRANSLATE("Enable alpha debug controls"),
		new BMessage(kMsgAlphaDebugEnabled));
	fForceEffectsAllCheckBox = new BCheckBox("force_effects_all",
		B_TRANSLATE("Force compositor effects for all windows"),
		new BMessage(kMsgCompositorEffectChanged));
	fShowOverlayCheckBox = new BCheckBox("show_compositor_overlay",
		B_TRANSLATE("Show compositor debug overlay"),
		new BMessage(kMsgCompositorEffectChanged));
	fLogTimingsCheckBox = new BCheckBox("log_compositor_timings",
		B_TRANSLATE("Log compositor timings"),
		new BMessage(kMsgCompositorEffectChanged));
	fStressInvalidateCheckBox = new BCheckBox("stress_invalidate",
		B_TRANSLATE("Stress invalidation (disable blur cache)"),
		new BMessage(kMsgCompositorEffectChanged));
	fForceOpacitySlider = new BSlider("force_opacity", "Force opacity: none",
		new BMessage(kMsgCompositorEffectChanged), 0, 100, B_HORIZONTAL);
	fForceOpacitySlider->SetHashMarks(B_HASH_MARKS_BOTTOM);
	fForceOpacitySlider->SetHashMarkCount(6);

	BView* arrowStyleView;
	arrowStyleView = BLayoutBuilder::Group<>()
		.AddGroup(B_VERTICAL, 1)
			.Add(new BStringView("single", B_TRANSLATE("Single:")))
			.Add(fArrowStyleSingle)
			.AddStrut(B_USE_DEFAULT_SPACING)
			.Add(new BStringView("double", B_TRANSLATE("Double:")))
			.Add(fArrowStyleDouble)
			.SetInsets(B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING,
				B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
			.End()
		.View();
	arrowStyleBox->AddChild(arrowStyleView);
	arrowStyleBox->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_CENTER));
	arrowStyleBox->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BStringView* scrollBarLabel
		= new BStringView("scroll bar", B_TRANSLATE("Scroll bar:"));
	scrollBarLabel->SetExplicitAlignment(
		BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));

	// control layout
	BLayoutBuilder::Grid<>(this, B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
		.Add(fDecorMenuField->CreateLabelLayoutItem(), 0, 0)
		.Add(fDecorMenuField->CreateMenuBarLayoutItem(), 1, 0)
		.Add(fDecorInfoButton, 2, 0)
		.Add(fControlLookMenuField->CreateLabelLayoutItem(), 0, 1)
		.Add(fControlLookMenuField->CreateMenuBarLayoutItem(), 1, 1)
		.Add(fControlLookInfoButton, 2, 1)
		.Add(scrollBarLabel, 0, 2)
		.Add(arrowStyleBox, 1, 2)
		.Add(fAlphaDebugCheckBox, 0, 3, 3)
		.Add(fForceEffectsAllCheckBox, 0, 4, 3)
		.Add(fForceOpacitySlider, 0, 5, 3)
		.Add(fShowOverlayCheckBox, 0, 6, 3)
		.Add(fLogTimingsCheckBox, 0, 7, 3)
		.Add(fStressInvalidateCheckBox, 0, 8, 3)
		.AddGlue(0, 9)
		.SetInsets(B_USE_WINDOW_SPACING);

	// TODO : Decorator Preview Image?
}


LookAndFeelSettingsView::~LookAndFeelSettingsView()
{
}


void
LookAndFeelSettingsView::AttachedToWindow()
{
	AdoptParentColors();

	if (Parent() == NULL)
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fDecorMenu->SetTargetForItems(this);
	fDecorInfoButton->SetTarget(this);
	fControlLookMenu->SetTargetForItems(this);
	fControlLookInfoButton->SetTarget(this);
	fArrowStyleSingle->SetTarget(this);
	fArrowStyleDouble->SetTarget(this);
	fAlphaDebugCheckBox->SetTarget(this);
	fForceEffectsAllCheckBox->SetTarget(this);
	fShowOverlayCheckBox->SetTarget(this);
	fLogTimingsCheckBox->SetTarget(this);
	fStressInvalidateCheckBox->SetTarget(this);
	fForceOpacitySlider->SetTarget(this);

	if (fSavedDoubleArrowsValue)
		fArrowStyleDouble->SetValue(B_CONTROL_ON);
	else
		fArrowStyleSingle->SetValue(B_CONTROL_ON);

	if (fCurrentAlphaDebugEnabled)
		fAlphaDebugCheckBox->SetValue(B_CONTROL_ON);

	fForceEffectsAllCheckBox->SetValue(fForceEffectsAll ? B_CONTROL_ON
		: B_CONTROL_OFF);
	fShowOverlayCheckBox->SetValue(fShowOverlay ? B_CONTROL_ON : B_CONTROL_OFF);
	fLogTimingsCheckBox->SetValue(fLogTimings ? B_CONTROL_ON : B_CONTROL_OFF);
	fStressInvalidateCheckBox->SetValue(fStressInvalidate ? B_CONTROL_ON
		: B_CONTROL_OFF);
	int32 sliderValue = fForceOpacity < 0.0f ? 0
		: (int32)(fForceOpacity * 100.0f + 0.5f);
	fForceOpacitySlider->SetValue(sliderValue);
	if (fForceOpacity < 0.0f)
		fForceOpacitySlider->SetLabel(B_TRANSLATE("Force opacity: none"));
	else {
		BString label;
		label.SetToFormat("Force opacity: %" B_PRId32 "%%", sliderValue);
		fForceOpacitySlider->SetLabel(label.String());
	}
}


void
LookAndFeelSettingsView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSetDecor:
		{
			BString newDecor;
			if (message->FindString("decor", &newDecor) == B_OK)
				_SetDecor(newDecor);
			break;
		}

		case kMsgDecorInfo:
		{
			DecorInfo* decor = fDecorUtility.FindDecorator(fCurrentDecor);
			if (decor == NULL)
				break;

			BString authorsText(decor->Authors().String());
			authorsText.ReplaceAll(", ", "\n\t");

			BString infoText(B_TRANSLATE("%decorName\n\n"
				"Authors:\n\t%decorAuthors\n\n"
				"URL: %decorURL\n"
				"License: %decorLic\n\n"
				"%decorDesc\n"));

			infoText.ReplaceFirst("%decorName", decor->Name().String());
			infoText.ReplaceFirst("%decorAuthors", authorsText.String());
			infoText.ReplaceFirst("%decorLic", decor->LicenseName().String());
			infoText.ReplaceFirst("%decorURL", decor->SupportURL().String());
			infoText.ReplaceFirst("%decorDesc",
				decor->ShortDescription().String());

			BAlert* infoAlert = new BAlert(B_TRANSLATE("About decorator"),
				infoText.String(), B_TRANSLATE("OK"));
			infoAlert->SetFlags(infoAlert->Flags() | B_CLOSE_ON_ESCAPE);
			infoAlert->Go();

			break;
		}

		case kMsgSetControlLook:
		{
			BString controlLook;
			if (message->FindString("control_look", &controlLook) == B_OK)
				_SetControlLook(controlLook);
			break;
		}

		case kMsgControlLookInfo:
		{
			BString infoText(B_TRANSLATE("Default Haiku ControlLook"));
			BString path;
			if (!BPrivate::get_control_look(path))
				break;

			if (path.Length() > 0) {
				BFile file(path.String(), B_READ_ONLY);
				if (file.InitCheck() != B_OK)
					break;

				BAppFileInfo info(&file);
				struct version_info version;
				if (info.InitCheck() != B_OK
					|| info.GetVersionInfo(&version, B_APP_VERSION_KIND) != B_OK)
					break;

				infoText = version.short_info;
				infoText << "\n" << version.long_info;
			}

			BAlert* infoAlert = new BAlert(B_TRANSLATE("About control look"),
				infoText.String(), B_TRANSLATE("OK"));
			infoAlert->SetFlags(infoAlert->Flags() | B_CLOSE_ON_ESCAPE);
			infoAlert->Go();

			break;
		}

		case kMsgArrowStyleSingle:
			_SetDoubleScrollBarArrows(false);
			break;

		case kMsgArrowStyleDouble:
			_SetDoubleScrollBarArrows(true);
			break;
		case kMsgAlphaDebugEnabled:
		{
			bool enabled = fAlphaDebugCheckBox->Value() == B_CONTROL_ON;
			_SetAlphaDebugEnabled(enabled);
			break;
		}

		case kMsgCompositorEffectChanged:
		{
		fForceEffectsAll = fForceEffectsAllCheckBox->Value() == B_CONTROL_ON;
		fShowOverlay = fShowOverlayCheckBox->Value() == B_CONTROL_ON;
		fLogTimings = fLogTimingsCheckBox->Value() == B_CONTROL_ON;
		fStressInvalidate = fStressInvalidateCheckBox->Value() == B_CONTROL_ON;
		int32 value = fForceOpacitySlider->Value();
		fForceOpacity = value <= 0 ? -1.0f : value / 100.0f;
		BString label;
		if (fForceOpacity < 0.0f)
			label = B_TRANSLATE("Force opacity: none");
		else
			label.SetToFormat("Force opacity: %" B_PRId32 "%%", value);
		fForceOpacitySlider->SetLabel(label.String());
		_SaveCompositorEffectsSettings();
		_SendCompositorEffectsMessage();
		Window()->PostMessage(kMsgUpdate);
		break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


//	#pragma mark - LookAndFeelSettingsView private methods


void
LookAndFeelSettingsView::_SetDecor(const BString& name)
{
	_SetDecor(fDecorUtility.FindDecorator(name));
}


void
LookAndFeelSettingsView::_SetDecor(DecorInfo* decorInfo)
{
	if (fDecorUtility.SetDecorator(decorInfo) == B_OK) {
		fCurrentDecor = fDecorUtility.CurrentDecorator()->ShortcutName();
		BString decorName = fDecorUtility.CurrentDecorator()->Name();
		fDecorMenu->FindItem(_DecorLabel(decorName))->SetMarked(true);
		Window()->PostMessage(kMsgUpdate);
	}
}


void
LookAndFeelSettingsView::_BuildDecorMenu()
{
	fDecorMenu = new BPopUpMenu(B_TRANSLATE("Choose Decorator"));

	// collect the current system decor settings
	int32 count = fDecorUtility.CountDecorators();
	for (int32 i = 0; i < count; ++i) {
		DecorInfo* decor = fDecorUtility.DecoratorAt(i);
		if (decor == NULL) {
			fprintf(stderr, "Decorator : error NULL entry @ %" B_PRId32
				" / %" B_PRId32 "\n", i, count);
			continue;
		}

		BMessage* message = new BMessage(kMsgSetDecor);
		message->AddString("decor", decor->ShortcutName());

		BMenuItem* item = new BMenuItem(_DecorLabel(decor->Name()), message);
		fDecorMenu->AddItem(item);
		if (decor->ShortcutName() == fCurrentDecor)
			item->SetMarked(true);
	}
}


const char*
LookAndFeelSettingsView::_DecorLabel(const BString& name)
{
	BString label(name);
	return label.RemoveLast("Decorator").Trim().String();
}


void
LookAndFeelSettingsView::_SetControlLook(const BString& path)
{
	fCurrentControlLook = path;
	BPrivate::set_control_look(path);

	if (path.Length() > 0) {
		BEntry entry(path.String());
		const char* label = _ControlLookLabel(entry.Name());
		fControlLookMenu->FindItem(label)->SetMarked(true);
	} else
		fControlLookMenu->FindItem(B_TRANSLATE("Default"))->SetMarked(true);

	Window()->PostMessage(kMsgUpdate);
}


void
LookAndFeelSettingsView::_BuildControlLookMenu()
{
	BPathFinder pathFinder;
	BStringList paths;
	BDirectory dir;

	fControlLookMenu = new BPopUpMenu(B_TRANSLATE("Choose ControlLook"));

	BMessage* message = new BMessage(kMsgSetControlLook);
	message->AddString("control_look", "");

	BMenuItem* item = new BMenuItem(B_TRANSLATE("Default"), message);
	if (fCurrentControlLook.Length() == 0)
		item->SetMarked(true);
	fControlLookMenu->AddItem(item);

	status_t error = pathFinder.FindPaths(B_FIND_PATH_ADD_ONS_DIRECTORY,
		"control_look", paths);

	int32 count = paths.CountStrings();
	for (int32 i = 0; i < count; ++i) {
		if (error != B_OK || dir.SetTo(paths.StringAt(i)) != B_OK)
			continue;

		BEntry entry;
		while (dir.GetNextEntry(&entry) == B_OK) {
			BPath path(paths.StringAt(i), entry.Name());
			message = new BMessage(kMsgSetControlLook);
			message->AddString("control_look", path.Path());

			item = new BMenuItem(_ControlLookLabel(entry.Name()), message);
			fControlLookMenu->AddItem(item);
			if (BString(path.Path()) == fCurrentControlLook)
				item->SetMarked(true);
		}
	}
}


const char*
LookAndFeelSettingsView::_ControlLookLabel(const char* name)
{
	BString label(name);
	return label.RemoveLast("ControlLook").Trim().String();
}


bool
LookAndFeelSettingsView::_DoubleScrollBarArrows()
{
	scroll_bar_info info;
	get_scroll_bar_info(&info);

	return info.double_arrows;
}


void
LookAndFeelSettingsView::_SetDoubleScrollBarArrows(bool doubleArrows)
{
	scroll_bar_info info;
	get_scroll_bar_info(&info);

	if (info.double_arrows == doubleArrows)
		return;

	info.double_arrows = doubleArrows;
	set_scroll_bar_info(&info);

	if (doubleArrows)
		fArrowStyleDouble->SetValue(B_CONTROL_ON);
	else
		fArrowStyleSingle->SetValue(B_CONTROL_ON);

	Window()->PostMessage(kMsgUpdate);
}


status_t
LookAndFeelSettingsView::_AlphaDebugSettingsPath(BPath& path) const
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status < B_OK)
		return status;

	status = path.Append("system/app_server");
	if (status < B_OK)
		return status;

	status = create_directory(path.Path(), 0755);
	if (status < B_OK)
		return status;

	return path.Append("alpha_debug");
}


bool
LookAndFeelSettingsView::_AlphaDebugEnabled() const
{
	BPath path;
	if (_AlphaDebugSettingsPath(path) != B_OK)
		return false;

	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return false;

	BMessage settings;
	if (settings.Unflatten(&file) != B_OK)
		return false;

	return settings.GetBool("enabled", false);
}


void
LookAndFeelSettingsView::_SetAlphaDebugEnabled(bool enabled)
{
	if (fCurrentAlphaDebugEnabled == enabled)
		return;

	BPath path;
	if (_AlphaDebugSettingsPath(path) != B_OK)
		return;

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return;

	BMessage settings;
	settings.AddBool("enabled", enabled);
	if (settings.Flatten(&file) != B_OK)
		return;

	fCurrentAlphaDebugEnabled = enabled;
	fAlphaDebugCheckBox->SetValue(enabled ? B_CONTROL_ON : B_CONTROL_OFF);
	Window()->PostMessage(kMsgUpdate);
}


status_t
LookAndFeelSettingsView::_CompositorEffectsSettingsPath(BPath& path) const
{
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status < B_OK)
		return status;

	status = path.Append("system/app_server");
	if (status < B_OK)
		return status;

	status = create_directory(path.Path(), 0755);
	if (status < B_OK)
		return status;

	return path.Append("compositor_effects");
}


void
LookAndFeelSettingsView::_LoadCompositorEffectsSettings()
{
	fForceEffectsAll = false;
	fShowOverlay = false;
	fLogTimings = false;
	fStressInvalidate = false;
	fForceOpacity = -1.0f;

	BPath path;
	if (_CompositorEffectsSettingsPath(path) != B_OK)
		return;

	BFile file(path.Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	BMessage settings;
	if (settings.Unflatten(&file) != B_OK)
		return;

	fForceEffectsAll = settings.GetBool("forceBlurAll", false);
	fShowOverlay = settings.GetBool("showOverlay", false);
	fLogTimings = settings.GetBool("logTimings", false);
	fStressInvalidate = settings.GetBool("stressInvalidate", false);
	fForceOpacity = settings.GetFloat("forceOpacity", -1.0f);
}


void
LookAndFeelSettingsView::_SaveCompositorEffectsSettings()
{
	BPath path;
	if (_CompositorEffectsSettingsPath(path) != B_OK)
		return;

	BFile file(path.Path(), B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (file.InitCheck() != B_OK)
		return;

	BMessage settings;
	settings.AddBool("forceBlurAll", fForceEffectsAll);
	settings.AddFloat("forceOpacity", fForceOpacity);
	settings.AddBool("showOverlay", fShowOverlay);
	settings.AddBool("logTimings", fLogTimings);
	settings.AddBool("stressInvalidate", fStressInvalidate);
	settings.Flatten(&file);
}


void
LookAndFeelSettingsView::_SendCompositorEffectsMessage() const
{
	BMessenger messenger("application/x-vnd.Haiku-app_server");
	if (!messenger.IsValid())
		return;

	BMessage message(kMsgSetCompositorDebugOptions);
	message.AddBool("forceBlurAll", fForceEffectsAll);
	message.AddFloat("forceOpacity", fForceOpacity);
	message.AddBool("showOverlay", fShowOverlay);
	message.AddBool("logTimings", fLogTimings);
	message.AddBool("stressInvalidate", fStressInvalidate);
	messenger.SendMessage(&message);
}


bool
LookAndFeelSettingsView::IsDefaultable()
{
	return fCurrentDecor != fDecorUtility.DefaultDecorator()->ShortcutName()
		|| fCurrentControlLook.Length() != 0
		|| _DoubleScrollBarArrows() != false
		|| fCurrentAlphaDebugEnabled != false
		|| fForceEffectsAll
		|| fShowOverlay
		|| fLogTimings
		|| fStressInvalidate
		|| fForceOpacity >= 0.0f;
}


void
LookAndFeelSettingsView::SetDefaults()
{
	_SetDecor(fDecorUtility.DefaultDecorator());
	_SetControlLook(BString(""));
	_SetDoubleScrollBarArrows(false);
	_SetAlphaDebugEnabled(false);
	fForceEffectsAll = false;
	fShowOverlay = false;
	fLogTimings = false;
	fStressInvalidate = false;
	fForceOpacity = -1.0f;
	_SaveCompositorEffectsSettings();
	_SendCompositorEffectsMessage();
}


bool
LookAndFeelSettingsView::IsRevertable()
{
	return fCurrentDecor != fSavedDecor
		|| fCurrentControlLook != fSavedControlLook
		|| _DoubleScrollBarArrows() != fSavedDoubleArrowsValue
		|| fCurrentAlphaDebugEnabled != fSavedAlphaDebugEnabled
		|| fForceEffectsAll != fSavedForceEffectsAll
		|| fShowOverlay != fSavedShowOverlay
		|| fLogTimings != fSavedLogTimings
		|| fStressInvalidate != fSavedStressInvalidate
		|| fForceOpacity != fSavedForceOpacity;
}


void
LookAndFeelSettingsView::Revert()
{
	if (IsRevertable()) {
		_SetDecor(fSavedDecor);
		_SetControlLook(fSavedControlLook);
		_SetDoubleScrollBarArrows(fSavedDoubleArrowsValue);
		_SetAlphaDebugEnabled(fSavedAlphaDebugEnabled);
		fForceEffectsAll = fSavedForceEffectsAll;
		fShowOverlay = fSavedShowOverlay;
		fLogTimings = fSavedLogTimings;
		fStressInvalidate = fSavedStressInvalidate;
		fForceOpacity = fSavedForceOpacity;
		_SaveCompositorEffectsSettings();
		_SendCompositorEffectsMessage();
	}
}
