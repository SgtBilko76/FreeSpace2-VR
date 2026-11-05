#include "graphics/openxr_quest_radial.h"

#include "graphics/openxr_quest_keyboard_font.h"

#include <math.h>
#include <string.h>

/* Rasterizer (premultiplied RGBA8), same look as the virtual keyboard */

typedef struct RRgba {
	uint8_t r, g, b, a;
} RRgba;

static void r_blend(uint8_t* p, RRgba c, int coverage) {
	const int a   = c.a * coverage / 255;
	const int inv = 255 - a;
	p[0]          = (uint8_t)((c.r * a + p[0] * inv) / 255);
	p[1]          = (uint8_t)((c.g * a + p[1] * inv) / 255);
	p[2]          = (uint8_t)((c.b * a + p[2] * inv) / 255);
	p[3]          = (uint8_t)(a + p[3] * inv / 255);
}

static void r_px(uint8_t* img, int x, int y, RRgba c, int coverage) {
	if (x < 0 || y < 0 || x >= VR_RADIAL_W || y >= VR_RADIAL_H)
		return;
	r_blend(img + ((size_t)y * VR_RADIAL_W + x) * 4, c, coverage);
}

static void r_round_rect(uint8_t* img, int x0, int y0, int x1, int y1, int r, RRgba c) {
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
				r_px(img, x, y, c, 255);
		}
	}
}

/* anti-aliased disc (filled) or ring of the given thickness */
static void r_disc(uint8_t* img, float cx, float cy, float radius, float thickness, RRgba c) {
	const int x0 = (int)(cx - radius - 2), x1 = (int)(cx + radius + 2);
	const int y0 = (int)(cy - radius - 2), y1 = (int)(cy + radius + 2);
	for (int y = y0; y <= y1; ++y) {
		for (int x = x0; x <= x1; ++x) {
			const float d = sqrtf((x + 0.5f - cx) * (x + 0.5f - cx) + (y + 0.5f - cy) * (y + 0.5f - cy));
			float cov     = radius + 0.5f - d; /* outer edge */
			if (thickness > 0.0f) {
				const float inner = d - (radius - thickness) + 0.5f;
				if (inner < cov)
					cov = inner;
			}
			if (cov <= 0.0f)
				continue;
			r_px(img, x, y, c, cov >= 1.0f ? 255 : (int)(cov * 255));
		}
	}
}

static int r_text_width(const char* text, size_t len) {
	int width = 0;
	for (size_t i = 0; i < len && text[i]; ++i) {
		const unsigned char ch = (unsigned char)text[i];
		if (ch >= VR_FONT_FIRST && ch <= VR_FONT_LAST)
			width += vr_font_glyphs[ch - VR_FONT_FIRST].advance;
	}
	return width;
}

static void r_text_line(uint8_t* img, const char* text, size_t len, int cx, int cy, RRgba c) {
	int pen_x    = cx - r_text_width(text, len) / 2;
	int baseline = cy + VR_FONT_ASCENT / 2 - 2;
	for (size_t i = 0; i < len && text[i]; ++i) {
		const unsigned char ch = (unsigned char)text[i];
		if (ch < VR_FONT_FIRST || ch > VR_FONT_LAST)
			continue;
		const VrGlyph* g = &vr_font_glyphs[ch - VR_FONT_FIRST];
		for (int gy = 0; gy < g->h; ++gy) {
			for (int gx = 0; gx < g->w; ++gx) {
				const int coverage = vr_font_bits[g->offset + gy * g->w + gx];
				if (coverage)
					r_px(img, pen_x + g->xoff + gx, baseline + g->yoff + gy, c, coverage);
			}
		}
		pen_x += g->advance;
	}
}

/* one or two lines ('\n'), centred on (cx, cy); returns the widest line */
static int r_label_width(const char* label) {
	const char* nl = strchr(label, '\n');
	if (!nl)
		return r_text_width(label, strlen(label));
	const int a = r_text_width(label, (size_t)(nl - label)), b = r_text_width(nl + 1, strlen(nl + 1));
	return a > b ? a : b;
}

static void r_label(uint8_t* img, const char* label, int cx, int cy, RRgba c) {
	const char* nl = strchr(label, '\n');
	if (!nl) {
		r_text_line(img, label, strlen(label), cx, cy, c);
		return;
	}
	const int half = VR_FONT_HEIGHT / 2 + 1;
	r_text_line(img, label, (size_t)(nl - label), cx, cy - half, c);
	r_text_line(img, nl + 1, strlen(nl + 1), cx, cy + half, c);
}

void VrRadial_Render(uint8_t* rgba, const char* const* labels, int count, int selected, float stick_x, float stick_y) {
	(void)stick_x;
	(void)stick_y;
	VrRadial_RenderDim(rgba, labels, NULL, count, selected);
}

void VrRadial_RenderDim(uint8_t* rgba, const char* const* labels, const int* dim, int count, int selected) {
	static const RRgba panel   = { 12, 16, 24, 200 };
	static const RRgba item    = { 40, 52, 72, 245 };
	static const RRgba itemSel = { 255, 200, 40, 255 };
	static const RRgba text    = { 235, 240, 250, 255 };
	static const RRgba textSel = { 20, 20, 25, 255 };
	static const RRgba itemDim = { 28, 32, 40, 235 };
	static const RRgba textDim = { 120, 128, 140, 255 };
	static const RRgba ring    = { 255, 200, 40, 255 };
	static const RRgba dot     = { 235, 240, 250, 255 };

	memset(rgba, 0, (size_t)VR_RADIAL_W * VR_RADIAL_H * 4);

	const float cx = VR_RADIAL_W * 0.5f, cy = VR_RADIAL_H * 0.5f;
	/* ellipse through the label centres: wide enough that neighbouring labels don't touch */
	const float rx = 650.0f, ry = 390.0f;
	r_disc(rgba, cx, cy, ry * 0.55f, 0.0f, panel);

	const int padX = 16, padY = 10;
	for (int i = 0; i < count; ++i) {
		const float ang = i * (6.2831853f / count);
		const int x     = (int)(cx + rx * sinf(ang));
		const int y     = (int)(cy - ry * cosf(ang));
		const int lines = strchr(labels[i], '\n') ? 2 : 1;
		const int w     = r_label_width(labels[i]);
		const int h     = lines * (VR_FONT_HEIGHT + 2);
		const int sel   = i == selected;
		const int off   = dim && dim[i];
		r_round_rect(rgba, x - w / 2 - padX, y - h / 2 - padY, x + w / 2 + padX, y + h / 2 + padY, 14, sel ? itemSel : (off ? itemDim : item));
		r_label(rgba, labels[i], x, y, sel ? textSel : (off ? textDim : text));
	}

	/* centre: ring, plus a pointer toward the selected item (redrawn only when the selection
	 * changes, so the image isn't re-uploaded every frame) */
	const float stickR = ry * 0.42f;
	r_disc(rgba, cx, cy, stickR, 4.0f, ring);
	if (selected >= 0 && selected < count) {
		const float ang = selected * (6.2831853f / count);
		r_disc(rgba, cx + stickR * 0.7f * sinf(ang) * (rx / ry) * 0.6f, cy - stickR * 0.7f * cosf(ang), 18.0f, 0.0f, dot);
	} else {
		r_disc(rgba, cx, cy, 12.0f, 0.0f, dot);
	}
}
