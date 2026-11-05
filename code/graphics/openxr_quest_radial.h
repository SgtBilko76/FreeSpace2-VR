#ifndef OPENXR_QUEST_RADIAL_H
#define OPENXR_QUEST_RADIAL_H

/* Radial menu image for the Quest build: a ring of labelled actions drawn by a
 * CPU rasterizer into an RGBA image (premultiplied alpha), shown as a small quad
 * above the left controller. Input handling and the quad pose live in
 * openxr_quest.cpp; the swapchain upload in opengl/gropenglopenxr.cpp. */

#include <stdint.h>

enum {
	VR_RADIAL_W = 1600,
	VR_RADIAL_H = 1000,
};

/* Draws `count` labels (a '\n' splits a label into two lines) clockwise from the
 * top, `selected` (or -1) highlighted. stick_x/stick_y are reserved (unused). */
void VrRadial_Render(uint8_t* rgba, const char* const* labels, int count, int selected, float stick_x, float stick_y);

#endif
