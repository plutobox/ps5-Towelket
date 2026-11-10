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

// Headers
#include "input_buttons.h"
#include "keys.h"
#include "game_config.h"

// The DualSense, as SDL's joystick reports it: Cross is "A", Circle "B",
// Square "X" and Triangle "Y".
Input::ButtonMappingArray Input::GetDefaultButtonMappings() {
	return {
		{UP, Keys::JOY_DPAD_UP},
		{DOWN, Keys::JOY_DPAD_DOWN},
		{LEFT, Keys::JOY_DPAD_LEFT},
		{RIGHT, Keys::JOY_DPAD_RIGHT},
		{DECISION, Keys::JOY_A},
		{CANCEL, Keys::JOY_B},
		{CANCEL, Keys::JOY_Y},
		{SHIFT, Keys::JOY_X},
		{N0, Keys::JOY_LSTICK},
		{N5, Keys::JOY_RSTICK},
		{DEBUG_THROUGH, Keys::JOY_SHOULDER_LEFT},
		{TOGGLE_FPS, Keys::JOY_SHOULDER_RIGHT},
		{SETTINGS_MENU, Keys::JOY_START},
		{RESET, Keys::JOY_BACK},
		{FAST_FORWARD_A, Keys::JOY_RTRIGGER_FULL},
		{DEBUG_MENU, Keys::JOY_LTRIGGER_FULL},

		{UP, Keys::JOY_LSTICK_UP},
		{DOWN, Keys::JOY_LSTICK_DOWN},
		{LEFT, Keys::JOY_LSTICK_LEFT},
		{RIGHT, Keys::JOY_LSTICK_RIGHT},
		{N1, Keys::JOY_RSTICK_DOWN_LEFT},
		{N2, Keys::JOY_RSTICK_DOWN},
		{N3, Keys::JOY_RSTICK_DOWN_RIGHT},
		{N4, Keys::JOY_RSTICK_LEFT},
		{N6, Keys::JOY_RSTICK_RIGHT},
		{N7, Keys::JOY_RSTICK_UP_LEFT},
		{N8, Keys::JOY_RSTICK_UP},
		{N9, Keys::JOY_RSTICK_UP_RIGHT}
	};
}

Input::KeyNamesArray Input::GetInputKeyNames() {
	return {
		{Keys::JOY_DPAD_UP, "D-Pad Up"},
		{Keys::JOY_DPAD_DOWN, "D-Pad Down"},
		{Keys::JOY_DPAD_LEFT, "D-Pad Left"},
		{Keys::JOY_DPAD_RIGHT, "D-Pad Right"},
		{Keys::JOY_A, "Cross"},
		{Keys::JOY_B, "Circle"},
		{Keys::JOY_X, "Square"},
		{Keys::JOY_Y, "Triangle"},
		{Keys::JOY_LSTICK, "L3"},
		{Keys::JOY_RSTICK, "R3"},
		{Keys::JOY_SHOULDER_LEFT, "L1"},
		{Keys::JOY_SHOULDER_RIGHT, "R1"},
		{Keys::JOY_LTRIGGER_FULL, "L2"},
		{Keys::JOY_RTRIGGER_FULL, "R2"},
		{Keys::JOY_BACK, "Create"},
		{Keys::JOY_START, "Options"},
		{Keys::JOY_TOUCH, "Touchpad"}
	};
}

void Input::GetSupportedConfig(Game_ConfigInput& cfg) {
	cfg.gamepad_swap_ab_and_xy.SetOptionVisible(true);
	cfg.gamepad_swap_analog.SetOptionVisible(true);
	cfg.gamepad_swap_dpad_with_buttons.SetOptionVisible(true);
}
