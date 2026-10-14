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

// Implements the Miles functions declared in mss/mss.h on top of miniaudio, so the game plays
// sound without mss32.dll. It replaces MilesLoader.cpp when RTS_BUILD_OPTION_MINIAUDIO is on.
//
// Only what the game's MilesAudioManager uses is implemented. The other functions keep the
// neutral results a missing mss32.dll export used to give.
//
// Like Miles, a service thread watches the playing sounds and calls their end of sample callbacks
// with the Miles lock held, and every function takes the same lock. Callbacks may call back into
// these functions, so the lock is recursive.
//
// Positional sounds are mixed the way the "Miles Fast 2D Positional Audio" provider did it: a pan
// and a volume from the listener position, nothing more.

#include "MilesLoader.h"
#include "MilesVideoAudio.h"
#include "mss/mss.h"

#include <miniaudio.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace
{

const char *const ProviderName = "Miles Fast 2D Positional Audio";
const HPROVIDER ProviderId = (HPROVIDER)1;
const U32 ServicePeriodMs = 5;
const S32 MaxUserData = 8;

// How far a positional sound is moved towards the side it is on. Below 1 so a sound beside the
// listener is still faintly heard in the other ear.
const float PositionalPanWidth = 0.75f;

// Cutoff frequencies for the low pass filter of occluded positional sounds.
const double OpenCutoffHz = 20000.0;
const double MinimumCutoffHz = 400.0;

enum VoiceKind
{
	VoiceKind_Sample,
	VoiceKind_3DSample,
	VoiceKind_Stream,
};

struct LoopSource
{
	ma_data_source_base base;
	ma_data_source *inner;
	volatile LONG loopsRemaining; // 0 loops forever, 1 plays the last time, as in Miles
};

struct Voice
{
	h3DPOBJECT object; // H3DSAMPLE handles point here
	VoiceKind kind;
	Voice *prev;
	Voice *next;

	ma_decoder decoder;
	LoopSource loop;
	ma_sound sound;
	ma_lpf_node lpf;
	bool decoderReady;
	bool soundReady;
	bool lpfReady;
	bool started; // Playing or paused; cleared when the end callback fires
	bool endPending; // Could not start; the end callback still fires so the game frees the voice

	ma_uint64 lengthFrames;
	bool lengthKnown;

	F32 volume;
	F32 pan;
	S32 rate;
	U32 nativeRate;
	S32 loopCount;

	F32 position[3];
	F32 minDistance;
	F32 maxDistance;
	F32 occlusion;

	S32 userData[MaxUserData];

	AIL_sample_callback sampleCallback;
	AIL_3dsample_callback sample3DCallback;
	AIL_stream_callback streamCallback;
};

struct Listener
{
	h3DPOBJECT object;
	F32 position[3];
	F32 face[3];
	F32 up[3];
};

CRITICAL_SECTION Lock;
bool LockReady = false;

ma_engine Engine;
bool EngineReady = false;

HANDLE ServiceThread = nullptr;
volatile LONG ServiceQuit = 0;

Voice *Voices = nullptr;
Listener TheListener;
DIG_DRIVER DigitalDriver;

AIL_file_open_callback FileOpen = nullptr;
AIL_file_close_callback FileClose = nullptr;
AIL_file_seek_callback FileSeek = nullptr;
AIL_file_read_callback FileRead = nullptr;

HMODULE DirectSoundModule = nullptr;
void *DirectSound = nullptr;

char LastError[64] = "";

void releaseVideoSounds();

//-------------------------------------------------------------------------------------------------
class ScopedLock
{
public:
	ScopedLock() { if (LockReady) EnterCriticalSection(&Lock); }
	~ScopedLock() { if (LockReady) LeaveCriticalSection(&Lock); }
};

void initLock()
{
	if (!LockReady)
	{
		InitializeCriticalSection(&Lock);
		LockReady = true;
	}
}

//-------------------------------------------------------------------------------------------------
// LoopSource plays its inner data source the requested number of times without a gap.

ma_result loopRead(ma_data_source *dataSource, void *framesOut, ma_uint64 frameCount, ma_uint64 *framesRead)
{
	LoopSource *source = (LoopSource *)dataSource;
	ma_format format;
	ma_uint32 channels;
	ma_uint32 sampleRate;
	ma_data_source_get_data_format(source->inner, &format, &channels, &sampleRate, nullptr, 0);

	ma_uint64 total = 0;
	bool readNothingSinceLoop = false;
	while (total < frameCount)
	{
		void *out = framesOut ? ma_offset_pcm_frames_ptr(framesOut, total, format, channels) : nullptr;
		ma_uint64 read = 0;
		const ma_result result = ma_data_source_read_pcm_frames(source->inner, out, frameCount - total, &read);
		total += read;

		if (read > 0)
			readNothingSinceLoop = false;

		if (result == MA_SUCCESS && read > 0)
			continue;

		// The end of the inner source. Loop if more plays are left, unless the source is empty.
		if (readNothingSinceLoop)
			break;

		const LONG loops = source->loopsRemaining;
		if (loops == 1)
			break;
		if (loops > 1)
			InterlockedDecrement(const_cast<LONG*>(&source->loopsRemaining));

		ma_data_source_seek_to_pcm_frame(source->inner, 0);
		readNothingSinceLoop = true;
	}

	*framesRead = total;
	return (total == 0 && frameCount > 0) ? MA_AT_END : MA_SUCCESS;
}

ma_result loopSeek(ma_data_source *dataSource, ma_uint64 frameIndex)
{
	return ma_data_source_seek_to_pcm_frame(((LoopSource *)dataSource)->inner, frameIndex);
}

ma_result loopGetDataFormat(ma_data_source *dataSource, ma_format *format, ma_uint32 *channels, ma_uint32 *sampleRate, ma_channel *channelMap, size_t channelMapCap)
{
	return ma_data_source_get_data_format(((LoopSource *)dataSource)->inner, format, channels, sampleRate, channelMap, channelMapCap);
}

ma_result loopGetCursor(ma_data_source *dataSource, ma_uint64 *cursor)
{
	return ma_data_source_get_cursor_in_pcm_frames(((LoopSource *)dataSource)->inner, cursor);
}

ma_result loopGetLength(ma_data_source *dataSource, ma_uint64 *length)
{
	return ma_data_source_get_length_in_pcm_frames(((LoopSource *)dataSource)->inner, length);
}

ma_data_source_vtable LoopSourceVtable =
{
	loopRead,
	loopSeek,
	loopGetDataFormat,
	loopGetCursor,
	loopGetLength,
	nullptr,
	0
};

//-------------------------------------------------------------------------------------------------
// Stream files are read through the callbacks the game registered with AIL_set_file_callbacks.

ma_result vfsOpen(ma_vfs *, const char *filePath, ma_uint32 openMode, ma_vfs_file *file)
{
	if ((openMode & MA_OPEN_MODE_WRITE) != 0)
		return MA_INVALID_OPERATION;

	void *handle = nullptr;
	if (FileOpen(filePath, &handle) == 0 || handle == nullptr)
		return MA_DOES_NOT_EXIST;

	*file = handle;
	return MA_SUCCESS;
}

ma_result vfsOpenW(ma_vfs *, const wchar_t *, ma_uint32, ma_vfs_file *)
{
	return MA_NOT_IMPLEMENTED;
}

ma_result vfsClose(ma_vfs *, ma_vfs_file file)
{
	FileClose(file);
	return MA_SUCCESS;
}

ma_result vfsRead(ma_vfs *, ma_vfs_file file, void *dst, size_t sizeInBytes, size_t *bytesRead)
{
	const U32 read = FileRead(file, dst, (U32)sizeInBytes);
	if (bytesRead != nullptr)
		*bytesRead = read;
	return (read == 0 && sizeInBytes > 0) ? MA_AT_END : MA_SUCCESS;
}

ma_result vfsWrite(ma_vfs *, ma_vfs_file, const void *, size_t, size_t *)
{
	return MA_NOT_IMPLEMENTED;
}

ma_result vfsSeek(ma_vfs *, ma_vfs_file file, ma_int64 offset, ma_seek_origin origin)
{
	U32 type = AIL_FILE_SEEK_BEGIN;
	if (origin == ma_seek_origin_current)
		type = AIL_FILE_SEEK_CURRENT;
	else if (origin == ma_seek_origin_end)
		type = AIL_FILE_SEEK_END;

	return FileSeek(file, (S32)offset, type) < 0 ? MA_ERROR : MA_SUCCESS;
}

ma_result vfsTell(ma_vfs *, ma_vfs_file file, ma_int64 *cursor)
{
	*cursor = FileSeek(file, 0, AIL_FILE_SEEK_CURRENT);
	return MA_SUCCESS;
}

ma_result vfsInfo(ma_vfs *, ma_vfs_file file, ma_file_info *info)
{
	const S32 cursor = FileSeek(file, 0, AIL_FILE_SEEK_CURRENT);
	const S32 size = FileSeek(file, 0, AIL_FILE_SEEK_END);
	FileSeek(file, cursor, AIL_FILE_SEEK_BEGIN);
	info->sizeInBytes = size < 0 ? 0 : (ma_uint64)size;
	return MA_SUCCESS;
}

ma_vfs_callbacks GameFileSystem =
{
	vfsOpen,
	vfsOpenW,
	vfsClose,
	vfsRead,
	vfsWrite,
	vfsSeek,
	vfsTell,
	vfsInfo,
};

bool hasFileCallbacks()
{
	return FileOpen != nullptr && FileClose != nullptr && FileSeek != nullptr && FileRead != nullptr;
}

//-------------------------------------------------------------------------------------------------
// WAV parsing for AIL_WAV_info and sample file images.

U32 readU32(const unsigned char *p) { return (U32)p[0] | ((U32)p[1] << 8) | ((U32)p[2] << 16) | ((U32)p[3] << 24); }
U32 readU16(const unsigned char *p) { return (U32)p[0] | ((U32)p[1] << 8); }

bool parseWav(const void *data, AILSOUNDINFO *info)
{
	memset(info, 0, sizeof(*info));
	if (data == nullptr)
		return false;

	const unsigned char *bytes = (const unsigned char *)data;
	if (memcmp(bytes, "RIFF", 4) != 0 || memcmp(bytes + 8, "WAVE", 4) != 0)
		return false;

	const U32 riffEnd = 8 + readU32(bytes + 4);
	U32 offset = 12;
	U32 factSamples = 0;
	bool haveFormat = false;

	while (offset + 8 <= riffEnd)
	{
		const unsigned char *chunk = bytes + offset;
		const U32 size = readU32(chunk + 4);

		if (memcmp(chunk, "fmt ", 4) == 0 && size >= 16)
		{
			info->format = (S32)readU16(chunk + 8);
			info->channels = (S32)readU16(chunk + 10);
			info->rate = readU32(chunk + 12);
			info->block_size = readU16(chunk + 20);
			info->bits = (S32)readU16(chunk + 22);
			haveFormat = true;
		}
		else if (memcmp(chunk, "fact", 4) == 0 && size >= 4)
		{
			factSamples = readU32(chunk + 8);
		}
		else if (memcmp(chunk, "data", 4) == 0)
		{
			info->data_ptr = chunk + 8;
			info->data_len = size;
			break;
		}

		offset += 8 + size + (size & 1);
	}

	if (!haveFormat || info->data_ptr == nullptr || info->channels <= 0)
		return false;

	info->initial_ptr = data;
	if (info->format == WAVE_FORMAT_PCM && info->bits > 0)
		info->samples = info->data_len / (U32)(info->channels * (info->bits / 8));
	else
		info->samples = factSamples;
	return true;
}

U32 wavImageSize(const AILSOUNDINFO &info)
{
	return (U32)((const char *)info.data_ptr - (const char *)info.initial_ptr) + info.data_len;
}

//-------------------------------------------------------------------------------------------------
// Voices

Voice *createVoice(VoiceKind kind)
{
	Voice *voice = (Voice *)calloc(1, sizeof(Voice));
	if (voice == nullptr)
		return nullptr;

	voice->kind = kind;
	voice->volume = 1.0f;
	voice->pan = 0.5f;
	voice->loopCount = 1;

	voice->next = Voices;
	if (Voices != nullptr)
		Voices->prev = voice;
	Voices = voice;
	return voice;
}

void teardownSound(Voice *voice)
{
	if (voice->soundReady)
	{
		ma_sound_uninit(&voice->sound);
		voice->soundReady = false;
	}
	voice->started = false;
	voice->endPending = false;
}

void teardownDecoder(Voice *voice)
{
	teardownSound(voice);
	if (voice->decoderReady)
	{
		ma_decoder_uninit(&voice->decoder);
		voice->decoderReady = false;
	}
	voice->nativeRate = 0;
	voice->lengthKnown = false;
}

void destroyVoice(Voice *voice)
{
	teardownDecoder(voice);
	if (voice->lpfReady)
	{
		ma_lpf_node_uninit(&voice->lpf, nullptr);
		voice->lpfReady = false;
	}

	if (voice->prev != nullptr)
		voice->prev->next = voice->next;
	else
		Voices = voice->next;
	if (voice->next != nullptr)
		voice->next->prev = voice->prev;

	free(voice);
}

bool finishDecoderSetup(Voice *voice)
{
	ma_data_source_config config = ma_data_source_config_init();
	config.vtable = &LoopSourceVtable;
	if (ma_data_source_init(&config, &voice->loop.base) != MA_SUCCESS)
	{
		ma_decoder_uninit(&voice->decoder);
		return false;
	}

	voice->loop.inner = &voice->decoder;
	voice->decoderReady = true;
	voice->nativeRate = voice->decoder.outputSampleRate;
	voice->rate = (S32)voice->nativeRate;
	return true;
}

bool setVoiceImage(Voice *voice, const void *image, U32 size)
{
	teardownDecoder(voice);

	ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
	config.encodingFormat = ma_encoding_format_wav;
	if (ma_decoder_init_memory(image, size, &config, &voice->decoder) != MA_SUCCESS)
		return false;
	return finishDecoderSetup(voice);
}

bool openVoiceStream(Voice *voice, const char *filename)
{
	if (filename == nullptr)
		return false;

	ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
	ma_vfs *fileSystem = hasFileCallbacks() ? &GameFileSystem : nullptr;
	if (ma_decoder_init_vfs(fileSystem, filename, &config, &voice->decoder) != MA_SUCCESS)
		return false;
	return finishDecoderSetup(voice);
}

// Pan and volume of a positional sound, in the way of the Miles Fast 2D provider.
void getPositionalMix(const Voice *voice, float *gain, float *pan)
{
	const float dx = voice->position[0] - TheListener.position[0];
	const float dy = voice->position[1] - TheListener.position[1];
	const float dz = voice->position[2] - TheListener.position[2];
	const float distance = (float)sqrt(dx * dx + dy * dy + dz * dz);

	// Full volume within the minimum distance, then falling off with the distance, and silent from
	// the maximum distance on when it is the larger one. MilesAudioManager passes the sound's
	// maximum range as the Miles minimum distance, so sounds keep their volume over their whole
	// range and the game fades and stops them itself, as it did with Miles.
	*gain = 1.0f;
	if (voice->minDistance > 0.0f && distance > voice->minDistance)
		*gain = voice->minDistance / distance;
	if (voice->maxDistance > voice->minDistance && distance >= voice->maxDistance)
		*gain = 0.0f;

	// The right side is up x face, as the game passes a downward up vector for its z up world.
	const float *up = TheListener.up;
	const float *face = TheListener.face;
	float right[3] =
	{
		up[1] * face[2] - up[2] * face[1],
		up[2] * face[0] - up[0] * face[2],
		up[0] * face[1] - up[1] * face[0],
	};
	const float rightLength = (float)sqrt(right[0] * right[0] + right[1] * right[1] + right[2] * right[2]);

	*pan = 0.0f;
	if (distance > 0.001f && rightLength > 0.001f)
		*pan = PositionalPanWidth * (dx * right[0] + dy * right[1] + dz * right[2]) / (distance * rightLength);
}

void applyVoiceMix(Voice *voice)
{
	if (!voice->soundReady)
		return;

	float gain = voice->volume;
	float pan = voice->pan * 2.0f - 1.0f;
	if (voice->kind == VoiceKind_3DSample)
	{
		float positionalGain;
		getPositionalMix(voice, &positionalGain, &pan);
		gain *= positionalGain;
	}

	ma_sound_set_volume(&voice->sound, gain);
	ma_sound_set_pan(&voice->sound, pan);
	if (voice->nativeRate > 0 && voice->rate > 0)
		ma_sound_set_pitch(&voice->sound, (float)voice->rate / (float)voice->nativeRate);
}

void routeThroughLowPass(Voice *voice)
{
	ma_node *endpoint = ma_engine_get_endpoint(&Engine);
	if (voice->occlusion <= 0.0f)
	{
		ma_node_attach_output_bus(&voice->sound, 0, endpoint, 0);
		return;
	}

	double cutoff = OpenCutoffHz * (1.0 - voice->occlusion);
	if (cutoff < MinimumCutoffHz)
		cutoff = MinimumCutoffHz;

	const ma_uint32 channels = ma_engine_get_channels(&Engine);
	const ma_uint32 sampleRate = ma_engine_get_sample_rate(&Engine);
	if (cutoff > sampleRate * 0.45)
		cutoff = sampleRate * 0.45;

	if (!voice->lpfReady)
	{
		ma_lpf_node_config config = ma_lpf_node_config_init(channels, sampleRate, cutoff, 2);
		if (ma_lpf_node_init(ma_engine_get_node_graph(&Engine), &config, nullptr, &voice->lpf) != MA_SUCCESS)
			return;
		ma_node_attach_output_bus(&voice->lpf, 0, endpoint, 0);
		voice->lpfReady = true;
	}
	else
	{
		ma_lpf_config config = ma_lpf_config_init(ma_format_f32, channels, sampleRate, cutoff, 2);
		ma_lpf_node_reinit(&config, &voice->lpf);
	}
	ma_node_attach_output_bus(&voice->sound, 0, &voice->lpf, 0);
}

// Starts the voice from the beginning. The sound is created anew each time, so the decoder can be
// rewound without racing the mixing thread.
void startVoice(Voice *voice)
{
	teardownSound(voice);
	voice->started = true;
	voice->endPending = true;
	if (!EngineReady || !voice->decoderReady)
		return;

	voice->loop.loopsRemaining = voice->loopCount;
	ma_data_source_seek_to_pcm_frame(&voice->decoder, 0);
	if (ma_sound_init_from_data_source(&Engine, &voice->loop, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &voice->sound) != MA_SUCCESS)
		return;

	voice->endPending = false;

	voice->soundReady = true;
	if (voice->kind == VoiceKind_3DSample)
		routeThroughLowPass(voice);
	applyVoiceMix(voice);

	ma_sound_start(&voice->sound);
}

void pauseVoice(Voice *voice)
{
	if (voice->soundReady)
		ma_sound_stop(&voice->sound);
}

void resumeVoice(Voice *voice)
{
	if (voice->soundReady && voice->started)
		ma_sound_start(&voice->sound);
}

void endVoice(Voice *voice)
{
	teardownSound(voice);
}

ma_uint64 getVoiceLength(Voice *voice)
{
	if (!voice->lengthKnown && voice->decoderReady && !voice->soundReady)
	{
		voice->lengthFrames = 0;
		ma_decoder_get_length_in_pcm_frames(&voice->decoder, &voice->lengthFrames);
		voice->lengthKnown = true;
	}
	return voice->lengthFrames;
}

void getVoiceMsPosition(Voice *voice, S32 *totalMs, S32 *currentMs)
{
	S32 total = 0;
	S32 current = 0;
	if (voice != nullptr && voice->nativeRate > 0)
	{
		total = (S32)(getVoiceLength(voice) * 1000 / voice->nativeRate);
		ma_uint64 cursor = 0;
		if (voice->soundReady)
			ma_sound_get_cursor_in_pcm_frames(&voice->sound, &cursor);
		current = (S32)(cursor * 1000 / voice->nativeRate);
	}
	if (totalMs != nullptr)
		*totalMs = total;
	if (currentMs != nullptr)
		*currentMs = current;
}

//-------------------------------------------------------------------------------------------------
// Service thread: calls the end callbacks of sounds that finished, like the Miles timer thread.

bool fireOneFinishedVoice()
{
	for (Voice *voice = Voices; voice != nullptr; voice = voice->next)
	{
		if (!voice->started)
			continue;
		if (!voice->endPending && !(voice->soundReady && ma_sound_at_end(&voice->sound)))
			continue;

		voice->started = false;
		voice->endPending = false;
		switch (voice->kind)
		{
		case VoiceKind_Sample:
			if (voice->sampleCallback != nullptr)
				voice->sampleCallback((HSAMPLE)voice);
			break;
		case VoiceKind_3DSample:
			if (voice->sample3DCallback != nullptr)
				voice->sample3DCallback((H3DSAMPLE)voice);
			break;
		case VoiceKind_Stream:
			if (voice->streamCallback != nullptr)
				voice->streamCallback((HSTREAM)voice);
			break;
		}

		// The callback may have changed the voice list, so start over.
		return true;
	}
	return false;
}

DWORD WINAPI serviceThreadMain(LPVOID)
{
	while (ServiceQuit == 0)
	{
		Sleep(ServicePeriodMs);
		ScopedLock lock;
		while (fireOneFinishedVoice())
		{
		}
	}
	return 0;
}

//-------------------------------------------------------------------------------------------------
// The DirectSound object Bink plays movie sound through. Miles handed out its own.

typedef HRESULT (WINAPI *DirectSoundCreateFunc)(const GUID *device, void **directSound, void *outer);
typedef HRESULT (STDMETHODCALLTYPE *DirectSoundSetCooperativeLevelFunc)(void *self, HWND window, DWORD level);
typedef ULONG (STDMETHODCALLTYPE *DirectSoundReleaseFunc)(void *self);
const DWORD DirectSoundPriorityLevel = 2; // DSSCL_PRIORITY
const int DirectSoundReleaseSlot = 2;
const int DirectSoundSetCooperativeLevelSlot = 6;

void *getDirectSound()
{
	if (DirectSound != nullptr)
		return DirectSound;

	if (DirectSoundModule == nullptr)
		DirectSoundModule = LoadLibraryA("dsound.dll");
	if (DirectSoundModule == nullptr)
		return nullptr;

	DirectSoundCreateFunc create = (DirectSoundCreateFunc)GetProcAddress(DirectSoundModule, "DirectSoundCreate");
	if (create == nullptr || FAILED(create(nullptr, &DirectSound, nullptr)) || DirectSound == nullptr)
	{
		DirectSound = nullptr;
		return nullptr;
	}

	HWND window = GetActiveWindow();
	if (window == nullptr)
		window = GetDesktopWindow();
	void **vtable = *(void ***)DirectSound;
	((DirectSoundSetCooperativeLevelFunc)vtable[DirectSoundSetCooperativeLevelSlot])(DirectSound, window, DirectSoundPriorityLevel);
	return DirectSound;
}

void releaseDirectSound()
{
	if (DirectSound != nullptr)
	{
		void **vtable = *(void ***)DirectSound;
		((DirectSoundReleaseFunc)vtable[DirectSoundReleaseSlot])(DirectSound);
		DirectSound = nullptr;
	}
	if (DirectSoundModule != nullptr)
	{
		FreeLibrary(DirectSoundModule);
		DirectSoundModule = nullptr;
	}
}

//-------------------------------------------------------------------------------------------------
Voice *toVoice(const void *handle)
{
	return (Voice *)handle;
}

bool isListener(H3DPOBJECT object)
{
	return object == &TheListener.object;
}

void applyAllPositionalMixes()
{
	for (Voice *voice = Voices; voice != nullptr; voice = voice->next)
	{
		if (voice->kind == VoiceKind_3DSample)
			applyVoiceMix(voice);
	}
}

} // namespace

//-------------------------------------------------------------------------------------------------
// MilesLoader: there is nothing to load, the functions are built in.

bool MilesLoader::isLoaded()
{
	return true;
}

bool MilesLoader::isFailed()
{
	return false;
}

unsigned long MilesLoader::getLastError()
{
	return 0;
}

bool MilesLoader::load()
{
	initLock();
	return true;
}

void MilesLoader::unload()
{
}

//-------------------------------------------------------------------------------------------------
// System

S32 __stdcall AIL_startup(void)
{
	initLock();
	return 1;
}

S32 __stdcall AIL_quick_startup(S32 use_digital, S32 use_MIDI, U32 output_rate, S32 output_bits, S32 output_channels)
{
	initLock();
	ScopedLock lock;

	if (!use_digital)
		return 0;

	if (!EngineReady)
	{
		// Mix at the device's own rate in stereo, so pans work the same everywhere and the system
		// does not have to resample the mix.
		ma_engine_config config = ma_engine_config_init();
		config.channels = 2;
		if (ma_engine_init(&config, &Engine) != MA_SUCCESS)
		{
			strcpy(LastError, "No audio device could be opened.");
			return 0;
		}
		EngineReady = true;
	}

	TheListener.face[1] = 1.0f;
	TheListener.up[2] = -1.0f;

	if (ServiceThread == nullptr)
	{
		ServiceQuit = 0;
		DWORD threadId;
		ServiceThread = CreateThread(nullptr, 0, serviceThreadMain, nullptr, 0, &threadId);
	}
	return 1;
}

void __stdcall AIL_quick_handles(HDIGDRIVER *pdig, HMDIDRIVER *pmdi, HDLSDEVICE *pdls)
{
	if (pdig != nullptr)
		*pdig = &DigitalDriver;
	if (pmdi != nullptr)
		*pmdi = nullptr;
	if (pdls != nullptr)
		*pdls = nullptr;
}

void __stdcall AIL_shutdown(void)
{
	if (ServiceThread != nullptr)
	{
		InterlockedExchange(const_cast<LONG*>(&ServiceQuit), 1);
		WaitForSingleObject(ServiceThread, INFINITE);
		CloseHandle(ServiceThread);
		ServiceThread = nullptr;
	}

	ScopedLock lock;
	while (Voices != nullptr)
		destroyVoice(Voices);
	releaseVideoSounds();

	if (EngineReady)
	{
		ma_engine_uninit(&Engine);
		EngineReady = false;
	}
	releaseDirectSound();
}

char *__stdcall AIL_set_redist_directory(const char *dir)
{
	return (char *)dir;
}

char *__stdcall AIL_last_error(void)
{
	return LastError;
}

U32 __stdcall AIL_get_timer_highest_delay(void)
{
	return 0;
}

void __stdcall AIL_lock(void)
{
	if (LockReady)
		EnterCriticalSection(&Lock);
}

void __stdcall AIL_unlock(void)
{
	if (LockReady)
		LeaveCriticalSection(&Lock);
}

void __stdcall AIL_lock_mutex(void)
{
	AIL_lock();
}

void __stdcall AIL_unlock_mutex(void)
{
	AIL_unlock();
}

S32 __stdcall AIL_set_preference(U32 number, S32 value)
{
	return 0;
}

void __stdcall AIL_set_file_callbacks(AIL_file_open_callback opencb, AIL_file_close_callback closecb,
	AIL_file_seek_callback seekcb, AIL_file_read_callback readcb)
{
	ScopedLock lock;
	FileOpen = opencb;
	FileClose = closecb;
	FileSeek = seekcb;
	FileRead = readcb;
}

S32 __stdcall AIL_waveOutOpen(HDIGDRIVER *driver, LPHWAVEOUT *waveout, S32 id, LPWAVEFORMAT format)
{
	return -1;
}

void __stdcall AIL_waveOutClose(HDIGDRIVER driver)
{
}

void __stdcall AIL_stop_timer(HTIMER timer)
{
}

void __stdcall AIL_release_timer_handle(HTIMER timer)
{
}

void __stdcall AIL_get_DirectSound_info(HSAMPLE sample, AILLPDIRECTSOUND *lplpDS, AILLPDIRECTSOUNDBUFFER *lplpDSB)
{
	ScopedLock lock;
	if (lplpDS != nullptr)
		*lplpDS = getDirectSound();
	if (lplpDSB != nullptr)
		*lplpDSB = nullptr;
}

//-------------------------------------------------------------------------------------------------
// Wave files

S32 __stdcall AIL_WAV_info(const void *data, AILSOUNDINFO *info)
{
	return parseWav(data, info) ? 1 : 0;
}

S32 __stdcall AIL_decompress_ADPCM(const AILSOUNDINFO *info, void **outdata, U32 *outsize)
{
	*outdata = nullptr;
	*outsize = 0;

	ma_decoder_config config = ma_decoder_config_init(ma_format_s16, 0, 0);
	config.encodingFormat = ma_encoding_format_wav;
	ma_uint64 frameCount = 0;
	void *frames = nullptr;
	if (ma_decode_memory(info->initial_ptr, wavImageSize(*info), &config, &frameCount, &frames) != MA_SUCCESS)
		return 0;

	// The last ADPCM block is padded; drop what lies past the sample count of the file.
	if (info->samples > 0 && frameCount > info->samples)
		frameCount = info->samples;

	// The decoded sound becomes a plain 16 bit wave file image, as Miles returns it.
	const U32 channels = config.channels;
	const U32 rate = config.sampleRate;
	const U32 dataSize = (U32)frameCount * channels * 2;
	const U32 headerSize = 44;
	unsigned char *image = (unsigned char *)malloc(headerSize + dataSize);
	if (image == nullptr)
	{
		ma_free(frames, nullptr);
		return 0;
	}

	const U32 fields[] =
	{
		0x46464952, 36 + dataSize, 0x45564157, // "RIFF" size "WAVE"
		0x20746D66, 16, // "fmt " size
		WAVE_FORMAT_PCM | (channels << 16), rate, rate * channels * 2, (channels * 2) | (16 << 16),
		0x61746164, dataSize, // "data" size
	};
	for (int i = 0; i < 11; ++i)
	{
		image[i * 4 + 0] = (unsigned char)(fields[i]);
		image[i * 4 + 1] = (unsigned char)(fields[i] >> 8);
		image[i * 4 + 2] = (unsigned char)(fields[i] >> 16);
		image[i * 4 + 3] = (unsigned char)(fields[i] >> 24);
	}
	memcpy(image + headerSize, frames, dataSize);
	ma_free(frames, nullptr);

	*outdata = image;
	*outsize = headerSize + dataSize;
	return 1;
}

void __stdcall AIL_mem_free_lock(void *ptr)
{
	free(ptr);
}

//-------------------------------------------------------------------------------------------------
// Filters: none. The game's delay filter is set to mix nothing in anyway.

S32 __stdcall AIL_enumerate_filters(HPROENUM *next, HPROVIDER *dest, char **name)
{
	return 0;
}

HPROVIDER __stdcall AIL_set_sample_processor(HSAMPLE sample, SAMPLESTAGE pipeline_stage, HPROVIDER provider)
{
	return nullptr;
}

void __stdcall AIL_set_filter_sample_preference(HSAMPLE sample, const char *name, const void *val)
{
}

//-------------------------------------------------------------------------------------------------
// 2D samples

HSAMPLE __stdcall AIL_allocate_sample_handle(HDIGDRIVER dig)
{
	ScopedLock lock;
	return (HSAMPLE)createVoice(VoiceKind_Sample);
}

void __stdcall AIL_release_sample_handle(HSAMPLE sample)
{
	ScopedLock lock;
	if (sample != nullptr)
		destroyVoice(toVoice(sample));
}

void __stdcall AIL_init_sample(HSAMPLE sample)
{
	ScopedLock lock;
	Voice *voice = toVoice(sample);
	if (voice == nullptr)
		return;

	teardownDecoder(voice);
	voice->volume = 1.0f;
	voice->pan = 0.5f;
	voice->rate = 0;
	voice->loopCount = 1;
}

S32 __stdcall AIL_set_sample_file(HSAMPLE sample, const void *file_image, S32 block)
{
	ScopedLock lock;
	AILSOUNDINFO info;
	Voice *voice = toVoice(sample);
	if (voice == nullptr || !parseWav(file_image, &info))
		return 0;

	// Like Miles, this sets the playback rate to the file's rate.
	return setVoiceImage(voice, file_image, wavImageSize(info)) ? 1 : 0;
}

S32 __stdcall AIL_set_named_sample_file(HSAMPLE sample, const char *file_name, const void *file_image, S32 file_size, S32 block)
{
	ScopedLock lock;
	Voice *voice = toVoice(sample);
	if (voice == nullptr || file_image == nullptr || file_size <= 0)
		return 0;
	return setVoiceImage(voice, file_image, (U32)file_size) ? 1 : 0;
}

void __stdcall AIL_start_sample(HSAMPLE sample)
{
	ScopedLock lock;
	if (sample != nullptr)
		startVoice(toVoice(sample));
}

void __stdcall AIL_stop_sample(HSAMPLE sample)
{
	ScopedLock lock;
	if (sample != nullptr)
		pauseVoice(toVoice(sample));
}

void __stdcall AIL_resume_sample(HSAMPLE sample)
{
	ScopedLock lock;
	if (sample != nullptr)
		resumeVoice(toVoice(sample));
}

void __stdcall AIL_end_sample(HSAMPLE sample)
{
	ScopedLock lock;
	if (sample != nullptr)
		endVoice(toVoice(sample));
}

AIL_sample_callback __stdcall AIL_register_EOS_callback(HSAMPLE sample, AIL_sample_callback EOS)
{
	ScopedLock lock;
	Voice *voice = toVoice(sample);
	if (voice == nullptr)
		return nullptr;

	AIL_sample_callback previous = voice->sampleCallback;
	voice->sampleCallback = EOS;
	return previous;
}

void __stdcall AIL_set_sample_volume_pan(HSAMPLE sample, F32 volume, F32 pan)
{
	ScopedLock lock;
	Voice *voice = toVoice(sample);
	if (voice == nullptr)
		return;

	voice->volume = volume;
	voice->pan = pan;
	applyVoiceMix(voice);
}

void __stdcall AIL_sample_volume_pan(HSAMPLE sample, F32 *volume, F32 *pan)
{
	ScopedLock lock;
	Voice *voice = toVoice(sample);
	if (volume != nullptr)
		*volume = voice ? voice->volume : 0.0f;
	if (pan != nullptr)
		*pan = voice ? voice->pan : 0.5f;
}

void __stdcall AIL_set_sample_playback_rate(HSAMPLE sample, S32 playback_rate)
{
	ScopedLock lock;
	Voice *voice = toVoice(sample);
	if (voice == nullptr)
		return;

	voice->rate = playback_rate;
	applyVoiceMix(voice);
}

S32 __stdcall AIL_sample_playback_rate(HSAMPLE sample)
{
	ScopedLock lock;
	Voice *voice = toVoice(sample);
	if (voice == nullptr)
		return 0;
	if (voice->rate > 0)
		return voice->rate;
	return EngineReady ? (S32)ma_engine_get_sample_rate(&Engine) : 44100;
}

void __stdcall AIL_sample_ms_position(HSAMPLE sample, S32 *total_ms, S32 *current_ms)
{
	ScopedLock lock;
	getVoiceMsPosition(toVoice(sample), total_ms, current_ms);
}

void __stdcall AIL_set_sample_ms_position(HSAMPLE sample, S32 pos)
{
}

S32 __stdcall AIL_sample_loop_count(HSAMPLE sample)
{
	ScopedLock lock;
	Voice *voice = toVoice(sample);
	if (voice == nullptr)
		return 0;
	return voice->soundReady ? (S32)voice->loop.loopsRemaining : voice->loopCount;
}

void __stdcall AIL_set_sample_loop_count(HSAMPLE sample, S32 count)
{
	ScopedLock lock;
	Voice *voice = toVoice(sample);
	if (voice == nullptr)
		return;

	voice->loopCount = count;
	if (voice->soundReady)
		voice->loop.loopsRemaining = count;
}

void __stdcall AIL_set_sample_user_data(HSAMPLE sample, U32 index, S32 value)
{
	ScopedLock lock;
	Voice *voice = toVoice(sample);
	if (voice != nullptr && index < (U32)MaxUserData)
		voice->userData[index] = value;
}

S32 __stdcall AIL_sample_user_data(HSAMPLE sample, U32 index)
{
	ScopedLock lock;
	Voice *voice = toVoice(sample);
	if (voice != nullptr && index < (U32)MaxUserData)
		return voice->userData[index];
	return 0;
}

//-------------------------------------------------------------------------------------------------
// 3D providers and listener

S32 __stdcall AIL_enumerate_3D_providers(HPROENUM *next, HPROVIDER *dest, char **name)
{
	if (next == nullptr || *next != HPROENUM_FIRST)
		return 0;

	*next = 1;
	if (dest != nullptr)
		*dest = ProviderId;
	if (name != nullptr)
		*name = (char *)ProviderName;
	return 1;
}

M3DRESULT __stdcall AIL_open_3D_provider(HPROVIDER lib)
{
	return lib == ProviderId ? M3D_NOERR : 1;
}

void __stdcall AIL_close_3D_provider(HPROVIDER lib)
{
}

void __stdcall AIL_set_3D_speaker_type(HPROVIDER lib, S32 speaker_type)
{
}

H3DPOBJECT __stdcall AIL_open_3D_listener(HPROVIDER lib)
{
	return &TheListener.object;
}

void __stdcall AIL_close_3D_listener(H3DPOBJECT listener)
{
}

void __stdcall AIL_set_3D_position(H3DPOBJECT obj, F32 X, F32 Y, F32 Z)
{
	ScopedLock lock;
	if (obj == nullptr)
		return;

	F32 *position = isListener(obj) ? TheListener.position : toVoice(obj)->position;
	position[0] = X;
	position[1] = Y;
	position[2] = Z;

	if (isListener(obj))
		applyAllPositionalMixes();
	else
		applyVoiceMix(toVoice(obj));
}

void __stdcall AIL_set_3D_orientation(H3DPOBJECT obj, F32 X_face, F32 Y_face, F32 Z_face, F32 X_up, F32 Y_up, F32 Z_up)
{
	ScopedLock lock;
	if (!isListener(obj))
		return;

	TheListener.face[0] = X_face;
	TheListener.face[1] = Y_face;
	TheListener.face[2] = Z_face;
	TheListener.up[0] = X_up;
	TheListener.up[1] = Y_up;
	TheListener.up[2] = Z_up;
	applyAllPositionalMixes();
}

void __stdcall AIL_set_3D_velocity_vector(H3DPOBJECT obj, F32 x, F32 y, F32 z)
{
}

void __stdcall AIL_set_3D_user_data(H3DPOBJECT obj, U32 index, S32 value)
{
	ScopedLock lock;
	if (obj != nullptr && !isListener(obj) && index < (U32)MaxUserData)
		toVoice(obj)->userData[index] = value;
}

S32 __stdcall AIL_3D_user_data(H3DPOBJECT obj, U32 index)
{
	ScopedLock lock;
	if (obj != nullptr && !isListener(obj) && index < (U32)MaxUserData)
		return toVoice(obj)->userData[index];
	return 0;
}

//-------------------------------------------------------------------------------------------------
// 3D samples

H3DSAMPLE __stdcall AIL_allocate_3D_sample_handle(HPROVIDER lib)
{
	ScopedLock lock;
	return (H3DSAMPLE)createVoice(VoiceKind_3DSample);
}

void __stdcall AIL_release_3D_sample_handle(H3DSAMPLE sample)
{
	ScopedLock lock;
	if (sample != nullptr)
		destroyVoice(toVoice(sample));
}

S32 __stdcall AIL_set_3D_sample_file(H3DSAMPLE sample, const void *file_image)
{
	ScopedLock lock;
	AILSOUNDINFO info;
	Voice *voice = toVoice(sample);
	if (voice == nullptr || !parseWav(file_image, &info))
		return 0;

	voice->occlusion = 0.0f;
	return setVoiceImage(voice, file_image, wavImageSize(info)) ? 1 : 0;
}

void __stdcall AIL_start_3D_sample(H3DSAMPLE sample)
{
	ScopedLock lock;
	if (sample != nullptr)
		startVoice(toVoice(sample));
}

void __stdcall AIL_stop_3D_sample(H3DSAMPLE sample)
{
	ScopedLock lock;
	if (sample != nullptr)
		pauseVoice(toVoice(sample));
}

void __stdcall AIL_resume_3D_sample(H3DSAMPLE sample)
{
	ScopedLock lock;
	if (sample != nullptr)
		resumeVoice(toVoice(sample));
}

void __stdcall AIL_end_3D_sample(H3DSAMPLE sample)
{
	ScopedLock lock;
	if (sample != nullptr)
		endVoice(toVoice(sample));
}

AIL_3dsample_callback __stdcall AIL_register_3D_EOS_callback(H3DSAMPLE sample, AIL_3dsample_callback EOS)
{
	ScopedLock lock;
	Voice *voice = toVoice(sample);
	if (voice == nullptr)
		return nullptr;

	AIL_3dsample_callback previous = voice->sample3DCallback;
	voice->sample3DCallback = EOS;
	return previous;
}

void __stdcall AIL_set_3D_sample_volume(H3DSAMPLE sample, F32 volume)
{
	ScopedLock lock;
	Voice *voice = toVoice(sample);
	if (voice == nullptr)
		return;

	voice->volume = volume;
	applyVoiceMix(voice);
}

F32 __stdcall AIL_3D_sample_volume(H3DSAMPLE sample)
{
	ScopedLock lock;
	Voice *voice = toVoice(sample);
	return voice ? voice->volume : 0.0f;
}

void __stdcall AIL_set_3D_sample_playback_rate(H3DSAMPLE sample, S32 playback_rate)
{
	AIL_set_sample_playback_rate((HSAMPLE)sample, playback_rate);
}

S32 __stdcall AIL_3D_sample_playback_rate(H3DSAMPLE sample)
{
	return AIL_sample_playback_rate((HSAMPLE)sample);
}

void __stdcall AIL_set_3D_sample_distances(H3DSAMPLE sample, F32 max_dist, F32 min_dist)
{
	ScopedLock lock;
	Voice *voice = toVoice(sample);
	if (voice == nullptr)
		return;

	voice->maxDistance = max_dist;
	voice->minDistance = min_dist;
	applyVoiceMix(voice);
}

void __stdcall AIL_set_3D_sample_occlusion(H3DSAMPLE sample, F32 occlusion)
{
	ScopedLock lock;
	Voice *voice = toVoice(sample);
	if (voice == nullptr)
		return;

	voice->occlusion = occlusion < 0.0f ? 0.0f : (occlusion > 1.0f ? 1.0f : occlusion);
	if (voice->soundReady)
		routeThroughLowPass(voice);
}

void __stdcall AIL_set_3D_sample_effects_level(H3DSAMPLE sample, F32 effect_level)
{
}

U32 __stdcall AIL_3D_sample_loop_count(H3DSAMPLE sample)
{
	return (U32)AIL_sample_loop_count((HSAMPLE)sample);
}

void __stdcall AIL_set_3D_sample_loop_count(H3DSAMPLE sample, U32 count)
{
	AIL_set_sample_loop_count((HSAMPLE)sample, (S32)count);
}

U32 __stdcall AIL_3D_sample_length(H3DSAMPLE sample)
{
	return 0;
}

U32 __stdcall AIL_3D_sample_offset(H3DSAMPLE sample)
{
	return 0;
}

void __stdcall AIL_set_3D_sample_offset(H3DSAMPLE sample, U32 offset)
{
}

//-------------------------------------------------------------------------------------------------
// Streams

HSTREAM __stdcall AIL_open_stream(HDIGDRIVER dig, const char *filename, S32 stream_mem)
{
	initLock();
	ScopedLock lock;
	Voice *voice = createVoice(VoiceKind_Stream);
	if (voice == nullptr)
		return nullptr;

	if (!openVoiceStream(voice, filename))
	{
		destroyVoice(voice);
		return nullptr;
	}
	return (HSTREAM)voice;
}

void __stdcall AIL_close_stream(HSTREAM stream)
{
	ScopedLock lock;
	if (stream != nullptr)
		destroyVoice(toVoice(stream));
}

void __stdcall AIL_start_stream(HSTREAM stream)
{
	ScopedLock lock;
	if (stream != nullptr)
		startVoice(toVoice(stream));
}

void __stdcall AIL_pause_stream(HSTREAM stream, S32 onoff)
{
	ScopedLock lock;
	Voice *voice = toVoice(stream);
	if (voice == nullptr)
		return;

	if (onoff)
		pauseVoice(voice);
	else
		resumeVoice(voice);
}

AIL_stream_callback __stdcall AIL_register_stream_callback(HSTREAM stream, AIL_stream_callback callback)
{
	ScopedLock lock;
	Voice *voice = toVoice(stream);
	if (voice == nullptr)
		return nullptr;

	AIL_stream_callback previous = voice->streamCallback;
	voice->streamCallback = callback;
	return previous;
}

void __stdcall AIL_set_stream_volume_pan(HSTREAM stream, F32 volume, F32 pan)
{
	AIL_set_sample_volume_pan((HSAMPLE)stream, volume, pan);
}

void __stdcall AIL_stream_volume_pan(HSTREAM stream, F32 *volume, F32 *pan)
{
	AIL_sample_volume_pan((HSAMPLE)stream, volume, pan);
}

void __stdcall AIL_set_stream_playback_rate(HSTREAM stream, S32 rate)
{
	AIL_set_sample_playback_rate((HSAMPLE)stream, rate);
}

S32 __stdcall AIL_stream_playback_rate(HSTREAM stream)
{
	return AIL_sample_playback_rate((HSAMPLE)stream);
}

S32 __stdcall AIL_stream_loop_count(HSTREAM stream)
{
	return AIL_sample_loop_count((HSAMPLE)stream);
}

void __stdcall AIL_set_stream_loop_count(HSTREAM stream, S32 count)
{
	AIL_set_sample_loop_count((HSAMPLE)stream, count);
}

void __stdcall AIL_set_stream_loop_block(HSTREAM stream, S32 loop_start, S32 loop_end)
{
}

void __stdcall AIL_stream_ms_position(HSTREAM stream, S32 *total_milliseconds, S32 *current_milliseconds)
{
	ScopedLock lock;
	getVoiceMsPosition(toVoice(stream), total_milliseconds, current_milliseconds);
}

void __stdcall AIL_set_stream_ms_position(HSTREAM stream, S32 pos)
{
}

//-------------------------------------------------------------------------------------------------
// Quick functions, used for sounds that are forced to play, such as mission briefings.

HAUDIO __stdcall AIL_quick_load_and_play(const char *filename, U32 loop_count, S32 wait_request)
{
	HSTREAM stream = AIL_open_stream(&DigitalDriver, filename, 0);
	if (stream == nullptr)
		return nullptr;

	AIL_set_stream_loop_count(stream, (S32)loop_count);
	AIL_start_stream(stream);
	return (HAUDIO)stream;
}

void __stdcall AIL_quick_set_volume(HAUDIO audio, F32 volume, F32 extravol)
{
	AIL_set_stream_volume_pan((HSTREAM)audio, volume, extravol);
}

void __stdcall AIL_quick_unload(HAUDIO audio)
{
	AIL_close_stream((HSTREAM)audio);
}

//-------------------------------------------------------------------------------------------------
// Video sound tracks, see MilesVideoAudio.h. Each one is a sound reading from a queue of samples.

class MilesVideoAudio
{
public:
	ma_data_source_base base; // Must be first, the sound reads the stream as its data source
	ma_pcm_rb queue;
	ma_sound sound;
	bool soundReady;
	ma_uint32 channels;
	ma_uint32 sampleRate;
	MilesVideoAudio *prev;
	MilesVideoAudio *next;
};

namespace
{

const ma_uint32 VideoQueueSeconds = 4;
const int MaxVideoChannels = 8;

MilesVideoAudio *VideoSounds = nullptr;

ma_result videoRead(ma_data_source *dataSource, void *framesOut, ma_uint64 frameCount, ma_uint64 *framesRead)
{
	MilesVideoAudio *stream = (MilesVideoAudio *)dataSource;
	const ma_uint32 frameSize = ma_get_bytes_per_frame(ma_format_f32, stream->channels);

	ma_uint64 total = 0;
	while (total < frameCount)
	{
		ma_uint64 remaining = frameCount - total;
		ma_uint32 count = remaining > 0xFFFFFFFF ? 0xFFFFFFFF : (ma_uint32)remaining;
		void *queued = nullptr;
		if (ma_pcm_rb_acquire_read(&stream->queue, &count, &queued) != MA_SUCCESS || count == 0)
			break;

		if (framesOut != nullptr)
			memcpy(ma_offset_pcm_frames_ptr(framesOut, total, ma_format_f32, stream->channels), queued, (size_t)count * frameSize);
		ma_pcm_rb_commit_read(&stream->queue, count);
		total += count;
	}

	// Never end: play silence until the video writes more.
	if (framesOut != nullptr && total < frameCount)
		memset(ma_offset_pcm_frames_ptr(framesOut, total, ma_format_f32, stream->channels), 0, (size_t)(frameCount - total) * frameSize);

	*framesRead = frameCount;
	return MA_SUCCESS;
}

ma_result videoSeek(ma_data_source *, ma_uint64)
{
	return MA_SUCCESS;
}

ma_result videoGetDataFormat(ma_data_source *dataSource, ma_format *format, ma_uint32 *channels, ma_uint32 *sampleRate, ma_channel *channelMap, size_t channelMapCap)
{
	MilesVideoAudio *stream = (MilesVideoAudio *)dataSource;
	if (format != nullptr)
		*format = ma_format_f32;
	if (channels != nullptr)
		*channels = stream->channels;
	if (sampleRate != nullptr)
		*sampleRate = stream->sampleRate;
	if (channelMap != nullptr)
		ma_channel_map_init_standard(ma_standard_channel_map_default, channelMap, channelMapCap, stream->channels);
	return MA_SUCCESS;
}

ma_result videoGetCursor(ma_data_source *, ma_uint64 *cursor)
{
	*cursor = 0;
	return MA_NOT_IMPLEMENTED;
}

ma_result videoGetLength(ma_data_source *, ma_uint64 *length)
{
	*length = 0;
	return MA_NOT_IMPLEMENTED;
}

ma_data_source_vtable VideoSourceVtable =
{
	videoRead,
	videoSeek,
	videoGetDataFormat,
	videoGetCursor,
	videoGetLength,
	nullptr,
	0
};

void releaseVideoSound(MilesVideoAudio *stream)
{
	if (stream->soundReady)
	{
		ma_sound_uninit(&stream->sound);
		stream->soundReady = false;
	}
}

// Called with the lock held before the engine goes away. Streams still open stay valid but silent.
void releaseVideoSounds()
{
	for (MilesVideoAudio *stream = VideoSounds; stream != nullptr; stream = stream->next)
		releaseVideoSound(stream);
}

} // namespace

MilesVideoAudio *MilesVideoAudio_open(int channels, int sampleRate)
{
	ScopedLock lock;
	if (!EngineReady || channels < 1 || channels > MaxVideoChannels || sampleRate < 1000 || sampleRate > 384000)
		return nullptr;

	MilesVideoAudio *stream = (MilesVideoAudio *)calloc(1, sizeof(MilesVideoAudio));
	if (stream == nullptr)
		return nullptr;

	stream->channels = (ma_uint32)channels;
	stream->sampleRate = (ma_uint32)sampleRate;

	ma_data_source_config config = ma_data_source_config_init();
	config.vtable = &VideoSourceVtable;
	if (ma_data_source_init(&config, &stream->base) != MA_SUCCESS)
	{
		free(stream);
		return nullptr;
	}

	if (ma_pcm_rb_init(ma_format_f32, stream->channels, stream->sampleRate * VideoQueueSeconds, nullptr, nullptr, &stream->queue) != MA_SUCCESS)
	{
		ma_data_source_uninit(&stream->base);
		free(stream);
		return nullptr;
	}

	if (ma_sound_init_from_data_source(&Engine, stream, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, &stream->sound) != MA_SUCCESS)
	{
		ma_pcm_rb_uninit(&stream->queue);
		ma_data_source_uninit(&stream->base);
		free(stream);
		return nullptr;
	}
	stream->soundReady = true;
	ma_sound_start(&stream->sound);

	stream->next = VideoSounds;
	if (VideoSounds != nullptr)
		VideoSounds->prev = stream;
	VideoSounds = stream;
	return stream;
}

void MilesVideoAudio_write(MilesVideoAudio *stream, const float *frames, unsigned int frameCount)
{
	if (stream == nullptr || frames == nullptr)
		return;

	const ma_uint32 frameSize = ma_get_bytes_per_frame(ma_format_f32, stream->channels);
	while (frameCount > 0)
	{
		ma_uint32 count = frameCount;
		void *space = nullptr;
		if (ma_pcm_rb_acquire_write(&stream->queue, &count, &space) != MA_SUCCESS || count == 0)
			return; // Queue full, drop the rest

		memcpy(space, frames, (size_t)count * frameSize);
		ma_pcm_rb_commit_write(&stream->queue, count);
		frames += (size_t)count * stream->channels;
		frameCount -= count;
	}
}

void MilesVideoAudio_setVolume(MilesVideoAudio *stream, float volume)
{
	ScopedLock lock;
	if (stream != nullptr && stream->soundReady)
		ma_sound_set_volume(&stream->sound, volume < 0.0f ? 0.0f : volume);
}

void MilesVideoAudio_pause(MilesVideoAudio *stream, bool paused)
{
	ScopedLock lock;
	if (stream == nullptr || !stream->soundReady)
		return;

	if (paused)
		ma_sound_stop(&stream->sound);
	else
		ma_sound_start(&stream->sound);
}

void MilesVideoAudio_close(MilesVideoAudio *stream)
{
	if (stream == nullptr)
		return;

	ScopedLock lock;
	releaseVideoSound(stream);

	if (stream->prev != nullptr)
		stream->prev->next = stream->next;
	else
		VideoSounds = stream->next;
	if (stream->next != nullptr)
		stream->next->prev = stream->prev;

	ma_pcm_rb_uninit(&stream->queue);
	ma_data_source_uninit(&stream->base);
	free(stream);
}
