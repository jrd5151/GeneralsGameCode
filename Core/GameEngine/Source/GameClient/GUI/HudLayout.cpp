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


#include "PreRTS.h"	// This must go first in EVERY cpp file in the GameEngine

#include "GameClient/HudLayout.h"

#include "Common/AddonCompat.h"
#include "Common/FileSystem.h"
#include "Common/GameDefines.h"
#include "Common/OptionPreferences.h"
#include "GameClient/Display.h"

namespace hud
{

namespace
{

Int s_centeredHudSetting = -1; // -1 = not read yet, 0 = off, 1 = on

struct LayoutAnchor
{
	const char *name; ///< Layout file name, or a prefix when it ends in '*'
	Anchor anchor;
};

const LayoutAnchor s_layoutAnchors[] =
{
	{ "ControlBar.wnd", Anchor_BottomCenter },
	{ "ControlBarPopupDescription.wnd", Anchor_BottomCenter },
	{ "GeneralsExpPoints.wnd", Anchor_BottomCenter },
	{ "ReplayControl.wnd", Anchor_BottomCenter },
	{ "GenPowersShortcutBar*", Anchor_TopRight },
};

const char *getBaseName(const char *filename)
{
	const char *base = filename;
	for (const char *c = filename; *c != '\0'; ++c)
	{
		if (*c == '\\' || *c == '/')
			base = c + 1;
	}
	return base;
}

Bool matchesLayoutName(const char *pattern, const char *name)
{
	const size_t len = strlen(pattern);
	if (len > 0 && pattern[len - 1] == '*')
		return strnicmp(pattern, name, len - 1) == 0;
	return stricmp(pattern, name) == 0;
}

} // namespace

Bool isCenteredHudEnabled()
{
	if (TheDisplay == nullptr || TheFileSystem == nullptr)
		return FALSE;

	// Read once: this is asked for every font size adjustment.
	if (s_centeredHudSetting < 0)
	{
		OptionPreferences pref;
		s_centeredHudSetting = (pref.getCenteredHud() && !addon::HasFullviewportDat()) ? 1 : 0;
	}

	return s_centeredHudSetting == 1;
}

Anchor getLayoutAnchor(const char *layoutFilename)
{
	if (layoutFilename == nullptr || !isCenteredHudEnabled())
		return Anchor_None;

	const char *name = getBaseName(layoutFilename);
	for (size_t i = 0; i < ARRAY_SIZE(s_layoutAnchors); ++i)
	{
		if (matchesLayoutName(s_layoutAnchors[i].name, name))
			return s_layoutAnchors[i].anchor;
	}
	return Anchor_None;
}

Bool getTransform(Anchor anchor, Int creationResX, Int creationResY, Coord2D *scale, ICoord2D *offset)
{
	const Real displayWidth = (Real)TheDisplay->getWidth();
	const Real displayHeight = (Real)TheDisplay->getHeight();
	const Real xScale = displayWidth / (Real)creationResX;
	const Real yScale = displayHeight / (Real)creationResY;

	if (anchor == Anchor_None || anchor >= Anchor_Count)
	{
		scale->x = xScale;
		scale->y = yScale;
		offset->x = 0;
		offset->y = 0;
		return FALSE;
	}

	// Fit the layout inside the display without distorting it. On wide screens this is the height
	// ratio, so the HUD covers the same share of the screen height as it did at 4:3.
	const Real uniform = min(xScale, yScale);
	const Real layoutWidth = (Real)creationResX * uniform;
	const Real layoutHeight = (Real)creationResY * uniform;

	scale->x = uniform;
	scale->y = uniform;

	switch (anchor)
	{
	default:
	case Anchor_BottomCenter:
		offset->x = REAL_TO_INT_FLOOR((displayWidth - layoutWidth) * 0.5f);
		offset->y = REAL_TO_INT_FLOOR(displayHeight - layoutHeight);
		break;
	case Anchor_TopRight:
		offset->x = REAL_TO_INT_FLOOR(displayWidth - layoutWidth);
		offset->y = 0;
		break;
	}
	return TRUE;
}

Bool isControlBarNarrowerThanScreen()
{
	if (!isCenteredHudEnabled())
		return FALSE;

	Coord2D scale;
	ICoord2D offset;
	getTransform(Anchor_BottomCenter, DEFAULT_DISPLAY_WIDTH, DEFAULT_DISPLAY_HEIGHT, &scale, &offset);

	// A pixel or two of rounding is not worth changing the view for.
	return offset.x > 1;
}

} // namespace hud
