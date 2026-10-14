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


// TheSuperHackers @feature Keeps the in-game HUD at its original 4:3 proportions on any screen.
// The original game stretches every .wnd layout from 800x600 to the display, separately on each
// axis, so on 16:9 and ultrawide screens the control bar, radar and cameos are pulled wide.
// With the centered HUD the HUD layouts are scaled by the same factor on both axes, so they fill
// the screen height like they do at 4:3, and are anchored to the bottom center (or the top right
// for the general's power shortcuts). Everything else, like the shell menus, still stretches.

#pragma once

#include "Lib/BaseType.h"

namespace hud
{

enum Anchor
{
	Anchor_None, ///< Stretch to the display, as in the original game
	Anchor_BottomCenter, ///< Control bar and the popups that belong to it
	Anchor_TopRight, ///< General's power shortcut bar

	Anchor_Count
};

/// Whether the centered HUD is turned on. It is off when the user set CenteredHud = no in
/// Options.ini, or when a 'Control Bar Pro' addon is installed, because those ship their own
/// stretched art for wide screens.
Bool isCenteredHudEnabled();

/// The anchor the given .wnd layout uses, or Anchor_None if it is not part of the HUD or the
/// centered HUD is off.
Anchor getLayoutAnchor(const char *layoutFilename);

/// Computes the uniform scale and the screen offset for a layout authored at the given
/// creation resolution. Returns FALSE (and the plain stretch factors) for Anchor_None.
/// A point p in the layout ends up at p * scale + offset on screen.
Bool getTransform(Anchor anchor, Int creationResX, Int creationResY, Coord2D *scale, ICoord2D *offset);

/// Whether the centered control bar leaves screen space on its sides, in which case the
/// tactical view must cover the whole screen instead of stopping above the control bar.
Bool isControlBarNarrowerThanScreen();

} // namespace hud
