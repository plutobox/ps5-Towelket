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
 * A game's "url" is a direct link to a ZIP, a Google Drive share link, or
 * the game's itch.io page (free games only). A ZIP is saved into the games
 * folder, which the game browser plays as it is; any other archive (7z, RAR,
 * LZH, ...) is unpacked into a folder of the game's id. Both the catalog and
 * the downloads run on a worker thread; the scene polls the state each frame.
 *
 * The catalog is {"games": [{"id", "name", "author", "license", "url",
 * "size", "upload"}, ...]}; "size" (bytes) and "upload" (an itch.io file id)
 * are optional. "original_release" is a year or ISO date for the original
 * game, and "series_order" is its release position within the series; both
 * are optional and do not refer to the English translation or PS5 port.
 */
namespace Ps5Library {

struct Entry {
	std::string id;       // file name stem; letters, digits, '.', '-' and '_'
	std::string name;
	std::string author;
	std::string license;
	std::string url;
	std::string original_release;
	int64_t series_order = 0;
	int64_t size = 0;
	int64_t upload = 0;   // itch.io: which of the page's files to take (0: the first)
};

enum class State { Idle, Working, Done, Failed };

struct Status {
	State state = State::Idle;
	int64_t done = 0;     // bytes (downloads)
	int64_t total = 0;    // bytes, 0 when unknown
	std::string error;    // when Failed
	bool unpacking = false; // downloaded; now being unpacked
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
