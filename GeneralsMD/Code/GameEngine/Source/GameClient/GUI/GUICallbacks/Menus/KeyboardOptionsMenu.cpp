/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2025 Electronic Arts Inc.
**
**	This program is free software: you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**	the Free Software Foundation, either version 3 of the License, or
**	(at your option) any later version.
**
**	This program is distributed in the hope that it will be useful,
**	but WITHOUT ANY WARRANTY; without even the implied warranty of
**	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**	GNU General Public License for more details.
**
**	You should have received a copy of the GNU General Public License
**	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

////////////////////////////////////////////////////////////////////////////////
//																																						//
//  (c) 2001-2003 Electronic Arts Inc.																				//
//																																						//
////////////////////////////////////////////////////////////////////////////////

// FILE: KeyboardOptionsMenu.cpp /////////////////////////////////////////////////////////
//-----------------------------------------------------------------------------
//
//                       Electronic Arts Pacific.
//
//                       Confidential Information
//                Copyright (C) 2002 - All Rights Reserved
//
//-----------------------------------------------------------------------------
//
// Project:   Command & Conquer: Generals
//
// File name: KeyboardOptionsMenu.cpp
//
// Created:   Chris Brue, July 2002
//
// Desc:      the Keyboard options window control
//
//-----------------------------------------------------------------------------
///////////////////////////////////////////////////////////////////////////////


// TheSuperHackers @feature The original keyboard options menu was never finished. It is replaced with a
// hotkey menu that lists the commands of the CommandMap, assigns a new key with modifiers by pressing it,
// and saves the changes to CommandMap.ini in the user data folder.

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GameText.h"
#include "GameClient/MetaEvent.h"

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
static WindowLayout *keyboardOptionsLayout = nullptr;

static NameKeyType parentKeyboardOptionsMenuID = NAMEKEY_INVALID;
static NameKeyType listBoxCommandListID = NAMEKEY_INVALID;
static NameKeyType buttonChangeKeyID = NAMEKEY_INVALID;
static NameKeyType buttonClearKeyID = NAMEKEY_INVALID;
static NameKeyType buttonResetAllID = NAMEKEY_INVALID;
static NameKeyType buttonBackID = NAMEKEY_INVALID;

static GameWindow *parentKeyboardOptionsMenu = nullptr;
static GameWindow *listBoxCommandList = nullptr;
static GameWindow *staticTextDescription = nullptr;
static GameWindow *staticTextStatus = nullptr;

static MetaMapRec *captureRec = nullptr; ///< The command that waits for a key press
static UnsignedByte swallowKeyUp = KEY_NONE; ///< The key whose release is ignored after it was assigned

static const MappableKeyCategories CategoryOrder[] =
{
	CATEGORY_CONTROL,
	CATEGORY_SELECTION,
	CATEGORY_TEAM,
	CATEGORY_INTERFACE,
	CATEGORY_INFORMATION,
	CATEGORY_TAUNT,
	CATEGORY_MISC,
};

struct KeyLabel
{
	MappableKeyType key;
	const WideChar *label;
};

static const KeyLabel SpecialKeyLabels[] =
{
	{ MK_KP0, L"Num 0" }, { MK_KP1, L"Num 1" }, { MK_KP2, L"Num 2" }, { MK_KP3, L"Num 3" }, { MK_KP4, L"Num 4" },
	{ MK_KP5, L"Num 5" }, { MK_KP6, L"Num 6" }, { MK_KP7, L"Num 7" }, { MK_KP8, L"Num 8" }, { MK_KP9, L"Num 9" },
	{ MK_KPDEL, L"Num Del" }, { MK_KPSTAR, L"Num *" }, { MK_KPMINUS, L"Num -" }, { MK_KPPLUS, L"Num +" },
	{ MK_KPENTER, L"Num Enter" }, { MK_KPSLASH, L"Num /" },
	{ MK_ESC, L"Esc" }, { MK_BACKSPACE, L"Backspace" }, { MK_ENTER, L"Enter" }, { MK_SPACE, L"Space" }, { MK_TAB, L"Tab" },
	{ MK_MINUS, L"-" }, { MK_EQUAL, L"=" }, { MK_LBRACKET, L"[" }, { MK_RBRACKET, L"]" }, { MK_SEMICOLON, L";" },
	{ MK_APOSTROPHE, L"'" }, { MK_TICK, L"`" }, { MK_BACKSLASH, L"\\" }, { MK_COMMA, L"," }, { MK_PERIOD, L"." },
	{ MK_SLASH, L"/" }, { MK_UP, L"Up" }, { MK_DOWN, L"Down" }, { MK_LEFT, L"Left" }, { MK_RIGHT, L"Right" },
	{ MK_HOME, L"Home" }, { MK_END, L"End" }, { MK_PGUP, L"Page Up" }, { MK_PGDN, L"Page Down" },
	{ MK_INS, L"Insert" }, { MK_DEL, L"Delete" },
};

// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
static Bool isMappableKey(Int key)
{
	if (key == MK_NONE)
		return FALSE;

	for (const LookupListRec *keyName = KeyNames; keyName->name; ++keyName)
	{
		if (keyName->value == key)
			return TRUE;
	}
	return FALSE;
}

//-------------------------------------------------------------------------------------------------
static UnicodeString getKeyLabel(MappableKeyType key, MappableKeyModState modState)
{
	if (key == MK_NONE)
		return TheGameText->FETCH_OR_SUBSTITUTE("KEYBOARD:Unassigned", L"-");

	UnicodeString label;
	if (modState & CTRL)
		label.concat(TheGameText->FETCH_OR_SUBSTITUTE("KEYBOARD:Ctrl+", L"Ctrl+"));
	if (modState & ALT)
		label.concat(TheGameText->FETCH_OR_SUBSTITUTE("KEYBOARD:Alt+", L"Alt+"));
	if (modState & SHIFT)
		label.concat(TheGameText->FETCH_OR_SUBSTITUTE("KEYBOARD:Shift+", L"Shift+"));

	for (Int i = 0; i < ARRAY_SIZE(SpecialKeyLabels); ++i)
	{
		if (SpecialKeyLabels[i].key == key)
		{
			label.concat(SpecialKeyLabels[i].label);
			return label;
		}
	}

	// Letters, digits and function keys use their INI name without the prefix, such as "KEY_F1" -> "F1"
	for (const LookupListRec *keyName = KeyNames; keyName->name; ++keyName)
	{
		if (keyName->value == key)
		{
			const char *name = keyName->name;
			if (strncmp(name, "KEY_", 4) == 0)
				name += 4;

			UnicodeString keyLabel;
			keyLabel.translate(AsciiString(name));
			label.concat(keyLabel);
			return label;
		}
	}

	return label;
}

//-------------------------------------------------------------------------------------------------
/** Returns the END_ command that belongs to a BEGIN_ command, such as the release of a held camera key */
//-------------------------------------------------------------------------------------------------
static MetaMapRec *findPairedRec(const MetaMapRec *rec)
{
	const char *name = MetaMap::getMetaTypeName(rec->m_meta);
	if (name == nullptr || strncmp(name, "BEGIN_", 6) != 0)
		return nullptr;

	AsciiString endName("END_");
	endName.concat(name + 6);

	const GameMessage::Type endType = MetaMap::getMetaTypeByName(endName.str());
	if (endType == GameMessage::MSG_INVALID)
		return nullptr;

	return TheMetaMap->findMetaMapRec(endType);
}

//-------------------------------------------------------------------------------------------------
static Bool isRebindable(const MetaMapRec *rec)
{
	if (rec->m_usableIn == COMMANDUSABLE_NONE || rec->m_category == CATEGORY_DEBUG)
		return FALSE;

	const char *name = MetaMap::getMetaTypeName(rec->m_meta);
	if (name == nullptr)
		return FALSE;

	// The release half of a held command follows its BEGIN_ command.
	if (strncmp(name, "END_", 4) == 0)
		return FALSE;

	if (strncmp(name, "DEMO_", 5) == 0 || strncmp(name, "DEBUG_", 6) == 0 || strncmp(name, "CHEAT_", 6) == 0)
		return FALSE;

	// Commands that are only a held modifier, such as force attack with CTRL, are not hotkeys.
	if (rec->m_defaultKey == MK_NONE && rec->m_defaultModState != NONE)
		return FALSE;

	return TRUE;
}

//-------------------------------------------------------------------------------------------------
static UnicodeString getCommandLabel(const MetaMapRec *rec)
{
	if (!rec->m_displayName.isEmpty())
		return rec->m_displayName;

	// Commands without a display name use their INI name, such as "BEGIN_CAMERA_ZOOM_IN" -> "Camera Zoom In"
	const char *name = MetaMap::getMetaTypeName(rec->m_meta);
	if (name == nullptr)
		return UnicodeString::TheEmptyString;

	if (strncmp(name, "BEGIN_", 6) == 0)
		name += 6;

	UnicodeString label;
	Bool startOfWord = TRUE;
	for (const char *c = name; *c; ++c)
	{
		if (*c == '_')
		{
			label.concat(L' ');
			startOfWord = TRUE;
			continue;
		}

		const char ch = startOfWord ? (char)toupper(*c) : (char)tolower(*c);
		label.concat((WideChar)ch);
		startOfWord = FALSE;
	}
	return label;
}

//-------------------------------------------------------------------------------------------------
static UnicodeString getCategoryLabel(MappableKeyCategories category)
{
	switch (category)
	{
	case CATEGORY_CONTROL:     return TheGameText->FETCH_OR_SUBSTITUTE("KEYBOARD:CategoryControl", L"Unit Commands");
	case CATEGORY_INFORMATION: return TheGameText->FETCH_OR_SUBSTITUTE("KEYBOARD:CategoryInformation", L"Information");
	case CATEGORY_INTERFACE:   return TheGameText->FETCH_OR_SUBSTITUTE("KEYBOARD:CategoryInterface", L"Interface");
	case CATEGORY_SELECTION:   return TheGameText->FETCH_OR_SUBSTITUTE("KEYBOARD:CategorySelection", L"Selection");
	case CATEGORY_TAUNT:       return TheGameText->FETCH_OR_SUBSTITUTE("KEYBOARD:CategoryTaunt", L"Taunts");
	case CATEGORY_TEAM:        return TheGameText->FETCH_OR_SUBSTITUTE("KEYBOARD:CategoryTeam", L"Control Groups");
	default:                   return TheGameText->FETCH_OR_SUBSTITUTE("KEYBOARD:CategoryMisc", L"Other");
	}
}

//-------------------------------------------------------------------------------------------------
static void setStatus(const UnicodeString &text)
{
	if (staticTextStatus)
		GadgetStaticTextSetText(staticTextStatus, text);
}

//-------------------------------------------------------------------------------------------------
static MetaMapRec *getSelectedRec()
{
	if (listBoxCommandList == nullptr)
		return nullptr;

	Int selected = -1;
	GadgetListBoxGetSelected(listBoxCommandList, &selected);
	if (selected < 0)
		return nullptr;

	return (MetaMapRec *)GadgetListBoxGetItemData(listBoxCommandList, selected, 0);
}

//-------------------------------------------------------------------------------------------------
static void updateDescription()
{
	if (staticTextDescription == nullptr)
		return;

	const MetaMapRec *rec = getSelectedRec();
	GadgetStaticTextSetText(staticTextDescription, rec ? rec->m_description : UnicodeString::TheEmptyString);
}

//-------------------------------------------------------------------------------------------------
static void fillCommandList()
{
	if (listBoxCommandList == nullptr)
		return;

	Int selected = -1;
	GadgetListBoxGetSelected(listBoxCommandList, &selected);
	const Int topEntry = GadgetListBoxGetTopVisibleEntry(listBoxCommandList);

	GadgetListBoxReset(listBoxCommandList);

	// The meta map is a list with the last parsed command first. Show the commands in INI order.
	std::vector<MetaMapRec *> recs;
	for (MetaMapRec *rec = TheMetaMap->getFirstMetaMapRec(); rec; rec = rec->m_next)
	{
		if (isRebindable(rec))
			recs.insert(recs.begin(), rec);
	}

	const Color headerColor = GameMakeColor(255, 204, 0, 255);
	const Color commandColor = GameMakeColor(255, 255, 255, 255);
	const Color keyColor = GameMakeColor(186, 255, 12, 255);
	const Color changedKeyColor = GameMakeColor(255, 160, 64, 255);

	for (Int c = 0; c < ARRAY_SIZE(CategoryOrder); ++c)
	{
		const MappableKeyCategories category = CategoryOrder[c];
		Bool hasHeader = FALSE;

		for (size_t i = 0; i < recs.size(); ++i)
		{
			MetaMapRec *rec = recs[i];
			const MappableKeyCategories recCategory = (rec->m_category < CATEGORY_NUM_CATEGORIES) ? rec->m_category : CATEGORY_MISC;
			if (recCategory != category)
				continue;

			if (!hasHeader)
			{
				const Int headerRow = GadgetListBoxAddEntryText(listBoxCommandList, getCategoryLabel(category), headerColor, -1, 0);
				GadgetListBoxAddEntryText(listBoxCommandList, UnicodeString::TheEmptyString, headerColor, headerRow, 1);
				GadgetListBoxSetItemData(listBoxCommandList, nullptr, headerRow, 0);
				hasHeader = TRUE;
			}

			const Bool changed = rec->m_key != rec->m_defaultKey || rec->m_modState != rec->m_defaultModState;
			UnicodeString commandLabel(L"   ");
			commandLabel.concat(getCommandLabel(rec));

			const Int row = GadgetListBoxAddEntryText(listBoxCommandList, commandLabel, commandColor, -1, 0);
			GadgetListBoxAddEntryText(listBoxCommandList, getKeyLabel(rec->m_key, rec->m_modState), changed ? changedKeyColor : keyColor, row, 1);
			GadgetListBoxSetItemData(listBoxCommandList, rec, row, 0);
		}
	}

	if (selected >= 0 && selected < GadgetListBoxGetNumEntries(listBoxCommandList))
	{
		GadgetListBoxSetSelected(listBoxCommandList, selected);
		GadgetListBoxSetTopVisibleEntry(listBoxCommandList, topEntry);
	}
}

//-------------------------------------------------------------------------------------------------
static void saveChanges()
{
	if (!TheMetaMap->saveUserMap())
	{
		setStatus(TheGameText->FETCH_OR_SUBSTITUTE("KEYBOARD:SaveFailed", L"The hotkeys could not be saved."));
	}
}

//-------------------------------------------------------------------------------------------------
static void setRecKey(MetaMapRec *rec, MappableKeyType key, MappableKeyModState modState)
{
	rec->m_key = key;
	rec->m_modState = modState;

	MetaMapRec *pairedRec = findPairedRec(rec);
	if (pairedRec)
	{
		pairedRec->m_key = key;
		pairedRec->m_modState = modState;
	}
}

//-------------------------------------------------------------------------------------------------
static void startCapture()
{
	MetaMapRec *rec = getSelectedRec();
	if (rec == nullptr)
	{
		setStatus(TheGameText->FETCH_OR_SUBSTITUTE("KEYBOARD:SelectCommand", L"Select a command first."));
		return;
	}

	captureRec = rec;

	UnicodeString text;
	text.format(TheGameText->FETCH_OR_SUBSTITUTE("KEYBOARD:PressKey", L"Press the new key for %ls. Esc cancels.").str(), getCommandLabel(rec).str());
	setStatus(text);

	TheWindowManager->winSetFocus(parentKeyboardOptionsMenu);
}

//-------------------------------------------------------------------------------------------------
static void assignCapturedKey(MappableKeyType key, MappableKeyModState modState)
{
	MetaMapRec *rec = captureRec;
	captureRec = nullptr;
	if (rec == nullptr)
		return;

	// A key combination triggers only one command, so it is taken away from commands that are usable at the same time.
	UnicodeString removedFrom;
	for (MetaMapRec *other = TheMetaMap->getFirstMetaMapRec(); other; other = other->m_next)
	{
		if (other == rec || other == findPairedRec(rec))
			continue;

		if (other->m_key != key || other->m_modState != modState || other->m_transition != rec->m_transition)
			continue;

		if ((other->m_usableIn & rec->m_usableIn) == 0)
			continue;

		setRecKey(other, MK_NONE, NONE);

		if (isRebindable(other))
		{
			if (!removedFrom.isEmpty())
				removedFrom.concat(L", ");
			removedFrom.concat(getCommandLabel(other));
		}
	}

	setRecKey(rec, key, modState);
	saveChanges();
	fillCommandList();

	UnicodeString text;
	if (removedFrom.isEmpty())
	{
		text.format(TheGameText->FETCH_OR_SUBSTITUTE("KEYBOARD:Assigned", L"%ls is now %ls.").str(),
			getCommandLabel(rec).str(), getKeyLabel(key, modState).str());
	}
	else
	{
		text.format(TheGameText->FETCH_OR_SUBSTITUTE("KEYBOARD:AssignedAndRemoved", L"%ls is now %ls. Removed it from %ls.").str(),
			getCommandLabel(rec).str(), getKeyLabel(key, modState).str(), removedFrom.str());
	}
	setStatus(text);
}

//-------------------------------------------------------------------------------------------------
static void closeKeyboardOptionsMenu()
{
	captureRec = nullptr;

	if (parentKeyboardOptionsMenu)
		TheWindowManager->winUnsetModal(parentKeyboardOptionsMenu);

	if (keyboardOptionsLayout)
	{
		keyboardOptionsLayout->destroyWindows();
		deleteInstance(keyboardOptionsLayout);
		keyboardOptionsLayout = nullptr;
	}

	parentKeyboardOptionsMenu = nullptr;
	listBoxCommandList = nullptr;
	staticTextDescription = nullptr;
	staticTextStatus = nullptr;

	// Give the keyboard back to the options menu underneath.
	GameWindow *optionsParent = TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:OptionsMenuParent" ) );
	if (optionsParent)
		TheWindowManager->winSetFocus( optionsParent );
}

//-------------------------------------------------------------------------------------------------
static void setButtonText(const char *name, const UnicodeString &text)
{
	GameWindow *button = TheWindowManager->winGetWindowFromId(nullptr, TheNameKeyGenerator->nameToKey(name));
	if (button)
		GadgetButtonSetText(button, text);
}

// PUBLIC FUNCTIONS ///////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
/** Initialize the keyboard options menu */
//-------------------------------------------------------------------------------------------------
void KeyboardOptionsMenuInit( WindowLayout *layout, void *userData )
{
	keyboardOptionsLayout = layout;
	captureRec = nullptr;
	swallowKeyUp = KEY_NONE;

	parentKeyboardOptionsMenuID = TheNameKeyGenerator->nameToKey( "KeyboardOptionsMenu.wnd:ParentKeyboardOptionsMenu" );
	listBoxCommandListID = TheNameKeyGenerator->nameToKey( "KeyboardOptionsMenu.wnd:ListBoxCommandList" );
	buttonChangeKeyID = TheNameKeyGenerator->nameToKey( "KeyboardOptionsMenu.wnd:ButtonChangeKey" );
	buttonClearKeyID = TheNameKeyGenerator->nameToKey( "KeyboardOptionsMenu.wnd:ButtonClearKey" );
	buttonResetAllID = TheNameKeyGenerator->nameToKey( "KeyboardOptionsMenu.wnd:ButtonResetAll" );
	buttonBackID = TheNameKeyGenerator->nameToKey( "KeyboardOptionsMenu.wnd:ButtonBack" );

	parentKeyboardOptionsMenu = TheWindowManager->winGetWindowFromId( nullptr, parentKeyboardOptionsMenuID );
	listBoxCommandList = TheWindowManager->winGetWindowFromId( nullptr, listBoxCommandListID );
	staticTextDescription = TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey( "KeyboardOptionsMenu.wnd:StaticTextDescription" ) );
	staticTextStatus = TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey( "KeyboardOptionsMenu.wnd:StaticTextStatus" ) );

	GameWindow *staticTextTitle = TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey( "KeyboardOptionsMenu.wnd:StaticTextTitle" ) );
	if (staticTextTitle)
		GadgetStaticTextSetText( staticTextTitle, TheGameText->FETCH_OR_SUBSTITUTE( "GUI:KeyboardOptions", L"Keyboard Hotkeys" ) );

	setButtonText( "KeyboardOptionsMenu.wnd:ButtonChangeKey", TheGameText->FETCH_OR_SUBSTITUTE( "KEYBOARD:ChangeKey", L"Change Key" ) );
	setButtonText( "KeyboardOptionsMenu.wnd:ButtonClearKey", TheGameText->FETCH_OR_SUBSTITUTE( "KEYBOARD:ClearKey", L"Clear Key" ) );
	setButtonText( "KeyboardOptionsMenu.wnd:ButtonResetAll", TheGameText->FETCH_OR_SUBSTITUTE( "KEYBOARD:ResetAll", L"Reset All" ) );
	setButtonText( "KeyboardOptionsMenu.wnd:ButtonBack", TheGameText->FETCH_OR_SUBSTITUTE( "GUI:Back", L"Back" ) );

	fillCommandList();
	updateDescription();
	setStatus( TheGameText->FETCH_OR_SUBSTITUTE( "KEYBOARD:Help", L"Double click a command or select it and press Change Key. Unit build hotkeys come from the language file and are not listed." ) );

	layout->hide( FALSE );

	if (parentKeyboardOptionsMenu)
	{
		TheWindowManager->winSetModal( parentKeyboardOptionsMenu );
		TheWindowManager->winSetFocus( parentKeyboardOptionsMenu );
	}
}

//-------------------------------------------------------------------------------------------------
/** Keyboard options menu shutdown method */
//-------------------------------------------------------------------------------------------------
void KeyboardOptionsMenuShutdown( WindowLayout *layout, void *userData )
{
	// The menu opens on top of the options menu and is not part of the shell stack, see closeKeyboardOptionsMenu
	layout->hide( TRUE );
}

//-------------------------------------------------------------------------------------------------
/** Keyboard options menu update method */
//-------------------------------------------------------------------------------------------------
void KeyboardOptionsMenuUpdate( WindowLayout *layout, void *userData )
{
}

//-------------------------------------------------------------------------------------------------
/** Keyboard options menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType KeyboardOptionsMenuInput( GameWindow *window, UnsignedInt msg,
																			 WindowMsgData mData1, WindowMsgData mData2 )
{
	if (msg != GWM_CHAR)
		return MSG_IGNORED;

	const UnsignedByte key = (UnsignedByte)mData1;
	const UnsignedByte state = (UnsignedByte)mData2;

	switch (key)
	{
	case KEY_LCTRL:
	case KEY_RCTRL:
	case KEY_LSHIFT:
	case KEY_RSHIFT:
	case KEY_LALT:
	case KEY_RALT:
		// Modifiers are read from the key state when the actual key is pressed.
		return MSG_HANDLED;
	}

	if (BitIsSet( state, KEY_STATE_UP ))
	{
		if (swallowKeyUp == key)
		{
			swallowKeyUp = KEY_NONE;
			return MSG_HANDLED;
		}

		if (key == KEY_ESC && captureRec == nullptr)
		{
			GameWindow *button = TheWindowManager->winGetWindowFromId( window, buttonBackID );
			TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, (WindowMsgData)button, buttonBackID );
		}

		return MSG_HANDLED;
	}

	if (captureRec == nullptr)
	{
		// Keep the keys away from the game while the menu is open.
		return MSG_HANDLED;
	}

	swallowKeyUp = key;

	Int modState = NONE;
	if (BitIsSet( state, KEY_STATE_CONTROL ))
		modState |= CTRL;
	if (BitIsSet( state, KEY_STATE_ALT ))
		modState |= ALT;
	if (BitIsSet( state, KEY_STATE_LSHIFT | KEY_STATE_RSHIFT ))
		modState |= SHIFT;

	if (key == KEY_ESC && modState == NONE)
	{
		captureRec = nullptr;
		setStatus( TheGameText->FETCH_OR_SUBSTITUTE( "KEYBOARD:Cancelled", L"No change." ) );
		return MSG_HANDLED;
	}

	if (!isMappableKey( key ))
	{
		setStatus( TheGameText->FETCH_OR_SUBSTITUTE( "KEYBOARD:KeyNotMappable", L"That key cannot be used. Press another key, or Esc to cancel." ) );
		return MSG_HANDLED;
	}

	assignCapturedKey( (MappableKeyType)key, (MappableKeyModState)modState );
	return MSG_HANDLED;
}

//-------------------------------------------------------------------------------------------------
/** Keyboard options menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType KeyboardOptionsMenuSystem( GameWindow *window, UnsignedInt msg,
																				WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg )
	{
		// --------------------------------------------------------------------------------------------
		case GWM_CREATE:
		case GWM_DESTROY:
			break;

		// --------------------------------------------------------------------------------------------
		case GWM_INPUT_FOCUS:
		{
			// if we're given the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			return MSG_HANDLED;
		}

		// ---------------------------------------------------------------------------------------------
		case GLM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			if( control && control->winGetWindowId() == listBoxCommandListID )
			{
				captureRec = nullptr;
				updateDescription();

				// The list takes the keyboard focus when clicked. Give it back so key presses reach this menu.
				TheWindowManager->winSetFocus( parentKeyboardOptionsMenu );
			}
			break;
		}

		// ---------------------------------------------------------------------------------------------
		case GLM_DOUBLE_CLICKED:
		{
			GameWindow *control = (GameWindow *)mData1;
			if( control && control->winGetWindowId() == listBoxCommandListID )
			{
				startCapture();
			}
			break;
		}

		// ---------------------------------------------------------------------------------------------
		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			const Int controlID = control ? control->winGetWindowId() : NAMEKEY_INVALID;

			if( controlID == buttonBackID )
			{
				closeKeyboardOptionsMenu();
			}
			else if( controlID == buttonChangeKeyID )
			{
				startCapture();
			}
			else if( controlID == buttonClearKeyID )
			{
				captureRec = nullptr;
				MetaMapRec *rec = getSelectedRec();
				if (rec)
				{
					setRecKey( rec, MK_NONE, NONE );
					saveChanges();
					fillCommandList();

					UnicodeString text;
					text.format( TheGameText->FETCH_OR_SUBSTITUTE( "KEYBOARD:Cleared", L"%ls has no key now." ).str(), getCommandLabel( rec ).str() );
					setStatus( text );
				}
				else
				{
					setStatus( TheGameText->FETCH_OR_SUBSTITUTE( "KEYBOARD:SelectCommand", L"Select a command first." ) );
				}
			}
			else if( controlID == buttonResetAllID )
			{
				captureRec = nullptr;
				TheMetaMap->resetToDefaults();
				saveChanges();
				fillCommandList();
				setStatus( TheGameText->FETCH_OR_SUBSTITUTE( "KEYBOARD:ResetDone", L"All hotkeys are back to their defaults." ) );
			}

			if (parentKeyboardOptionsMenu)
				TheWindowManager->winSetFocus( parentKeyboardOptionsMenu );

			break;
		}

		default:
			return MSG_IGNORED;
	}

	return MSG_HANDLED;
}
