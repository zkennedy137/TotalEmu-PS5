#pragma once
#include <SDL.h>
#include <string>
#include "config.hpp"
#include "platform.hpp"

// Runs a game on the built-in core until the user quits back to the menu.
// Returns when the player holds the quit combo. Uses the same SDL renderer the
// menu draws into (software renderer on PS5). Audio plays if SDL audio opened.
std::string playGame(SDL_Renderer* ren, Platform& plat, const SystemConfig& sys,
              const std::string& gamePath, SDL_GameController* pad);
