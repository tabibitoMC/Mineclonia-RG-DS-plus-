// [RG DS Plus] evdev touchscreen reader.
// On the stock firmware SDL (KMSDRM) does not deliver the bottom touchscreen
// as touch events, so the engine reads /dev/input directly.
// Env: LUANTI_TOUCH_EVDEV=0 disables, LUANTI_TOUCH_DEVICE=/dev/input/eventN forces
// a device, LUANTI_TOUCH_ROTATE=0/90/180/270, LUANTI_TOUCH_SWAPXY=1,
// LUANTI_TOUCH_INVERTX=1, LUANTI_TOUCH_INVERTY=1 fix the orientation.
#include "rgds_evdev.h"

#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <string>
#include <algorithm>

#define TBIT(arr, bit) ((arr[(bit) / (8 * sizeof(long))] >> ((bit) % (8 * sizeof(long)))) & 1UL)

namespace {
int g_fd = -1;
bool g_mt = false;
bool g_has_slot = true; // false = MT protocol A (contacts separated by SYN_MT_REPORT)
int g_contact = 0;
bool g_seen[10] = {};
int g_minx = 0, g_maxx = 1, g_miny = 0, g_maxy = 1;
int g_rot = 0;
bool g_swap = false, g_invx = false, g_invy = false;
int g_slot = 0;
int g_logleft = 10;
rgds_log_fn g_log;
int g_events = 0, g_touches = 0, g_sdl_touches = 0;
float g_lastx = -1, g_lasty = -1;
struct Slot {
	bool active = false, pend_down = false, pend_up = false, moved = false;
	int x = 0, y = 0;
} g_slots[10];

bool env_on(const char *n)
{
	const char *e = getenv(n);
	return e && e[0] == '1';
}

void emit(const rgds_touch_fn &cb, int s, int type)
{
	const Slot &st = g_slots[s];
	float nx = (float)(st.x - g_minx) / (g_maxx - g_minx);
	float ny = (float)(st.y - g_miny) / (g_maxy - g_miny);
	if (g_swap) std::swap(nx, ny);
	switch (g_rot) {
	case 90: { float t = nx; nx = ny; ny = 1.0f - t; break; }
	case 180: nx = 1.0f - nx; ny = 1.0f - ny; break;
	case 270: { float t = nx; nx = 1.0f - ny; ny = t; break; }
	default: break;
	}
	if (g_invx) nx = 1.0f - nx;
	if (g_invy) ny = 1.0f - ny;
	nx = std::min(1.0f, std::max(0.0f, nx));
	ny = std::min(1.0f, std::max(0.0f, ny));
	if (g_logleft > 0 && type != 1 && g_log) {
		g_logleft--;
		char buf[160];
		snprintf(buf, sizeof(buf), "[touch] %s slot %d raw %d,%d -> %.3f,%.3f",
				type == 0 ? "down" : "up", s, st.x, st.y, nx, ny);
		g_log(buf, true);
	}
	g_events++;
	if (type == 0) g_touches++;
	g_lastx = nx; g_lasty = ny;
	cb(s, type, nx, ny);
}
} // namespace

int rgds_touch_open(const rgds_log_fn &log)
{
	g_log = log;
	const char *en = getenv("LUANTI_TOUCH_EVDEV");
	if (en && en[0] == '0')
		return -1;
	char buf[400];
	// The stock launcher disables the touch panel in the kernel driver before
	// starting external apps (tpctrl = 1). 0 = touch enabled.
	if (FILE *f = fopen("/sys/class/anbernic_misc/tpctrl", "w")) {
		fputs("0\n", f);
		fclose(f);
		g_log("[touch] tpctrl set to 0 (touch panel enabled)", true);
	}
	std::string best;
	int best_score = 0;
	const char *forced = getenv("LUANTI_TOUCH_DEVICE");
	for (int i = 0; i < 32; i++) {
		std::string path = "/dev/input/event" + std::to_string(i);
		if (forced && path != forced)
			continue;
		int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			continue;
		char name[128] = "?";
		ioctl(fd, EVIOCGNAME(sizeof(name)), name);
		unsigned long absbits[(ABS_MAX + 8 * sizeof(long)) / (8 * sizeof(long))] = {};
		unsigned long keybits[(KEY_MAX + 8 * sizeof(long)) / (8 * sizeof(long))] = {};
		unsigned long props[(INPUT_PROP_MAX + 8 * sizeof(long)) / (8 * sizeof(long))] = {};
		ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits);
		ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits);
		ioctl(fd, EVIOCGPROP(sizeof(props)), props);
		close(fd);
		const bool mt = TBIT(absbits, ABS_MT_POSITION_X);
		const bool st = TBIT(absbits, ABS_X) && TBIT(keybits, BTN_TOUCH);
		const bool pad = TBIT(keybits, BTN_GAMEPAD) || TBIT(keybits, BTN_JOYSTICK);
		const bool direct = TBIT(props, INPUT_PROP_DIRECT);
		std::string lname = name;
		for (auto &c : lname) c = (char)tolower((unsigned char)c);
		const bool named = lname.find("touch") != std::string::npos ||
				lname.find("goodix") != std::string::npos || lname.find("ft5") != std::string::npos ||
				lname.find("gt9") != std::string::npos || lname.find("hyn") != std::string::npos ||
				lname.find("cst") != std::string::npos || lname.find("_ts") != std::string::npos ||
				lname.find("-ts") != std::string::npos;
		int score = 0;
		if ((mt || st) && (!pad || forced))
			score = 1 + (mt ? 2 : 0) + (direct ? 4 : 0) + (named ? 2 : 0);
		snprintf(buf, sizeof(buf), "[touch] %s '%s' mt=%d st=%d pad=%d direct=%d score=%d",
				path.c_str(), name, mt, st, pad, direct, score);
		g_log(buf, false);
		if (score > best_score) {
			best_score = score;
			best = path;
		}
	}
	if (best.empty()) {
		g_log("[touch] no evdev touchscreen found, using SDL touch", true);
		return -1;
	}
	g_fd = open(best.c_str(), O_RDONLY | O_NONBLOCK);
	if (g_fd < 0) {
		snprintf(buf, sizeof(buf), "[touch] cannot open %s", best.c_str());
		g_log(buf, true);
		return -1;
	}
	unsigned long absbits[(ABS_MAX + 8 * sizeof(long)) / (8 * sizeof(long))] = {};
	ioctl(g_fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits);
	g_mt = TBIT(absbits, ABS_MT_POSITION_X);
	g_has_slot = TBIT(absbits, ABS_MT_SLOT);
	struct input_absinfo ax{}, ay{};
	ioctl(g_fd, EVIOCGABS(g_mt ? ABS_MT_POSITION_X : ABS_X), &ax);
	ioctl(g_fd, EVIOCGABS(g_mt ? ABS_MT_POSITION_Y : ABS_Y), &ay);
	g_minx = ax.minimum; g_maxx = ax.maximum > ax.minimum ? ax.maximum : ax.minimum + 1;
	g_miny = ay.minimum; g_maxy = ay.maximum > ay.minimum ? ay.maximum : ay.minimum + 1;
	if (const char *r = getenv("LUANTI_TOUCH_ROTATE"))
		g_rot = atoi(r);
	g_swap = env_on("LUANTI_TOUCH_SWAPXY");
	g_invx = env_on("LUANTI_TOUCH_INVERTX");
	g_invy = env_on("LUANTI_TOUCH_INVERTY");
	snprintf(buf, sizeof(buf), "[touch] using %s mt=%d slots=%d x=%d..%d y=%d..%d rot=%d swap=%d inv=%d,%d",
			best.c_str(), g_mt, g_has_slot, g_minx, g_maxx, g_miny, g_maxy, g_rot, g_swap, g_invx, g_invy);
	g_log(buf, true);
	return g_fd;
}

void rgds_touch_poll(const rgds_touch_fn &cb)
{
	if (g_fd < 0)
		return;
	struct input_event evs[64];
	for (;;) {
		ssize_t n = read(g_fd, evs, sizeof(evs));
		if (n <= 0)
			break;
		const int cnt = (int)(n / sizeof(struct input_event));
		for (int i = 0; i < cnt; i++) {
			const auto &ev = evs[i];
			if (g_mt && !g_has_slot) {
				// MT protocol A: every contact ends with SYN_MT_REPORT, the frame
				// with SYN_REPORT; a frame without contacts means all fingers up.
				if (ev.type == EV_ABS && g_contact < 10) {
					if (ev.code == ABS_MT_POSITION_X) { g_slots[g_contact].x = ev.value; g_seen[g_contact] = true; }
					else if (ev.code == ABS_MT_POSITION_Y) { g_slots[g_contact].y = ev.value; g_seen[g_contact] = true; }
				} else if (ev.type == EV_SYN && ev.code == SYN_MT_REPORT) {
					g_contact++;
				} else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
					for (int s = 0; s < 10; s++) {
						Slot &st = g_slots[s];
						if (g_seen[s] && !st.active) { st.active = true; emit(cb, s, 0); }
						else if (g_seen[s]) emit(cb, s, 1);
						else if (st.active) { emit(cb, s, 2); st.active = false; }
						g_seen[s] = false;
					}
					g_contact = 0;
				}
				continue;
			}
			Slot &cur = g_slots[g_slot];
			if (ev.type == EV_ABS) {
				switch (ev.code) {
				case ABS_MT_SLOT:
					g_slot = std::min(9, std::max(0, ev.value));
					break;
				case ABS_MT_TRACKING_ID:
					if (ev.value < 0) {
						if (cur.active) cur.pend_up = true;
					} else if (!cur.active) {
						cur.pend_down = true;
					}
					break;
				case ABS_MT_POSITION_X: cur.x = ev.value; cur.moved = true; break;
				case ABS_MT_POSITION_Y: cur.y = ev.value; cur.moved = true; break;
				case ABS_X: if (!g_mt) { g_slots[0].x = ev.value; g_slots[0].moved = true; } break;
				case ABS_Y: if (!g_mt) { g_slots[0].y = ev.value; g_slots[0].moved = true; } break;
				default: break;
				}
			} else if (ev.type == EV_KEY && ev.code == BTN_TOUCH && !g_mt) {
				// Single-touch devices, or MT devices without tracking ids
				Slot &s0 = g_slots[0];
				if (ev.value && !s0.active && !s0.pend_down) s0.pend_down = true;
				if (!ev.value) {
					for (auto &s : g_slots)
						if (s.active) s.pend_up = true;
				}
			} else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
				for (int s = 0; s < 10; s++) {
					Slot &st = g_slots[s];
					if (st.pend_down && !st.active) {
						st.active = true;
						emit(cb, s, 0);
					} else if (st.active && st.moved && !st.pend_up) {
						emit(cb, s, 1);
					}
					if (st.pend_up && st.active) {
						emit(cb, s, 2);
						st.active = false;
					}
					st.pend_down = st.pend_up = st.moved = false;
				}
			} else if (ev.type == EV_SYN && ev.code == SYN_DROPPED) {
				for (int s = 0; s < 10; s++)
					if (g_slots[s].active) {
						emit(cb, s, 2);
						g_slots[s].active = false;
					}
			}
		}
	}
}

void rgds_touch_close()
{
	if (g_fd >= 0)
		close(g_fd);
	g_fd = -1;
}

int rgds_touch_events() { return g_events; }

void rgds_touch_note_sdl(float nx, float ny)
{
	g_sdl_touches++;
	g_lastx = nx; g_lasty = ny;
}

extern "C" int rgds_touch_debug(float *nx, float *ny, int *sdl_count)
{
	if (nx) *nx = g_lastx;
	if (ny) *ny = g_lasty;
	if (sdl_count) *sdl_count = g_sdl_touches;
	return g_touches;
}
