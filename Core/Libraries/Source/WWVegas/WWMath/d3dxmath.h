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

// TheSuperHackers @build The subset of the D3DX8 math library the game uses, written out so the game
// does not link d3dx8.lib, which only exists for 32-bit x86. The names, layouts and conventions match
// D3DX8: row vectors, matrices multiplied left to right, and output pointers that may alias the inputs.

#include <d3d8types.h>
#include <math.h>

#define D3DX_PI ((float)3.141592654f)

struct D3DXVECTOR3 : public D3DVECTOR
{
	D3DXVECTOR3() {}
	D3DXVECTOR3( float fx, float fy, float fz ) { x = fx; y = fy; z = fz; }
	D3DXVECTOR3( const D3DVECTOR &v ) { x = v.x; y = v.y; z = v.z; }

	operator float* () { return &x; }
	operator const float* () const { return &x; }
};

struct D3DXVECTOR4
{
	D3DXVECTOR4() {}
	D3DXVECTOR4( float fx, float fy, float fz, float fw ) { x = fx; y = fy; z = fz; w = fw; }
	D3DXVECTOR4( const float *f ) { x = f[0]; y = f[1]; z = f[2]; w = f[3]; }

	operator float* () { return &x; }
	operator const float* () const { return &x; }

	float x, y, z, w;
};

struct D3DXMATRIX : public D3DMATRIX
{
	D3DXMATRIX() {}
	D3DXMATRIX( const D3DMATRIX &mat ) { *(D3DMATRIX *)this = mat; }
	D3DXMATRIX( const float *f )
	{
		for (int i = 0; i < 16; ++i)
			(&_11)[i] = f[i];
	}
	D3DXMATRIX(
		float f11, float f12, float f13, float f14,
		float f21, float f22, float f23, float f24,
		float f31, float f32, float f33, float f34,
		float f41, float f42, float f43, float f44 )
	{
		_11 = f11; _12 = f12; _13 = f13; _14 = f14;
		_21 = f21; _22 = f22; _23 = f23; _24 = f24;
		_31 = f31; _32 = f32; _33 = f33; _34 = f34;
		_41 = f41; _42 = f42; _43 = f43; _44 = f44;
	}

	float &operator () ( unsigned int row, unsigned int col ) { return m[row][col]; }
	float operator () ( unsigned int row, unsigned int col ) const { return m[row][col]; }

	operator float* () { return &_11; }
	operator const float* () const { return &_11; }

	D3DXMATRIX &operator *= ( const D3DXMATRIX &mat );
	D3DXMATRIX operator * ( const D3DXMATRIX &mat ) const;
};

inline D3DXMATRIX *D3DXMatrixIdentity( D3DXMATRIX *out )
{
	for (int row = 0; row < 4; ++row)
		for (int col = 0; col < 4; ++col)
			out->m[row][col] = (row == col) ? 1.0f : 0.0f;
	return out;
}

inline D3DXMATRIX *D3DXMatrixMultiply( D3DXMATRIX *out, const D3DXMATRIX *m1, const D3DXMATRIX *m2 )
{
	D3DXMATRIX result;
	for (int row = 0; row < 4; ++row)
	{
		for (int col = 0; col < 4; ++col)
		{
			result.m[row][col] =
				m1->m[row][0] * m2->m[0][col] +
				m1->m[row][1] * m2->m[1][col] +
				m1->m[row][2] * m2->m[2][col] +
				m1->m[row][3] * m2->m[3][col];
		}
	}
	*out = result;
	return out;
}

inline D3DXMATRIX *D3DXMatrixTranspose( D3DXMATRIX *out, const D3DXMATRIX *mat )
{
	D3DXMATRIX result;
	for (int row = 0; row < 4; ++row)
		for (int col = 0; col < 4; ++col)
			result.m[row][col] = mat->m[col][row];
	*out = result;
	return out;
}

// Returns null when the matrix cannot be inverted.
inline D3DXMATRIX *D3DXMatrixInverse( D3DXMATRIX *out, float *determinant, const D3DXMATRIX *mat )
{
	const float *a = &mat->_11;
	float inv[16];

	inv[0] = a[5]*a[10]*a[15] - a[5]*a[11]*a[14] - a[9]*a[6]*a[15] + a[9]*a[7]*a[14] + a[13]*a[6]*a[11] - a[13]*a[7]*a[10];
	inv[4] = -a[4]*a[10]*a[15] + a[4]*a[11]*a[14] + a[8]*a[6]*a[15] - a[8]*a[7]*a[14] - a[12]*a[6]*a[11] + a[12]*a[7]*a[10];
	inv[8] = a[4]*a[9]*a[15] - a[4]*a[11]*a[13] - a[8]*a[5]*a[15] + a[8]*a[7]*a[13] + a[12]*a[5]*a[11] - a[12]*a[7]*a[9];
	inv[12] = -a[4]*a[9]*a[14] + a[4]*a[10]*a[13] + a[8]*a[5]*a[14] - a[8]*a[6]*a[13] - a[12]*a[5]*a[10] + a[12]*a[6]*a[9];
	inv[1] = -a[1]*a[10]*a[15] + a[1]*a[11]*a[14] + a[9]*a[2]*a[15] - a[9]*a[3]*a[14] - a[13]*a[2]*a[11] + a[13]*a[3]*a[10];
	inv[5] = a[0]*a[10]*a[15] - a[0]*a[11]*a[14] - a[8]*a[2]*a[15] + a[8]*a[3]*a[14] + a[12]*a[2]*a[11] - a[12]*a[3]*a[10];
	inv[9] = -a[0]*a[9]*a[15] + a[0]*a[11]*a[13] + a[8]*a[1]*a[15] - a[8]*a[3]*a[13] - a[12]*a[1]*a[11] + a[12]*a[3]*a[9];
	inv[13] = a[0]*a[9]*a[14] - a[0]*a[10]*a[13] - a[8]*a[1]*a[14] + a[8]*a[2]*a[13] + a[12]*a[1]*a[10] - a[12]*a[2]*a[9];
	inv[2] = a[1]*a[6]*a[15] - a[1]*a[7]*a[14] - a[5]*a[2]*a[15] + a[5]*a[3]*a[14] + a[13]*a[2]*a[7] - a[13]*a[3]*a[6];
	inv[6] = -a[0]*a[6]*a[15] + a[0]*a[7]*a[14] + a[4]*a[2]*a[15] - a[4]*a[3]*a[14] - a[12]*a[2]*a[7] + a[12]*a[3]*a[6];
	inv[10] = a[0]*a[5]*a[15] - a[0]*a[7]*a[13] - a[4]*a[1]*a[15] + a[4]*a[3]*a[13] + a[12]*a[1]*a[7] - a[12]*a[3]*a[5];
	inv[14] = -a[0]*a[5]*a[14] + a[0]*a[6]*a[13] + a[4]*a[1]*a[14] - a[4]*a[2]*a[13] - a[12]*a[1]*a[6] + a[12]*a[2]*a[5];
	inv[3] = -a[1]*a[6]*a[11] + a[1]*a[7]*a[10] + a[5]*a[2]*a[11] - a[5]*a[3]*a[10] - a[9]*a[2]*a[7] + a[9]*a[3]*a[6];
	inv[7] = a[0]*a[6]*a[11] - a[0]*a[7]*a[10] - a[4]*a[2]*a[11] + a[4]*a[3]*a[10] + a[8]*a[2]*a[7] - a[8]*a[3]*a[6];
	inv[11] = -a[0]*a[5]*a[11] + a[0]*a[7]*a[9] + a[4]*a[1]*a[11] - a[4]*a[3]*a[9] - a[8]*a[1]*a[7] + a[8]*a[3]*a[5];
	inv[15] = a[0]*a[5]*a[10] - a[0]*a[6]*a[9] - a[4]*a[1]*a[10] + a[4]*a[2]*a[9] + a[8]*a[1]*a[6] - a[8]*a[2]*a[5];

	const float det = a[0]*inv[0] + a[1]*inv[4] + a[2]*inv[8] + a[3]*inv[12];
	if (determinant)
		*determinant = det;
	if (det == 0.0f)
		return 0;

	const float invDet = 1.0f / det;
	for (int i = 0; i < 16; ++i)
		(&out->_11)[i] = inv[i] * invDet;
	return out;
}

inline D3DXMATRIX *D3DXMatrixScaling( D3DXMATRIX *out, float sx, float sy, float sz )
{
	D3DXMatrixIdentity(out);
	out->_11 = sx;
	out->_22 = sy;
	out->_33 = sz;
	return out;
}

inline D3DXMATRIX *D3DXMatrixTranslation( D3DXMATRIX *out, float x, float y, float z )
{
	D3DXMatrixIdentity(out);
	out->_41 = x;
	out->_42 = y;
	out->_43 = z;
	return out;
}

inline D3DXMATRIX *D3DXMatrixRotationZ( D3DXMATRIX *out, float angle )
{
	const float s = (float)sin(angle);
	const float c = (float)cos(angle);
	D3DXMatrixIdentity(out);
	out->_11 = c;
	out->_12 = s;
	out->_21 = -s;
	out->_22 = c;
	return out;
}

inline float D3DXVec4Dot( const D3DXVECTOR4 *v1, const D3DXVECTOR4 *v2 )
{
	return v1->x * v2->x + v1->y * v2->y + v1->z * v2->z + v1->w * v2->w;
}

inline D3DXVECTOR4 *D3DXVec4Transform( D3DXVECTOR4 *out, const D3DXVECTOR4 *v, const D3DXMATRIX *mat )
{
	const D3DXVECTOR4 in = *v;
	out->x = in.x * mat->_11 + in.y * mat->_21 + in.z * mat->_31 + in.w * mat->_41;
	out->y = in.x * mat->_12 + in.y * mat->_22 + in.z * mat->_32 + in.w * mat->_42;
	out->z = in.x * mat->_13 + in.y * mat->_23 + in.z * mat->_33 + in.w * mat->_43;
	out->w = in.x * mat->_14 + in.y * mat->_24 + in.z * mat->_34 + in.w * mat->_44;
	return out;
}

// Transforms (x, y, z, 1).
inline D3DXVECTOR4 *D3DXVec3Transform( D3DXVECTOR4 *out, const D3DXVECTOR3 *v, const D3DXMATRIX *mat )
{
	const D3DXVECTOR4 in(v->x, v->y, v->z, 1.0f);
	return D3DXVec4Transform(out, &in, mat);
}

inline D3DXMATRIX &D3DXMATRIX::operator *= ( const D3DXMATRIX &mat )
{
	D3DXMatrixMultiply(this, this, &mat);
	return *this;
}

inline D3DXMATRIX D3DXMATRIX::operator * ( const D3DXMATRIX &mat ) const
{
	D3DXMATRIX result;
	D3DXMatrixMultiply(&result, this, &mat);
	return result;
}
