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

Bool s_settingsRead = FALSE;
Bool s_centeredHud = FALSE;
Bool s_centeredMenus = FALSE;
Int s_hudSizePercent = HudSizePercentMax;

struct LayoutAnchor
{
	const char *name; ///< Layout file name, or a prefix when it ends in '*'
	Anchor anchor;
};

const LayoutAnchor s_hudLayouts[] =
{
	{ "ControlBar.wnd", Anchor_BottomCenter },
	{ "ControlBarPopupDescription.wnd", Anchor_BottomCenter },
	{ "GeneralsExpPoints.wnd", Anchor_BottomCenter },
	{ "ReplayControl.wnd", Anchor_BottomCenter },
	{ "GenPowersShortcutBar*", Anchor_TopRight },
};

// Layouts that keep stretching even with centered menus: their code places them on screen itself.
const char *const s_stretchedLayouts[] =
{
	"InGamePopupMessage.wnd",
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

Bool readSettings()
{
	if (TheDisplay == nullptr || TheFileSystem == nullptr)
		return FALSE;

	// Read once: this is asked for every font size adjustment.
	if (!s_settingsRead)
	{
		OptionPreferences pref;
		const Bool addonUi = addon::HasFullviewportDat();
		s_centeredHud = pref.getCenteredHud() && !addonUi;
		s_centeredMenus = pref.getCenteredMenus() && !addonUi;
		s_hudSizePercent = pref.getHudSizePercent();
		s_settingsRead = TRUE;
	}
	return TRUE;
}

// A layout edge within this many layout pixels of the layout border counts as touching it.
const Int SpanTolerance = 2;

} // namespace

Bool isCenteredHudEnabled()
{
	return readSettings() && s_centeredHud;
}

Bool isCenteredMenusEnabled()
{
	return readSettings() && s_centeredMenus;
}

Int getHudSizePercent()
{
	if (!readSettings() || !s_centeredHud)
		return HudSizePercentMax;
	return s_hudSizePercent;
}

void reloadSettings()
{
	s_settingsRead = FALSE;
}

Bool isHudAnchor(Anchor anchor)
{
	return anchor == Anchor_BottomCenter || anchor == Anchor_TopRight;
}

Anchor getLayoutAnchor(const char *layoutFilename)
{
	if (layoutFilename == nullptr)
		return Anchor_None;

	const char *name = getBaseName(layoutFilename);
	size_t i;
	for (i = 0; i < ARRAY_SIZE(s_hudLayouts); ++i)
	{
		if (matchesLayoutName(s_hudLayouts[i].name, name))
			return isCenteredHudEnabled() ? s_hudLayouts[i].anchor : Anchor_None;
	}

	if (!isCenteredMenusEnabled())
		return Anchor_None;

	for (i = 0; i < ARRAY_SIZE(s_stretchedLayouts); ++i)
	{
		if (matchesLayoutName(s_stretchedLayouts[i], name))
			return Anchor_None;
	}
	return Anchor_Center;
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
	// ratio, so the layout covers the same share of the screen height as it did at 4:3.
	Real uniform = min(xScale, yScale);
	if (isHudAnchor(anchor))
		uniform *= (Real)getHudSizePercent() / 100.0f;

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
	case Anchor_Center:
		offset->x = REAL_TO_INT_FLOOR((displayWidth - layoutWidth) * 0.5f);
		offset->y = REAL_TO_INT_FLOOR((displayHeight - layoutHeight) * 0.5f);
		break;
	}
	return TRUE;
}

void transformRect(Anchor anchor, Int creationResX, Int creationResY, IRegion2D *rect)
{
	Coord2D scale;
	ICoord2D offset;
	getTransform(anchor, creationResX, creationResY, &scale, &offset);

	const Bool spansWidth = rect->lo.x <= SpanTolerance && rect->hi.x >= creationResX - 1 - SpanTolerance;
	const Bool spansHeight = rect->lo.y <= SpanTolerance && rect->hi.y >= creationResY - 1 - SpanTolerance;

	if (anchor == Anchor_Center && spansWidth)
	{
		// Backgrounds and full width bars reach the screen edges, like they did before.
		const Real xScale = (Real)TheDisplay->getWidth() / (Real)creationResX;
		rect->lo.x = (Int)((Real)rect->lo.x * xScale);
		rect->hi.x = (Int)((Real)rect->hi.x * xScale);
	}
	else
	{
		rect->lo.x = (Int)((Real)rect->lo.x * scale.x) + offset.x;
		rect->hi.x = (Int)((Real)rect->hi.x * scale.x) + offset.x;
	}

	if (anchor == Anchor_Center && spansHeight)
	{
		const Real yScale = (Real)TheDisplay->getHeight() / (Real)creationResY;
		rect->lo.y = (Int)((Real)rect->lo.y * yScale);
		rect->hi.y = (Int)((Real)rect->hi.y * yScale);
	}
	else
	{
		rect->lo.y = (Int)((Real)rect->lo.y * scale.y) + offset.y;
		rect->hi.y = (Int)((Real)rect->hi.y * scale.y) + offset.y;
	}
}

Coord2D getMenuScale()
{
	Coord2D scale;
	ICoord2D offset;
	getTransform(isCenteredMenusEnabled() ? Anchor_Center : Anchor_None,
		DEFAULT_DISPLAY_WIDTH, DEFAULT_DISPLAY_HEIGHT, &scale, &offset);
	return scale;
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
