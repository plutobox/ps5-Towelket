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
#include "scene_gamebrowser.h"

#include <memory>
#include "options.h"
#include "audio.h"
#include "scene_settings.h"
#include "audio_midi.h"
#include "audio_secache.h"
#include "cache.h"
#include "game_system.h"
#include "input.h"
#include "player.h"
#include "scene_logo.h"
#include "bitmap.h"
#include "output.h"
#ifdef _WIN32
#  include <SDL3/SDL.h>
#endif
#ifdef PLAYER_PS5
#  include "platform/ps5/library.h"
#  include "platform/ps5/text_input.h"
#  include <cstdio>
#endif

Scene_GameBrowser::Scene_GameBrowser() {
	type = Scene::GameBrowser;
}

void Scene_GameBrowser::Start() {
	initial_debug_flag = Player::debug_flag;
	Main_Data::game_system = std::make_unique<Game_System>();
	Main_Data::game_system->SetSystemGraphic(CACHE_DEFAULT_BITMAP, lcf::rpg::System::Stretch_stretch, lcf::rpg::System::Font_gothic);
	stack.push_back({ FileFinder::Game(), 0 });
	CreateWindows();
	StartMenuMusic();
	Game_Clock::ResetFrame(Game_Clock::now());
}

void Scene_GameBrowser::StartMenuMusic() {
	std::string music_path;
#ifdef PLAYER_PS5
	music_path = "/app0/towelket_menu.mid";
#elif defined(_WIN32)
	const char* base_path = SDL_GetBasePath();
	if (base_path) {
		music_path = FileFinder::MakePath(base_path, "towelket_menu.mid");
		SDL_free(const_cast<char*>(base_path));
	}
#endif

	if (music_path.empty()) {
		return;
	}

	auto stream = FileFinder::Root().OpenInputStream(music_path);
	if (!stream) {
		Output::Warning("Could not load the Towelket menu music");
		return;
	}

	Audio().BGM_Play(std::move(stream), 80, 100, 0, 50);
}

void Scene_GameBrowser::Continue(SceneType /* prev_scene */) {
	Main_Data::game_system->BgmStop();

	Cache::ClearAll();
	AudioSeCache::Clear();
	MidiDecoder::Reset();
	lcf::Data::Clear();
	Player::ResetGameObjects();

	// Restore the base resolution
	Player::RestoreBaseResolution();

	Player::game_title = "";
	Player::game_title_original = "";

	Player::translation.Reset();

	Font::ResetDefault();

	Main_Data::game_system = std::make_unique<Game_System>();
	Main_Data::game_system->SetSystemGraphic(CACHE_DEFAULT_BITMAP, lcf::rpg::System::Stretch_stretch, lcf::rpg::System::Font_gothic);

	Player::debug_flag = initial_debug_flag;
}

void Scene_GameBrowser::vUpdate() {
	if (game_loading) {
		BootGame();
		return;
	}
	command_window->Update();
	gamelist_window->Update();
#ifdef PLAYER_PS5
	library_window->Update();
	UpdateLibraryStatus();
#endif

	if (command_window->GetActive()) {
		UpdateCommand();
	}
	else if (gamelist_window->GetActive()) {
		UpdateGameListSelection();
	}
#ifdef PLAYER_PS5
	else if (library_window->GetActive()) {
		UpdateLibrary();
	}
#endif
}

void Scene_GameBrowser::CreateWindows() {
	// Create Options Window
	std::vector<std::string> options;

	options.push_back("Games");
#ifdef PLAYER_PS5
	options.push_back("Library");
#endif
	options.push_back("Settings");
	options.push_back("About");
	options.push_back("Exit");

	command_window = std::make_unique<Window_Command_Horizontal>(options, Player::screen_width);
	command_window->SetY(32);
	command_window->SetIndex(0);

	gamelist_window = std::make_unique<Window_GameList>(0, 64, Player::screen_width, Player::screen_height - 64);
	gamelist_window->Refresh(stack.back().filesystem, false);

	if (stack.size() == 1 && !gamelist_window->HasValidEntry()) {
		command_window->DisableItem(0);
	}

	help_window = std::make_unique<Window_Help>(0, 0, Player::screen_width, 32);
	help_window->SetText("EasyRPG Player - RPG Maker 2000/2003 interpreter");

	load_window = std::make_unique<Window_Help>(Player::screen_width / 4, Player::screen_height / 2 - 16, Player::screen_width / 2, 32);
	load_window->SetText("Loading...");
	load_window->SetVisible(false);

#ifdef PLAYER_PS5
	search_window = std::make_unique<Window_Help>(0, 64, Player::screen_width, 32);
	search_window->SetVisible(false);
	UpdateSearchText();
	library_window = std::make_unique<Window_Library>(0, 96, Player::screen_width, Player::screen_height - 96);
	library_window->SetMessage("Loading the library...", false);
	library_window->SetVisible(false);
#endif

	about_window = std::make_unique<Window_About>(0, 64, Player::screen_width, Player::screen_height - 64);
	about_window->Refresh();
	about_window->SetVisible(false);
}
void Scene_GameBrowser::UpdateCommand() {
	int menu_index = command_window->GetIndex();

	switch (menu_index) {
		case GameList:
			gamelist_window->SetVisible(true);
			about_window->SetVisible(false);
#ifdef PLAYER_PS5
			library_window->SetVisible(false);
			search_window->SetVisible(false);
#endif
			break;
#ifdef PLAYER_PS5
		case Library:
			gamelist_window->SetVisible(false);
			about_window->SetVisible(false);
			library_window->SetVisible(true);
			search_window->SetVisible(true);
			if (Ps5Library::CatalogStatus().state == Ps5Library::State::Idle) {
				Ps5Library::FetchCatalog();
			}
			break;
#endif
		case About:
			gamelist_window->SetVisible(false);
			about_window->SetVisible(true);
#ifdef PLAYER_PS5
			library_window->SetVisible(false);
			search_window->SetVisible(false);
#endif
			break;
		default:
			break;
	}

	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		Scene::Pop();
	} else if (Input::IsTriggered(Input::DECISION)) {

		switch (menu_index) {
			case GameList:
				if (stack.size() == 1 && !gamelist_window->HasValidEntry()) {
					return;
				}
				Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
				command_window->SetActive(false);
				command_window->SetIndex(-1);
				gamelist_window->SetActive(true);
				gamelist_window->SetIndex(old_gamelist_index);
				break;
#ifdef PLAYER_PS5
			case Library:
				Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
				command_window->SetActive(false);
				command_window->SetIndex(-1);
				gamelist_window->SetActive(false);
				gamelist_window->SetIndex(-1);
				library_window->SetActive(true);
				library_window->SetIndex(library_window->HasEntries() ? 0 : -1);
				break;
#endif
			case About:
				break;
			case Options:
				Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
				Scene::Push(std::make_shared<Scene_Settings>());
				break;
			default:
				Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
				Scene::Pop();
		}
	}
}

void Scene_GameBrowser::UpdateGameListSelection() {
	if (Input::IsTriggered(Input::CANCEL)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		command_window->SetActive(true);
		command_window->SetIndex(0);
		gamelist_window->SetActive(false);
		old_gamelist_index = gamelist_window->GetIndex();
		gamelist_window->SetIndex(-1);
	} else if (Input::IsTriggered(Input::DECISION)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		load_window->SetVisible(true);
		game_loading = true;
	} else if (Input::IsTriggered(Input::DEBUG_MENU) || Input::IsTriggered(Input::SHIFT)) {
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
		Player::debug_flag = true;
		load_window->SetVisible(true);
		game_loading = true;
	}
}

void Scene_GameBrowser::BootGame() {
	if (stack.size() > 1 && gamelist_window->GetIndex() == 0) {
		// ".." -> Go one level up
		int index = stack.back().index;
		stack.pop_back();
		gamelist_window->Refresh(stack.back().filesystem, stack.size() > 1);
		gamelist_window->SetIndex(index);
		load_window->SetVisible(false);
		game_loading = false;
		return;
	}

	auto entry = gamelist_window->GetFilesystemEntry();

	if (!entry.fs) {
		Output::Warning("The selected file or directory cannot be opened");
		load_window->SetVisible(false);
		game_loading = false;
		return;
	}

	if (entry.type == FileFinder::ProjectType::Unknown) {
		// Fetched again for platforms where the type is not populated due to bad IO performance
		entry.type = FileFinder::GetProjectType(entry.fs);
	}

	if (entry.type > FileFinder::ProjectType::Supported) {
		// Game is using a known unsupported engine
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Buzzer));
		Output::Warning(
				"{} has unsupported engine {}",
				FileFinder::GetPathAndFilename(entry.fs.GetFullPath()).second,
				FileFinder::kProjectType.tag(entry.type)
		);
		load_window->SetVisible(false);
		game_loading = false;
		return;
	}

	if (entry.type == FileFinder::ProjectType::Unknown && !FileFinder::OpenViewToEasyRpgFile(entry.fs)) {
		// Not a game: Open as directory
		load_window->SetVisible(false);
		game_loading = false;
		if (!gamelist_window->Refresh(entry.fs, true)) {
			Output::Warning("The selected file or directory cannot be opened");
			return;
		}
		stack.push_back({ entry.fs, gamelist_window->GetIndex() });
		gamelist_window->SetIndex(0);

		return;
	}

	FileFinder::SetGameFilesystem(entry.fs);
	Player::CreateGameObjects();

	game_loading = false;
	load_window->SetVisible(false);

	auto logos = Scene_Logo::LoadLogos();
	if (!logos.empty()) {
		// Delegate to Scene_Logo when a startup graphic was found
		Scene::Push(std::make_shared<Scene_Logo>(std::move(logos), 1));
		return;
	}

	Scene::PushTitleScene();
}

#ifdef PLAYER_PS5

namespace {
	std::string Mb(int64_t bytes) {
		char buf[32];
		std::snprintf(buf, sizeof buf, "%.1f", static_cast<double>(bytes) / (1024 * 1024));
		return buf;
	}
}

void Scene_GameBrowser::UpdateSearchText() {
	const std::string& f = library_window ? library_window->GetFilter() : std::string();
	search_window->SetText(f.empty() ? "Search: (all games)    Triangle: search by name"
		: "Search: " + f + "    Triangle: change (empty shows all)");
}

// The help line over the list: about the selected game when idle.
void Scene_GameBrowser::ShowLibraryHelp() {
	const auto* e = library_window->GetSelected();
	if (e && library_window->GetActive()) {
		std::string text = e->author.empty() ? e->license : (e->license.empty() ? e->author : e->author + " - " + e->license);
		help_window->SetText(text.empty() ? e->name : text);
	} else {
		help_window->SetText("EasyRPG Player - RPG Maker 2000/2003 interpreter");
	}
}

// Follows the catalog fetch and the download; run every frame.
void Scene_GameBrowser::UpdateLibraryStatus() {
	using Ps5Library::State;

	auto catalog = Ps5Library::CatalogStatus();
	if (static_cast<int>(catalog.state) != library_catalog_state) {
		library_catalog_state = static_cast<int>(catalog.state);
		switch (catalog.state) {
			case State::Working:
				library_window->SetMessage("Loading the library...", false);
				break;
			case State::Done:
				library_window->SetEntries(Ps5Library::Catalog());
				if (library_window->GetActive()) {
					library_window->SetIndex(library_window->HasEntries() ? 0 : -1);
				}
				break;
			case State::Failed:
				library_window->SetMessage("Could not load the library: " + catalog.error + "  (Retry: confirm)", true);
				break;
			default:
				break;
		}
	}

	if (!library_downloading) {
		if (library_window->GetActive()) {
			ShowLibraryHelp();
		}
		return;
	}

	auto status = Ps5Library::DownloadStatus();
	switch (status.state) {
		case State::Working: {
			std::string text = "Downloading " + library_download_name;
			if (status.unpacking) {
				text = "Unpacking " + library_download_name + "...";
			} else if (status.total > 0) {
				text += ": " + std::to_string(static_cast<int>(status.done * 100 / status.total)) + "% ("
					+ Mb(status.done) + " / " + Mb(status.total) + " MB)";
			} else {
				text += ": " + Mb(status.done) + " MB";
			}
			help_window->SetText(text);
			// and on the game's own row in the list
			library_window->SetBusy(Ps5Library::DownloadId(),
				status.unpacking ? "Unpacking"
				: status.total > 0 ? std::to_string(static_cast<int>(status.done * 100 / status.total)) + "%"
				: Mb(status.done) + " MB");
			break;
		}
		case State::Done:
			library_downloading = false;
			library_window->SetBusy("", "");
			library_window->Refresh();
			// The new archive is not in the cached directory listing yet.
			stack.back().filesystem.ClearCache();
			gamelist_window->Refresh(stack.back().filesystem, stack.size() > 1);
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Decision));
			help_window->SetText(library_download_name + " is installed. Find it under Games.");
			break;
		case State::Failed:
			library_downloading = false;
			library_window->SetBusy("", "");
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Buzzer));
			help_window->SetText("Download failed: " + status.error, Font::ColorKnockout);
			break;
		default:
			break;
	}
}

void Scene_GameBrowser::UpdateLibrary() {
	using Ps5Library::State;

	if (Input::IsRawKeyTriggered(Input::Keys::JOY_Y)) {
		// Triangle: search by name with the system keyboard
		std::string text = library_window->GetFilter();
		if (Ps5::TextInput("Search the library", text)) {
			library_window->SetFilter(text);
			UpdateSearchText();
		}
		return;
	}

	if (Input::IsTriggered(Input::CANCEL)) {
		if (library_downloading) {
			Ps5Library::CancelDownload();
			return;
		}
		Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Cancel));
		library_window->SetActive(false);
		library_window->SetIndex(-1);
		command_window->SetActive(true);
		command_window->SetIndex(Library);
		help_window->SetText("EasyRPG Player - RPG Maker 2000/2003 interpreter");
	} else if (Input::IsTriggered(Input::DECISION)) {
		if (Ps5Library::CatalogStatus().state == State::Failed) {
			Ps5Library::FetchCatalog(); // retry
			return;
		}
		const auto* e = library_window->GetSelected();
		if (e && e->url.empty()) {
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Buzzer));
			help_window->SetText(e->name + " has no download link yet.");
			return;
		}
		if (!e || library_downloading || Ps5Library::IsInstalled(*e)) {
			Main_Data::game_system->SePlay(Main_Data::game_system->GetSystemSE(Main_Data::game_system->SFX_Buzzer));
			return;
		}
		library_download_name = e->name;
		library_downloading = true;
		Ps5Library::StartDownload(*e);
	}
}

#endif
