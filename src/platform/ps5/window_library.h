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

#ifndef EP_PLATFORM_PS5_WINDOW_LIBRARY_H
#define EP_PLATFORM_PS5_WINDOW_LIBRARY_H

#include "window_selectable.h"
#include "library.h"

#include <string>
#include <vector>

/**
 * The game library's list: each game of the catalog with its size, or
 * "Installed" when it is already in the games folder. A message replaces the
 * list while the catalog loads or when it cannot be had.
 */
class Window_Library : public Window_Selectable {
public:
	Window_Library(int ix, int iy, int iwidth, int iheight);

	/** Shows the games. */
	void SetEntries(std::vector<Ps5Library::Entry> entries);
	/** Shows a message instead of the games. */
	void SetMessage(const std::string& message, bool error);
	/** Redraws the list (after a game was installed). */
	void Refresh();

	bool HasEntries() const { return !entries.empty(); }
	/** The selected game, or null if none. */
	const Ps5Library::Entry* GetSelected() const;

	void DrawItem(int index);

private:
	std::vector<Ps5Library::Entry> entries;
};

#endif
