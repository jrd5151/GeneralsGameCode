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

#pragma once

// TheSuperHackers @build The D3DX8 texture and vertex helpers the game uses, written out so the game
// does not link d3dx8.lib, which only exists for 32-bit x86. They behave like their D3DX8 namesakes for
// the arguments the game passes. Palettes and color keys are not supported.

#include <d3d8.h>
#include "WWMath/d3dxmath.h"

#define D3DX_DEFAULT ((UINT)-1)

#define D3DX_FILTER_NONE      (1 << 0)
#define D3DX_FILTER_POINT     (2 << 0)
#define D3DX_FILTER_LINEAR    (3 << 0)
#define D3DX_FILTER_TRIANGLE  (4 << 0)
#define D3DX_FILTER_BOX       (5 << 0)

// Returns the size in bytes of one vertex of the flexible vertex format.
UINT D3DXGetFVFVertexSize( DWORD fvf );

// Writes the name of a Direct3D error code. Unknown codes are written as hexadecimal numbers.
HRESULT D3DXGetErrorStringA( HRESULT hr, char *buffer, UINT bufferLength );

// Create textures after fitting the arguments to the device: the size is rounded to what the device
// supports, an unsupported format is replaced by the closest supported one, and 0 or D3DX_DEFAULT mip
// levels make a full chain.
HRESULT D3DXCreateTexture( IDirect3DDevice8 *device, UINT width, UINT height, UINT mipLevels,
	DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DTexture8 **texture );
HRESULT D3DXCreateCubeTexture( IDirect3DDevice8 *device, UINT size, UINT mipLevels,
	DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DCubeTexture8 **texture );
HRESULT D3DXCreateVolumeTexture( IDirect3DDevice8 *device, UINT width, UINT height, UINT depth, UINT mipLevels,
	DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DVolumeTexture8 **texture );

// Copies a rectangle of one surface to a rectangle of another, converting the format and scaling.
// Null rectangles mean the whole surface. Palettes must be null.
HRESULT D3DXLoadSurfaceFromSurface( IDirect3DSurface8 *destSurface, const PALETTEENTRY *destPalette, const RECT *destRect,
	IDirect3DSurface8 *srcSurface, const PALETTEENTRY *srcPalette, const RECT *srcRect, DWORD filter, D3DCOLOR colorKey );

// Fills every mip level after srcLevel from the level above it.
HRESULT D3DXFilterTexture( IDirect3DBaseTexture8 *texture, const PALETTEENTRY *palette, UINT srcLevel, DWORD filter );
