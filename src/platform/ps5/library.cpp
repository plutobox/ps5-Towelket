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

#include "library.h"

#include "output.h"

#include <curl/curl.h>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <sys/stat.h>

namespace Ps5Library {

namespace {

// Where the catalog is by default; a title's own file overrides it, e.g. to
// try a catalog served from a PC on the local network.
constexpr const char* kDefaultCatalogUrl =
	"https://raw.githubusercontent.com/plutobox/ps5-Player/ps5/games.json";
constexpr const char* kUrlOverrideFile = "/data/easyrpg/library-url.txt";
constexpr const char* kGamesDir = "/data/easyrpg/games";
constexpr const char* kCaBundle = "/app0/cacert.pem";
constexpr size_t kMaxCatalogBytes = 1 << 20;

std::mutex lock;
Status catalog_status;
std::vector<Entry> catalog;
Status download_status;
std::string download_id;
std::atomic<bool> cancel_requested{false};

bool EnsureCurl() {
	static bool ok = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
	return ok;
}

void SetCommonOptions(CURL* curl, const std::string& url) {
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 8L);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
	// Give up on a stalled transfer: under 1 KB/s for 30 s.
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1024L);
	curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
	curl_easy_setopt(curl, CURLOPT_CAINFO, kCaBundle);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "EasyRPG-Player-PS5");
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	// A title has no ~/.curlrc or proxy variables, and no c-ares.
	curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
}

std::string Describe(CURLcode code, const char* detail) {
	std::string text = curl_easy_strerror(code);
	if (detail && *detail && text != detail) {
		text += ": ";
		text += detail;
	}
	return text;
}

/* ---- A small JSON reader: just enough for the catalog ------------------- */

struct Json {
	const std::string& s;
	size_t i = 0;
	std::string error;

	explicit Json(const std::string& text) : s(text) {}

	void Ws() {
		while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) {
			++i;
		}
	}
	bool Fail(const char* what) {
		if (error.empty()) {
			error = std::string(what) + " at offset " + std::to_string(i);
		}
		return false;
	}
	bool Expect(char c) {
		Ws();
		if (i < s.size() && s[i] == c) {
			++i;
			return true;
		}
		return Fail((std::string("expected '") + c + "'").c_str());
	}
	static void Utf8(std::string& out, uint32_t cp) {
		if (cp < 0x80) {
			out += static_cast<char>(cp);
		} else if (cp < 0x800) {
			out += static_cast<char>(0xC0 | (cp >> 6));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		} else if (cp < 0x10000) {
			out += static_cast<char>(0xE0 | (cp >> 12));
			out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		} else {
			out += static_cast<char>(0xF0 | (cp >> 18));
			out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
			out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
			out += static_cast<char>(0x80 | (cp & 0x3F));
		}
	}
	bool Hex4(uint32_t& v) {
		if (i + 4 > s.size()) {
			return Fail("short \\u escape");
		}
		v = 0;
		for (int k = 0; k < 4; ++k) {
			char c = s[i++];
			v <<= 4;
			if (c >= '0' && c <= '9') v |= c - '0';
			else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
			else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
			else return Fail("bad \\u escape");
		}
		return true;
	}
	bool String(std::string& out) {
		out.clear();
		if (!Expect('"')) {
			return false;
		}
		while (i < s.size()) {
			char c = s[i++];
			if (c == '"') {
				return true;
			}
			if (c != '\\') {
				out += c;
				continue;
			}
			if (i >= s.size()) {
				break;
			}
			char e = s[i++];
			switch (e) {
			case 'n': out += '\n'; break;
			case 't': out += '\t'; break;
			case 'r': out += '\r'; break;
			case 'b': out += '\b'; break;
			case 'f': out += '\f'; break;
			case 'u': {
				uint32_t cp;
				if (!Hex4(cp)) return false;
				if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && s[i] == '\\' && s[i + 1] == 'u') {
					i += 2;
					uint32_t lo;
					if (!Hex4(lo)) return false;
					cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
				}
				Utf8(out, cp);
				break;
			}
			default: out += e; break; // \" \\ \/
			}
		}
		return Fail("unterminated string");
	}
	bool Number(int64_t& out) {
		Ws();
		size_t start = i;
		if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
		while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.' || s[i] == 'e'
				|| s[i] == 'E' || s[i] == '-' || s[i] == '+')) {
			++i;
		}
		if (i == start) {
			return Fail("expected a number");
		}
		out = static_cast<int64_t>(std::strtod(s.substr(start, i - start).c_str(), nullptr));
		return true;
	}
	// Skips any value.
	bool Skip() {
		Ws();
		if (i >= s.size()) {
			return Fail("unexpected end");
		}
		char c = s[i];
		if (c == '"') {
			std::string tmp;
			return String(tmp);
		}
		if (c == '{' || c == '[') {
			char close = c == '{' ? '}' : ']';
			++i;
			Ws();
			if (i < s.size() && s[i] == close) {
				++i;
				return true;
			}
			while (true) {
				if (c == '{') {
					std::string key;
					if (!String(key) || !Expect(':')) return false;
				}
				if (!Skip()) return false;
				Ws();
				if (i < s.size() && s[i] == ',') { ++i; continue; }
				return Expect(close);
			}
		}
		if (std::isalpha(static_cast<unsigned char>(c))) { // true, false, null
			while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i]))) ++i;
			return true;
		}
		int64_t n;
		return Number(n);
	}
};

bool ValidId(const std::string& id) {
	if (id.empty() || id.size() > 80 || id[0] == '.') {
		return false;
	}
	for (char c : id) {
		if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-' || c == '_')) {
			return false;
		}
	}
	return true;
}

bool ReadEntry(Json& j, Entry& e) {
	if (!j.Expect('{')) return false;
	j.Ws();
	if (j.i < j.s.size() && j.s[j.i] == '}') { ++j.i; return true; }
	while (true) {
		std::string key;
		if (!j.String(key) || !j.Expect(':')) return false;
		j.Ws();
		if (key == "id" || key == "name" || key == "author" || key == "license" || key == "url") {
			std::string v;
			if (!j.String(v)) return false;
			(key == "id" ? e.id : key == "name" ? e.name : key == "author" ? e.author
				: key == "license" ? e.license : e.url) = v;
		} else if (key == "size") {
			if (!j.Number(e.size)) return false;
		} else if (!j.Skip()) {
			return false;
		}
		j.Ws();
		if (j.i < j.s.size() && j.s[j.i] == ',') { ++j.i; continue; }
		return j.Expect('}');
	}
}

/* ---- Worker threads ----------------------------------------------------- */

size_t CollectCallback(char* data, size_t size, size_t count, void* user) {
	auto* out = static_cast<std::string*>(user);
	size_t n = size * count;
	if (out->size() + n > kMaxCatalogBytes) {
		return 0; // too large: abort
	}
	out->append(data, n);
	return n;
}

std::string ReadUrlOverride() {
	std::string url;
	if (FILE* f = std::fopen(kUrlOverrideFile, "r")) {
		char buf[512];
		if (std::fgets(buf, sizeof buf, f)) {
			url = buf;
			while (!url.empty() && std::isspace(static_cast<unsigned char>(url.back()))) {
				url.pop_back();
			}
		}
		std::fclose(f);
	}
	return url;
}

void CatalogWorker() {
	std::string body, error;
	std::vector<Entry> entries;
	bool ok = false;

	if (!EnsureCurl()) {
		error = "Networking could not start";
	} else if (CURL* curl = curl_easy_init()) {
		char detail[CURL_ERROR_SIZE] = "";
		SetCommonOptions(curl, CatalogUrl());
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CollectCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
		curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, detail);
		CURLcode code = curl_easy_perform(curl);
		curl_easy_cleanup(curl);
		if (code != CURLE_OK) {
			error = Describe(code, detail);
		} else {
			ok = ParseCatalog(body, entries, error);
		}
	} else {
		error = "Networking could not start";
	}

	std::lock_guard<std::mutex> guard(lock);
	if (ok) {
		catalog = std::move(entries);
		catalog_status.state = State::Done;
		catalog_status.error.clear();
	} else {
		catalog_status.state = State::Failed;
		catalog_status.error = error;
		Output::Debug("Library: catalog failed: {}", error);
	}
}

int ProgressCallback(void*, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
	{
		std::lock_guard<std::mutex> guard(lock);
		download_status.done = now;
		if (total > 0) {
			download_status.total = total;
		}
	}
	return cancel_requested ? 1 : 0;
}

void DownloadWorker(Entry entry) {
	const std::string path = std::string(kGamesDir) + "/" + entry.id + ".zip";
	const std::string part = path + ".part";
	std::string error;
	bool ok = false;

	std::FILE* file = std::fopen(part.c_str(), "wb");
	if (!file) {
		error = "Cannot write to " + std::string(kGamesDir);
	} else if (!EnsureCurl()) {
		error = "Networking could not start";
		std::fclose(file);
	} else if (CURL* curl = curl_easy_init()) {
		char detail[CURL_ERROR_SIZE] = "";
		SetCommonOptions(curl, entry.url);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, fwrite);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
		curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, detail);
		curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
		curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
		CURLcode code = curl_easy_perform(curl);
		curl_easy_cleanup(curl);
		const bool flushed = std::fflush(file) == 0;
		std::fclose(file);

		if (code == CURLE_ABORTED_BY_CALLBACK) {
			error = "Cancelled";
		} else if (code != CURLE_OK) {
			error = Describe(code, detail);
		} else if (!flushed) {
			error = "Could not save the file (disk full?)";
		} else {
			// A game archive is a ZIP: reject an error page or other junk.
			char magic[4] = {};
			std::FILE* check = std::fopen(part.c_str(), "rb");
			bool zip = check && std::fread(magic, 1, 4, check) == 4 && std::memcmp(magic, "PK", 2) == 0;
			if (check) std::fclose(check);
			if (!zip) {
				error = "The download is not a ZIP archive";
			} else if (std::rename(part.c_str(), path.c_str()) != 0) {
				error = "Could not save the game";
			} else {
				ok = true;
			}
		}
	} else {
		error = "Networking could not start";
		std::fclose(file);
	}
	if (!ok) {
		std::remove(part.c_str());
	}

	std::lock_guard<std::mutex> guard(lock);
	if (ok) {
		download_status.state = State::Done;
		download_status.error.clear();
	} else {
		download_status.state = State::Failed;
		download_status.error = error;
		Output::Debug("Library: download of {} failed: {}", entry.id, error);
	}
}

} // namespace

std::string CatalogUrl() {
	std::string url = ReadUrlOverride();
	return url.empty() ? kDefaultCatalogUrl : url;
}

const char* GamesDirectory() {
	return kGamesDir;
}

namespace {

bool ReadGames(Json& j, std::vector<Entry>& entries) {
	if (!j.Expect('[')) return false;
	j.Ws();
	if (j.i < j.s.size() && j.s[j.i] == ']') { ++j.i; return true; }
	while (true) {
		Entry e;
		if (!ReadEntry(j, e)) return false;
		if (!ValidId(e.id) || e.url.empty()) {
			Output::Debug("Library: skipping entry '{}' (bad id or no url)", e.name);
		} else {
			if (e.name.empty()) e.name = e.id;
			entries.push_back(std::move(e));
		}
		j.Ws();
		if (j.i < j.s.size() && j.s[j.i] == ',') { ++j.i; continue; }
		return j.Expect(']');
	}
}

} // namespace

bool ParseCatalog(const std::string& text, std::vector<Entry>& out, std::string& error) {
	Json j(text);
	std::vector<Entry> entries;
	bool found = false;
	bool ok = j.Expect('{');

	j.Ws();
	if (ok && j.i < text.size() && text[j.i] == '}') {
		++j.i;
	} else {
		while (ok) {
			std::string key;
			if (!j.String(key) || !j.Expect(':')) { ok = false; break; }
			if (key == "games") {
				found = true;
				ok = ReadGames(j, entries);
			} else {
				ok = j.Skip();
			}
			if (!ok) break;
			j.Ws();
			if (j.i < text.size() && text[j.i] == ',') { ++j.i; continue; }
			ok = j.Expect('}');
			break;
		}
	}

	if (!ok) {
		error = "The catalog is malformed (" + j.error + ")";
		return false;
	}
	if (!found) {
		error = "The catalog has no \"games\" list";
		return false;
	}
	out = std::move(entries);
	return true;
}

void FetchCatalog() {
	{
		std::lock_guard<std::mutex> guard(lock);
		if (catalog_status.state == State::Working) {
			return;
		}
		catalog_status = Status();
		catalog_status.state = State::Working;
	}
	std::thread(CatalogWorker).detach();
}

Status CatalogStatus() {
	std::lock_guard<std::mutex> guard(lock);
	return catalog_status;
}

std::vector<Entry> Catalog() {
	std::lock_guard<std::mutex> guard(lock);
	return catalog;
}

bool IsInstalled(const Entry& entry) {
	struct stat st;
	const std::string base = std::string(kGamesDir) + "/" + entry.id;
	return stat((base + ".zip").c_str(), &st) == 0 || (stat(base.c_str(), &st) == 0 && S_ISDIR(st.st_mode));
}

void StartDownload(const Entry& entry) {
	{
		std::lock_guard<std::mutex> guard(lock);
		if (download_status.state == State::Working) {
			return;
		}
		download_status = Status();
		download_status.state = State::Working;
		download_status.total = entry.size;
		download_id = entry.id;
	}
	cancel_requested = false;
	std::thread(DownloadWorker, entry).detach();
}

void CancelDownload() {
	cancel_requested = true;
}

Status DownloadStatus() {
	std::lock_guard<std::mutex> guard(lock);
	return download_status;
}

std::string DownloadId() {
	std::lock_guard<std::mutex> guard(lock);
	return download_id;
}

}
