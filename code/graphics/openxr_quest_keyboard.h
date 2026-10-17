#ifndef OPENXR_QUEST_KEYBOARD_H
#define OPENXR_QUEST_KEYBOARD_H

/* Virtual keyboard for the Quest build: layout, hit testing and a CPU
 * rasterizer that draws it into an RGBA image (premultiplied alpha). The
 * OpenXR side (swapchain, quad placement, controller rays, event injection)
 * lives in openxr_quest.cpp / opengl/gropenglopenxr.cpp. */

#include <stdint.h>

enum {
	VR_KB_WIDTH_PX  = 1280,
	VR_KB_HEIGHT_PX = 496,
	VR_KB_NO_KEY    = -1,
};

typedef enum VrKeyKind {
	VR_KEY_KIND_CHAR,
	VR_KEY_KIND_BACKSPACE,
	VR_KEY_KIND_ENTER,
	VR_KEY_KIND_SHIFT,
	VR_KEY_KIND_SPACE,
} VrKeyKind;

typedef struct VrKeyboard {
	int      shift;          /* one-shot: next letter is upper case */
	int      hover[2];       /* key under each controller ray, or VR_KB_NO_KEY */
	int      flash_key;      /* last pressed key, drawn bright until flash_until_ms */
	uint64_t flash_until_ms;
} VrKeyboard;

void VrKeyboard_Reset(VrKeyboard* kb);

/* Key at normalised image coordinates (0..1, v down), or VR_KB_NO_KEY. */
int VrKeyboard_KeyAt(float u, float v);

VrKeyKind VrKeyboard_KeyKind(int key);

/* Applies a key press to the keyboard state (shift). Returns the UTF-8 text a
 * character key types (static storage), or NULL for the special keys. */
const char* VrKeyboard_Press(VrKeyboard* kb, int key, uint64_t now_ms);

/* Draws the keyboard into `rgba` (VR_KB_WIDTH_PX x VR_KB_HEIGHT_PX, 4 bytes
 * per pixel, premultiplied alpha). `bgra` swaps red and blue. */
void VrKeyboard_Render(const VrKeyboard* kb, uint8_t* rgba, int bgra, uint64_t now_ms);

#endif
