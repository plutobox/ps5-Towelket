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

#include "window_library.h"

#include "bitmap.h"
#include "font.h"
#include "text.h"

#include <cstdio>

namespace {

std::string Megabytes(int64_t bytes) {
	char buf[32];
	if (bytes >= 1024 * 1024) {
		std::snprintf(buf, sizeof buf, "%.1f MB", static_cast<double>(bytes) / (1024 * 1024));
	} else {
		std::snprintf(buf, sizeof buf, "%lld KB", static_cast<long long>((bytes + 1023) / 1024));
	}
	return buf;
}

}

Window_Library::Window_Library(int ix, int iy, int iwidth, int iheight) :
	Window_Selectable(ix, iy, iwidth, iheight) {
	column_max = 1;
	SetMenuItemHeight(32);
}

void Window_Library::SetEntries(std::vector<Ps5Library::Entry> new_entries) {
	entries = std::move(new_entries);
	Refresh();
}

void Window_Library::Refresh() {
	if (entries.empty()) {
		SetMessage("The library has no games yet.", false);
		return;
	}
	item_max = static_cast<int>(entries.size());
	CreateContents();
	contents->Clear();
	for (int i = 0; i < item_max; ++i) {
		DrawItem(i);
	}
	if (GetIndex() >= item_max) {
		SetIndex(item_max - 1);
	}
}

void Window_Library::SetMessage(const std::string& message, bool error) {
	entries.clear();
	item_max = 1;
	SetIndex(-1);
	SetContents(Bitmap::Create(width - 16, height - 16));
	contents->TextDraw(0, 0, error ? Font::ColorKnockout : Font::ColorDefault, message);
}

void Window_Library::SetBusy(const std::string& id, const std::string& status) {
	if (id == busy_id && status == busy_status) {
		return;
	}
	busy_id = id;
	busy_status = status;
	if (!entries.empty()) {
		Refresh();
	}
}

const Ps5Library::Entry* Window_Library::GetSelected() const {
	int i = GetIndex();
	if (i < 0 || i >= static_cast<int>(entries.size())) {
		return nullptr;
	}
	return &entries[i];
}

void Window_Library::DrawItem(int index) {
	Rect rect = GetItemRect(index);
	contents->ClearRect(rect);

	const auto& e = entries[index];
	const bool installed = Ps5Library::IsInstalled(e);

	const bool busy = !busy_id.empty() && e.id == busy_id;
	const int name_color = busy ? Font::ColorHeal
		: installed ? Font::ColorDisabled : Font::ColorDefault;
	// Keep dates on a separate line so long titles cannot obscure them.
	contents->TextDraw(Rect(rect.x, rect.y, rect.width, 12), name_color, e.name);
	std::string detail;
	if (!e.original_release.empty()) {
		detail = "Original: " + e.original_release;
	}
	if (e.series_order > 0) {
		if (!detail.empty()) detail += " | ";
		detail += "Series #" + std::to_string(e.series_order);
	}
	contents->TextDraw(rect.x, rect.y + 14, Font::ColorDefault, detail);
	const std::string status = busy ? busy_status
		: installed ? "Installed" : (e.size > 0 ? Megabytes(e.size) : "");
	if (!status.empty()) {
		contents->TextDraw(rect.x + rect.width, rect.y + 14,
			busy || installed ? Font::ColorHeal : Font::ColorDefault,
			status, Text::AlignRight);
	}
}
