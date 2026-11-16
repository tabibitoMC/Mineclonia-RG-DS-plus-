// [RG DS Plus] evdev touchscreen reader (kept apart: linux/input.h clashes with Irrlicht keycodes)
#pragma once
#include <functional>

using rgds_log_fn = std::function<void(const char *, bool)>;
// type: 0 = down, 1 = moved, 2 = up; nx/ny normalized 0..1 (orientation applied)
using rgds_touch_fn = std::function<void(int slot, int type, float nx, float ny)>;

int rgds_touch_open(const rgds_log_fn &log);
void rgds_touch_poll(const rgds_touch_fn &cb);
void rgds_touch_close();

// Number of touch events read from evdev so far (0 = evdev silent, SDL is used)
int rgds_touch_events();
// Debug info for the on-screen indicator: total touches and last position (0..1)
void rgds_touch_note_sdl(float nx, float ny);
extern "C" int rgds_touch_debug(float *nx, float *ny, int *sdl_count);
