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
