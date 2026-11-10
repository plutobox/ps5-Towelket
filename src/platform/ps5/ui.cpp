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

#include "ui.h"

#include "audio.h"
#include "bitmap.h"
#include "color.h"
#include "game_config.h"
#include "input.h"
#include "keys.h"
#include "output.h"
#include "player.h"
#include "scene.h"

#include <SDL.h>
#include <SDL_ttf.h>
#include <GL/glcorearb.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern "C" int sceKernelUsleep(unsigned int microseconds);

namespace {

/* ---- OpenGL ------------------------------------------------------------ */

PFNGLGENTEXTURESPROC gl_GenTextures;
PFNGLDELETETEXTURESPROC gl_DeleteTextures;
PFNGLBINDTEXTUREPROC gl_BindTexture;
PFNGLTEXPARAMETERIPROC gl_TexParameteri;
PFNGLTEXIMAGE2DPROC gl_TexImage2D;
PFNGLTEXSUBIMAGE2DPROC gl_TexSubImage2D;
PFNGLPIXELSTOREIPROC gl_PixelStorei;
PFNGLCREATESHADERPROC gl_CreateShader;
PFNGLSHADERSOURCEPROC gl_ShaderSource;
PFNGLCOMPILESHADERPROC gl_CompileShader;
PFNGLGETSHADERIVPROC gl_GetShaderiv;
PFNGLGETSHADERINFOLOGPROC gl_GetShaderInfoLog;
PFNGLCREATEPROGRAMPROC gl_CreateProgram;
PFNGLATTACHSHADERPROC gl_AttachShader;
PFNGLLINKPROGRAMPROC gl_LinkProgram;
PFNGLGETPROGRAMIVPROC gl_GetProgramiv;
PFNGLUSEPROGRAMPROC gl_UseProgram;
PFNGLGETUNIFORMLOCATIONPROC gl_GetUniformLocation;
PFNGLUNIFORM1IPROC gl_Uniform1i;
PFNGLUNIFORM4FPROC gl_Uniform4f;
PFNGLGENVERTEXARRAYSPROC gl_GenVertexArrays;
PFNGLBINDVERTEXARRAYPROC gl_BindVertexArray;
PFNGLDRAWARRAYSPROC gl_DrawArrays;
PFNGLVIEWPORTPROC gl_Viewport;
PFNGLACTIVETEXTUREPROC gl_ActiveTexture;
PFNGLCLEARCOLORPROC gl_ClearColor;
PFNGLCLEARPROC gl_Clear;

template <typename T>
bool Load(T& fn, const char* name) {
	fn = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
	return fn != nullptr;
}

/* A rectangle (NDC corners in `rect`) as a strip of two triangles; row 0 of
 * the texture is the top. */
const char* const kVertexSource =
	"#version 330 core\n"
	"uniform vec4 rect;\n"
	"out vec2 uv;\n"
	"void main() {\n"
	"    vec2 p = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));\n"
	"    uv = vec2(p.x, 1.0 - p.y);\n"
	"    gl_Position = vec4(mix(rect.xy, rect.zw, p), 0.0, 1.0);\n"
	"}\n";
const char* const kFragmentSource =
	"#version 330 core\n"
	"in vec2 uv;\n"
	"uniform sampler2D surface;\n"
	"out vec4 colour;\n"
	"void main() { colour = vec4(texture(surface, uv).rgb, 1.0); }\n";

GLuint CompileShader(GLenum type, const char* source) {
	GLuint shader = gl_CreateShader(type);
	GLint ok = 0;
	gl_ShaderSource(shader, 1, &source, nullptr);
	gl_CompileShader(shader);
	gl_GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512] = "";
		gl_GetShaderInfoLog(shader, sizeof log, nullptr, log);
		Output::Warning("Shader compile failed: {}", log);
	}
	return shader;
}

GLuint MakeTexture(int w, int h, GLint filter) {
	GLuint t;
	gl_GenTextures(1, &t);
	gl_BindTexture(GL_TEXTURE_2D, t);
	gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
	gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
	gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	gl_TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	gl_TexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	return t;
}

/* ---- Presenter state --------------------------------------------------- */

SDL_Window* window;
int screen_w, screen_h;     // the output, in pixels
GLuint game_texture, bar_texture, program, vertex_array;
GLint rect_uniform;
int game_w, game_h;         // the game's frame
int bar_h;                  // height of the hint bar
float game_rect[4];         // where the frame is drawn, in NDC
bool stretch;               // fill the area above the bar, ignoring the aspect

SDL_Surface* bar;           // the hint bar, RGBA
bool bar_dirty;
int hint_context = -1;

bool showing_message;       // a full-screen message is up: the game is over

/* ---- Text and symbols for the hint bar --------------------------------- */

struct Rgb { uint8_t r, g, b; };

const Rgb kBarBackground = {12, 14, 24};
const Rgb kBarText = {220, 220, 230};
const Rgb kBarDim = {150, 150, 165};
const Rgb kRule = {52, 58, 88};
const Rgb kNamed = {190, 190, 205};
const Rgb kSymbolColour[] = {
	{120, 160, 255}, {255, 110, 110}, {240, 130, 210}, {70, 220, 180},
};

TTF_Font* font;
int cell_h;                 // the height of a glyph cell, and of a button symbol

void BlendPixel(SDL_Surface* s, int x, int y, Rgb c, float alpha) {
	if (x < 0 || y < 0 || x >= s->w || y >= s->h || alpha <= 0) {
		return;
	}
	float a = alpha > 1 ? 1 : alpha;
	uint8_t* p = static_cast<uint8_t*>(s->pixels) + y * s->pitch + x * 4;
	p[0] = static_cast<uint8_t>(p[0] + (c.r - p[0]) * a);
	p[1] = static_cast<uint8_t>(p[1] + (c.g - p[1]) * a);
	p[2] = static_cast<uint8_t>(p[2] + (c.b - p[2]) * a);
	p[3] = 255;
}

void Fill(SDL_Surface* s, int x, int y, int w, int h, Rgb c) {
	SDL_Rect r = {x, y, w, h};
	SDL_FillRect(s, &r, SDL_MapRGB(s->format, c.r, c.g, c.b));
}

int TextWidth(const char* s) {
	int w = 0, h = 0;
	if (!font || TTF_SizeUTF8(font, s, &w, &h) != 0) {
		return static_cast<int>(strlen(s)) * cell_h / 2;
	}
	return w;
}

int DrawText(SDL_Surface* dst, int x, int y, const char* s, Rgb c) {
	if (!font || !*s) {
		return x + TextWidth(s);
	}
	SDL_Color sc = {c.r, c.g, c.b, 255};
	SDL_Surface* text = TTF_RenderUTF8_Blended(font, s, sc);
	if (!text) {
		return x + TextWidth(s);
	}
	for (int py = 0; py < text->h; ++py) {
		const uint32_t* line = reinterpret_cast<const uint32_t*>(
			static_cast<uint8_t*>(text->pixels) + py * text->pitch);
		for (int px = 0; px < text->w; ++px) {
			BlendPixel(dst, x + px, y + py, c, static_cast<float>(line[px] >> 24) / 255.0f);
		}
	}
	x += text->w;
	SDL_FreeSurface(text);
	return x;
}

enum Symbol { S_CROSS, S_CIRCLE, S_SQUARE, S_TRIANGLE };

float SegmentDistance(float px, float py, float ax, float ay, float bx, float by) {
	float vx = bx - ax, vy = by - ay;
	float t = std::clamp(((px - ax) * vx + (py - ay) * vy) / (vx * vx + vy * vy), 0.0f, 1.0f);
	float dx = px - (ax + vx * t), dy = py - (ay + vy * t);
	return std::sqrt(dx * dx + dy * dy);
}

/* The DualSense's face buttons, as outlines in their PlayStation colours,
 * inside a square box of cell_h pixels. */
void DrawSymbol(int x, int y, Symbol symbol, Rgb colour) {
	const float size = static_cast<float>(cell_h), centre = size / 2, stroke = 2.6f;
	const float r = size * 0.36f;

	for (int py = 0; py < cell_h; ++py) {
		for (int px = 0; px < cell_h; ++px) {
			float cover = 0;
			for (int sy = 0; sy < 3; ++sy) {
				for (int sx = 0; sx < 3; ++sx) {
					float fx = px + (sx + 0.5f) / 3 - centre;
					float fy = py + (sy + 0.5f) / 3 - centre;
					float d;
					switch (symbol) {
					case S_CIRCLE:
						d = std::fabs(std::sqrt(fx * fx + fy * fy) - r);
						break;
					case S_SQUARE: {
						float qx = std::fabs(fx) - r * 0.92f;
						float qy = std::fabs(fy) - r * 0.92f;
						float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
						float inside = qx > qy ? qx : qy;
						d = std::fabs(std::sqrt(ox * ox + oy * oy) + (inside < 0 ? inside : 0));
						break;
					}
					case S_TRIANGLE: {
						float top = -r * 1.05f, base = r * 0.78f, half = r * 1.05f;
						float d1 = SegmentDistance(fx, fy, 0, top, -half, base);
						float d2 = SegmentDistance(fx, fy, 0, top, half, base);
						float d3 = SegmentDistance(fx, fy, -half, base, half, base);
						d = std::min({d1, d2, d3});
						break;
					}
					default: {
						float d1 = SegmentDistance(fx, fy, -r, -r, r, r);
						float d2 = SegmentDistance(fx, fy, -r, r, r, -r);
						d = std::min(d1, d2);
						break;
					}
					}
					if (d < stroke / 2) {
						cover += 1;
					}
				}
			}
			BlendPixel(bar, x + px, y + py, colour, cover / 9.0f);
		}
	}
}

/* A button named in words (L1, D-pad, Options): a rounded outline round it. */
int DrawBadge(int x, int y, const char* label, Rgb colour) {
	int w = TextWidth(label) + 14, h = cell_h + 2;
	for (int i = 3; i < w - 3; ++i) {
		BlendPixel(bar, x + i, y - 1, colour, 0.9f);
		BlendPixel(bar, x + i, y + h - 2, colour, 0.9f);
	}
	for (int i = 3; i < h - 4; ++i) {
		BlendPixel(bar, x, y + i, colour, 0.9f);
		BlendPixel(bar, x + 1, y + i, colour, 0.9f);
		BlendPixel(bar, x + w - 1, y + i, colour, 0.9f);
		BlendPixel(bar, x + w - 2, y + i, colour, 0.9f);
	}
	BlendPixel(bar, x + 1, y, colour, 0.9f);
	BlendPixel(bar, x + 2, y - 1, colour, 0.9f);
	BlendPixel(bar, x + w - 2, y, colour, 0.9f);
	BlendPixel(bar, x + w - 3, y - 1, colour, 0.9f);
	BlendPixel(bar, x + 1, y + h - 3, colour, 0.9f);
	BlendPixel(bar, x + 2, y + h - 2, colour, 0.9f);
	BlendPixel(bar, x + w - 2, y + h - 3, colour, 0.9f);
	BlendPixel(bar, x + w - 3, y + h - 2, colour, 0.9f);
	DrawText(bar, x + 7, y - 1, label, colour);
	return x + w;
}

struct Hint {
	int symbol;             // a Symbol, or -1 for a named button
	const char* button;     // the named button
	const char* action;
};

#define SYM(s) (s), nullptr
#define NAMED(n) -1, (n)
#define END_HINTS {0, nullptr, nullptr}

const Hint kMapHints[] = {
	{SYM(S_CROSS), "Talk / Check"}, {SYM(S_CIRCLE), "Menu"},
	{SYM(S_SQUARE), "Run"}, {SYM(S_TRIANGLE), "Menu"},
	{NAMED("R2"), "Fast-forward"},
	{NAMED("Options"), "Settings"}, END_HINTS
};
const Hint kBattleHints[] = {
	{SYM(S_CROSS), "Confirm"}, {SYM(S_CIRCLE), "Back"},
	{NAMED("R2"), "Fast-forward"}, {NAMED("Options"), "Settings"}, END_HINTS
};
const Hint kMenuHints[] = {
	{NAMED("D-pad"), "Choose"}, {SYM(S_CROSS), "Select"},
	{SYM(S_CIRCLE), "Back"}, END_HINTS
};
const Hint kNameHints[] = {
	{NAMED("D-pad"), "Choose"}, {SYM(S_CROSS), "Enter"},
	{SYM(S_CIRCLE), "Delete"}, END_HINTS
};
const Hint kBrowserHints[] = {
	{NAMED("D-pad"), "Choose"}, {SYM(S_CROSS), "Play"},
	{NAMED("Options"), "Settings"}, END_HINTS
};
const Hint kTitleHints[] = {
	{NAMED("D-pad"), "Choose"}, {SYM(S_CROSS), "Select"},
	{NAMED("Options"), "Settings"}, END_HINTS
};

int HintWidth(const Hint& h) {
	int w = h.symbol >= 0 ? cell_h : TextWidth(h.button) + 14;
	return w + 12 + TextWidth(h.action);
}

const Hint* HintsFor(int context) {
	switch (context) {
	case Scene::Map: return kMapHints;
	case Scene::Battle: return kBattleHints;
	case Scene::Name: return kNameHints;
	case Scene::GameBrowser: return kBrowserHints;
	case Scene::Title: return kTitleHints;
	default: return kMenuHints;
	}
}

void DrawHints(int context) {
	const Hint* set = HintsFor(context);
	int count = 0, total = 0;

	Fill(bar, 0, 0, bar->w, bar->h, kBarBackground);
	Fill(bar, 0, 0, bar->w, 2, kRule);
	for (const Hint* h = set; h->action; ++h, ++count) {
		total += HintWidth(*h);
	}
	int gap = 40;
	if (count > 1 && total + gap * (count - 1) > bar->w - 64) {
		gap = std::max(14, (bar->w - 64 - total) / (count - 1));
	}
	total += gap * (count - 1);
	int x = (bar->w - total) / 2;
	int y = (bar->h - cell_h) / 2 + 1;
	for (const Hint* h = set; h->action; ++h) {
		if (h != set) {
			x += gap;
		}
		if (h->symbol >= 0) {
			DrawSymbol(x, y, static_cast<Symbol>(h->symbol), kSymbolColour[h->symbol]);
			x += cell_h;
		} else {
			x = DrawBadge(x, y, h->button, kNamed);
		}
		x = DrawText(bar, x + 12, y - 1, h->action, kBarText);
	}
	bar_dirty = true;
}

int CurrentContext() {
	return Scene::instance ? static_cast<int>(Scene::instance->type) : static_cast<int>(Scene::Null);
}

/* ---- The game's frame --------------------------------------------------- */

void LayoutFrame() {
	int area_h = screen_h - bar_h;
	float fw, fh;
	if (stretch) {
		fw = 1.0f;
		fh = static_cast<float>(area_h) / screen_h;
	} else {
		// Whole-number scale: every game pixel is the same size, never blurred.
		float scale = std::min(static_cast<float>(screen_w) / game_w, static_cast<float>(area_h) / game_h);
		if (scale >= 1.0f) {
			scale = std::floor(scale);
		}
		fw = game_w * scale / screen_w;
		fh = game_h * scale / screen_h;
	}
	// NDC: y up; the bar takes the bottom bar_h pixels
	game_rect[0] = -fw;
	game_rect[2] = fw;
	float top = 1.0f, bottom = 1.0f - 2.0f * area_h / screen_h;
	float mid = (top + bottom) / 2;
	game_rect[1] = mid - fh;
	game_rect[3] = mid + fh;
}

void SetupFrame(int w, int h) {
	game_w = w;
	game_h = h;
	gl_ActiveTexture(GL_TEXTURE0);
	if (game_texture) {
		gl_DeleteTextures(1, &game_texture);
	}
	game_texture = MakeTexture(w, h, GL_NEAREST);
	LayoutFrame();
}

void Present() {
	gl_Clear(GL_COLOR_BUFFER_BIT);
	gl_ActiveTexture(GL_TEXTURE0);
	if (game_texture) {
		gl_BindTexture(GL_TEXTURE_2D, game_texture);
		gl_Uniform4f(rect_uniform, game_rect[0], game_rect[1], game_rect[2], game_rect[3]);
		gl_DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	}
	gl_BindTexture(GL_TEXTURE_2D, bar_texture);
	if (bar_dirty) {
		gl_PixelStorei(GL_UNPACK_ALIGNMENT, 4);
		gl_TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, screen_w, bar_h, GL_RGBA, GL_UNSIGNED_BYTE, bar->pixels);
		bar_dirty = false;
	}
	gl_Uniform4f(rect_uniform, -1.0f, -1.0f, 1.0f, -1.0f + 2.0f * bar_h / screen_h);
	gl_DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	SDL_GL_SwapWindow(window);
}

bool SetupPresenter() {
	SDL_DisplayMode mode;
	if (SDL_GetDesktopDisplayMode(0, &mode) < 0) {
		return false;
	}
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	// ps5-opengl's driver supports one fixed window the size of the output.
	window = SDL_CreateWindow(GAME_TITLE, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		mode.w, mode.h, SDL_WINDOW_OPENGL);
	SDL_GLContext context;
	if (!window || !(context = SDL_GL_CreateContext(window)) || SDL_GL_MakeCurrent(window, context) < 0) {
		return false;
	}
	SDL_GL_SetSwapInterval(1);
	screen_w = mode.w;
	screen_h = mode.h;

	if (!(Load(gl_GenTextures, "glGenTextures") && Load(gl_DeleteTextures, "glDeleteTextures")
		&& Load(gl_BindTexture, "glBindTexture") && Load(gl_TexParameteri, "glTexParameteri")
		&& Load(gl_TexImage2D, "glTexImage2D") && Load(gl_TexSubImage2D, "glTexSubImage2D")
		&& Load(gl_PixelStorei, "glPixelStorei") && Load(gl_CreateShader, "glCreateShader")
		&& Load(gl_ShaderSource, "glShaderSource") && Load(gl_CompileShader, "glCompileShader")
		&& Load(gl_GetShaderiv, "glGetShaderiv") && Load(gl_GetShaderInfoLog, "glGetShaderInfoLog")
		&& Load(gl_CreateProgram, "glCreateProgram") && Load(gl_AttachShader, "glAttachShader")
		&& Load(gl_LinkProgram, "glLinkProgram") && Load(gl_GetProgramiv, "glGetProgramiv")
		&& Load(gl_UseProgram, "glUseProgram") && Load(gl_GetUniformLocation, "glGetUniformLocation")
		&& Load(gl_Uniform1i, "glUniform1i") && Load(gl_Uniform4f, "glUniform4f")
		&& Load(gl_GenVertexArrays, "glGenVertexArrays") && Load(gl_BindVertexArray, "glBindVertexArray")
		&& Load(gl_DrawArrays, "glDrawArrays") && Load(gl_Viewport, "glViewport")
		&& Load(gl_ActiveTexture, "glActiveTexture") && Load(gl_ClearColor, "glClearColor")
		&& Load(gl_Clear, "glClear"))) {
		SDL_SetError("missing OpenGL function");
		return false;
	}

	// The bar is 56 pixels on a 1080-line screen.
	bar_h = (screen_h * 56 + 540) / 1080;
	bar = SDL_CreateRGBSurfaceWithFormat(0, screen_w, bar_h, 32, SDL_PIXELFORMAT_ABGR8888);
	if (!bar) {
		return false;
	}
	Fill(bar, 0, 0, screen_w, bar_h, kBarBackground);

	gl_ActiveTexture(GL_TEXTURE0);
	bar_texture = MakeTexture(screen_w, bar_h, GL_LINEAR);

	program = gl_CreateProgram();
	gl_AttachShader(program, CompileShader(GL_VERTEX_SHADER, kVertexSource));
	gl_AttachShader(program, CompileShader(GL_FRAGMENT_SHADER, kFragmentSource));
	gl_LinkProgram(program);
	GLint linked = 0;
	gl_GetProgramiv(program, GL_LINK_STATUS, &linked);
	if (!linked) {
		SDL_SetError("shader program failed to link");
		return false;
	}
	gl_UseProgram(program);
	gl_Uniform1i(gl_GetUniformLocation(program, "surface"), 0);
	rect_uniform = gl_GetUniformLocation(program, "rect");
	gl_GenVertexArrays(1, &vertex_array);
	gl_BindVertexArray(vertex_array);
	gl_Viewport(0, 0, screen_w, screen_h);
	gl_ClearColor(0, 0, 0, 1);

	cell_h = bar_h * 27 / 56;
	if (TTF_Init() == 0) {
		font = TTF_OpenFont("/app0/DejaVuSans-Bold.ttf", cell_h * 85 / 100);
	}
	if (!font) {
		Output::Warning("No hint font: {}", TTF_GetError());
	}
	return true;
}

/* ---- DualSense ---------------------------------------------------------- */

// Button indices of SDL's PS5 joystick driver.
enum Button {
	B_CROSS = 0, B_CIRCLE, B_SQUARE, B_TRIANGLE, B_TOUCHPAD, B_CREATE,
	B_OPTIONS, B_L3, B_R3, B_L1, B_R1, B_UP, B_DOWN, B_LEFT, B_RIGHT,
	B_L2, B_R2, B_COUNT
};

SDL_Joystick* joystick;

float Normalize(int value) {
	const int dead = 6000;
	if (std::abs(value) < dead) {
		return 0.0f;
	}
	return std::clamp(static_cast<float>(value) / 32767.0f, -1.0f, 1.0f);
}

void OpenFirstJoystick() {
	if (joystick && SDL_JoystickGetAttached(joystick)) {
		return;
	}
	joystick = nullptr;
	for (int i = 0; i < SDL_NumJoysticks(); ++i) {
		joystick = SDL_JoystickOpen(i);
		if (joystick) {
			Output::Debug("Controller {}: {} ({} buttons, {} axes)", i, SDL_JoystickName(joystick),
				SDL_JoystickNumButtons(joystick), SDL_JoystickNumAxes(joystick));
			break;
		}
	}
}

/* ---- Full-screen messages ----------------------------------------------- */

/* Greedy word wrap of `text` to `width` pixels. */
std::vector<std::string> Wrap(const std::string& text, int width) {
	std::vector<std::string> lines;
	size_t pos = 0;
	while (pos <= text.size()) {
		size_t end = text.find('\n', pos);
		std::string paragraph = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
		std::string line;
		size_t p = 0;
		while (p < paragraph.size()) {
			size_t sp = paragraph.find(' ', p);
			std::string word = paragraph.substr(p, sp == std::string::npos ? std::string::npos : sp - p);
			std::string trial = line.empty() ? word : line + " " + word;
			if (!line.empty() && TextWidth(trial.c_str()) > width) {
				lines.push_back(line);
				line = word;
			} else {
				line = trial;
			}
			if (sp == std::string::npos) {
				break;
			}
			p = sp + 1;
		}
		lines.push_back(line);
		if (end == std::string::npos) {
			break;
		}
		pos = end + 1;
	}
	return lines;
}

/* Draws a message over the whole screen and presents it. */
void ShowMessage(const std::string& headline, const std::string& detail, const std::string& footer) {
	if (!bar || !font) {
		return;
	}
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, screen_w, screen_h, 32, SDL_PIXELFORMAT_ABGR8888);
	if (!s) {
		return;
	}
	Fill(s, 0, 0, screen_w, screen_h, kBarBackground);
	const int margin = screen_w / 10;
	const int line_h = TTF_FontLineSkip(font) + 6;
	auto lines = Wrap(detail, screen_w - 2 * margin);
	int total = line_h * (2 + static_cast<int>(lines.size()));
	int y = std::max(margin / 2, (screen_h - total) / 2);

	DrawText(s, margin, y, headline.c_str(), kBarText);
	y += line_h * 2;
	for (const auto& line : lines) {
		DrawText(s, margin, y, line.c_str(), kBarDim);
		y += line_h;
	}
	DrawText(s, margin, screen_h - margin, footer.c_str(), kBarText);

	gl_ActiveTexture(GL_TEXTURE0);
	GLuint t = MakeTexture(screen_w, screen_h, GL_LINEAR);
	gl_BindTexture(GL_TEXTURE_2D, t);
	gl_PixelStorei(GL_UNPACK_ALIGNMENT, 4);
	gl_TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, screen_w, screen_h, GL_RGBA, GL_UNSIGNED_BYTE, s->pixels);
	gl_Clear(GL_COLOR_BUFFER_BIT);
	gl_Uniform4f(rect_uniform, -1.0f, -1.0f, 1.0f, 1.0f);
	gl_DrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	SDL_GL_SwapWindow(window);
	gl_DeleteTextures(1, &t);
	SDL_FreeSurface(s);
	showing_message = true;
}

} // namespace

/* ---- Ps5Ui ---------------------------------------------------------------- */

Ps5Ui::Ps5Ui(long width, long height, const Game_Config& cfg) : BaseUi(cfg) {
	SetIsFullscreen(true);
	SetFrameRateSynchronized(true);

	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_TIMER) != 0 || !SetupPresenter()) {
		Output::Error("Display setup failed: {}", SDL_GetError());
		return;
	}
	stretch = vcfg.stretch.Get();

	current_display_mode.width = static_cast<int>(width);
	current_display_mode.height = static_cast<int>(height);
	current_display_mode.bpp = 32;
	current_display_mode.vsync = true;
	current_display_mode.effective = true;

	// RGBA in memory, as OpenGL takes it.
	const DynamicFormat format(32, 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000, PF::NoAlpha);
	Bitmap::SetFormat(Bitmap::ChooseFormat(format));
	main_surface = Bitmap::Create(static_cast<int>(width), static_cast<int>(height), true, 32);
	SetupFrame(static_cast<int>(width), static_cast<int>(height));

	SDL_JoystickEventState(SDL_ENABLE);
	OpenFirstJoystick();

#ifdef SUPPORT_AUDIO
	audio_ = std::make_unique<Ps5Audio>(cfg.audio);
#endif
}

Ps5Ui::~Ps5Ui() {
#ifdef SUPPORT_AUDIO
	audio_.reset();
#endif
}

bool Ps5Ui::vChangeDisplaySurfaceResolution(int new_width, int new_height) {
	BitmapRef surface = Bitmap::Create(new_width, new_height, Color(0, 0, 0, 255));
	if (!surface) {
		Output::Warning("ChangeDisplaySurfaceResolution Bitmap::Create failed");
		return false;
	}
	main_surface = surface;
	current_display_mode.width = new_width;
	current_display_mode.height = new_height;
	SetupFrame(new_width, new_height);
	return true;
}

namespace {
// Test hook: keys typed into /data/easyrpg-input.txt are pressed one after
// another (x Cross, o Circle, s Square, t Triangle, u d l r d-pad, p Options).
// Checked every 20 frames; the file is deleted once read.
std::string injected;
int inject_phase = 0;
int inject_frame = 0;
int injected_key = -1;

void PollInjected() {
	if (injected.empty() && ++inject_frame % 20 == 0) {
		if (FILE* f = std::fopen("/data/easyrpg-input.txt", "rb")) {
			char buf[256];
			size_t n = std::fread(buf, 1, sizeof(buf), f);
			std::fclose(f);
			std::remove("/data/easyrpg-input.txt");
			injected.assign(buf, n);
		}
	}
	injected_key = -1;
	if (injected.empty()) {
		return;
	}
	// 5 frames down, 5 frames up per key
	int step = inject_phase++ % 10;
	char c = injected[0];
	if (step < 5) {
		switch (c) {
			case 'x': injected_key = Input::Keys::JOY_A; break;
			case 'o': injected_key = Input::Keys::JOY_B; break;
			case 's': injected_key = Input::Keys::JOY_X; break;
			case 't': injected_key = Input::Keys::JOY_Y; break;
			case 'u': injected_key = Input::Keys::JOY_DPAD_UP; break;
			case 'd': injected_key = Input::Keys::JOY_DPAD_DOWN; break;
			case 'l': injected_key = Input::Keys::JOY_DPAD_LEFT; break;
			case 'r': injected_key = Input::Keys::JOY_DPAD_RIGHT; break;
			case 'p': injected_key = Input::Keys::JOY_START; break;
			default: break;
		}
	}
	if (step == 9) {
		injected.erase(0, 1);
	}
}
}

bool Ps5Ui::ProcessEvents() {
	SDL_Event e;
	while (SDL_PollEvent(&e)) {
		switch (e.type) {
		case SDL_JOYDEVICEADDED:
		case SDL_JOYDEVICEREMOVED:
			OpenFirstJoystick();
			break;
		case SDL_QUIT:
			return false;
		default:
			break;
		}
	}

	PollInjected();

	if (!joystick) {
		if (injected_key >= 0) {
			keys[injected_key] = true;
		}
		return true;
	}

	auto down = [&](int b) { return SDL_JoystickGetButton(joystick, b) != 0; };
	keys[Input::Keys::JOY_DPAD_UP] = down(B_UP);
	keys[Input::Keys::JOY_DPAD_DOWN] = down(B_DOWN);
	keys[Input::Keys::JOY_DPAD_LEFT] = down(B_LEFT);
	keys[Input::Keys::JOY_DPAD_RIGHT] = down(B_RIGHT);
	keys[Input::Keys::JOY_A] = down(B_CROSS);
	keys[Input::Keys::JOY_B] = down(B_CIRCLE);
	keys[Input::Keys::JOY_X] = down(B_SQUARE);
	keys[Input::Keys::JOY_Y] = down(B_TRIANGLE);
	keys[Input::Keys::JOY_LSTICK] = down(B_L3);
	keys[Input::Keys::JOY_RSTICK] = down(B_R3);
	keys[Input::Keys::JOY_SHOULDER_LEFT] = down(B_L1);
	keys[Input::Keys::JOY_SHOULDER_RIGHT] = down(B_R1);
	keys[Input::Keys::JOY_BACK] = down(B_CREATE);
	keys[Input::Keys::JOY_START] = down(B_OPTIONS);
	keys[Input::Keys::JOY_TOUCH] = down(B_TOUCHPAD);
	if (injected_key >= 0) {
		keys[injected_key] = true;
	}

	analog_input.primary.x = Normalize(SDL_JoystickGetAxis(joystick, 0));
	analog_input.primary.y = Normalize(SDL_JoystickGetAxis(joystick, 1));
	analog_input.secondary.x = Normalize(SDL_JoystickGetAxis(joystick, 2));
	analog_input.secondary.y = Normalize(SDL_JoystickGetAxis(joystick, 3));
	analog_input.trigger_left = down(B_L2) ? Input::AnalogInput::kMaxValue : 0.f;
	analog_input.trigger_right = down(B_R2) ? Input::AnalogInput::kMaxValue : 0.f;
	return true;
}

void Ps5Ui::UpdateDisplay() {
	if (showing_message) {
		return;
	}
	int context = CurrentContext();
	if (context != hint_context) {
		hint_context = context;
		DrawHints(context);
	}
	gl_ActiveTexture(GL_TEXTURE0);
	gl_BindTexture(GL_TEXTURE_2D, game_texture);
	gl_PixelStorei(GL_UNPACK_ALIGNMENT, 4);
	gl_TexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, game_w, game_h, GL_RGBA, GL_UNSIGNED_BYTE, main_surface->pixels());
	Present();
}

void Ps5Ui::ToggleStretch() {
	vcfg.stretch.Toggle();
	stretch = vcfg.stretch.Get();
	LayoutFrame();
}

void Ps5Ui::vGetConfig(Game_ConfigVideo& cfg) const {
	cfg.renderer.Lock("PS5 (OpenGL)");
	cfg.stretch.SetOptionVisible(true);
}

bool Ps5Ui::HandleErrorOutput(const std::string& message) {
	ShowMessage("EasyRPG Player has stopped.", message,
		"Press the PS button and close EasyRPG Player to leave.");
	return true;
}

void Ps5Ui::Farewell(const char* headline, const char* detail) {
	if (showing_message) {
		return;
	}
	ShowMessage(headline, detail, "Press the PS button and close EasyRPG Player to leave.");
}

#ifdef SUPPORT_AUDIO
AudioInterface& Ps5Ui::GetAudio() {
	return *audio_;
}
#endif

/* ---- Leaving: a title may not exit() ------------------------------------- */

extern "C" void __wrap_exit(int status) {
	std::fprintf(stderr, "easyrpg: exit(%d)\n", status);
	std::fflush(nullptr);
	Ps5Ui::Farewell(status == 0 ? "Until next time." : "EasyRPG Player has stopped.", "");
	for (;;) {
		sceKernelUsleep(1000000);
	}
}
