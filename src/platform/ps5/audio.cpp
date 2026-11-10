/*
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * EasyRPG Player is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with EasyRPG Player. If not, see <http://www.gnu.org/licenses/>.
 */

#include "system.h"

#ifdef SUPPORT_AUDIO

#include "audio.h"
#include "output.h"

#include <SDL.h>
#include <cstdint>
#include <vector>

extern "C" {
int sceAudioOutInit(void);
int sceAudioOutOpen(int user_id, int type, int index, unsigned len,
                    unsigned freq, unsigned param);
int sceAudioOutOutput(int handle, const void* buffer);
int sceAudioOutClose(int handle);
}

namespace {
	constexpr int kSystemUser = 0xFF;
	constexpr int kPortMain = 0;
	constexpr int kFormatS16Stereo = 1;
	constexpr unsigned kGrain = 1024; // frames per output block
	constexpr unsigned kRate = 48000;
}

static int SDLCALL AudioThread(void* data) {
	static_cast<Ps5Audio*>(data)->Run();
	return 0;
}

Ps5Audio::Ps5Audio(const Game_ConfigAudio& cfg) : GenericAudio(cfg) {
	sceAudioOutInit();
	port = sceAudioOutOpen(kSystemUser, kPortMain, 0, kGrain, kRate, kFormatS16Stereo);
	if (port < 0) {
		Output::Warning("Couldn't open the audio output ({:#x})", static_cast<unsigned>(port));
		port = -1;
		return;
	}

	mutex = SDL_CreateMutex();
	SetFormat(kRate, AudioDecoder::Format::S16, 2);

	running = true;
	thread = SDL_CreateThread(AudioThread, "easyrpg-audio", this);
}

Ps5Audio::~Ps5Audio() {
	running = false;
	if (thread) {
		SDL_WaitThread(thread, nullptr);
	}
	if (port >= 0) {
		sceAudioOutClose(port);
	}
	if (mutex) {
		SDL_DestroyMutex(mutex);
	}
}

void Ps5Audio::LockMutex() const {
	if (mutex) {
		SDL_LockMutex(mutex);
	}
}

void Ps5Audio::UnlockMutex() const {
	if (mutex) {
		SDL_UnlockMutex(mutex);
	}
}

void Ps5Audio::Run() {
	std::vector<int16_t> block(kGrain * 2);
	const int bytes = static_cast<int>(block.size() * sizeof(int16_t));

	while (running) {
		LockMutex();
		Decode(reinterpret_cast<uint8_t*>(block.data()), bytes);
		UnlockMutex();
		// Blocks until the console has taken the grain: this paces the loop.
		if (sceAudioOutOutput(port, block.data()) < 0) {
			SDL_Delay(10);
		}
	}
}

#endif
