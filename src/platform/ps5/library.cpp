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

#include <archive.h>
#include <archive_entry.h>
#include <curl/curl.h>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

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

int DebugCallback(CURL*, curl_infotype type, char* data, size_t size, void*);

/* The write callback for a download. Not fwrite itself: a libc function's
 * address is not usable until it has been called once (the console binds an
 * import at its first call), and curl calls through the pointer. */
size_t FileWriteCallback(char* data, size_t size, size_t count, void* file) {
	return std::fwrite(data, size, count, static_cast<std::FILE*>(file));
}

/* Frees a curl handle on a thread of its own. On the console closing a TLS
 * connection can block (seen after a finished transfer), and a request that
 * is done must not wait on that: the worker goes on, and the side thread ends
 * whenever the close does. */
void ReleaseCurl(CURL* curl) {
	std::thread([curl] {
		curl_easy_cleanup(curl);
		Output::Debug("Library: curl handle released");
	}).detach();
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
	curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (PlayStation 5) EasyRPG-Player");
	curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
	// A title has no ~/.curlrc or proxy variables, and no c-ares.
	curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
	curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, DebugCallback);
}

/* A game's address as the catalog gives it, turned into one curl can fetch.
 * A Google Drive share link (drive.google.com/file/d/<id>/view, or ?id=<id>)
 * becomes Drive's direct-download address; confirm=t skips the "cannot scan
 * for viruses" page that large files otherwise stop at. Others are as given. */
std::string ResolveUrl(const std::string& url) {
	if (url.find("drive.google.com") == std::string::npos && url.find("docs.google.com") == std::string::npos) {
		return url;
	}
	std::string id;
	size_t at = url.find("/d/");
	size_t from = std::string::npos;
	if (at != std::string::npos) {
		from = at + 3;
	} else if ((at = url.find("id=")) != std::string::npos) {
		from = at + 3;
	}
	if (from != std::string::npos) {
		while (from < url.size() && (std::isalnum(static_cast<unsigned char>(url[from])) || url[from] == '-' || url[from] == '_')) {
			id += url[from++];
		}
	}
	if (id.empty()) {
		return url; // a folder or something else: let the ZIP check say so
	}
	return "https://drive.usercontent.google.com/download?id=" + id + "&export=download&confirm=t";
}

bool IsDriveUrl(const std::string& url) {
	return url.find("drive.google.com") != std::string::npos || url.find("docs.google.com") != std::string::npos;
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
		} else if (key == "upload") {
			if (!j.Number(e.upload)) return false;
		} else if (!j.Skip()) {
			return false;
		}
		j.Ws();
		if (j.i < j.s.size() && j.s[j.i] == ',') { ++j.i; continue; }
		return j.Expect('}');
	}
}

size_t CollectCallback(char* data, size_t size, size_t count, void* user);

bool IsItchUrl(const std::string& url) {
	return url.find(".itch.io/") != std::string::npos;
}

/* ---- itch.io --------------------------------------------------------------
 * A free game's page hands out files without an account, as the site's own
 * "download" button does: a page load (session cookie and CSRF token), a POST
 * for the download page, and a POST for the chosen file, which answers with a
 * signed address on itch's storage that works for a minute. */

std::string Collect(CURL* curl, const std::string& url, const std::string* post, std::string& error) {
	std::string body;
	curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CollectCallback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
	if (post) {
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post->c_str());
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(post->size()));
	} else {
		curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
	}
	CURLcode code = curl_easy_perform(curl);
	if (code != CURLE_OK) {
		error = std::string("itch.io: ") + curl_easy_strerror(code);
		return {};
	}
	return body;
}

std::string UrlEncode(CURL* curl, const std::string& s) {
	char* e = curl_easy_escape(curl, s.c_str(), static_cast<int>(s.size()));
	std::string out = e ? e : "";
	if (e) curl_free(e);
	return out;
}

std::string FindCsrf(const std::string& html) {
	const std::string marker = "csrf_token\" value=\"";
	size_t at = html.find(marker);
	if (at == std::string::npos) return {};
	at += marker.size();
	return html.substr(at, html.find('"', at) - at);
}

// A top-level string member of a JSON object, or "" (itch lists its members in
// a different order from one reply to the next, with other "url"s nested in them).
std::string JsonTopString(const std::string& text, const char* wanted) {
	Json j(text);
	if (!j.Expect('{')) return {};
	j.Ws();
	if (j.i < text.size() && text[j.i] == '}') return {};
	while (true) {
		std::string key;
		if (!j.String(key) || !j.Expect(':')) return {};
		j.Ws();
		if (key == wanted && j.i < text.size() && text[j.i] == '"') {
			std::string value;
			return j.String(value) ? value : std::string();
		}
		if (!j.Skip()) return {};
		j.Ws();
		if (j.i < text.size() && text[j.i] == ',') { ++j.i; continue; }
		return {};
	}
}

// True if a top-level member is the literal true.
bool JsonTopTrue(const std::string& text, const char* wanted) {
	Json j(text);
	if (!j.Expect('{')) return false;
	j.Ws();
	if (j.i < text.size() && text[j.i] == '}') return false;
	while (true) {
		std::string key;
		if (!j.String(key) || !j.Expect(':')) return false;
		j.Ws();
		if (key == wanted) return text.compare(j.i, 4, "true") == 0;
		if (!j.Skip()) return false;
		j.Ws();
		if (j.i < text.size() && text[j.i] == ',') { ++j.i; continue; }
		return false;
	}
}

bool ResolveItch(const Entry& entry, std::string& direct, std::string& error) {
	CURL* curl = curl_easy_init();
	if (!curl) {
		error = "Networking could not start";
		return false;
	}
	char detail[CURL_ERROR_SIZE] = "";
	SetCommonOptions(curl, entry.url);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 40L);
	curl_easy_setopt(curl, CURLOPT_COOKIEFILE, ""); // keep cookies in memory
	curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, detail);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L); // itch answers errors in JSON

	std::string base = entry.url.substr(0, entry.url.find('?'));
	while (!base.empty() && base.back() == '/') base.pop_back();

	bool ok = false;
	do {
		std::string page = Collect(curl, base, nullptr, error);
		if (page.empty()) { if (error.empty()) error = "itch.io: the game's page is empty"; break; }
		std::string csrf = FindCsrf(page);
		if (csrf.empty()) { error = "itch.io: no session token on the page"; break; }

		std::string form = "csrf_token=" + UrlEncode(curl, csrf);
		std::string reply = Collect(curl, base + "/download_url", &form, error);
		std::string download_page = JsonTopString(reply, "url");
		if (download_page.empty()) { if (error.empty()) error = "itch.io: this game cannot be downloaded without an account"; break; }

		page = Collect(curl, download_page, nullptr, error);
		if (page.empty()) { if (error.empty()) error = "itch.io: the download page is empty"; break; }
		std::string csrf2 = FindCsrf(page);
		if (!csrf2.empty()) form = "csrf_token=" + UrlEncode(curl, csrf2);

		int64_t upload = entry.upload;
		if (!upload) {
			const std::string marker = "data-upload_id=\"";
			size_t at = page.find(marker);
			if (at == std::string::npos) { error = "itch.io: the page offers no files"; break; }
			upload = std::strtoll(page.c_str() + at + marker.size(), nullptr, 10);
		}

		reply = Collect(curl, base + "/file/" + std::to_string(upload)
			+ "?source=game_download&as_props=1&after_download_lightbox=true", &form, error);
		if (JsonTopTrue(reply, "external")) {
			error = "itch.io: that file is a link to another site";
			break;
		}
		direct = JsonTopString(reply, "url");
		if (direct.empty()) { if (error.empty()) error = "itch.io: no download address was given"; break; }
		ok = true;
	} while (false);
	ReleaseCurl(curl);
	return ok;
}

/* ---- Archives other than ZIP ------------------------------------------------
 * libarchive reads 7z, RAR, LZH and more. A game is unpacked into a folder
 * (which the browser plays as it is); paths that would leave it are skipped. */

bool MakeDirs(const std::string& path) {
	for (size_t i = 1; i <= path.size(); ++i) {
		if (i == path.size() || path[i] == '/') {
			std::string part = path.substr(0, i);
			if (mkdir(part.c_str(), 0755) != 0 && errno != EEXIST) {
				return false;
			}
		}
	}
	return true;
}

std::string SafeRelative(const char* name) {
	std::string out;
	std::string part;
	std::string path = name ? name : "";
	for (char& c : path) if (c == '\\') c = '/';
	path += '/';
	for (char c : path) {
		if (c != '/') { part += c; continue; }
		if (part.empty() || part == ".") { part.clear(); continue; }
		if (part == "..") return {};
		if (!out.empty()) out += '/';
		out += part;
		part.clear();
	}
	return out;
}

bool Unpack(const std::string& file, const std::string& dir, std::string& error) {
	struct archive* in = archive_read_new();
	archive_read_support_filter_all(in);
	archive_read_support_format_all(in);
	if (archive_read_open_filename(in, file.c_str(), 64 * 1024) != ARCHIVE_OK) {
		error = std::string("Cannot open the archive: ") + archive_error_string(in);
		archive_read_free(in);
		return false;
	}
	if (!MakeDirs(dir)) {
		error = "Cannot create " + dir;
		archive_read_free(in);
		return false;
	}

	std::vector<char> buffer(256 * 1024);
	struct archive_entry* entry;
	int files = 0;
	bool ok = true;
	int r;
	while ((r = archive_read_next_header(in, &entry)) == ARCHIVE_OK || r == ARCHIVE_WARN) {
		std::string rel = SafeRelative(archive_entry_pathname(entry));
		if (rel.empty()) continue;
		std::string target = dir + "/" + rel;
		const auto type = archive_entry_filetype(entry);
		if (type == AE_IFDIR) {
			MakeDirs(target);
			continue;
		}
		if (type != AE_IFREG) continue; // no links or devices
		MakeDirs(target.substr(0, target.rfind('/')));
		std::FILE* out = std::fopen(target.c_str(), "wb");
		if (!out) {
			error = "Cannot write " + rel;
			ok = false;
			break;
		}
		ssize_t n;
		while ((n = archive_read_data(in, buffer.data(), buffer.size())) > 0) {
			if (std::fwrite(buffer.data(), 1, static_cast<size_t>(n), out) != static_cast<size_t>(n)) {
				error = "Could not save " + rel + " (disk full?)";
				ok = false;
				break;
			}
		}
		if (n < 0 && ok) {
			error = std::string("Damaged archive: ") + archive_error_string(in);
			ok = false;
		}
		std::fclose(out);
		if (!ok) break;
		++files;
	}
	if (ok && r != ARCHIVE_EOF) {
		error = std::string("Damaged archive: ") + archive_error_string(in);
		ok = false;
	}
	if (ok && files == 0) {
		error = "The archive is empty";
		ok = false;
	}
	archive_read_free(in);
	return ok;
}

// Removes a folder and what is in it (the half-unpacked game after a failure).
void RemoveTree(const std::string& dir) {
	if (DIR* d = opendir(dir.c_str())) {
		while (struct dirent* e = readdir(d)) {
			std::string name = e->d_name;
			if (name == "." || name == "..") continue;
			std::string path = dir + "/" + name;
			struct stat st;
			if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) RemoveTree(path);
			else std::remove(path.c_str());
		}
		closedir(d);
	}
	rmdir(dir.c_str());
}

// curl's own account of what it is doing (resolving, connecting, TLS), kept in
// the log: a stuck or failed download is otherwise a silent "loading".
int DebugCallback(CURL*, curl_infotype type, char* data, size_t size, void*) {
	if (type == CURLINFO_TEXT) {
		std::string line(data, size);
		while (!line.empty() && (line.back() == 10 || line.back() == 13)) line.pop_back();
		Output::Debug("curl: {}", line);
	}
	return 0;
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
		Output::Debug("Library: catalog request done ({}), {} bytes", static_cast<int>(code), body.size());
		ReleaseCurl(curl);
		Output::Debug("Library: catalog connection closed");
		if (code != CURLE_OK) {
			error = Describe(code, detail);
		} else {
			ok = ParseCatalog(body, entries, error);
			Output::Debug("Library: catalog parsed ({}), {} games", ok ? "ok" : "failed", entries.size());
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
	Output::Debug("Library: download of {} started ({})", entry.id, entry.url);
	const std::string base = std::string(kGamesDir) + "/" + entry.id;
	const std::string part = base + ".download";
	std::string error;
	bool ok = false;
	bool unpacked = false;

	std::string url = ResolveUrl(entry.url);
	if (IsItchUrl(entry.url) && !ResolveItch(entry, url, error)) {
		url.clear();
	}

	std::FILE* file = url.empty() ? nullptr : std::fopen(part.c_str(), "wb");
	if (!url.empty() && !file) {
		error = "Cannot write to " + std::string(kGamesDir);
	} else if (file && !EnsureCurl()) {
		error = "Networking could not start";
		std::fclose(file);
	} else if (file) {
		CURL* curl = curl_easy_init();
		char detail[CURL_ERROR_SIZE] = "";
		if (!curl) {
			error = "Networking could not start";
			std::fclose(file);
		} else {
			SetCommonOptions(curl, url);
			curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, FileWriteCallback);
			curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
			curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, detail);
			curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
			curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
			CURLcode code = curl_easy_perform(curl);
			ReleaseCurl(curl);
			const bool flushed = std::fflush(file) == 0;
			std::fclose(file);

			if (code == CURLE_ABORTED_BY_CALLBACK) {
				error = "Cancelled";
			} else if (code != CURLE_OK) {
				error = Describe(code, detail);
			} else if (!flushed) {
				error = "Could not save the file (disk full?)";
			} else {
				char magic[8] = {};
				std::FILE* check = std::fopen(part.c_str(), "rb");
				size_t got = check ? std::fread(magic, 1, sizeof magic, check) : 0;
				if (check) std::fclose(check);
				const bool zip = got >= 4 && std::memcmp(magic, "PK", 2) == 0;
				const bool html = got >= 5 && (std::memcmp(magic, "<!DOC", 5) == 0 || std::memcmp(magic, "<html", 5) == 0
					|| std::memcmp(magic, "<HTML", 5) == 0);
				if (html || got < 4) {
					error = IsDriveUrl(entry.url) ? "Google Drive did not give the file (private, or over its download limit)"
						: "The address did not give a game file";
				} else if (zip) {
					if (std::rename(part.c_str(), (base + ".zip").c_str()) != 0) error = "Could not save the game";
					else ok = true;
				} else {
					// 7z, RAR, LZH, tar...: unpack into a folder of the game's id
					{
						std::lock_guard<std::mutex> guard(lock);
						download_status.unpacking = true;
					}
					const std::string tmp = base + ".unpack";
					RemoveTree(tmp);
					if (!Unpack(part, tmp, error)) {
						RemoveTree(tmp);
					} else if (std::rename(tmp.c_str(), base.c_str()) != 0) {
						error = "Could not save the game";
						RemoveTree(tmp);
					} else {
						ok = true;
						unpacked = true;
					}
				}
			}
		}
	}
	(void) unpacked;
	std::remove(part.c_str()); // the ZIP was renamed away; anything else is spent

	std::lock_guard<std::mutex> guard(lock);
	download_status.unpacking = false;
	if (ok) {
		download_status.state = State::Done;
		download_status.error.clear();
	} else {
		download_status.state = State::Failed;
		download_status.error = error.empty() ? "The download failed" : error;
		Output::Debug("Library: download of {} failed: {}", entry.id, download_status.error);
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
	// main.cpp normally creates this at boot, but keep downloads self-contained
	// so a catalog install still works if the library is started independently.
	if (!MakeDirs(kGamesDir)) {
		std::lock_guard<std::mutex> guard(lock);
		download_status = Status();
		download_status.state = State::Failed;
		download_status.error = "Cannot create " + std::string(kGamesDir);
		return;
	}

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
