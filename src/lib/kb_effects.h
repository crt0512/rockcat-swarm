// the effects the pc draws on a keyboard, and the settings each of them has. engine.cpp owns the thread and the device,
// this only turns "what time is it, which keys got pressed" into a colour for every key
#pragma once

#include "rockcat/rockcat.h"

#include <map>
#include <string>
#include <vector>

struct kb_board {
    int leds = 0;
    std::vector<double> x, y;        // middle of each key, 0 to 1 across and 0 (top) to 1 (bottom). below 0 = no key there
    double aspect = 0.4;             // height of the board over its width
    std::vector<rockcat_color> base; // the colours the profile has for the keys, black where a key is off
    double level = 1;                // brightness of the profile, 0 to 1
};

struct kb_fx;
kb_fx *kb_fx_new();
void kb_fx_free(kb_fx *fx);
// effect = which one to draw from now on. options = its settings by key (whatever is missing gets its default)
void kb_fx_setup(kb_fx *fx, int effect, const std::map<std::string, int> &options);
void kb_fx_key(kb_fx *fx, const kb_board &board, int led, bool down, double t);
// writes board.leds colours. t in seconds since the engine started, dt since the frame before
void kb_fx_render(kb_fx *fx, const kb_board &board, double t, double dt, rockcat_color *out);
// every option of that effect as it is stored for this device and profile
std::map<std::string, int> kb_fx_options(uint16_t pid, uint8_t profile, int effect);
