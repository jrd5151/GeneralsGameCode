/*
**	Command & Conquer Generals Zero Hour(tm)
**	Copyright 2026 TheSuperHackers
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

// FILE: ModMenu.cpp //////////////////////////////////////////////////////////////////////////////
// TheSuperHackers @feature The mod menu lists the mods in the Mods folder of the user data folder.
// A mod is a folder of .big files or a single .big file, the same as the -mod command line option takes.
// Picking one saves it to Options.ini and restarts the game, which then loads it.
///////////////////////////////////////////////////////////////////////////////////////////////////

// INCLUDES ///////////////////////////////////////////////////////////////////////////////////////
#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "Common/GameEngine.h"
#include "Common/GlobalData.h"
#include "Common/OptionPreferences.h"
#include "GameClient/ClientInstance.h"
#include "GameClient/WindowLayout.h"
#include "GameClient/Gadget.h"
#include "GameClient/GadgetListBox.h"
#include "GameClient/GadgetPushButton.h"
#include "GameClient/GadgetStaticText.h"
#include "GameClient/KeyDefs.h"
#include "GameClient/GameWindowManager.h"
#include "GameClient/GameText.h"

// PRIVATE DATA ///////////////////////////////////////////////////////////////////////////////////
static WindowLayout *modMenuLayout = nullptr;

static NameKeyType listBoxModsID = NAMEKEY_INVALID;
static NameKeyType buttonLoadModID = NAMEKEY_INVALID;
static NameKeyType buttonBackID = NAMEKEY_INVALID;

static GameWindow *parentModMenu = nullptr;
static GameWindow *listBoxMods = nullptr;
static GameWindow *staticTextStatus = nullptr;

static std::vector<AsciiString> modPaths; ///< Per list row, relative to the user data folder. Empty for no mod.

// PRIVATE FUNCTIONS //////////////////////////////////////////////////////////////////////////////

//-------------------------------------------------------------------------------------------------
static AsciiString getModsFolder()
{
	AsciiString folder = TheGlobalData->getPath_UserData();
	folder.concat("Mods\\");
	return folder;
}

//-------------------------------------------------------------------------------------------------
/** Returns the mod that this game session runs with, relative to the user data folder like the list */
//-------------------------------------------------------------------------------------------------
static AsciiString getLoadedModPath()
{
	AsciiString loaded = TheGlobalData->m_modDir.isNotEmpty() ? TheGlobalData->m_modDir : TheGlobalData->m_modBIG;
	if (loaded.endsWith("\\"))
		loaded.removeLastChar();

	const AsciiString &userData = TheGlobalData->getPath_UserData();
	if (loaded.startsWithNoCase(userData.str()))
		return AsciiString(loaded.str() + userData.getLength());

	return loaded;
}

//-------------------------------------------------------------------------------------------------
static void setStatus(const UnicodeString &text)
{
	if (staticTextStatus)
		GadgetStaticTextSetText(staticTextStatus, text);
}

//-------------------------------------------------------------------------------------------------
static void addModRow(const UnicodeString &label, const AsciiString &path, const AsciiString &loadedPath)
{
	const Bool isLoaded = (path.compareNoCase(loadedPath) == 0);

	UnicodeString text = label;
	if (isLoaded)
		text.concat(TheGameText->FETCH_OR_SUBSTITUTE("MODMENU:LoadedSuffix", L"   (running now)"));

	const Color color = isLoaded ? GameMakeColor(186, 255, 12, 255) : GameMakeColor(255, 255, 255, 255);
	const Int row = GadgetListBoxAddEntryText(listBoxMods, text, color, -1, 0);
	if (row < 0)
		return;

	GadgetListBoxSetItemData(listBoxMods, (void *)(modPaths.size()), row, 0);
	modPaths.push_back(path);

	if (isLoaded)
		GadgetListBoxSetSelected(listBoxMods, row);
}

//-------------------------------------------------------------------------------------------------
static void fillModList()
{
	if (listBoxMods == nullptr)
		return;

	GadgetListBoxReset(listBoxMods);
	modPaths.clear();

	const AsciiString loadedPath = getLoadedModPath();
	addModRow(TheGameText->FETCH_OR_SUBSTITUTE("MODMENU:NoMod", L"No mod (original game)"), AsciiString::TheEmptyString, loadedPath);

	const AsciiString modsFolder = getModsFolder();
	CreateDirectory(modsFolder.str(), nullptr); // so the player can find where mods go

	AsciiString pattern = modsFolder;
	pattern.concat("*");

	WIN32_FIND_DATA findData;
	HANDLE findHandle = FindFirstFile(pattern.str(), &findData);
	if (findHandle == INVALID_HANDLE_VALUE)
		return;

	do
	{
		const char *name = findData.cFileName;
		if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
			continue;

		const Bool isFolder = (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		const size_t nameLength = strlen(name);
		const Bool isBig = !isFolder && nameLength > 4 && stricmp(name + nameLength - 4, ".big") == 0;
		if (!isFolder && !isBig)
			continue;

		AsciiString path("Mods\\");
		path.concat(name);

		UnicodeString label;
		label.translate(AsciiString(name));
		addModRow(label, path, loadedPath);
	}
	while (FindNextFile(findHandle, &findData));

	FindClose(findHandle);
}

//-------------------------------------------------------------------------------------------------
static Bool getSelectedModPath(AsciiString &path)
{
	Int selected = -1;
	GadgetListBoxGetSelected(listBoxMods, &selected);
	if (selected < 0)
		return FALSE;

	const size_t index = (size_t)GadgetListBoxGetItemData(listBoxMods, selected, 0);
	if (index >= modPaths.size())
		return FALSE;

	path = modPaths[index];
	return TRUE;
}

//-------------------------------------------------------------------------------------------------
static void closeModMenu()
{
	if (parentModMenu)
		TheWindowManager->winUnsetModal(parentModMenu);

	if (modMenuLayout)
	{
		modMenuLayout->destroyWindows();
		deleteInstance(modMenuLayout);
		modMenuLayout = nullptr;
	}

	parentModMenu = nullptr;
	listBoxMods = nullptr;
	staticTextStatus = nullptr;
	modPaths.clear();

	GameWindow *optionsParent = TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey( "OptionsMenu.wnd:OptionsMenuParent" ) );
	if (optionsParent)
		TheWindowManager->winSetFocus( optionsParent );
}

//-------------------------------------------------------------------------------------------------
static void loadSelectedMod()
{
	AsciiString path;
	if (!getSelectedModPath(path))
	{
		setStatus(TheGameText->FETCH_OR_SUBSTITUTE("MODMENU:SelectMod", L"Select a mod first."));
		return;
	}

	if (path.compareNoCase(getLoadedModPath()) == 0)
	{
		setStatus(TheGameText->FETCH_OR_SUBSTITUTE("MODMENU:AlreadyRunning", L"The game is already running with this choice."));
		return;
	}

	OptionPreferences pref;
	pref["Mod"] = path;
	if (!pref.write())
	{
		setStatus(TheGameText->FETCH_OR_SUBSTITUTE("MODMENU:SaveFailed", L"The choice could not be saved to Options.ini."));
		return;
	}

	setStatus(TheGameText->FETCH_OR_SUBSTITUTE("MODMENU:Restarting", L"Restarting..."));

	// The mod's files are loaded when the game starts, so the game starts itself again.
	rts::ClientInstance::setRestartOnExit(true);
	TheGameEngine->setQuitting(TRUE);
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
/** Initialize the mod menu */
//-------------------------------------------------------------------------------------------------
void ModMenuInit( WindowLayout *layout, void *userData )
{
	modMenuLayout = layout;

	listBoxModsID = TheNameKeyGenerator->nameToKey( "ModMenu.wnd:ListBoxMods" );
	buttonLoadModID = TheNameKeyGenerator->nameToKey( "ModMenu.wnd:ButtonLoadMod" );
	buttonBackID = TheNameKeyGenerator->nameToKey( "ModMenu.wnd:ButtonBack" );

	parentModMenu = TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey( "ModMenu.wnd:ParentModMenu" ) );
	listBoxMods = TheWindowManager->winGetWindowFromId( nullptr, listBoxModsID );
	staticTextStatus = TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey( "ModMenu.wnd:StaticTextStatus" ) );

	GameWindow *staticTextTitle = TheWindowManager->winGetWindowFromId( nullptr, TheNameKeyGenerator->nameToKey( "ModMenu.wnd:StaticTextTitle" ) );
	if (staticTextTitle)
		GadgetStaticTextSetText( staticTextTitle, TheGameText->FETCH_OR_SUBSTITUTE( "GUI:ModMenu", L"Mods" ) );

	setButtonText( "ModMenu.wnd:ButtonLoadMod", TheGameText->FETCH_OR_SUBSTITUTE( "MODMENU:LoadMod", L"Load and Restart" ) );
	setButtonText( "ModMenu.wnd:ButtonBack", TheGameText->FETCH_OR_SUBSTITUTE( "GUI:Back", L"Back" ) );

	fillModList();

	UnicodeString folder;
	folder.translate( getModsFolder() );
	UnicodeString help;
	help.format( TheGameText->FETCH_OR_SUBSTITUTE( "MODMENU:Help", L"Put each mod in %ls as a folder of .big files or as one .big file." ).str(), folder.str() );
	setStatus( help );

	layout->hide( FALSE );

	if (parentModMenu)
	{
		TheWindowManager->winSetModal( parentModMenu );
		TheWindowManager->winSetFocus( parentModMenu );
	}
}

//-------------------------------------------------------------------------------------------------
/** Mod menu shutdown method */
//-------------------------------------------------------------------------------------------------
void ModMenuShutdown( WindowLayout *layout, void *userData )
{
	// The menu opens on top of the options menu and is not part of the shell stack, see closeModMenu
	layout->hide( TRUE );
}

//-------------------------------------------------------------------------------------------------
/** Mod menu input callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType ModMenuInput( GameWindow *window, UnsignedInt msg,
																	 WindowMsgData mData1, WindowMsgData mData2 )
{
	if (msg != GWM_CHAR)
		return MSG_IGNORED;

	const UnsignedByte key = (UnsignedByte)mData1;
	const UnsignedByte state = (UnsignedByte)mData2;

	if (key == KEY_ESC && BitIsSet( state, KEY_STATE_UP ))
	{
		GameWindow *button = TheWindowManager->winGetWindowFromId( window, buttonBackID );
		TheWindowManager->winSendSystemMsg( window, GBM_SELECTED, (WindowMsgData)button, buttonBackID );
	}

	// Keep the keys away from the game while the menu is open.
	return MSG_HANDLED;
}

//-------------------------------------------------------------------------------------------------
/** Mod menu window system callback */
//-------------------------------------------------------------------------------------------------
WindowMsgHandledType ModMenuSystem( GameWindow *window, UnsignedInt msg,
																		WindowMsgData mData1, WindowMsgData mData2 )
{
	switch( msg )
	{
		case GWM_CREATE:
		case GWM_DESTROY:
			break;

		case GWM_INPUT_FOCUS:
		{
			// if we're given the opportunity to take the keyboard focus we must say we want it
			if( mData1 == TRUE )
				*(Bool *)mData2 = TRUE;

			return MSG_HANDLED;
		}

		case GLM_SELECTED:
			break;

		case GLM_DOUBLE_CLICKED:
		{
			GameWindow *control = (GameWindow *)mData1;
			if( control && control->winGetWindowId() == listBoxModsID )
				loadSelectedMod();
			break;
		}

		case GBM_SELECTED:
		{
			GameWindow *control = (GameWindow *)mData1;
			const Int controlID = control ? control->winGetWindowId() : NAMEKEY_INVALID;

			if( controlID == buttonBackID )
				closeModMenu();
			else if( controlID == buttonLoadModID )
				loadSelectedMod();

			break;
		}

		default:
			return MSG_IGNORED;
	}

	return MSG_HANDLED;
}
