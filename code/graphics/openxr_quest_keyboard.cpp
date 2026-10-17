#include "graphics/openxr_quest_keyboard.h"

#include "graphics/openxr_quest_keyboard_font.h"

#include <string.h>

/* Layout in key units; one unit = KEY_UNIT_PX. Pilot callsigns only take letters,
 * digits, space, _ and -; the input box itself ignores the other characters. */
typedef struct VrKeyDef {
	float       x, w;
	int         row;
	VrKeyKind   kind;
	const char* lower;
	const char* upper;
} VrKeyDef;

#define CH(x, row, lo, up) { (float)(x), 1.0f, row, VR_KEY_KIND_CHAR, lo, up }

static const VrKeyDef k_keys[] = {
	/* row 0 */
	CH(0, 0, "1", "1"), CH(1, 0, "2", "2"), CH(2, 0, "3", "3"), CH(3, 0, "4", "4"), CH(4, 0, "5", "5"),
	CH(5, 0, "6", "6"), CH(6, 0, "7", "7"), CH(7, 0, "8", "8"), CH(8, 0, "9", "9"), CH(9, 0, "0", "0"),
	CH(10, 0, "-", "-"),
	{ 11.0f, 2.5f, 0, VR_KEY_KIND_BACKSPACE, "Back", "Back" },
	/* row 1 */
	CH(0.5, 1, "q", "Q"), CH(1.5, 1, "w", "W"), CH(2.5, 1, "e", "E"), CH(3.5, 1, "r", "R"), CH(4.5, 1, "t", "T"),
	CH(5.5, 1, "y", "Y"), CH(6.5, 1, "u", "U"), CH(7.5, 1, "i", "I"), CH(8.5, 1, "o", "O"), CH(9.5, 1, "p", "P"),
	CH(10.5, 1, ".", "."), CH(11.5, 1, "'", "'"), CH(12.5, 1, "!", "!"),
	/* row 2 */
	CH(1, 2, "a", "A"), CH(2, 2, "s", "S"), CH(3, 2, "d", "D"), CH(4, 2, "f", "F"), CH(5, 2, "g", "G"),
	CH(6, 2, "h", "H"), CH(7, 2, "j", "J"), CH(8, 2, "k", "K"), CH(9, 2, "l", "L"),
	{ 10.0f, 3.5f, 2, VR_KEY_KIND_ENTER, "Enter", "Enter" },
	/* row 3 */
	{ 0.0f, 2.0f, 3, VR_KEY_KIND_SHIFT, "Shift", "Shift" },
	CH(2, 3, "z", "Z"), CH(3, 3, "x", "X"), CH(4, 3, "c", "C"), CH(5, 3, "v", "V"), CH(6, 3, "b", "B"),
	CH(7, 3, "n", "N"), CH(8, 3, "m", "M"), CH(9, 3, "_", "_"), CH(10, 3, "#", "#"), CH(11, 3, "?", "?"),
	{ 12.0f, 1.5f, 3, VR_KEY_KIND_SHIFT, "Shift", "Shift" },
	/* row 4 */
	{ 3.0f, 7.5f, 4, VR_KEY_KIND_SPACE, "Space", "Space" },
};

#undef CH

enum {
	KEY_COUNT   = (int)(sizeof(k_keys) / sizeof(k_keys[0])),
	MARGIN_PX   = 16,
	KEY_UNIT_PX = 92,
	KEY_GAP_PX  = 6,
	KEY_RADIUS  = 10,
};

static void key_rect(int key, int* x0, int* y0, int* x1, int* y1) {
	const VrKeyDef* def = &k_keys[key];
	const int       ox  = (VR_KB_WIDTH_PX - (int)(13.5f * KEY_UNIT_PX)) / 2;

	*x0 = ox + (int)(def->x * KEY_UNIT_PX) + KEY_GAP_PX / 2;
	*x1 = ox + (int)((def->x + def->w) * KEY_UNIT_PX) - KEY_GAP_PX / 2;
	*y0 = MARGIN_PX + def->row * KEY_UNIT_PX + KEY_GAP_PX / 2;
	*y1 = MARGIN_PX + (def->row + 1) * KEY_UNIT_PX - KEY_GAP_PX / 2;
}

void VrKeyboard_Reset(VrKeyboard* kb) {
	memset(kb, 0, sizeof(*kb));
	kb->shift     = 1;
	kb->hover[0]  = VR_KB_NO_KEY;
	kb->hover[1]  = VR_KB_NO_KEY;
	kb->flash_key = VR_KB_NO_KEY;
}

int VrKeyboard_KeyAt(float u, float v) {
	const int px = (int)(u * VR_KB_WIDTH_PX);
	const int py = (int)(v * VR_KB_HEIGHT_PX);

	for (int key = 0; key < KEY_COUNT; ++key) {
		int x0, y0, x1, y1;
		key_rect(key, &x0, &y0, &x1, &y1);
		/* Include the gap so the ray never falls between keys. */
		if (px >= x0 - KEY_GAP_PX / 2 && px < x1 + KEY_GAP_PX / 2 && py >= y0 - KEY_GAP_PX / 2 &&
			py < y1 + KEY_GAP_PX / 2) {
			return key;
		}
	}
	return VR_KB_NO_KEY;
}

VrKeyKind VrKeyboard_KeyKind(int key) {
	return key >= 0 && key < KEY_COUNT ? k_keys[key].kind : VR_KEY_KIND_CHAR;
}

const char* VrKeyboard_Press(VrKeyboard* kb, int key, uint64_t now_ms) {
	const VrKeyDef* def;

	if (key < 0 || key >= KEY_COUNT) {
		return NULL;
	}
	def                = &k_keys[key];
	kb->flash_key      = key;
	kb->flash_until_ms = now_ms + 150;
	switch (def->kind) {
		case VR_KEY_KIND_SHIFT:
			kb->shift = !kb->shift;
			return NULL;
		case VR_KEY_KIND_SPACE:
			return " ";
		case VR_KEY_KIND_CHAR: {
			const char* text = kb->shift ? def->upper : def->lower;
			if (def->lower[0] >= 'a' && def->lower[0] <= 'z') {
				kb->shift = 0;
			}
			return text;
		}
		default:
			return NULL;
	}
}

/* ------------------------------------------------------------------------ */
/* Rasterizer (premultiplied RGBA8)                                         */

typedef struct Rgba {
	uint8_t r, g, b, a;
} Rgba;

static void blend_px(uint8_t* p, Rgba c, int coverage) {
	/* c is straight alpha; coverage 0..255 */
	const int a   = c.a * coverage / 255;
	const int inv = 255 - a;
	p[0]          = (uint8_t)((c.r * a + p[0] * inv) / 255);
	p[1]          = (uint8_t)((c.g * a + p[1] * inv) / 255);
	p[2]          = (uint8_t)((c.b * a + p[2] * inv) / 255);
	p[3]          = (uint8_t)(a + p[3] * inv / 255);
}

static void fill_round_rect(uint8_t* img, int x0, int y0, int x1, int y1, int r, Rgba c) {
	for (int y = y0; y < y1; ++y) {
		if (y < 0 || y >= VR_KB_HEIGHT_PX) {
			continue;
		}
		for (int x = x0; x < x1; ++x) {
			int dx = 0, dy = 0;
			if (x < 0 || x >= VR_KB_WIDTH_PX) {
				continue;
			}
			if (x < x0 + r) {
				dx = x0 + r - x;
			} else if (x >= x1 - r) {
				dx = x - (x1 - r - 1);
			}
			if (y < y0 + r) {
				dy = y0 + r - y;
			} else if (y >= y1 - r) {
				dy = y - (y1 - r - 1);
			}
			if (dx * dx + dy * dy > r * r) {
				continue;
			}
			blend_px(img + ((size_t)y * VR_KB_WIDTH_PX + x) * 4, c, 255);
		}
	}
}

static int text_width(const char* text) {
	int width = 0;
	for (; *text; ++text) {
		const unsigned char ch = (unsigned char)*text;
		if (ch >= VR_FONT_FIRST && ch <= VR_FONT_LAST) {
			width += vr_font_glyphs[ch - VR_FONT_FIRST].advance;
		}
	}
	return width;
}

static void draw_text_centered(uint8_t* img, const char* text, int cx, int cy, Rgba c) {
	int pen_x    = cx - text_width(text) / 2;
	int baseline = cy + VR_FONT_ASCENT / 2 - 2;

	for (; *text; ++text) {
		const unsigned char ch = (unsigned char)*text;
		const VrGlyph*      g;
		if (ch < VR_FONT_FIRST || ch > VR_FONT_LAST) {
			continue;
		}
		g = &vr_font_glyphs[ch - VR_FONT_FIRST];
		for (int gy = 0; gy < g->h; ++gy) {
			const int y = baseline + g->yoff + gy;
			if (y < 0 || y >= VR_KB_HEIGHT_PX) {
				continue;
			}
			for (int gx = 0; gx < g->w; ++gx) {
				const int x        = pen_x + g->xoff + gx;
				const int coverage = vr_font_bits[g->offset + gy * g->w + gx];
				if (coverage && x >= 0 && x < VR_KB_WIDTH_PX) {
					blend_px(img + ((size_t)y * VR_KB_WIDTH_PX + x) * 4, c, coverage);
				}
			}
		}
		pen_x += g->advance;
	}
}

void VrKeyboard_Render(const VrKeyboard* kb, uint8_t* rgba, int bgra, uint64_t now_ms) {
	static const Rgba panel     = { 12, 16, 24, 225 };
	static const Rgba key_char  = { 52, 58, 70, 245 };
	static const Rgba key_spec  = { 34, 40, 52, 245 };
	static const Rgba key_hover = { 70, 120, 200, 255 };
	static const Rgba key_shift = { 200, 140, 40, 255 };
	static const Rgba key_flash = { 230, 230, 240, 255 };
	static const Rgba label     = { 240, 240, 245, 255 };
	static const Rgba label_dk  = { 20, 20, 25, 255 };

	memset(rgba, 0, (size_t)VR_KB_WIDTH_PX * VR_KB_HEIGHT_PX * 4);
	fill_round_rect(rgba, 0, 0, VR_KB_WIDTH_PX, VR_KB_HEIGHT_PX, 24, panel);

	for (int key = 0; key < KEY_COUNT; ++key) {
		const VrKeyDef* def     = &k_keys[key];
		const int       flashed = key == kb->flash_key && now_ms < kb->flash_until_ms;
		const int       hovered = key == kb->hover[0] || key == kb->hover[1];
		Rgba            fill    = def->kind == VR_KEY_KIND_CHAR ? key_char : key_spec;
		int             x0, y0, x1, y1;

		if (def->kind == VR_KEY_KIND_SHIFT && kb->shift) {
			fill = key_shift;
		}
		if (hovered) {
			fill = key_hover;
		}
		if (flashed) {
			fill = key_flash;
		}
		key_rect(key, &x0, &y0, &x1, &y1);
		fill_round_rect(rgba, x0, y0, x1, y1, KEY_RADIUS, fill);
		draw_text_centered(rgba, kb->shift ? def->upper : def->lower, (x0 + x1) / 2, (y0 + y1) / 2,
						   flashed ? label_dk : label);
	}

	if (bgra) {
		const size_t count = (size_t)VR_KB_WIDTH_PX * VR_KB_HEIGHT_PX;
		for (size_t i = 0; i < count; ++i) {
			const uint8_t r = rgba[i * 4];
			rgba[i * 4]     = rgba[i * 4 + 2];
			rgba[i * 4 + 2] = r;
		}
	}
}
