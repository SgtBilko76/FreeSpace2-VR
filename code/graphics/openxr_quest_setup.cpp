#include "graphics/openxr_quest_setup.h"

#include "graphics/openxr_quest_keyboard.h"
#include "graphics/openxr_quest_keyboard_font.h"

#include <string.h>

/* Rasterizer (premultiplied RGBA8), same look as the keyboard and the radial menus */

namespace {

const int W = VR_KB_WIDTH_PX, H = VR_KB_HEIGHT_PX;

struct SRgba {
	uint8_t r, g, b, a;
};

void s_blend(uint8_t* p, SRgba c, int coverage)
{
	const int a   = c.a * coverage / 255;
	const int inv = 255 - a;
	p[0]          = (uint8_t)((c.r * a + p[0] * inv) / 255);
	p[1]          = (uint8_t)((c.g * a + p[1] * inv) / 255);
	p[2]          = (uint8_t)((c.b * a + p[2] * inv) / 255);
	p[3]          = (uint8_t)(a + p[3] * inv / 255);
}

void s_px(uint8_t* img, int x, int y, SRgba c, int coverage)
{
	if (x < 0 || y < 0 || x >= W || y >= H)
		return;
	s_blend(img + ((size_t)y * W + x) * 4, c, coverage);
}

void s_round_rect(uint8_t* img, int x0, int y0, int x1, int y1, int r, SRgba c)
{
	for (int y = y0; y < y1; ++y) {
		for (int x = x0; x < x1; ++x) {
			int dx = 0, dy = 0;
			if (x < x0 + r)
				dx = x0 + r - x;
			else if (x >= x1 - r)
				dx = x - (x1 - r - 1);
			if (y < y0 + r)
				dy = y0 + r - y;
			else if (y >= y1 - r)
				dy = y - (y1 - r - 1);
			if (dx * dx + dy * dy <= r * r)
				s_px(img, x, y, c, 255);
		}
	}
}

int s_text_width(const char* text)
{
	int width = 0;
	for (const char* p = text; *p; ++p) {
		const unsigned char ch = (unsigned char)*p;
		if (ch >= VR_FONT_FIRST && ch <= VR_FONT_LAST)
			width += vr_font_glyphs[ch - VR_FONT_FIRST].advance;
	}
	return width;
}

void s_text(uint8_t* img, const char* text, int cx, int cy, SRgba c)
{
	int pen_x    = cx - s_text_width(text) / 2;
	int baseline = cy + VR_FONT_ASCENT / 2 - 2;
	for (const char* p = text; *p; ++p) {
		const unsigned char ch = (unsigned char)*p;
		if (ch < VR_FONT_FIRST || ch > VR_FONT_LAST)
			continue;
		const VrGlyph* g = &vr_font_glyphs[ch - VR_FONT_FIRST];
		for (int gy = 0; gy < g->h; ++gy)
			for (int gx = 0; gx < g->w; ++gx) {
				const int coverage = vr_font_bits[g->offset + gy * g->w + gx];
				if (coverage)
					s_px(img, pen_x + g->xoff + gx, baseline + g->yoff + gy, c, coverage);
			}
		pen_x += g->advance;
	}
}

// button rectangles (pixels): Jet left, Classic right
const int BTN_Y0 = 100, BTN_Y1 = 404;
const int BTN_X[2][2] = { { 40, 620 }, { 660, 1240 } };

struct ButtonText {
	const char* name;
	const char* lines[2];
};
const ButtonText BUTTONS[2] = {
	{ "Jet", { "Right stick: fly", "Left stick: throttle, roll" } },
	{ "Classic", { "Left stick: fly", "Right stick: throttle, roll" } },
};

} // namespace

int VrSetup_ButtonAt(float u, float v)
{
	const int x = (int)(u * W), y = (int)(v * H);
	if (y < BTN_Y0 || y >= BTN_Y1)
		return VR_SETUP_NONE;
	for (int b = 0; b < 2; b++)
		if (x >= BTN_X[b][0] && x < BTN_X[b][1])
			return b;
	return VR_SETUP_NONE;
}

void VrSetup_Render(uint8_t* rgba, int active, int hover0, int hover1)
{
	static const SRgba panel   = { 12, 16, 24, 225 };
	static const SRgba item    = { 40, 52, 72, 245 };
	static const SRgba itemHov = { 64, 82, 112, 250 };
	static const SRgba itemSel = { 255, 200, 40, 255 };
	static const SRgba text    = { 235, 240, 250, 255 };
	static const SRgba dim     = { 170, 180, 196, 255 };
	static const SRgba textSel = { 20, 20, 25, 255 };
	static const SRgba frame   = { 235, 240, 250, 255 };

	memset(rgba, 0, (size_t)W * H * 4);
	s_round_rect(rgba, 0, 0, W, H, 28, panel);
	s_text(rgba, "VR controller setup: sticks", W / 2, 52, text);

	for (int b = 0; b < 2; b++) {
		const int x0 = BTN_X[b][0], x1 = BTN_X[b][1];
		const bool sel = b == active, hov = b == hover0 || b == hover1;
		if (hov) // white frame under the laser
			s_round_rect(rgba, x0 - 6, BTN_Y0 - 6, x1 + 6, BTN_Y1 + 6, 26, frame);
		s_round_rect(rgba, x0, BTN_Y0, x1, BTN_Y1, 22, sel ? itemSel : (hov ? itemHov : item));
		const int cx = (x0 + x1) / 2;
		const SRgba& t = sel ? textSel : text;
		s_text(rgba, BUTTONS[b].name, cx, BTN_Y0 + 62, t);
		s_text(rgba, BUTTONS[b].lines[0], cx, BTN_Y0 + 152, t);
		s_text(rgba, BUTTONS[b].lines[1], cx, BTN_Y0 + 212, t);
		if (sel)
			s_text(rgba, "(active)", cx, BTN_Y0 + 266, t);
	}
	s_text(rgba, "Point + trigger: saved at once.  Menu or B: close", W / 2, 448, dim);
}
