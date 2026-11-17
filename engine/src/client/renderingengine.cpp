// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>
// Copyright (C) 2017 nerzhul, Loic Blot <loic.blot@unix-experience.fr>

#include <IGUIEditBox.h>
#include <optional>
#include <unordered_map>
#include <irrlicht.h>
#include "IMeshCache.h"
#include "fontengine.h"
#include "client.h"
#include "clouds.h"
#include "util/numeric.h"
#include "guiscalingfilter.h"
#include "localplayer.h"
#include "gettext.h"
#include "client/hud.h"
#include "client/texturesource.h"
#include "camera.h"
#include "minimap.h"
#include "porting.h"
#include "clientmap.h"
#include "renderingengine.h"
#include "rgds_ui.h"
#include "render/core.h"
#include "render/factory.h"
#include "filesys.h"
#include "irrlicht_changes/static_text.h"
#include "irr_ptr.h"

RenderingEngine *RenderingEngine::s_singleton = nullptr;

/* Helper classes */

void FpsControl::reset()
{
	last_time = porting::getTimeUs();
}

void FpsControl::limit(IrrlichtDevice *device, f32 *dtime)
{
	const float fps_limit = device->isWindowFocused()
			? g_settings->getFloat("fps_max")
			: g_settings->getFloat("fps_max_unfocused");
	const u64 frametime_min = 1000000.0f / std::max(fps_limit, 1.0f);

	u64 time = porting::getTimeUs();

	if (time > last_time) // Make sure time hasn't overflowed
		busy_time = time - last_time;
	else
		busy_time = 0;

	if (busy_time < frametime_min) {
		sleep_time = frametime_min - busy_time;
		porting::preciseSleepUs(sleep_time);
	} else {
		sleep_time = 0;
	}

	// Read the timer again to accurately determine how long we actually slept,
	// rather than calculating it by adding sleep_time to time.
	time = porting::getTimeUs();

	if (time > last_time) // Make sure last_time hasn't overflowed
		*dtime = (time - last_time) / 1000000.0f;
	else
		*dtime = 0;

	last_time = time;
}

class FogShaderUniformSetter : public IShaderUniformSetter
{
	CachedPixelShaderSetting<float, 4> m_fog_color{"fogColor"};
	CachedPixelShaderSetting<float> m_fog_distance{"fogDistance"};
	CachedPixelShaderSetting<float> m_fog_shading_parameter{"fogShadingParameter"};

public:
	void onSetUniforms(video::IMaterialRendererServices *services) override
	{
		auto *driver = services->getVideoDriver();
		assert(driver);

		video::SColor fog_color;
		video::E_FOG_TYPE fog_type = video::EFT_FOG_LINEAR;
		f32 fog_start = 0;
		f32 fog_end = 0;
		f32 fog_density = 0;
		bool fog_pixelfog = false;
		bool fog_rangefog = false;
		driver->getFog(fog_color, fog_type, fog_start, fog_end, fog_density,
				fog_pixelfog, fog_rangefog);

		video::SColorf fog_colorf(fog_color);
		m_fog_color.set(fog_colorf, services);

		m_fog_distance.set(&fog_end, services);

		float parameter = 0;
		if (fog_end > 0)
			parameter = 1.0f / (1.0f - fog_start / fog_end);
		m_fog_shading_parameter.set(&parameter, services);
	}
};

IShaderUniformSetter *FogShaderUniformSetterFactory::create(const std::string &name)
{
	return new FogShaderUniformSetter();
}

/* Other helpers */

static std::optional<video::E_DRIVER_TYPE> chooseVideoDriver()
{
	auto &&configured_name = g_settings->get("video_driver");
	if (configured_name.empty())
		return std::nullopt;

	auto &&drivers = RenderingEngine::getSupportedVideoDrivers();
	for (auto driver: drivers) {
		auto &&info = RenderingEngine::getVideoDriverInfo(driver);
		if (!strcasecmp(configured_name.c_str(), info.name.c_str()))
			return driver;
	}

	errorstream << "Invalid video_driver specified: " << configured_name << std::endl;
	return std::nullopt;
}

static inline auto getVideoDriverName(video::E_DRIVER_TYPE driver)
{
	return RenderingEngine::getVideoDriverInfo(driver).friendly_name;
}

static IrrlichtDevice *createDevice(SIrrlichtCreationParameters params, std::optional<video::E_DRIVER_TYPE> requested_driver)
{
	if (requested_driver) {
		params.DriverType = *requested_driver;
		infostream << "Trying video driver " << getVideoDriverName(params.DriverType) << std::endl;
		if (auto *device = createDeviceEx(params))
			return device;
		errorstream << "Failed to initialize the " << getVideoDriverName(params.DriverType) << " video driver" << std::endl;
	}
	sanity_check(requested_driver != video::EDT_NULL);

	// try to find any working video driver
	for (auto fallback_driver: RenderingEngine::getSupportedVideoDrivers()) {
		if (fallback_driver == video::EDT_NULL || fallback_driver == requested_driver)
			continue;
		params.DriverType = fallback_driver;
		infostream << "Trying video driver " << getVideoDriverName(params.DriverType) << std::endl;
		if (auto *device = createDeviceEx(params))
			return device;
	}

	throw BaseException(gettext("Could not initialize any supported video driver!"));
}

/* RenderingEngine class */

RenderingEngine::RenderingEngine(MyEventReceiver *receiver)
{
	sanity_check(!s_singleton);

	// Resolution selection
	bool fullscreen = g_settings->getBool("fullscreen");
#ifdef __ANDROID__
	u16 screen_w = 0, screen_h = 0;
	bool window_maximized = false;
#else
	u16 screen_w = std::max<u16>(g_settings->getU16("screen_w"), 1);
	u16 screen_h = std::max<u16>(g_settings->getU16("screen_h"), 1);
	// If I…
	// 1. … set fullscreen = true and window_maximized = true on startup
	// 2. … set fullscreen = false later
	// on Linux with SDL, everything breaks.
	// => Don't do it.
	bool window_maximized = !fullscreen && g_settings->getBool("window_maximized");
#endif

	// bpp, fsaa, vsync
	bool vsync = g_settings->getBool("vsync");
	// Don't enable MSAA in OpenGL context creation if post-processing is enabled,
	// the post-processing pipeline handles it.
	bool enable_fsaa = g_settings->get("antialiasing") == "fsaa" &&
			!g_settings->getBool("enable_post_processing");
	u16 fsaa = enable_fsaa ? MYMAX(2, g_settings->getU16("fsaa")) : 0;

	// Determine driver
	auto driverType = chooseVideoDriver();

	SIrrlichtCreationParameters params = SIrrlichtCreationParameters();
	if (tracestream)
		params.LoggingLevel = ELL_DEBUG;
	params.WindowSize = core::dimension2d<u32>(screen_w, screen_h);
	params.AntiAlias = fsaa;
	params.Fullscreen = fullscreen;
	params.WindowMaximized = window_maximized;
	params.WindowResizable = 1; // 1 means always (required for window_maximized)
	params.Stencilbuffer = false;
	params.Vsync = vsync;
	params.EventReceiver = receiver;
	params.DriverDebug = g_settings->getBool("opengl_debug");

	// there is no standardized path for these on desktop
	std::string rel_path = std::string("client") + DIR_DELIM
			+ "shaders" + DIR_DELIM + "Irrlicht";
	params.OGLES2ShaderPath = (porting::path_share + DIR_DELIM + rel_path + DIR_DELIM).c_str();

	m_device = createDevice(params, driverType);
	driver = m_device->getVideoDriver();
	verbosestream << "Using the " << getVideoDriverName(driver->getDriverType()) << " video driver" << std::endl;

	// This changes the minimum allowed number of vertices in a VBO. Default is 500.
	driver->setMinHardwareBufferVertexCount(4);

	m_receiver = receiver;

	s_singleton = this;

	g_settings->registerChangedCallback("fullscreen", settingChangedCallback, this);
	g_settings->registerChangedCallback("window_maximized", settingChangedCallback, this);
}

RenderingEngine::~RenderingEngine()
{
	sanity_check(s_singleton == this);

	g_settings->deregisterAllChangedCallbacks(this);

	core.reset();
	m_device->closeDevice();
	m_device->drop();
	s_singleton = nullptr;
}

void RenderingEngine::settingChangedCallback(const std::string &name, void *data)
{
	IrrlichtDevice *device = static_cast<RenderingEngine*>(data)->m_device;
	if (name == "fullscreen") {
		device->setFullscreen(g_settings->getBool("fullscreen"));

	} else if (name == "window_maximized") {
		if (!device->isFullscreen()) {
			if (g_settings->getBool("window_maximized"))
				device->maximizeWindow();
			else
				device->restoreWindow();
		}
	}
}

v2u32 RenderingEngine::_getWindowSize() const
{
	if (core)
		return core->getVirtualSize();
	return m_device->getVideoDriver()->getScreenSize();
}

void RenderingEngine::setResizable(bool resize)
{
	m_device->setResizable(resize);
}

void RenderingEngine::cleanupMeshCache()
{
	auto mesh_cache = m_device->getSceneManager()->getMeshCache();
	mesh_cache->clear();
}

bool RenderingEngine::setupTopLevelWindow()
{
	return setWindowIcon();
}

bool RenderingEngine::setWindowIcon()
{
	irr_ptr<video::IImage> img(driver->createImageFromFile(
			(porting::path_share + "/textures/base/pack/logo.png").c_str()));
	if (!img) {
		warningstream << "Could not load icon file." << std::endl;
		return false;
	}

	return m_device->setWindowIcon(img.get());
}

/*
	Draws a screen with a single text on it.
	Text will be removed when the screen is drawn the next time.
	Additionally, a progressbar can be drawn when percent is set between 0 and 100.
*/
void RenderingEngine::draw_load_screen(const std::wstring &text,
		gui::IGUIEnvironment *guienv, ITextureSource *tsrc, float dtime,
		int percent, float *indef_pos, const std::wstring &bottom_text)
{
	v2u32 screensize = getWindowSize();

	v2s32 textsize(g_fontengine->getTextWidth(text), g_fontengine->getLineHeight());
	v2s32 center(screensize.X / 2, screensize.Y / 2);
	core::rect<s32> textrect(center - textsize / 2, center + textsize / 2);
	// [RG DS Plus] text above the progress bar, Minecraft style
	textrect -= v2s32(0, textsize.Y * 5 / 2);

	gui::IGUIStaticText *guitext =
			gui::StaticText::add(guienv, text, textrect, false, false);
	guitext->setTextAlignment(gui::EGUIA_CENTER, gui::EGUIA_UPPERLEFT);

	auto *driver = get_video_driver();

	driver->setFog(m_menu_sky_color);
	driver->beginScene(true, true, m_menu_sky_color);
	// [RG DS Plus] Minecraft-like dirt loading background
	rgdsTile(rgdsTexture("rgds_dirt_dark.png"),
			core::rect<s32>(0, 0, screensize.X, screensize.Y));

	if (g_settings->getBool("menu_clouds")) {
		g_menuclouds->step(dtime * 3);
		g_menucloudsmgr->drawAll();
	}

	int percent_min = 0;
	int percent_max = percent;
	if (indef_pos) {
		*indef_pos = fmodf(*indef_pos + (dtime * 50.0f), 140.0f);
		percent_max = std::min((int) *indef_pos, 100);
		percent_min = std::max((int) *indef_pos - 40, 0);
	}
	// draw progress bar
	u32 imgW = 0, imgH = 0;
	if ((percent_min >= 0) && (percent_max <= 100)) {
		video::ITexture *progress_img = tsrc->getTexture("progress_bar.png");
		video::ITexture *progress_img_bg =
				tsrc->getTexture("progress_bar_bg.png");

		if (progress_img && progress_img_bg) {
#ifndef __ANDROID__
			const core::dimension2d<u32> &img_size =
					progress_img_bg->getSize();
			float density = g_settings->getFloat("gui_scaling", 0.5f, 20.0f) *
					getDisplayDensity();
			imgW = rangelim(img_size.Width, 200, 600) * density;
			imgH = rangelim(img_size.Height, 24, 72) * density;
#else
			const core::dimension2d<u32> img_size(256, 48);
			float imgRatio = (float)img_size.Height / img_size.Width;
			imgW = screensize.X / 2.2f;
			imgH = floor(imgW * imgRatio);
#endif
			v2s32 img_pos((screensize.X - imgW) / 2,
					(screensize.Y - imgH) / 2);

			draw2DImageFilterScaled(get_video_driver(), progress_img_bg,
					core::rect<s32>(img_pos.X, img_pos.Y,
							img_pos.X + imgW,
							img_pos.Y + imgH),
					core::rect<s32>(0, 0, img_size.Width,
							img_size.Height),
					0, 0, true);

			draw2DImageFilterScaled(get_video_driver(), progress_img,
					core::rect<s32>(img_pos.X + (percent_min * imgW) / 100, img_pos.Y,
							img_pos.X + (percent_max * imgW) / 100,
							img_pos.Y + imgH),
					core::rect<s32>(percent_min * img_size.Width / 100, 0,
							percent_max * img_size.Width / 100,
							img_size.Height),
					0, 0, true);
		}
	}

	gui::IGUIStaticText *gui_bottom_text = nullptr;
	if (!bottom_text.empty()) {
		auto line_height = g_fontengine->getLineHeight();
		const v2s32 bottom_textsize = v2s32::from(v2u32(
				g_fontengine->getFont()->getDimension(bottom_text.c_str())));
		s32 padding = line_height * 0.5f;

		v2s32 offset(
			imgW == 0 ? bottom_textsize.X * -0.5f : imgW * -0.5f + padding,
			std::max<s32>(imgH, textsize.Y) * 0.5f + padding
		);
		v2s32 pos = center + offset;

		core::rect<s32> rect(pos, pos + bottom_textsize);
		gui_bottom_text = gui::StaticText::add(guienv, bottom_text, rect, false, false);
	}

	guienv->drawAll();
	driver->endScene();
	if (isDualScreen())
		drawBottomScreen(BS_LOADING, 1000);
	guitext->remove();
	if (gui_bottom_text)
		gui_bottom_text->remove();
}

// ---------------------------------------------------------------------------
// [RG DS Plus] Minecraft-style drawing helpers for the two screens
extern "C" int rgds_touch_debug(float *nx, float *ny, int *sdl_count);
// ---------------------------------------------------------------------------
video::ITexture *RenderingEngine::rgdsTexture(const std::string &name)
{
	// Cache: the driver normalizes paths, so looking a path up again by its
	// raw name would load (and leak) a new texture on every call.
	static std::unordered_map<std::string, video::ITexture *> cache;
	auto it = cache.find(name);
	if (it != cache.end())
		return it->second;
	auto *driver = get_video_driver();
	const std::string path = porting::path_share + DIR_DELIM "textures" DIR_DELIM
			"base" DIR_DELIM "pack" DIR_DELIM + name;
	video::ITexture *tex = fs::PathExists(path) ? driver->getTexture(path.c_str()) : nullptr;
	if (tex) {
		tex->grab(); // keep it alive for the whole session
		cache[name] = tex;
	}
	return tex;
}

void RenderingEngine::rgdsTile(video::ITexture *tex, const core::rect<s32> &area)
{
	// The background image is pre-tiled at screen size: draw it as one quad.
	if (!tex)
		return;
	const auto ts = tex->getOriginalSize();
	draw2DImageFilterScaled(get_video_driver(), tex, area,
			core::rect<s32>(0, 0, ts.Width, ts.Height), nullptr, nullptr, false);
}

void RenderingEngine::rgdsText(const std::wstring &text, const core::rect<s32> &r,
		video::SColor color, bool hcenter, gui::IGUIFont *font)
{
	if (!font)
		font = g_fontengine ? g_fontengine->getFont() : nullptr;
	if (!font)
		return;
	// Minecraft-like text shadow: same text, 1/4 brightness, offset down-right
	const s32 o = std::max<s32>(2, font->getDimension(L"M").Height / 10);
	video::SColor sh(color.getAlpha(), color.getRed() / 4, color.getGreen() / 4, color.getBlue() / 4);
	font->draw(text.c_str(), r + v2s32(o, o), sh, hcenter, true);
	font->draw(text.c_str(), r, color, hcenter, true);
}

void RenderingEngine::rgdsImageFit(video::ITexture *tex, const core::rect<s32> &box)
{
	if (!tex)
		return;
	const auto ts = tex->getOriginalSize();
	const f32 sc = std::min((f32)box.getWidth() / ts.Width, (f32)box.getHeight() / ts.Height);
	const s32 w = ts.Width * sc, h = ts.Height * sc;
	const v2s32 c = box.getCenter();
	core::rect<s32> dst(c.X - w / 2, c.Y - h / 2, c.X - w / 2 + w, c.Y - h / 2 + h);
	draw2DImageFilterScaled(get_video_driver(), tex, dst,
			core::rect<s32>(0, 0, ts.Width, ts.Height), nullptr, nullptr, true);
}

void RenderingEngine::drawBottomScreen(BottomMode mode, u32 min_interval_ms,
		const std::function<void()> &draw_hotbar)
{
	// Each bottom-screen frame switches the GL drawable, which is not free.
	// Redraw only when the mode or the shown content changes (at most ~12x per
	// second), or after min_interval_ms.
	static u64 last_ms = 0;
	static int last_mode = -1;
	static u32 last_sig = 0;
	const u64 now = porting::getTimeMs();
	const u32 sig = mode == BS_GAME ? g_rgds_ui.signature : 0;
	const bool changed = (int)mode != last_mode || (sig != last_sig && now - last_ms >= 80);
	if (!changed && now - last_ms < min_interval_ms)
		return;
	last_sig = sig;
	if ((int)mode != last_mode)
		warningstream << "[bottom] mode " << (int)mode << std::endl;
	if (!m_device->beginSecondScreen()) {
		static bool warned = false;
		if (!warned)
			warningstream << "[bottom] beginSecondScreen failed" << std::endl;
		warned = true;
		return;
	}
	last_ms = now;
	last_mode = (int)mode;

	auto *driver = get_video_driver();
	driver->beginScene(true, true, video::SColor(255, 20, 16, 12));
	const v2u32 ss = driver->getScreenSize();
	const core::rect<s32> full(0, 0, ss.X, ss.Y);
	const s32 lh = g_fontengine ? g_fontengine->getLineHeight() : 20;

	switch (mode) {
	case BS_LOADING:
		rgdsTile(rgdsTexture("rgds_dirt_dark.png"), full);
		rgdsImageFit(rgdsTexture("rgds_header.png"),
				core::rect<s32>(ss.X / 10, ss.Y / 4, ss.X * 9 / 10, ss.Y / 2));
		rgdsText(L"Cargando el mundo...", core::rect<s32>(0, ss.Y * 6 / 10, ss.X, ss.Y * 6 / 10 + lh),
				video::SColor(255, 255, 255, 255));
		break;
	case BS_MAINMENU:
		rgdsTile(rgdsTexture("rgds_dirt_dark.png"), full);
		break;
	case BS_GAME_MENU:
		rgdsTile(rgdsTexture("rgds_dirt_darker.png"), full);
		break;
	case BS_GAME: {
		// 3DS-style layout: hotbar, coordinates, touchpad and buttons
		RgdsUi &ui = g_rgds_ui;
		ui.layout(ss);
		rgdsTile(rgdsTexture("rgds_dirt_dark.png"), full);
		if (draw_hotbar)
			draw_hotbar();

		// Coordinates bar
		driver->draw2DRectangle(video::SColor(170, 0, 0, 0), ui.coords);
		rgdsText(ui.coords_text, ui.coords, video::SColor(255, 255, 255, 255));

		// Touchpad area
		if (video::ITexture *pt = rgdsTexture("rgds_panel.png"))
			draw2DImage9Slice(driver, pt, ui.pad, core::rect<s32>(0, 0, 48, 48),
					core::rect<s32>(8, 8, 40, 40));
		{
			const v2s32 c = ui.pad.getCenter();
			const s32 w = ui.pad.getWidth();
			rgdsText(L"Desliza aqu\u00ed para mover la c\u00e1mara",
					core::rect<s32>(ui.pad.UpperLeftCorner.X, c.Y - lh * 3, ui.pad.LowerRightCorner.X, c.Y - lh * 2),
					video::SColor(255, 255, 255, 255));
			static const wchar_t *hints[] = {
				L"X B Y A: c\u00e1mara     Stick: caminar",
				L"R1: picar/atacar     L1: usar/construir",
				L"R2: saltar     L2: agacharse",
				L"L3 (apretar stick): correr",
				L"SELECT: inventario     START: pausa",
			};
			s32 y = c.Y - lh * 3 / 2;
			for (const wchar_t *h : hints) {
				rgdsText(h, core::rect<s32>(c.X - w / 2, y, c.X + w / 2, y + lh),
						video::SColor(255, 170, 170, 170));
				y += lh * 13 / 10;
			}
			// Touch diagnostics (small): touches seen by the game
			float tx = -1, ty = -1;
			int sdl_n = 0;
			const int ev_n = rgds_touch_debug(&tx, &ty, &sdl_n);
			wchar_t dbg[96];
			swprintf(dbg, 96, L"toques: %d", ev_n + sdl_n);
			rgdsText(dbg, core::rect<s32>(ui.pad.UpperLeftCorner.X, ui.pad.LowerRightCorner.Y - lh * 3 / 2,
					ui.pad.LowerRightCorner.X, ui.pad.LowerRightCorner.Y - lh / 2),
					video::SColor(255, 90, 110, 90));
		}

		// Buttons
		static const wchar_t *labels[RgdsUi::BTN_COUNT] = {L"Inventario", L"C\u00e1mara", L"Pausa"};
		static const char *icons[RgdsUi::BTN_COUNT] = {
			"rgds_icon_inventory.png", "rgds_icon_camera.png", "rgds_icon_pause.png"};
		for (int i = 0; i < RgdsUi::BTN_COUNT; i++) {
			const core::rect<s32> &r = ui.button[i];
			video::ITexture *bt = rgdsTexture(ui.pressed_button == i ?
					"rgds_btn_pressed.png" : "rgds_btn.png");
			if (bt) {
				const auto ts = bt->getOriginalSize();
				draw2DImage9Slice(driver, bt, r, core::rect<s32>(0, 0, ts.Width, ts.Height),
						core::rect<s32>(8, 8, ts.Width - 8, ts.Height - 8));
			} else {
				driver->draw2DRectangle(video::SColor(255, 111, 111, 111), r);
			}
			const s32 ih = r.getHeight() * 55 / 100;
			rgdsImageFit(rgdsTexture(icons[i]), core::rect<s32>(r.UpperLeftCorner.X,
					r.UpperLeftCorner.Y + r.getHeight() / 10, r.LowerRightCorner.X,
					r.UpperLeftCorner.Y + r.getHeight() / 10 + ih));
			rgdsText(labels[i], core::rect<s32>(r.UpperLeftCorner.X, r.LowerRightCorner.Y - lh * 2,
					r.LowerRightCorner.X, r.LowerRightCorner.Y - lh / 2),
					video::SColor(255, 255, 255, 255));
		}
		break;
	}
	}

	if (mode != BS_LOADING)
		get_gui_env()->drawAll();

	// On-screen keyboard while a text field has the focus
	if (mode == BS_MAINMENU || mode == BS_GAME_MENU) {
		RgdsKbd &kb = g_rgds_kbd;
		gui::IGUIElement *f = get_gui_env()->getFocus();
		// Only real input fields: read-only text boxes (credits, logs) don't count
		const bool edit = f && f->getType() == gui::EGUIET_EDIT_BOX &&
				static_cast<gui::IGUIEditBox *>(f)->isWritable();
		if (!edit)
			kb.dismissed_for = nullptr;
		kb.visible = edit && kb.dismissed_for != f;
		if (kb.visible) {
			kb.layout(ss);
			driver->draw2DRectangle(video::SColor(245, 24, 24, 28), kb.area);
			driver->draw2DRectangle(video::SColor(255, 0, 0, 0), kb.preview);
			std::wstring txt = f->getText();
			if (static_cast<gui::IGUIEditBox *>(f)->isPasswordBox())
				txt.assign(txt.size(), L'*');
			rgdsText(txt + L"_", kb.preview, video::SColor(255, 255, 255, 255));
			video::ITexture *bt = rgdsTexture("rgds_btn.png");
			auto draw_key = [&](const core::rect<s32> &r0, const std::wstring &label, bool lit) {
				core::rect<s32> r(r0.UpperLeftCorner + v2s32(2, 2), r0.LowerRightCorner - v2s32(2, 2));
				if (bt) {
					const auto ts = bt->getOriginalSize();
					draw2DImage9Slice(driver, bt, r, core::rect<s32>(0, 0, ts.Width, ts.Height),
							core::rect<s32>(8, 8, ts.Width - 8, ts.Height - 8));
				} else {
					driver->draw2DRectangle(video::SColor(255, 111, 111, 111), r);
				}
				rgdsText(label, r, lit ? video::SColor(255, 255, 255, 85) : video::SColor(255, 255, 255, 255));
			};
			for (int r = 0; r < RgdsKbd::ROWS; r++) {
				const wchar_t *chars = RgdsKbd::row(r, kb.shift);
				for (int c = 0; c < RgdsKbd::COLS; c++)
					draw_key(kb.key[r][c], std::wstring(1, chars[c]), false);
			}
			draw_key(kb.special[RgdsKbd::SP_SHIFT], L"MAY\u00daS", kb.shift);
			draw_key(kb.special[RgdsKbd::SP_SPACE], L"Espacio", false);
			draw_key(kb.special[RgdsKbd::SP_BACK], L"Borrar", false);
			draw_key(kb.special[RgdsKbd::SP_OK], L"Listo", false);
		}
	} else {
		g_rgds_kbd.visible = false;
	}
	driver->endScene();
	m_device->endSecondScreen();
}

void RenderingEngine::drawMenuTopScreen(video::ITexture *panorama, video::ITexture *header,
		const std::wstring &splash)
{
	auto *driver = get_video_driver();
	const v2u32 ss = driver->getScreenSize();
	if (!panorama)
		panorama = rgdsTexture("rgds_panorama.png");
	if (!header)
		header = rgdsTexture("rgds_header.png");
	if (panorama) {
		const auto ps = panorama->getOriginalSize();
		draw2DImageFilterScaled(driver, panorama, core::rect<s32>(0, 0, ss.X, ss.Y),
				core::rect<s32>(0, 0, ps.Width, ps.Height), nullptr, nullptr, true);
	}
	// Big centered logo, like the Minecraft title screen
	const core::rect<s32> logo_box(ss.X / 14, ss.Y / 6, ss.X * 13 / 14, ss.Y / 6 + ss.Y / 4);
	rgdsImageFit(header, logo_box);
	// Yellow splash text
	if (g_fontengine && !splash.empty()) {
		gui::IGUIFont *big = g_fontengine->getFont(g_fontengine->getDefaultFontSize() * 13 / 10);
		const s32 y = logo_box.LowerRightCorner.Y + 6;
		rgdsText(splash, core::rect<s32>(ss.X / 3, y, ss.X, y + ss.Y / 10),
				video::SColor(255, 255, 255, 0), true, big);
	}
	const s32 lh = g_fontengine ? g_fontengine->getLineHeight() : 20;
	rgdsText(L"Toca la pantalla de abajo para jugar",
			core::rect<s32>(0, ss.Y * 7 / 10, ss.X, ss.Y * 7 / 10 + lh),
			video::SColor(255, 255, 255, 255));
	rgdsText(L"Luanti 5.18  ·  Mineclonia 0.124",
			core::rect<s32>(10, ss.Y - lh - 8, ss.X, ss.Y - 8),
			video::SColor(255, 255, 255, 255), false);
}

std::vector<video::E_DRIVER_TYPE> RenderingEngine::getSupportedVideoDrivers()
{
	// Only check these drivers. We do not support software and D3D in any capacity.
	// ordered by preference (best first)
	static const video::E_DRIVER_TYPE glDrivers[] = {
		video::EDT_OPENGL3,
		video::EDT_OPENGL,
		video::EDT_OGLES2,
		video::EDT_NULL,
	};
	std::vector<video::E_DRIVER_TYPE> drivers;

	for (auto driver : glDrivers) {
		if (IrrlichtDevice::isDriverSupported(driver))
			drivers.push_back(driver);
	}

	return drivers;
}

void RenderingEngine::initialize(Client *client, Hud *hud)
{
	const std::string &draw_mode = g_settings->get("3d_mode");
	core.reset(createRenderingCore(draw_mode, m_device, client, hud));
}

void RenderingEngine::finalize()
{
	core.reset();
}

void RenderingEngine::draw_scene(video::SColor skycolor, bool show_hud,
		bool draw_wield_tool, bool draw_crosshair)
{
	core->draw(skycolor, show_hud, draw_wield_tool, draw_crosshair);
}

const VideoDriverInfo &RenderingEngine::getVideoDriverInfo(video::E_DRIVER_TYPE type)
{
	static const std::unordered_map<int, VideoDriverInfo> driver_info_map = {
		{(int)video::EDT_NULL,    {"null",    "NULL Driver"}},
		{(int)video::EDT_OPENGL,  {"opengl",  "OpenGL (legacy)"}},
		{(int)video::EDT_OPENGL3, {"opengl3", "OpenGL 3+"}},
		{(int)video::EDT_OGLES2,  {"ogles2",  "OpenGL ES 2"}},
	};
	return driver_info_map.at((int)type);
}

void RenderingEngine::showErrorMessageBox(const std::string &message)
{
	auto *device = s_singleton ? s_singleton->m_device : nullptr;
	::showErrorMessageBox(device, PROJECT_NAME_C, message.c_str());
}

float RenderingEngine::getDisplayDensity()
{
	float user_factor = g_settings->getFloat("display_density_factor", 0.5f, 5.0f);
#ifndef __ANDROID__
	float dpi = get_raw_device()->getDisplayDensity();
	if (dpi == 0.0f)
		dpi = 96.0f;
	return std::max(dpi / 96.0f * user_factor, 0.5f);
#else // __ANDROID__
	return porting::getDisplayDensity() * user_factor;
#endif // __ANDROID__
}

void RenderingEngine::autosaveScreensizeAndCo(
		const core::dimension2d<u32> initial_screen_size,
		const bool initial_window_maximized)
{
	if (!g_settings->getBool("autosave_screensize"))
		return;

	// Note: If the screensize or similar hasn't changed (i.e. it's the same as
	// the setting was when luanti started, as given by the initial_* parameters),
	// we do not want to save the thing. This allows users to also manually change
	// the settings.

	// Don't save the fullscreen size, we want the windowed size.
	bool fullscreen = RenderingEngine::get_raw_device()->isFullscreen();
	// Screen size
	const core::dimension2d<u32> current_screen_size =
		RenderingEngine::get_video_driver()->getScreenSize();
	// Don't replace good value with (0, 0)
	if (!fullscreen &&
			current_screen_size != core::dimension2d<u32>(0, 0) &&
			current_screen_size != initial_screen_size) {
		g_settings->setU16("screen_w", current_screen_size.Width);
		g_settings->setU16("screen_h", current_screen_size.Height);
	}

	// Window maximized
	const bool is_window_maximized = RenderingEngine::get_raw_device()
			->isWindowMaximized();
	if (is_window_maximized != initial_window_maximized)
		g_settings->setBool("window_maximized", is_window_maximized);
}
