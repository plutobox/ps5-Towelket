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

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

#include "player.h"
#include "output.h"
#include "ui.h"

#define STORAGE "/download0/easyrpg"
// Games go in folders here (FTP them to /data/easyrpg/games/<name>/); the
// game browser lists them.
#define GAMES "/data/easyrpg/games"

/**
 * If the main function ever needs to change, be sure to update the `main()`
 * functions of the other platforms as well.
 */
int main(int argc, char* argv[]) {
	// A title has no console: output goes to a file that can be read back
	// over FTP. Unbuffered, so a crash loses nothing.
	if (std::freopen("/data/easyrpg-ps5.log", "w", stdout)) {
		setvbuf(stdout, nullptr, _IONBF, 0);
		dup2(fileno(stdout), fileno(stderr));
	}
	if (std::freopen("/data/easyrpg-ps5.log", "a", stderr)) {
		setvbuf(stderr, nullptr, _IONBF, 0);
	}
	std::printf("EasyRPG Player starting\n");

	mkdir("/data/easyrpg", 0755);
	mkdir(GAMES, 0755);
	mkdir("/data/easyrpg/rtp", 0755);
	mkdir(STORAGE, 0755);

	std::vector<std::string> args;
	args.push_back("/app0/eboot.bin");
	args.push_back("--project-path");
	args.push_back(GAMES);
	for (int i = 1; i < argc; ++i) {
		args.push_back(argv[i]);
	}

	Player::Init(std::move(args));
	Player::Run();

	std::exit(Player::exit_code);
}
