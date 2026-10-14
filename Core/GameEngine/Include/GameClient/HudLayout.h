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


// TheSuperHackers @feature Keeps the in-game HUD and the menus at their original 4:3 proportions on any screen.
// The original game stretches every .wnd layout from 800x600 to the display, separately on each
// axis, so on 16:9 and ultrawide screens everything is pulled wide. With the centered layouts,
// layouts are scaled by the same factor on both axes, so they fill the screen height like they do
// at 4:3. The HUD is anchored to the bottom center (or the top right for the general's power
// shortcuts) and can be made smaller with the HUD size option. Menus spread to the screen shape:
// each part keeps its shape and stays with the left edge, the middle or the right edge, so the
// shell map shows between them. Windows that cover the whole width or height of the layout, like
// backgrounds, still stretch to the screen edges.

#pragma once

#include "Lib/BaseType.h"

namespace hud
{

enum Anchor
{
	Anchor_None, ///< Stretch to the display, as in the original game
	Anchor_BottomCenter, ///< Control bar and the popups that belong to it
	Anchor_TopRight, ///< General's power shortcut bar
	Anchor_Center, ///< Menus and dialogs, spread over the screen width

	Anchor_Count
};

enum
{
	HudSizePercentMin = 60,
	HudSizePercentMax = 100,
	HudSizePercentStep = 10,
};

/// Whether the centered HUD is turned on. It is off when the user set CenteredHud = no in
/// Options.ini, or when a 'Control Bar Pro' addon is installed, because those ship their own
/// stretched art for wide screens.
Bool isCenteredHudEnabled();

/// Whether the menus keep their shape. KeepMenuShape = no in Options.ini turns it off, and it is
/// off with a 'Control Bar Pro' addon too.
Bool isMenuShapeKept();

/// The HUD size option in percent of the full height fit, from HudSizePercentMin to 100.
Int getHudSizePercent();

/// Reads the options again, after the user changed them.
void reloadSettings();

/// Whether the layout is part of the HUD, as opposed to a menu.
Bool isHudAnchor(Anchor anchor);

/// The anchor the given .wnd layout uses, or Anchor_None if it stretches.
Anchor getLayoutAnchor(const char *layoutFilename);

/// Computes the uniform scale and the screen offset for a layout authored at the given
/// creation resolution. Returns FALSE (and the plain stretch factors) for Anchor_None.
/// A point p in the layout ends up at p * scale + offset on screen.
Bool getTransform(Anchor anchor, Int creationResX, Int creationResY, Coord2D *scale, ICoord2D *offset);

/// How a menu window sits across the screen width.
enum Side
{
	Side_Auto, ///< No parent to follow: pick by which half of the layout the window is in
	Side_Left,
	Side_Center,
	Side_Right,
	Side_Stretch, ///< Spans the whole layout width, so it stretches to the screen edges
};

/// Moves a window rectangle from layout to screen coordinates. Unlike getTransform, menu windows
/// spread over the screen width: they keep their shape and stay with the left edge, the middle or
/// the right edge, the same as their parent window. Windows that span the whole layout width or
/// height stretch to the screen edges on that axis. Returns the side the window took, for its
/// children to follow.
Side transformRect(Anchor anchor, Int creationResX, Int creationResY, IRegion2D *rect, Side parentSide);

/// The scale from 800x600 that menu art drawn in code should use, so it matches the windows.
Coord2D getMenuScale();

/// Whether the centered control bar leaves screen space on its sides, in which case the
/// tactical view must cover the whole screen instead of stopping above the control bar.
Bool isControlBarNarrowerThanScreen();

} // namespace hud
