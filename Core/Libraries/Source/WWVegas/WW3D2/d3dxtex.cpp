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

#include "d3dxtex.h"

#include <stdio.h>
#include <string.h>
#include <vector>

namespace
{

// Pixels are handled as D3DCOLOR values (8 bits per channel, ARGB) while they are converted and scaled.
typedef std::vector<D3DCOLOR> PixelVector;

struct Image
{
	UINT width;
	UINT height;
	PixelVector pixels;
};

//-------------------------------------------------------------------------------------------------
// Pixel formats
//-------------------------------------------------------------------------------------------------

enum FormatKind
{
	FormatKind_Unsupported,
	FormatKind_Masks,       // channels described by bit masks
	FormatKind_Luminance,   // r, g and b all come from one luminance channel
	FormatKind_Signed,      // signed u and v in the red and green channels, such as bump maps
	FormatKind_Compressed,  // DXT blocks of 4x4 pixels
};

struct FormatInfo
{
	FormatKind kind;
	UINT bytesPerPixel;     // bytes per 4x4 block for compressed formats
	DWORD alphaMask;
	DWORD redMask;
	DWORD greenMask;
	DWORD blueMask;
};

FormatInfo getFormatInfo( D3DFORMAT format )
{
	FormatInfo info = { FormatKind_Masks, 0, 0, 0, 0, 0 };
	switch (format)
	{
	case D3DFMT_A8R8G8B8: info.bytesPerPixel = 4; info.alphaMask = 0xff000000; info.redMask = 0x00ff0000; info.greenMask = 0x0000ff00; info.blueMask = 0x000000ff; break;
	case D3DFMT_X8R8G8B8: info.bytesPerPixel = 4; info.redMask = 0x00ff0000; info.greenMask = 0x0000ff00; info.blueMask = 0x000000ff; break;
	case D3DFMT_R8G8B8: info.bytesPerPixel = 3; info.redMask = 0x00ff0000; info.greenMask = 0x0000ff00; info.blueMask = 0x000000ff; break;
	case D3DFMT_R5G6B5: info.bytesPerPixel = 2; info.redMask = 0xf800; info.greenMask = 0x07e0; info.blueMask = 0x001f; break;
	case D3DFMT_X1R5G5B5: info.bytesPerPixel = 2; info.redMask = 0x7c00; info.greenMask = 0x03e0; info.blueMask = 0x001f; break;
	case D3DFMT_A1R5G5B5: info.bytesPerPixel = 2; info.alphaMask = 0x8000; info.redMask = 0x7c00; info.greenMask = 0x03e0; info.blueMask = 0x001f; break;
	case D3DFMT_A4R4G4B4: info.bytesPerPixel = 2; info.alphaMask = 0xf000; info.redMask = 0x0f00; info.greenMask = 0x00f0; info.blueMask = 0x000f; break;
	case D3DFMT_X4R4G4B4: info.bytesPerPixel = 2; info.redMask = 0x0f00; info.greenMask = 0x00f0; info.blueMask = 0x000f; break;
	case D3DFMT_A8R3G3B2: info.bytesPerPixel = 2; info.alphaMask = 0xff00; info.redMask = 0x00e0; info.greenMask = 0x001c; info.blueMask = 0x0003; break;
	case D3DFMT_R3G3B2: info.bytesPerPixel = 1; info.redMask = 0xe0; info.greenMask = 0x1c; info.blueMask = 0x03; break;
	case D3DFMT_A8: info.bytesPerPixel = 1; info.alphaMask = 0xff; break;
	case D3DFMT_L8: info.kind = FormatKind_Luminance; info.bytesPerPixel = 1; info.redMask = 0xff; break;
	case D3DFMT_A8L8: info.kind = FormatKind_Luminance; info.bytesPerPixel = 2; info.alphaMask = 0xff00; info.redMask = 0x00ff; break;
	case D3DFMT_A4L4: info.kind = FormatKind_Luminance; info.bytesPerPixel = 1; info.alphaMask = 0xf0; info.redMask = 0x0f; break;
	case D3DFMT_V8U8: info.kind = FormatKind_Signed; info.bytesPerPixel = 2; info.redMask = 0x00ff; info.greenMask = 0xff00; break;
	case D3DFMT_DXT1: info.kind = FormatKind_Compressed; info.bytesPerPixel = 8; break;
	case D3DFMT_DXT2:
	case D3DFMT_DXT3:
	case D3DFMT_DXT4:
	case D3DFMT_DXT5: info.kind = FormatKind_Compressed; info.bytesPerPixel = 16; break;
	default: info.kind = FormatKind_Unsupported; break;
	}
	return info;
}

UINT maskShift( DWORD mask )
{
	UINT shift = 0;
	if (mask == 0)
		return 0;
	while ((mask & 1) == 0)
	{
		mask >>= 1;
		++shift;
	}
	return shift;
}

// Expands a channel to 8 bits.
UINT unpackChannel( DWORD pixel, DWORD mask, UINT defaultValue )
{
	if (mask == 0)
		return defaultValue;
	const UINT shift = maskShift(mask);
	const DWORD maxValue = mask >> shift;
	const DWORD value = (pixel & mask) >> shift;
	return (UINT)((value * 255 + maxValue / 2) / maxValue);
}

// Narrows an 8 bit channel.
DWORD packChannel( UINT value, DWORD mask )
{
	if (mask == 0)
		return 0;
	const UINT shift = maskShift(mask);
	const DWORD maxValue = mask >> shift;
	return (((value * maxValue + 127) / 255) << shift) & mask;
}

DWORD readRaw( const BYTE *p, UINT bytes )
{
	DWORD value = 0;
	for (UINT i = 0; i < bytes; ++i)
		value |= (DWORD)p[i] << (8 * i);
	return value;
}

void writeRaw( BYTE *p, UINT bytes, DWORD value )
{
	for (UINT i = 0; i < bytes; ++i)
		p[i] = (BYTE)(value >> (8 * i));
}

D3DCOLOR makeColor( UINT a, UINT r, UINT g, UINT b )
{
	return (D3DCOLOR)((a << 24) | (r << 16) | (g << 8) | b);
}

UINT colorA( D3DCOLOR c ) { return (c >> 24) & 0xff; }
UINT colorR( D3DCOLOR c ) { return (c >> 16) & 0xff; }
UINT colorG( D3DCOLOR c ) { return (c >> 8) & 0xff; }
UINT colorB( D3DCOLOR c ) { return c & 0xff; }

D3DCOLOR decodePixel( const FormatInfo &info, DWORD raw )
{
	switch (info.kind)
	{
	case FormatKind_Luminance:
	{
		const UINT l = unpackChannel(raw, info.redMask, 255);
		return makeColor(unpackChannel(raw, info.alphaMask, 255), l, l, l);
	}
	case FormatKind_Signed:
		// Store the signed values offset by 128, so averaging them stays correct.
		return makeColor(255, (UINT)(((int)(signed char)(raw & 0xff)) + 128), (UINT)(((int)(signed char)((raw >> 8) & 0xff)) + 128), 255);
	default:
		return makeColor(
			unpackChannel(raw, info.alphaMask, 255),
			unpackChannel(raw, info.redMask, 0),
			unpackChannel(raw, info.greenMask, 0),
			unpackChannel(raw, info.blueMask, 0));
	}
}

DWORD encodePixel( const FormatInfo &info, D3DCOLOR c )
{
	switch (info.kind)
	{
	case FormatKind_Luminance:
	{
		// The same weights as D3DX8.
		const UINT l = (colorR(c) * 2125 + colorG(c) * 7154 + colorB(c) * 721 + 5000) / 10000;
		return packChannel(colorA(c), info.alphaMask) | packChannel(l, info.redMask);
	}
	case FormatKind_Signed:
	{
		const DWORD u = (DWORD)(BYTE)(signed char)((int)colorR(c) - 128);
		const DWORD v = (DWORD)(BYTE)(signed char)((int)colorG(c) - 128);
		return u | (v << 8);
	}
	default:
		return packChannel(colorA(c), info.alphaMask) | packChannel(colorR(c), info.redMask)
			| packChannel(colorG(c), info.greenMask) | packChannel(colorB(c), info.blueMask);
	}
}

//-------------------------------------------------------------------------------------------------
// DXT blocks
//-------------------------------------------------------------------------------------------------

D3DCOLOR expand565( UINT c )
{
	const UINT r = (c >> 11) & 0x1f;
	const UINT g = (c >> 5) & 0x3f;
	const UINT b = c & 0x1f;
	return makeColor(255, (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2));
}

UINT pack565( D3DCOLOR c )
{
	return ((colorR(c) * 31 + 127) / 255 << 11) | ((colorG(c) * 63 + 127) / 255 << 5) | ((colorB(c) * 31 + 127) / 255);
}

D3DCOLOR mixColors( D3DCOLOR a, D3DCOLOR b, UINT weightA, UINT weightB, UINT divisor )
{
	return makeColor(255,
		(colorR(a) * weightA + colorR(b) * weightB) / divisor,
		(colorG(a) * weightA + colorG(b) * weightB) / divisor,
		(colorB(a) * weightA + colorB(b) * weightB) / divisor);
}

void colorPalette( UINT c0, UINT c1, bool allowTransparent, D3DCOLOR palette[4] )
{
	palette[0] = expand565(c0);
	palette[1] = expand565(c1);
	if (c0 > c1 || !allowTransparent)
	{
		palette[2] = mixColors(palette[0], palette[1], 2, 1, 3);
		palette[3] = mixColors(palette[0], palette[1], 1, 2, 3);
	}
	else
	{
		palette[2] = mixColors(palette[0], palette[1], 1, 1, 2);
		palette[3] = 0;
	}
}

void alphaPalette( UINT a0, UINT a1, UINT palette[8] )
{
	palette[0] = a0;
	palette[1] = a1;
	if (a0 > a1)
	{
		for (UINT i = 1; i < 7; ++i)
			palette[i + 1] = ((7 - i) * a0 + i * a1) / 7;
	}
	else
	{
		for (UINT j = 1; j < 5; ++j)
			palette[j + 1] = ((5 - j) * a0 + j * a1) / 5;
		palette[6] = 0;
		palette[7] = 255;
	}
}

// Decodes one block into 16 pixels, row by row.
void decodeBlock( D3DFORMAT format, const BYTE *block, D3DCOLOR out[16] )
{
	const BYTE *colorBlock = (format == D3DFMT_DXT1) ? block : block + 8;
	const UINT c0 = colorBlock[0] | (colorBlock[1] << 8);
	const UINT c1 = colorBlock[2] | (colorBlock[3] << 8);
	D3DCOLOR palette[4];
	colorPalette(c0, c1, format == D3DFMT_DXT1, palette);
	const DWORD indices = readRaw(colorBlock + 4, 4);
	for (UINT i = 0; i < 16; ++i)
		out[i] = palette[(indices >> (2 * i)) & 3];

	if (format == D3DFMT_DXT2 || format == D3DFMT_DXT3)
	{
		for (UINT p = 0; p < 16; ++p)
		{
			const UINT a = (block[p / 2] >> ((p & 1) * 4)) & 0xf;
			out[p] = (out[p] & 0x00ffffff) | ((a * 17) << 24);
		}
	}
	else if (format == D3DFMT_DXT4 || format == D3DFMT_DXT5)
	{
		UINT alphas[8];
		alphaPalette(block[0], block[1], alphas);
		const DWORD lowBits = readRaw(block + 2, 3);
		const DWORD highBits = readRaw(block + 5, 3);
		for (UINT p = 0; p < 16; ++p)
		{
			const UINT index = (p < 8) ? ((lowBits >> (3 * p)) & 7) : ((highBits >> (3 * (p - 8))) & 7);
			out[p] = (out[p] & 0x00ffffff) | (alphas[index] << 24);
		}
	}
}

UINT colorDistance( D3DCOLOR a, D3DCOLOR b )
{
	const int dr = (int)colorR(a) - (int)colorR(b);
	const int dg = (int)colorG(a) - (int)colorG(b);
	const int db = (int)colorB(a) - (int)colorB(b);
	return (UINT)(dr * dr + dg * dg + db * db);
}

// A simple encoder: the end colors are the darkest and brightest pixels of the block.
void encodeBlock( D3DFORMAT format, const D3DCOLOR in[16], BYTE *block )
{
	BYTE *colorBlock = (format == D3DFMT_DXT1) ? block : block + 8;
	const bool transparent1Bit = (format == D3DFMT_DXT1);

	bool anyTransparent = false;
	UINT minLum = 0xffffffff;
	UINT maxLum = 0;
	D3DCOLOR minColor = 0;
	D3DCOLOR maxColor = 0;
	for (UINT i = 0; i < 16; ++i)
	{
		if (transparent1Bit && colorA(in[i]) < 128)
		{
			anyTransparent = true;
			continue;
		}
		const UINT lum = colorR(in[i]) * 77 + colorG(in[i]) * 150 + colorB(in[i]) * 29;
		if (lum < minLum) { minLum = lum; minColor = in[i]; }
		if (lum >= maxLum) { maxLum = lum; maxColor = in[i]; }
	}

	UINT c0 = pack565(maxColor);
	UINT c1 = pack565(minColor);
	if (anyTransparent)
	{
		// c0 <= c1 selects three colors and transparent black.
		if (c0 > c1) { const UINT t = c0; c0 = c1; c1 = t; }
	}
	else
	{
		if (c0 < c1) { const UINT t = c0; c0 = c1; c1 = t; }
		if (c0 == c1)
		{
			// Every pixel uses c0, and c0 > c1 keeps four color mode.
			if (c1 > 0) --c1; else ++c0;
		}
	}

	D3DCOLOR palette[4];
	colorPalette(c0, c1, transparent1Bit, palette);
	const UINT paletteSize = anyTransparent ? 3 : 4;
	DWORD indices = 0;
	for (UINT p = 0; p < 16; ++p)
	{
		UINT best = 0;
		if (anyTransparent && colorA(in[p]) < 128)
		{
			best = 3;
		}
		else
		{
			UINT bestDistance = 0xffffffff;
			for (UINT k = 0; k < paletteSize; ++k)
			{
				const UINT d = colorDistance(in[p], palette[k]);
				if (d < bestDistance) { bestDistance = d; best = k; }
			}
		}
		indices |= (DWORD)best << (2 * p);
	}
	writeRaw(colorBlock, 2, c0);
	writeRaw(colorBlock + 2, 2, c1);
	writeRaw(colorBlock + 4, 4, indices);

	if (format == D3DFMT_DXT2 || format == D3DFMT_DXT3)
	{
		memset(block, 0, 8);
		for (UINT q = 0; q < 16; ++q)
			block[q / 2] |= (BYTE)(((colorA(in[q]) * 15 + 127) / 255) << ((q & 1) * 4));
	}
	else if (format == D3DFMT_DXT4 || format == D3DFMT_DXT5)
	{
		UINT a0 = 0;
		UINT a1 = 255;
		for (UINT q = 0; q < 16; ++q)
		{
			if (colorA(in[q]) > a0) a0 = colorA(in[q]);
			if (colorA(in[q]) < a1) a1 = colorA(in[q]);
		}
		if (a0 == a1)
		{
			if (a1 > 0) --a1; else ++a0;
		}
		UINT alphas[8];
		alphaPalette(a0, a1, alphas);
		DWORD lowBits = 0;
		DWORD highBits = 0;
		for (UINT r = 0; r < 16; ++r)
		{
			UINT best = 0;
			int bestDistance = 1000;
			for (UINT k = 0; k < 8; ++k)
			{
				int d = (int)alphas[k] - (int)colorA(in[r]);
				if (d < 0) d = -d;
				if (d < bestDistance) { bestDistance = d; best = k; }
			}
			if (r < 8) lowBits |= (DWORD)best << (3 * r);
			else highBits |= (DWORD)best << (3 * (r - 8));
		}
		block[0] = (BYTE)a0;
		block[1] = (BYTE)a1;
		writeRaw(block + 2, 3, lowBits);
		writeRaw(block + 5, 3, highBits);
	}
}

//-------------------------------------------------------------------------------------------------
// Reading and writing locked memory
//-------------------------------------------------------------------------------------------------

// Decodes the pixels of a locked area whose top left pixel is the start of the lock.
void decodeArea( D3DFORMAT format, const FormatInfo &info, const D3DLOCKED_RECT &locked, UINT width, UINT height, Image &image )
{
	image.width = width;
	image.height = height;
	image.pixels.resize(width * height);
	const BYTE *bits = (const BYTE *)locked.pBits;

	if (info.kind == FormatKind_Compressed)
	{
		for (UINT by = 0; by < height; by += 4)
		{
			const BYTE *blockRow = bits + (by / 4) * locked.Pitch;
			for (UINT bx = 0; bx < width; bx += 4)
			{
				D3DCOLOR block[16];
				decodeBlock(format, blockRow + (bx / 4) * info.bytesPerPixel, block);
				for (UINT y = 0; y < 4 && by + y < height; ++y)
					for (UINT x = 0; x < 4 && bx + x < width; ++x)
						image.pixels[(by + y) * width + bx + x] = block[y * 4 + x];
			}
		}
		return;
	}

	for (UINT y = 0; y < height; ++y)
	{
		const BYTE *row = bits + y * locked.Pitch;
		for (UINT x = 0; x < width; ++x)
			image.pixels[y * width + x] = decodePixel(info, readRaw(row + x * info.bytesPerPixel, info.bytesPerPixel));
	}
}

void encodeArea( D3DFORMAT format, const FormatInfo &info, const D3DLOCKED_RECT &locked, const Image &image )
{
	BYTE *bits = (BYTE *)locked.pBits;

	if (info.kind == FormatKind_Compressed)
	{
		for (UINT by = 0; by < image.height; by += 4)
		{
			BYTE *blockRow = bits + (by / 4) * locked.Pitch;
			for (UINT bx = 0; bx < image.width; bx += 4)
			{
				// Pixels past the edge of a small surface repeat the last row and column.
				D3DCOLOR block[16];
				for (UINT y = 0; y < 4; ++y)
				{
					const UINT sy = (by + y < image.height) ? by + y : image.height - 1;
					for (UINT x = 0; x < 4; ++x)
					{
						const UINT sx = (bx + x < image.width) ? bx + x : image.width - 1;
						block[y * 4 + x] = image.pixels[sy * image.width + sx];
					}
				}
				encodeBlock(format, block, blockRow + (bx / 4) * info.bytesPerPixel);
			}
		}
		return;
	}

	for (UINT y = 0; y < image.height; ++y)
	{
		BYTE *row = bits + y * locked.Pitch;
		for (UINT x = 0; x < image.width; ++x)
			writeRaw(row + x * info.bytesPerPixel, info.bytesPerPixel, encodePixel(info, image.pixels[y * image.width + x]));
	}
}

//-------------------------------------------------------------------------------------------------
// Surfaces
//-------------------------------------------------------------------------------------------------

RECT fullRect( const D3DSURFACE_DESC &desc )
{
	RECT rect;
	rect.left = 0;
	rect.top = 0;
	rect.right = (LONG)desc.Width;
	rect.bottom = (LONG)desc.Height;
	return rect;
}

// Compressed surfaces lock in whole blocks.
RECT blockAlignedRect( const RECT &rect, const D3DSURFACE_DESC &desc )
{
	RECT aligned;
	aligned.left = rect.left & ~3;
	aligned.top = rect.top & ~3;
	aligned.right = (rect.right + 3) & ~3;
	aligned.bottom = (rect.bottom + 3) & ~3;
	if (aligned.right > (LONG)desc.Width) aligned.right = (LONG)desc.Width;
	if (aligned.bottom > (LONG)desc.Height) aligned.bottom = (LONG)desc.Height;
	return aligned;
}

// Surfaces in video memory that cannot be locked are copied through a surface in system memory.
IDirect3DSurface8 *createStagingSurface( IDirect3DSurface8 *surface, const D3DSURFACE_DESC &desc )
{
	IDirect3DDevice8 *device = nullptr;
	if (FAILED(surface->GetDevice(&device)))
		return nullptr;
	IDirect3DSurface8 *staging = nullptr;
	if (FAILED(device->CreateImageSurface(desc.Width, desc.Height, desc.Format, &staging)))
		staging = nullptr;
	device->Release();
	return staging;
}

bool copySurface( IDirect3DSurface8 *from, IDirect3DSurface8 *to )
{
	IDirect3DDevice8 *device = nullptr;
	if (FAILED(from->GetDevice(&device)))
		return false;
	const HRESULT hr = device->CopyRects(from, nullptr, 0, to, nullptr);
	device->Release();
	return SUCCEEDED(hr);
}

HRESULT readSurface( IDirect3DSurface8 *surface, const RECT &rect, Image &image )
{
	D3DSURFACE_DESC desc;
	HRESULT hr = surface->GetDesc(&desc);
	if (FAILED(hr))
		return hr;
	const FormatInfo info = getFormatInfo(desc.Format);
	if (info.kind == FormatKind_Unsupported)
		return D3DERR_INVALIDCALL;

	const RECT lockRect = (info.kind == FormatKind_Compressed) ? blockAlignedRect(rect, desc) : rect;
	IDirect3DSurface8 *source = surface;
	IDirect3DSurface8 *staging = nullptr;
	D3DLOCKED_RECT locked;
	hr = surface->LockRect(&locked, &lockRect, D3DLOCK_READONLY);
	if (FAILED(hr))
	{
		staging = createStagingSurface(surface, desc);
		if (staging == nullptr || !copySurface(surface, staging))
		{
			if (staging) staging->Release();
			return hr;
		}
		source = staging;
		hr = source->LockRect(&locked, &lockRect, D3DLOCK_READONLY);
		if (FAILED(hr))
		{
			staging->Release();
			return hr;
		}
	}

	Image area;
	decodeArea(desc.Format, info, locked, lockRect.right - lockRect.left, lockRect.bottom - lockRect.top, area);
	source->UnlockRect();
	if (staging)
		staging->Release();

	// Crop the block aligned area to the asked rectangle.
	image.width = rect.right - rect.left;
	image.height = rect.bottom - rect.top;
	image.pixels.resize(image.width * image.height);
	const UINT offsetX = rect.left - lockRect.left;
	const UINT offsetY = rect.top - lockRect.top;
	for (UINT y = 0; y < image.height; ++y)
		for (UINT x = 0; x < image.width; ++x)
			image.pixels[y * image.width + x] = area.pixels[(y + offsetY) * area.width + x + offsetX];
	return D3D_OK;
}

HRESULT writeSurface( IDirect3DSurface8 *surface, const RECT &rect, const Image &image )
{
	D3DSURFACE_DESC desc;
	HRESULT hr = surface->GetDesc(&desc);
	if (FAILED(hr))
		return hr;
	const FormatInfo info = getFormatInfo(desc.Format);
	if (info.kind == FormatKind_Unsupported)
		return D3DERR_INVALIDCALL;

	// A compressed surface is written in whole blocks, so blocks the rectangle only covers in part keep
	// the pixels around it.
	const RECT lockRect = (info.kind == FormatKind_Compressed) ? blockAlignedRect(rect, desc) : rect;
	Image area;
	if (lockRect.left != rect.left || lockRect.top != rect.top || lockRect.right != rect.right || lockRect.bottom != rect.bottom)
	{
		hr = readSurface(surface, lockRect, area);
		if (FAILED(hr))
			return hr;
	}
	else
	{
		area.width = lockRect.right - lockRect.left;
		area.height = lockRect.bottom - lockRect.top;
		area.pixels.resize(area.width * area.height);
	}
	const UINT offsetX = rect.left - lockRect.left;
	const UINT offsetY = rect.top - lockRect.top;
	for (UINT y = 0; y < image.height; ++y)
		for (UINT x = 0; x < image.width; ++x)
			area.pixels[(y + offsetY) * area.width + x + offsetX] = image.pixels[y * image.width + x];

	D3DLOCKED_RECT locked;
	hr = surface->LockRect(&locked, &lockRect, 0);
	if (SUCCEEDED(hr))
	{
		encodeArea(desc.Format, info, locked, area);
		surface->UnlockRect();
		return D3D_OK;
	}

	IDirect3DSurface8 *staging = createStagingSurface(surface, desc);
	if (staging == nullptr)
		return hr;
	// Keep the rest of the surface when only a part of it is written.
	copySurface(surface, staging);
	hr = staging->LockRect(&locked, &lockRect, 0);
	if (SUCCEEDED(hr))
	{
		encodeArea(desc.Format, info, locked, area);
		staging->UnlockRect();

		IDirect3DDevice8 *device = nullptr;
		hr = surface->GetDevice(&device);
		if (SUCCEEDED(hr))
		{
			POINT destPoint;
			destPoint.x = lockRect.left;
			destPoint.y = lockRect.top;
			hr = device->CopyRects(staging, &lockRect, 1, surface, &destPoint);
			device->Release();
		}
	}
	staging->Release();
	return hr;
}

//-------------------------------------------------------------------------------------------------
// Scaling
//-------------------------------------------------------------------------------------------------

struct Weight
{
	UINT index;
	float weight;
};

enum ScaleFilter
{
	ScaleFilter_Point,
	ScaleFilter_Box,
	ScaleFilter_Triangle,
};

// For each destination pixel, the source pixels it takes and how much of each.
void buildWeights( UINT srcSize, UINT dstSize, ScaleFilter filter, std::vector< std::vector<Weight> > &weights )
{
	weights.resize(dstSize);
	const float scale = (float)srcSize / (float)dstSize;
	for (UINT d = 0; d < dstSize; ++d)
	{
		std::vector<Weight> &list = weights[d];
		list.clear();
		Weight w;
		if (filter == ScaleFilter_Point)
		{
			w.index = (UINT)((d + 0.5f) * scale);
			if (w.index >= srcSize) w.index = srcSize - 1;
			w.weight = 1.0f;
			list.push_back(w);
		}
		else if (scale > 1.0f && filter == ScaleFilter_Triangle)
		{
			// Shrinking: a tent as wide as two destination pixels, centered on this one.
			const float center = (d + 0.5f) * scale;
			int first = (int)(center - scale);
			if (first < 0) first = 0;
			float total = 0.0f;
			for (UINT s = (UINT)first; s < srcSize && (float)s < center + scale; ++s)
			{
				float distance = ((float)s + 0.5f - center) / scale;
				if (distance < 0.0f) distance = -distance;
				if (distance < 1.0f)
				{
					w.index = s;
					w.weight = 1.0f - distance;
					total += w.weight;
					list.push_back(w);
				}
			}
			for (size_t i = 0; i < list.size(); ++i)
				list[i].weight /= total;
		}
		else if (scale >= 1.0f)
		{
			// Shrinking: average the source pixels under the destination pixel.
			const float start = d * scale;
			const float end = start + scale;
			for (UINT s = (UINT)start; s < srcSize && (float)s < end; ++s)
			{
				const float lo = ((float)s > start) ? (float)s : start;
				const float hi = ((float)(s + 1) < end) ? (float)(s + 1) : end;
				if (hi > lo)
				{
					w.index = s;
					w.weight = (hi - lo) / scale;
					list.push_back(w);
				}
			}
		}
		else
		{
			// Growing: blend the two nearest source pixels.
			float center = (d + 0.5f) * scale - 0.5f;
			if (center < 0.0f) center = 0.0f;
			UINT s0 = (UINT)center;
			if (s0 >= srcSize) s0 = srcSize - 1;
			const UINT s1 = (s0 + 1 < srcSize) ? s0 + 1 : s0;
			const float t = center - (float)s0;
			w.index = s0;
			w.weight = 1.0f - t;
			list.push_back(w);
			if (s1 != s0)
			{
				w.index = s1;
				w.weight = t;
				list.push_back(w);
			}
		}
	}
}

void scaleImage( const Image &src, UINT dstWidth, UINT dstHeight, DWORD filter, Image &dst )
{
	dst.width = dstWidth;
	dst.height = dstHeight;
	if (src.width == dstWidth && src.height == dstHeight)
	{
		dst.pixels = src.pixels;
		return;
	}
	dst.pixels.resize(dstWidth * dstHeight);

	const DWORD filterType = filter & 0xffff;
	if (filterType == D3DX_FILTER_NONE)
	{
		// No scaling: the source is copied to the top left, and the pixels it does not cover are transparent black.
		for (UINT y = 0; y < dstHeight; ++y)
		{
			for (UINT x = 0; x < dstWidth; ++x)
				dst.pixels[y * dstWidth + x] = (x < src.width && y < src.height) ? src.pixels[y * src.width + x] : 0;
		}
		return;
	}

	ScaleFilter scaleFilter = ScaleFilter_Box;
	if (filterType == D3DX_FILTER_POINT)
		scaleFilter = ScaleFilter_Point;
	else if (filterType == D3DX_FILTER_LINEAR || filterType == D3DX_FILTER_TRIANGLE)
		scaleFilter = ScaleFilter_Triangle;
	std::vector< std::vector<Weight> > columnWeights;
	std::vector< std::vector<Weight> > rowWeights;
	buildWeights(src.width, dstWidth, scaleFilter, columnWeights);
	buildWeights(src.height, dstHeight, scaleFilter, rowWeights);

	for (UINT row = 0; row < dstHeight; ++row)
	{
		const std::vector<Weight> &rows = rowWeights[row];
		for (UINT column = 0; column < dstWidth; ++column)
		{
			const std::vector<Weight> &columns = columnWeights[column];
			float a = 0.0f, r = 0.0f, g = 0.0f, b = 0.0f;
			for (size_t i = 0; i < rows.size(); ++i)
			{
				for (size_t j = 0; j < columns.size(); ++j)
				{
					const D3DCOLOR c = src.pixels[rows[i].index * src.width + columns[j].index];
					const float w = rows[i].weight * columns[j].weight;
					a += w * colorA(c);
					r += w * colorR(c);
					g += w * colorG(c);
					b += w * colorB(c);
				}
			}
			dst.pixels[row * dstWidth + column] = makeColor((UINT)(a + 0.5f), (UINT)(r + 0.5f), (UINT)(g + 0.5f), (UINT)(b + 0.5f));
		}
	}
}

//-------------------------------------------------------------------------------------------------
// Texture creation
//-------------------------------------------------------------------------------------------------

bool isCompressed( D3DFORMAT format )
{
	return format == D3DFMT_DXT1 || format == D3DFMT_DXT2 || format == D3DFMT_DXT3 || format == D3DFMT_DXT4 || format == D3DFMT_DXT5;
}

struct DeviceInfo
{
	IDirect3D8 *d3d;
	D3DCAPS8 caps;
	D3DDEVICE_CREATION_PARAMETERS creation;
	D3DDISPLAYMODE mode;
};

HRESULT getDeviceInfo( IDirect3DDevice8 *device, DeviceInfo &info )
{
	HRESULT hr = device->GetDirect3D(&info.d3d);
	if (FAILED(hr))
		return hr;
	if (FAILED(hr = device->GetDeviceCaps(&info.caps))
		|| FAILED(hr = device->GetCreationParameters(&info.creation))
		|| FAILED(hr = device->GetDisplayMode(&info.mode)))
	{
		info.d3d->Release();
		return hr;
	}
	return D3D_OK;
}

// The formats to try in order when a format is not supported.
D3DFORMAT findFormat( const DeviceInfo &info, DWORD usage, D3DRESOURCETYPE type, D3DFORMAT format )
{
	if (format == D3DFMT_UNKNOWN || format == (D3DFORMAT)D3DX_DEFAULT)
		format = D3DFMT_A8R8G8B8;
	if (SUCCEEDED(info.d3d->CheckDeviceFormat(info.creation.AdapterOrdinal, info.creation.DeviceType, info.mode.Format, usage, type, format)))
		return format;

	static const D3DFORMAT withAlpha[] = { D3DFMT_A8R8G8B8, D3DFMT_A4R4G4B4, D3DFMT_A1R5G5B5, D3DFMT_X8R8G8B8, D3DFMT_R5G6B5 };
	static const D3DFORMAT withoutAlpha[] = { D3DFMT_X8R8G8B8, D3DFMT_R5G6B5, D3DFMT_X1R5G5B5, D3DFMT_A8R8G8B8, D3DFMT_A1R5G5B5, D3DFMT_A4R4G4B4 };
	static const D3DFORMAT luminance[] = { D3DFMT_L8, D3DFMT_A8L8, D3DFMT_X8R8G8B8, D3DFMT_R5G6B5, D3DFMT_A8R8G8B8 };
	static const D3DFORMAT luminanceAlpha[] = { D3DFMT_A8L8, D3DFMT_A8R8G8B8, D3DFMT_A4R4G4B4 };
	static const D3DFORMAT alphaOnly[] = { D3DFMT_A8, D3DFMT_A8L8, D3DFMT_A8R8G8B8, D3DFMT_A4R4G4B4 };

	const D3DFORMAT *candidates = withAlpha;
	size_t count = sizeof(withAlpha) / sizeof(withAlpha[0]);
	switch (format)
	{
	case D3DFMT_X8R8G8B8: case D3DFMT_R8G8B8: case D3DFMT_R5G6B5: case D3DFMT_X1R5G5B5:
	case D3DFMT_X4R4G4B4: case D3DFMT_R3G3B2: case D3DFMT_DXT1:
		candidates = withoutAlpha; count = sizeof(withoutAlpha) / sizeof(withoutAlpha[0]); break;
	case D3DFMT_L8:
		candidates = luminance; count = sizeof(luminance) / sizeof(luminance[0]); break;
	case D3DFMT_A8L8: case D3DFMT_A4L4:
		candidates = luminanceAlpha; count = sizeof(luminanceAlpha) / sizeof(luminanceAlpha[0]); break;
	case D3DFMT_A8:
		candidates = alphaOnly; count = sizeof(alphaOnly) / sizeof(alphaOnly[0]); break;
	default:
		break;
	}
	for (size_t i = 0; i < count; ++i)
	{
		if (SUCCEEDED(info.d3d->CheckDeviceFormat(info.creation.AdapterOrdinal, info.creation.DeviceType, info.mode.Format, usage, type, candidates[i])))
			return candidates[i];
	}
	// Let the device report the error.
	return format;
}

UINT fitSize( UINT size, bool powerOfTwo, UINT maxSize )
{
	if (size == D3DX_DEFAULT)
		size = 256;
	else if (size == 0)
		size = 1;
	if (powerOfTwo)
	{
		UINT pow2 = 1;
		while (pow2 < size)
			pow2 <<= 1;
		size = pow2;
	}
	if (maxSize != 0 && size > maxSize)
	{
		size = maxSize;
		if (powerOfTwo)
		{
			UINT pow2 = 1;
			while (pow2 * 2 <= maxSize)
				pow2 <<= 1;
			size = pow2;
		}
	}
	return size;
}

UINT fitMipLevels( UINT mipLevels, UINT width, UINT height, UINT depth, bool canMipmap )
{
	UINT largest = width;
	if (height > largest) largest = height;
	if (depth > largest) largest = depth;
	UINT fullChain = 1;
	while (largest > 1)
	{
		largest >>= 1;
		++fullChain;
	}
	if (!canMipmap)
		return 1;
	if (mipLevels == 0 || mipLevels == D3DX_DEFAULT || mipLevels > fullChain)
		return fullChain;
	return mipLevels;
}

} // namespace

//-------------------------------------------------------------------------------------------------
UINT D3DXGetFVFVertexSize( DWORD fvf )
{
	UINT size = 0;
	switch (fvf & D3DFVF_POSITION_MASK)
	{
	case D3DFVF_XYZ: size += 12; break;
	case D3DFVF_XYZRHW: size += 16; break;
	case D3DFVF_XYZB1: size += 16; break;
	case D3DFVF_XYZB2: size += 20; break;
	case D3DFVF_XYZB3: size += 24; break;
	case D3DFVF_XYZB4: size += 28; break;
	case D3DFVF_XYZB5: size += 32; break;
	default: break;
	}
	if (fvf & D3DFVF_NORMAL) size += 12;
	if (fvf & D3DFVF_PSIZE) size += 4;
	if (fvf & D3DFVF_DIFFUSE) size += 4;
	if (fvf & D3DFVF_SPECULAR) size += 4;

	const UINT texCount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
	for (UINT i = 0; i < texCount; ++i)
	{
		switch ((fvf >> (16 + 2 * i)) & 3)
		{
		case D3DFVF_TEXTUREFORMAT1: size += 4; break;
		case D3DFVF_TEXTUREFORMAT2: size += 8; break;
		case D3DFVF_TEXTUREFORMAT3: size += 12; break;
		case D3DFVF_TEXTUREFORMAT4: size += 16; break;
		}
	}
	return size;
}

//-------------------------------------------------------------------------------------------------
HRESULT D3DXGetErrorStringA( HRESULT hr, char *buffer, UINT bufferLength )
{
	if (buffer == nullptr || bufferLength == 0)
		return D3DERR_INVALIDCALL;

	const char *name = nullptr;
	switch (hr)
	{
	case D3D_OK: name = "D3D_OK"; break;
	case D3DERR_WRONGTEXTUREFORMAT: name = "D3DERR_WRONGTEXTUREFORMAT"; break;
	case D3DERR_UNSUPPORTEDCOLOROPERATION: name = "D3DERR_UNSUPPORTEDCOLOROPERATION"; break;
	case D3DERR_UNSUPPORTEDCOLORARG: name = "D3DERR_UNSUPPORTEDCOLORARG"; break;
	case D3DERR_UNSUPPORTEDALPHAOPERATION: name = "D3DERR_UNSUPPORTEDALPHAOPERATION"; break;
	case D3DERR_UNSUPPORTEDALPHAARG: name = "D3DERR_UNSUPPORTEDALPHAARG"; break;
	case D3DERR_TOOMANYOPERATIONS: name = "D3DERR_TOOMANYOPERATIONS"; break;
	case D3DERR_CONFLICTINGTEXTUREFILTER: name = "D3DERR_CONFLICTINGTEXTUREFILTER"; break;
	case D3DERR_UNSUPPORTEDFACTORVALUE: name = "D3DERR_UNSUPPORTEDFACTORVALUE"; break;
	case D3DERR_CONFLICTINGRENDERSTATE: name = "D3DERR_CONFLICTINGRENDERSTATE"; break;
	case D3DERR_UNSUPPORTEDTEXTUREFILTER: name = "D3DERR_UNSUPPORTEDTEXTUREFILTER"; break;
	case D3DERR_CONFLICTINGTEXTUREPALETTE: name = "D3DERR_CONFLICTINGTEXTUREPALETTE"; break;
	case D3DERR_DRIVERINTERNALERROR: name = "D3DERR_DRIVERINTERNALERROR"; break;
	case D3DERR_NOTFOUND: name = "D3DERR_NOTFOUND"; break;
	case D3DERR_MOREDATA: name = "D3DERR_MOREDATA"; break;
	case D3DERR_DEVICELOST: name = "D3DERR_DEVICELOST"; break;
	case D3DERR_DEVICENOTRESET: name = "D3DERR_DEVICENOTRESET"; break;
	case D3DERR_NOTAVAILABLE: name = "D3DERR_NOTAVAILABLE"; break;
	case D3DERR_OUTOFVIDEOMEMORY: name = "D3DERR_OUTOFVIDEOMEMORY"; break;
	case D3DERR_INVALIDDEVICE: name = "D3DERR_INVALIDDEVICE"; break;
	case D3DERR_INVALIDCALL: name = "D3DERR_INVALIDCALL"; break;
	case D3DERR_DRIVERINVALIDCALL: name = "D3DERR_DRIVERINVALIDCALL"; break;
	case E_OUTOFMEMORY: name = "E_OUTOFMEMORY"; break;
	case E_FAIL: name = "E_FAIL"; break;
	case E_NOTIMPL: name = "E_NOTIMPL"; break;
	case E_INVALIDARG: name = "E_INVALIDARG"; break;
	default: break;
	}

	if (name)
		_snprintf(buffer, bufferLength, "%s", name);
	else
		_snprintf(buffer, bufferLength, "HRESULT 0x%08lX", (unsigned long)hr);
	buffer[bufferLength - 1] = '\0';
	return D3D_OK;
}

//-------------------------------------------------------------------------------------------------
HRESULT D3DXCreateTexture( IDirect3DDevice8 *device, UINT width, UINT height, UINT mipLevels,
	DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DTexture8 **texture )
{
	if (device == nullptr || texture == nullptr)
		return D3DERR_INVALIDCALL;
	DeviceInfo info;
	HRESULT hr = getDeviceInfo(device, info);
	if (FAILED(hr))
		return hr;

	format = findFormat(info, usage, D3DRTYPE_TEXTURE, format);
	const bool powerOfTwo = (info.caps.TextureCaps & D3DPTEXTURECAPS_POW2) != 0;
	width = fitSize(width, powerOfTwo, info.caps.MaxTextureWidth);
	height = fitSize(height, powerOfTwo, info.caps.MaxTextureHeight);
	if (info.caps.TextureCaps & D3DPTEXTURECAPS_SQUAREONLY)
	{
		if (width > height) height = width;
		else width = height;
	}
	if (isCompressed(format))
	{
		width = (width + 3) & ~3;
		height = (height + 3) & ~3;
	}
	mipLevels = fitMipLevels(mipLevels, width, height, 1, (info.caps.TextureCaps & D3DPTEXTURECAPS_MIPMAP) != 0);
	info.d3d->Release();

	return device->CreateTexture(width, height, mipLevels, usage, format, pool, texture);
}

//-------------------------------------------------------------------------------------------------
HRESULT D3DXCreateCubeTexture( IDirect3DDevice8 *device, UINT size, UINT mipLevels,
	DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DCubeTexture8 **texture )
{
	if (device == nullptr || texture == nullptr)
		return D3DERR_INVALIDCALL;
	DeviceInfo info;
	HRESULT hr = getDeviceInfo(device, info);
	if (FAILED(hr))
		return hr;

	if ((info.caps.TextureCaps & D3DPTEXTURECAPS_CUBEMAP) == 0)
	{
		info.d3d->Release();
		return D3DERR_NOTAVAILABLE;
	}
	format = findFormat(info, usage, D3DRTYPE_CUBETEXTURE, format);
	size = fitSize(size, (info.caps.TextureCaps & D3DPTEXTURECAPS_CUBEMAP_POW2) != 0, info.caps.MaxTextureWidth);
	if (isCompressed(format))
		size = (size + 3) & ~3;
	mipLevels = fitMipLevels(mipLevels, size, size, 1, (info.caps.TextureCaps & D3DPTEXTURECAPS_MIPCUBEMAP) != 0);
	info.d3d->Release();

	return device->CreateCubeTexture(size, mipLevels, usage, format, pool, texture);
}

//-------------------------------------------------------------------------------------------------
HRESULT D3DXCreateVolumeTexture( IDirect3DDevice8 *device, UINT width, UINT height, UINT depth, UINT mipLevels,
	DWORD usage, D3DFORMAT format, D3DPOOL pool, IDirect3DVolumeTexture8 **texture )
{
	if (device == nullptr || texture == nullptr)
		return D3DERR_INVALIDCALL;
	DeviceInfo info;
	HRESULT hr = getDeviceInfo(device, info);
	if (FAILED(hr))
		return hr;

	if ((info.caps.TextureCaps & D3DPTEXTURECAPS_VOLUMEMAP) == 0)
	{
		info.d3d->Release();
		return D3DERR_NOTAVAILABLE;
	}
	format = findFormat(info, usage, D3DRTYPE_VOLUMETEXTURE, format);
	const bool powerOfTwo = (info.caps.TextureCaps & D3DPTEXTURECAPS_VOLUMEMAP_POW2) != 0;
	width = fitSize(width, powerOfTwo, info.caps.MaxVolumeExtent);
	height = fitSize(height, powerOfTwo, info.caps.MaxVolumeExtent);
	depth = fitSize(depth, powerOfTwo, info.caps.MaxVolumeExtent);
	mipLevels = fitMipLevels(mipLevels, width, height, depth, (info.caps.TextureCaps & D3DPTEXTURECAPS_MIPVOLUMEMAP) != 0);
	info.d3d->Release();

	return device->CreateVolumeTexture(width, height, depth, mipLevels, usage, format, pool, texture);
}

//-------------------------------------------------------------------------------------------------
HRESULT D3DXLoadSurfaceFromSurface( IDirect3DSurface8 *destSurface, const PALETTEENTRY *destPalette, const RECT *destRect,
	IDirect3DSurface8 *srcSurface, const PALETTEENTRY *srcPalette, const RECT *srcRect, DWORD filter, D3DCOLOR colorKey )
{
	if (destSurface == nullptr || srcSurface == nullptr || destPalette != nullptr || srcPalette != nullptr)
		return D3DERR_INVALIDCALL;

	D3DSURFACE_DESC destDesc;
	D3DSURFACE_DESC srcDesc;
	HRESULT hr = destSurface->GetDesc(&destDesc);
	if (FAILED(hr))
		return hr;
	hr = srcSurface->GetDesc(&srcDesc);
	if (FAILED(hr))
		return hr;

	const RECT dest = destRect ? *destRect : fullRect(destDesc);
	const RECT src = srcRect ? *srcRect : fullRect(srcDesc);
	if (dest.right <= dest.left || dest.bottom <= dest.top || src.right <= src.left || src.bottom <= src.top
		|| dest.right > (LONG)destDesc.Width || dest.bottom > (LONG)destDesc.Height
		|| src.right > (LONG)srcDesc.Width || src.bottom > (LONG)srcDesc.Height || dest.left < 0 || dest.top < 0
		|| src.left < 0 || src.top < 0)
	{
		return D3DERR_INVALIDCALL;
	}

	Image source;
	hr = readSurface(srcSurface, src, source);
	if (FAILED(hr))
		return hr;

	if (colorKey != 0)
	{
		for (size_t i = 0; i < source.pixels.size(); ++i)
		{
			if (source.pixels[i] == colorKey)
				source.pixels[i] = 0;
		}
	}

	Image scaled;
	if (filter == D3DX_DEFAULT)
		filter = D3DX_FILTER_TRIANGLE;
	scaleImage(source, dest.right - dest.left, dest.bottom - dest.top, filter, scaled);
	return writeSurface(destSurface, dest, scaled);
}

//-------------------------------------------------------------------------------------------------
HRESULT D3DXFilterTexture( IDirect3DBaseTexture8 *texture, const PALETTEENTRY *palette, UINT srcLevel, DWORD filter )
{
	if (texture == nullptr || palette != nullptr || texture->GetType() != D3DRTYPE_TEXTURE)
		return D3DERR_INVALIDCALL;
	if (srcLevel == D3DX_DEFAULT)
		srcLevel = 0;
	if (filter == D3DX_DEFAULT)
		filter = D3DX_FILTER_BOX;

	IDirect3DTexture8 *texture2D = (IDirect3DTexture8 *)texture;
	const UINT levels = texture2D->GetLevelCount();
	for (UINT level = srcLevel + 1; level < levels; ++level)
	{
		IDirect3DSurface8 *src = nullptr;
		IDirect3DSurface8 *dst = nullptr;
		HRESULT hr = texture2D->GetSurfaceLevel(level - 1, &src);
		if (SUCCEEDED(hr))
			hr = texture2D->GetSurfaceLevel(level, &dst);
		if (SUCCEEDED(hr))
			hr = D3DXLoadSurfaceFromSurface(dst, nullptr, nullptr, src, nullptr, nullptr, filter, 0);
		if (src) src->Release();
		if (dst) dst->Release();
		if (FAILED(hr))
			return hr;
	}
	return D3D_OK;
}
