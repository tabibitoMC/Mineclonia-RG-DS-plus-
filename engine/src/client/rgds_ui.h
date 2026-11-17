// [RG DS Plus] Shared state of the bottom-screen game UI (3DS-style layout):
// hotbar on top, coordinates, a touchpad area for the camera and three buttons.
#pragma once
#include "irrlichttypes_bloated.h"
#include <rect.h>
#include <string>

struct RgdsUi
{
	static constexpr int SLOTS = 9;
	enum Button { BTN_NONE = -1, BTN_INVENTORY = 0, BTN_CAMERA, BTN_PAUSE, BTN_COUNT };

	core::rect<s32> hotbar;          // whole hotbar strip
	core::rect<s32> slot[SLOTS];     // touch + draw rect of each slot
	core::rect<s32> coords;          // coordinates bar
	core::rect<s32> pad;             // touchpad (camera) area
	core::rect<s32> button[BTN_COUNT];

	// Written by the input handler, consumed by the game
	int pending_slot = -1;
	int pressed_button = BTN_NONE;   // button currently held by a finger

	// Written by the game, read by the renderer
	std::wstring coords_text;
	u32 signature = 0;               // changes whenever the bottom screen must be redrawn

	void layout(const v2u32 &ss)
	{
		const s32 m = ss.X / 128;                       // margin (8 px at 1024)
		const s32 sw = (ss.X - 2 * m) / SLOTS;          // slot width
		hotbar = core::rect<s32>(m, m, m + sw * SLOTS, m + sw);
		for (int i = 0; i < SLOTS; i++)
			slot[i] = core::rect<s32>(m + i * sw, m, m + (i + 1) * sw, m + sw);
		const s32 cy = hotbar.LowerRightCorner.Y + m;
		const s32 ch = ss.Y / 16;
		coords = core::rect<s32>(m, cy, ss.X - m, cy + ch);
		const s32 top = cy + ch + m;
		const s32 bw = ss.X * 22 / 100;                 // buttons column
		pad = core::rect<s32>(m, top, ss.X - bw - 2 * m, ss.Y - m);
		const s32 bh = (ss.Y - m - top - 2 * m) / BTN_COUNT;
		for (int i = 0; i < BTN_COUNT; i++) {
			const s32 y = top + i * (bh + m);
			button[i] = core::rect<s32>(ss.X - bw - m, y, ss.X - m, y + bh);
		}
	}

	int slotAt(const v2s32 &p) const
	{
		for (int i = 0; i < SLOTS; i++)
			if (slot[i].isPointInside(p))
				return i;
		return -1;
	}

	int buttonAt(const v2s32 &p) const
	{
		for (int i = 0; i < BTN_COUNT; i++)
			if (button[i].isPointInside(p))
				return i;
		return BTN_NONE;
	}
};

extern RgdsUi g_rgds_ui;

// [RG DS Plus] On-screen keyboard for text fields (bottom screen, touch).
struct RgdsKbd
{
	static constexpr int COLS = 10, ROWS = 4;
	enum Special { SP_SHIFT = 0, SP_SPACE, SP_BACK, SP_OK, SP_COUNT };

	bool visible = false;
	bool shift = false;
	const void *dismissed_for = nullptr; // field for which the user closed the keyboard
	size_t touch_id = (size_t)-1;

	core::rect<s32> area;      // whole keyboard (touches inside never reach the menu)
	core::rect<s32> preview;   // text being edited
	core::rect<s32> key[ROWS][COLS];
	core::rect<s32> special[SP_COUNT];

	static const wchar_t *row(int r, bool shifted)
	{
		static const wchar_t *lo[ROWS] = {L"1234567890", L"qwertyuiop", L"asdfghjkl-", L"zxcvbnm,._"};
		static const wchar_t *up[ROWS] = {L"1234567890", L"QWERTYUIOP", L"ASDFGHJKL-", L"ZXCVBNM,._"};
		return (shifted ? up : lo)[r];
	}

	void layout(const v2u32 &ss)
	{
		const s32 kh = ss.Y / 10;                 // key height
		const s32 kw = ss.X / COLS;
		const s32 top = ss.Y - kh * (ROWS + 1);
		preview = core::rect<s32>(0, top - kh * 8 / 10, ss.X, top);
		area = core::rect<s32>(0, preview.UpperLeftCorner.Y, ss.X, ss.Y);
		for (int r = 0; r < ROWS; r++)
			for (int c = 0; c < COLS; c++)
				key[r][c] = core::rect<s32>(c * kw, top + r * kh, (c + 1) * kw, top + (r + 1) * kh);
		const s32 y = top + ROWS * kh;
		special[SP_SHIFT] = core::rect<s32>(0, y, kw * 2, ss.Y);
		special[SP_SPACE] = core::rect<s32>(kw * 2, y, kw * 6, ss.Y);
		special[SP_BACK] = core::rect<s32>(kw * 6, y, kw * 8, ss.Y);
		special[SP_OK] = core::rect<s32>(kw * 8, y, ss.X, ss.Y);
	}
};

extern RgdsKbd g_rgds_kbd;
