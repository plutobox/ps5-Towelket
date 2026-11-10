#ifndef EP_PLATFORM_PS5_TEXT_INPUT_H
#define EP_PLATFORM_PS5_TEXT_INPUT_H

#include <string>

namespace Ps5 {

/**
 * Asks for a line of text with the PS5's own on-screen keyboard (the system
 * IME dialog), shown over the player. Returns false if the player cancelled
 * or the keyboard could not open; `text` is the starting text and receives the
 * result. Blocks (keeping the screen drawn) until the keyboard closes.
 */
bool TextInput(const std::string& title, std::string& text);

}

#endif
