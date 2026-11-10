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

#ifndef EP_PLATFORM_PS5_LIBRARY_H
#define EP_PLATFORM_PS5_LIBRARY_H

#include <cstdint>
#include <string>
#include <vector>

/**
 * The game library: a catalog of freely redistributable RPG Maker games
 * (a JSON file fetched over HTTP(S)) and downloads of the games in it.
 *
 * A game is a ZIP archive, saved into the games folder, which the game
 * browser already plays as it is. Both the catalog and the downloads run on
 * a worker thread; the scene polls the state each frame.
 *
 * The catalog is {"games": [{"id", "name", "author", "license", "url",
 * "size"}, ...]}; "size" (bytes) is optional.
 */
namespace Ps5Library {

struct Entry {
	std::string id;       // file name stem; letters, digits, '.', '-' and '_'
	std::string name;
	std::string author;
	std::string license;
	std::string url;
	int64_t size = 0;
};

enum class State { Idle, Working, Done, Failed };

struct Status {
	State state = State::Idle;
	int64_t done = 0;     // bytes (downloads)
	int64_t total = 0;    // bytes, 0 when unknown
	std::string error;    // when Failed
};

/** Where the catalog is fetched from; overridable (see library.cpp). */
std::string CatalogUrl();

/** Where games are saved. */
const char* GamesDirectory();

/** Starts fetching the catalog, unless that is under way. */
void FetchCatalog();
Status CatalogStatus();
/** A copy of the catalog as fetched (empty until Done). */
std::vector<Entry> Catalog();

/** True when the game's archive (or a folder of its name) is in the games folder. */
bool IsInstalled(const Entry& entry);

/** Starts downloading a game, unless one is under way. */
void StartDownload(const Entry& entry);
/** Asks the running download to stop; it ends Failed ("Cancelled"). */
void CancelDownload();
Status DownloadStatus();
/** The id of the game being (or last) downloaded. */
std::string DownloadId();

/** Parses a catalog; false with a reason in `error`. Exposed for testing. */
bool ParseCatalog(const std::string& json, std::vector<Entry>& out, std::string& error);

}

#endif
