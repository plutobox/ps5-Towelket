/*
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "scene_introduction.h"

#include "bitmap.h"
#include "cache.h"
#include "filefinder.h"
#include "font.h"
#include "game_system.h"
#include "input.h"
#include "main_data.h"
#include "output.h"
#include "player.h"
#include "scene_gamebrowser.h"
#include "scene_settings.h"
#include "sprite.h"
#include "text.h"
#include "transition.h"
#include "window_base.h"
#include "window_command.h"
#include "window_help.h"

#include <initializer_list>
#include <lcf/rpg/system.h>
#include <string_view>

#ifdef _WIN32
#  include <SDL3/SDL.h>
#endif

namespace {

constexpr const char* kLines[] = {
	"Hey! My name is plutobox and I made this lil thang I like to call ps5-Towelket, a fork of another project EasyRPG (HUUUGE credit to them wowow.)",
	"This fork is special becuase it has several interesting modifications, one of which you are seeing right now. ;) This dialogue isn't normal, I'm really smart.",
	"This port of EasyRPG is limited to '''secial''' series of games called Towelket (NOT FOR KIDS, GRAPHIC). You will never forget playing them if you get through the first 3 entries in the series. There are quite a few. I think like 14?",
	"Towelket is a special game. Towelket 3 came out first, then Towelket 2, then Towelket 1. You don't have to play any of them in order, but the first 3 are highly regarded.",
	"Look, I've been up for over 24 hours so my head isn't on straight. I think that's why I ported this fucking series, I'm wired man, time for bed. Please give these games a shot.",
	"Oh wait wait. Directions. These games aren't shipped with the Towelket releases. You will have to go to library and click on them and then theyll download. Sorry for any bugs.",
};

constexpr unsigned kLineCount = sizeof(kLines) / sizeof(kLines[0]);

} // namespace

/** A native-skinned dialogue box that displays several clean text lines. */
class IntroDialogueWindow final : public Window_Base {
public:
	IntroDialogueWindow(int x, int y, int width, int height) :
		Window_Base(x, y, width, height) {
		SetContents(Bitmap::Create(width - 16, height - 16));
	}

	void SetLines(std::initializer_list<std::string_view> lines) {
		contents->Clear();
		int y = 2;
		for (auto line : lines) {
			contents->TextDraw(0, y, Font::ColorDefault, line);
			y += 16;
		}
	}
};

Scene_Introduction::~Scene_Introduction() = default;

Scene_Introduction::Scene_Introduction() {
	type = Scene::Introduction;
}

void Scene_Introduction::Start() {
	// Match the game browser's built-in UI skin and background palette.  The
	// introduction is shown before the browser has initialized a Game_System.
	Main_Data::game_system = std::make_unique<Game_System>();
	Main_Data::game_system->SetSystemGraphic(CACHE_DEFAULT_BITMAP,
		lcf::rpg::System::Stretch_stretch, lcf::rpg::System::Font_gothic);

	const int prompt_height = 32;
	// Let the dialogue occupy almost the whole screen: Towel takes the upper
	// left, and the large panel fills everything below him.
	const int dialogue_y = Player::screen_height / 4;
	const int dialogue_height = Player::screen_height - dialogue_y - prompt_height;
	dialogue_window = std::make_unique<IntroDialogueWindow>(0, dialogue_y,
		Player::screen_width, dialogue_height);
	prompt_window = std::make_unique<Window_Help>(0, dialogue_y + dialogue_height,
		Player::screen_width, prompt_height);
	skip_window = std::make_unique<Window_Command>(
		std::vector<std::string>{ "Skip this time", "No" }, 128, 2);
	skip_window->SetX(Player::screen_width - skip_window->GetWidth() - 16);
	skip_window->SetY(Player::screen_height - skip_window->GetHeight() - prompt_height - 8);
	skip_window->SetActive(false);
	skip_window->SetVisible(false);
	RefreshText();

	std::string portrait_path;

	// The PS5 package installs this asset in /app0. On Windows, use SDL's
	// executable directory rather than the caller's working directory.
#ifdef PLAYER_PS5
	portrait_path = "/app0/towel_protagonist.png";
#elif defined(_WIN32)
	const char* base_path = SDL_GetBasePath();
	if (base_path) {
		portrait_path = FileFinder::MakePath(base_path, "towel_protagonist.png");
		SDL_free(const_cast<char*>(base_path));
	}
#endif

	if (portrait_path.empty()) {
		Output::Warning("Could not locate the introduction portrait");
		return;
	}

	// portrait_path is an absolute path (SDL's executable directory on
	// Windows and /app0 on PS5), so bypass the asset search and open it
	// directly.
	auto bitmap = Bitmap::Create(FileFinder::Root().OpenInputStream(portrait_path), true);
	if (!bitmap) {
		Output::Warning("Could not load the introduction portrait");
		return;
	}

	portrait = std::make_unique<Sprite>();
	portrait->SetBitmap(bitmap);

	// Enlarge Towel with one shared scale factor, preserving the original
	// aspect ratio (no horizontal or vertical squeezing).
	const double scale = (Player::screen_height * 0.20) / portrait->GetHeight();
	const int scaled_height = static_cast<int>(portrait->GetHeight() * scale);
	portrait->SetZoomX(scale);
	portrait->SetZoomY(scale);
	portrait->SetX(16);
	// Rest the unaltered portrait directly on the dialogue panel.
	portrait->SetY(dialogue_y - scaled_height);
}

void Scene_Introduction::vUpdate() {
	dialogue_window->Update();
	prompt_window->Update();
	if (saying_goodbye) {
		if (goodbye_fade_started) {
			if (Transition::instance().IsErasedNotActive()) {
				FinishIntroduction();
			}
		} else if (++goodbye_frames >= 45) {
			goodbye_fade_started = true;
			Transition::instance().InitErase(Transition::TransitionFadeOut, this, 20);
		}
		return;
	}

	if (Input::IsMenuCancelTriggered()) {
		if (confirming_skip) {
			confirming_skip = false;
			skip_window->ReplaceCommands({ "Skip this time", "No" });
			skip_window->SetIndex(1);
		} else if (choosing_skip) {
			choosing_skip = false;
			skip_window->SetActive(false);
			skip_window->SetVisible(false);
			prompt_window->SetVisible(true);
		} else if (line > 0) {
			--line;
		}
		RefreshText();
		return;
	}

	if (choosing_skip) {
		skip_window->Update();
		if (Input::IsTriggered(Input::DECISION) || Input::IsTriggered(Input::MOUSE_LEFT)) {
			if (!confirming_skip && skip_window->GetIndex() == 0) {
				confirming_skip = true;
				skip_window->ReplaceCommands({ "Skip", "No" });
				skip_window->SetIndex(0);
				RefreshText();
			} else {
				if (confirming_skip && skip_window->GetIndex() == 0) {
					Player::player_config.skip_introduction.Set(true);
					Scene_Settings::SaveConfig(true);
					FinishIntroduction();
				} else {
					SayGoodbye();
				}
			}
		}
		return;
	}

	if (!Input::IsTriggered(Input::DECISION) && !Input::IsTriggered(Input::MOUSE_LEFT)) {
		return;
	}

	if (line + 1 < kLineCount) {
		++line;
		RefreshText();
		return;
	}

	choosing_skip = true;
	skip_window->SetVisible(true);
	skip_window->SetActive(true);
	prompt_window->SetVisible(false);
	RefreshText();
}

void Scene_Introduction::RefreshText() {
	if (choosing_skip) {
		if (confirming_skip) {
			dialogue_window->SetLines({
				"Awww, you sure?",
				"I love the sound of my own voice."
			});
		} else {
			dialogue_window->SetLines({
				"Next time you launch, would you like",
				"to skip this conversation?",
				"I won't be offended."
			});
		}
		return;
	}

	dialogue_window->SetLines({ kLines[line] });
	// Window_Help places right-aligned words relative to the same origin;
	// keeping this left-aligned prevents those words from stacking together.
	prompt_window->SetText("plutobox - X  Next", Font::ColorHeal);
}

void Scene_Introduction::FinishIntroduction() {
	Scene::Push(std::make_shared<Scene_GameBrowser>(), true);
}

void Scene_Introduction::SayGoodbye() {
	Player::player_config.skip_introduction.Set(false);
	Scene_Settings::SaveConfig(true);
	choosing_skip = false;
	saying_goodbye = true;
	skip_window->SetActive(false);
	skip_window->SetVisible(false);
	prompt_window->SetVisible(false);
	dialogue_window->SetLines({ "Fine. Plutobox - out. If you try any of them, try the third in the series 'Towelket: One More Time'. Peace." });
}
