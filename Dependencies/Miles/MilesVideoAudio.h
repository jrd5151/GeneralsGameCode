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

// Plays the sound track of a video that the game decodes itself, such as with the FFmpeg video
// player. It is an addition to the Miles functions, only present when the game is built with
// RTS_BUILD_OPTION_MINIAUDIO, which defines RTS_HAS_MINIAUDIO.
//
// The video player writes decoded samples as it goes and they play as soon as the mixer reaches
// them. Silence plays while no samples are queued. Samples that do not fit in the queue, which
// holds a few seconds, are dropped.
//
// Open after AIL_quick_startup. Every function may be called from any thread, but one stream
// must only be written from one thread at a time.

class MilesVideoAudio;

// Returns null when no sound device is running or the format is not supported.
MilesVideoAudio *MilesVideoAudio_open(int channels, int sampleRate);

// Writes interleaved 32 bit float frames.
void MilesVideoAudio_write(MilesVideoAudio *stream, const float *frames, unsigned int frameCount);

// Sets the linear volume, 1 being the samples unchanged.
void MilesVideoAudio_setVolume(MilesVideoAudio *stream, float volume);

void MilesVideoAudio_pause(MilesVideoAudio *stream, bool paused);

void MilesVideoAudio_close(MilesVideoAudio *stream);
