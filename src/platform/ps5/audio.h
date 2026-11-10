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

#ifndef EP_PLATFORM_PS5_AUDIO_H
#define EP_PLATFORM_PS5_AUDIO_H

#include "audio_generic.h"

#include <atomic>

struct SDL_mutex;
struct SDL_Thread;

/** Plays the Player's mixed output through the console's audio port. */
class Ps5Audio final : public GenericAudio {
public:
	explicit Ps5Audio(const Game_ConfigAudio& cfg);
	~Ps5Audio() override;

	void LockMutex() const override;
	void UnlockMutex() const override;

	void Run();

private:
	SDL_mutex* mutex = nullptr;
	SDL_Thread* thread = nullptr;
	int port = -1;
	std::atomic<bool> running{false};
};

#endif
