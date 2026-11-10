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

#ifndef EP_PLATFORM_PS5_UI_H
#define EP_PLATFORM_PS5_UI_H

#include "baseui.h"
#include "system.h"

#include <memory>

/**
 * Ps5Ui: the native PS5 title's display, DualSense and audio.
 *
 * ps5-opengl's SDL2 offers one fixed fullscreen OpenGL window and the
 * joystick, and nothing else: no window surface, no render backend. The
 * Player draws its frame in software (pixman), so this class shows that
 * frame with OpenGL, scaled to fit above a bar of button hints.
 */
class Ps5Ui final : public BaseUi {
public:
	Ps5Ui(long width, long height, const Game_Config& cfg);
	~Ps5Ui() override;

	bool ProcessEvents() override;
	void UpdateDisplay() override;
	void ToggleStretch() override;
	void vGetConfig(Game_ConfigVideo& cfg) const override;
	bool vChangeDisplaySurfaceResolution(int new_width, int new_height) override;
	bool HandleErrorOutput(const std::string& message) override;

#ifdef SUPPORT_AUDIO
	AudioInterface& GetAudio() override;
#endif

	/** Shows a full-screen message and keeps it up (the title cannot exit). */
	static void Farewell(const char* headline, const char* detail);

private:
#ifdef SUPPORT_AUDIO
	std::unique_ptr<AudioInterface> audio_;
#endif
};

#endif
