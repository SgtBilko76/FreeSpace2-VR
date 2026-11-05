#ifndef OPENXR_QUEST_SETUP_H
#define OPENXR_QUEST_SETUP_H

/* VR controller setup panel for the Quest build: opened in flight by a long press of the
 * left Menu button, shown on the same quad as the virtual keyboard
 * (VR_KB_WIDTH_PX x VR_KB_HEIGHT_PX). Two buttons pick the stick layout. Drawn by a CPU
 * rasterizer into an RGBA image (premultiplied alpha); input and the quad pose live in
 * openxr_quest.cpp. */

#include <stdint.h>

enum {
	VR_STICKS_JET     = 0, /* right stick flies, left stick = throttle lever + roll */
	VR_STICKS_CLASSIC = 1, /* left stick flies, right stick = throttle lever + roll */
	VR_SETUP_NONE     = -1,
};

/* Button (VR_STICKS_JET / VR_STICKS_CLASSIC) at normalised image coordinates (0..1, v down),
 * or VR_SETUP_NONE. */
int VrSetup_ButtonAt(float u, float v);

/* Draws the panel. `active` = current layout, hover0/hover1 = button under each controller
 * ray (or VR_SETUP_NONE). */
void VrSetup_Render(uint8_t* rgba, int active, int hover0, int hover1);

#endif
