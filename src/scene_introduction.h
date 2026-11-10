/*
 * This file is part of EasyRPG Player.
 *
 * EasyRPG Player is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef EP_SCENE_INTRODUCTION_H
#define EP_SCENE_INTRODUCTION_H

#include "scene.h"

#include <memory>

class Sprite;
class IntroDialogueWindow;
class Window_Help;
class Window_Command;

/** A short welcome shown before the game browser. */
class Scene_Introduction final : public Scene {
public:
	Scene_Introduction();
	~Scene_Introduction() override;

	void Start() override;
	void vUpdate() override;

private:
	std::unique_ptr<Sprite> portrait;
	std::unique_ptr<IntroDialogueWindow> dialogue_window;
	std::unique_ptr<Window_Help> prompt_window;
	std::unique_ptr<Window_Command> skip_window;
	unsigned line = 0;
	bool choosing_skip = false;
	bool confirming_skip = false;
	bool saying_goodbye = false;
	bool goodbye_fade_started = false;
	int goodbye_frames = 0;

	void RefreshText();
	void FinishIntroduction();
	void SayGoodbye();
};

#endif
