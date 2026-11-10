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

#include "text_input.h"

#include "baseui.h"
#include "output.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

/* As ps5-payload-dev's SDL port declares them. The text is UTF-16. */
struct ImeDialogParam {
	int userId;
	int type;                   /* 1: basic Latin */
	uint64_t supportedLanguages;
	int enterLabel;
	int inputMethod;
	void* filter;
	uint32_t option;
	uint32_t maxTextLength;
	uint16_t* inputTextBuffer;
	float posx, posy;
	int halign, valign;         /* 1, 1: centred */
	const uint16_t* placeholder;
	const uint16_t* title;
	int8_t reserved[16];
};

struct ImeDialogResult {
	int outcome;                /* 0 OK, 1 cancelled, 2 aborted */
	int8_t reserved[12];
};

/* libSceImeDialog is not given to a title at start: the module is loaded at
 * run time, and its functions are called directly as linked imports (the
 * console binds an import at its first call, from the modules loaded by then;
 * never take their address early). Same approach as the NetHack port. */
extern "C" {
int sceKernelLoadStartModule(const char* path, size_t argc, const void* argv, uint32_t flags, void* option, int* result);
const char* sceKernelGetFsSandboxRandomWord(void);
int sceUserServiceGetForegroundUser(int* userId);
int sceKernelUsleep(unsigned int microseconds);
int sceImeDialogInit(const ImeDialogParam* param, void* extended);
int sceImeDialogGetStatus(void);
int sceImeDialogGetResult(ImeDialogResult* result);
int sceImeDialogTerm(void);
}

namespace {

bool LoadIme() {
	static int state = 0; // 0 not tried, 1 loaded, -1 unavailable
	if (state != 0) {
		return state > 0;
	}
	state = -1;
	const char* word = sceKernelGetFsSandboxRandomWord();
	char path[128];
	std::snprintf(path, sizeof path, "/%s/common/lib/libSceImeDialog.sprx", word ? word : "system");
	int handle = sceKernelLoadStartModule(path, 0, nullptr, 0, nullptr, nullptr);
	if (handle < 0) {
		Output::Debug("Keyboard: cannot load {} (0x{:08x})", path, static_cast<unsigned>(handle));
		return false;
	}
	state = 1;
	return true;
}

}

bool Ps5::TextInput(const std::string& title_text, std::string& text) {
	static uint16_t title[128], buffer[256];
	if (!LoadIme()) {
		return false;
	}
	std::memset(title, 0, sizeof title);
	std::memset(buffer, 0, sizeof buffer);
	for (size_t i = 0; i < title_text.size() && i < 127; ++i) {
		title[i] = static_cast<unsigned char>(title_text[i]);
	}
	for (size_t i = 0; i < text.size() && i < 255; ++i) {
		buffer[i] = static_cast<unsigned char>(text[i]);
	}

	ImeDialogParam param;
	std::memset(&param, 0, sizeof param);
	int user = -1;
	sceUserServiceGetForegroundUser(&user);
	param.userId = user;
	param.type = 1;
	param.maxTextLength = 40;
	param.inputTextBuffer = buffer;
	param.posx = 1920 / 2.0f;
	param.posy = 1080 / 2.0f;
	param.halign = param.valign = 1;
	param.title = title;

	int rc = sceImeDialogInit(&param, nullptr);
	if (rc < 0) {
		Output::Debug("Keyboard: cannot open (0x{:08x})", static_cast<unsigned>(rc));
		return false;
	}
	int status;
	while ((status = sceImeDialogGetStatus()) == 1) {
		if (DisplayUi) {
			DisplayUi->UpdateDisplay();
		}
		sceKernelUsleep(16000);
	}
	ImeDialogResult result;
	std::memset(&result, 0, sizeof result);
	rc = status == 2 ? sceImeDialogGetResult(&result) : -1;
	sceImeDialogTerm();
	if (rc < 0 || result.outcome != 0) {
		return false;
	}
	text.clear();
	for (int i = 0; buffer[i] && i < 255; ++i) {
		text += buffer[i] < 128 ? static_cast<char>(buffer[i]) : '?';
	}
	return true;
}
