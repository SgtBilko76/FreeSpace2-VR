#pragma once

// Meta Quest (Android) OpenXR additions: Touch controller input.
//  - Menus (mono frames, shown on the virtual screen): laser pointer from the controller
//    drives the mouse, trigger = left click, A = Enter, B = Escape.
//  - Flight (stereo frames): Touch is mirrored 1:1 onto a virtual SDL gamepad
//    ("Quest Touch (VR)") that FSO uses as Joystick 0, so every control is bindable
//    in the game's own control options.
//  - Left menu button = Escape everywhere.
// All functions are no-ops on other platforms.

#include "globalincs/pstypes.h"

SCP_vector<const char*> openxr_quest_instance_extensions();
void openxr_quest_init();
void openxr_quest_close();
void openxr_quest_update(bool stereo_frame);

// Cockpit-fixed HUD layer (stereo frames): draw the HUD between begin() and end().
// begin() returns false when the HUD layer is not available (then draw nothing).
bool openxr_quest_hud_begin();
void openxr_quest_hud_end();
// Half vertical FOV (radians) of the symmetric camera the HUD layer is drawn with (0 = not in VR)
float openxr_quest_hud_half_fov();
// The HUD is drawn on a 4:3 canvas (the aspect its gauge layouts are made for), so gauges and
// the crosshair keep their shape: width = eye render width, height = width / this aspect
constexpr float OPENXR_QUEST_HUD_ASPECT = 4.0f / 3.0f;
// Distance (metres) of the HUD layer in front of the recentred head; far enough that head
// movement and eye convergence barely shift the reticle against distant targets
constexpr float OPENXR_QUEST_HUD_DISTANCE = 10.0f;
// While the death popup is up in flight it is drawn on the HUD layer (once, cockpit-fixed) and
// the laser pointer aims at that layer; mouse coordinates are then in HUD canvas pixels.

// Virtual keyboard (menus): Android's system keyboard never shows in an immersive OpenXR app,
// so a laser keyboard quad is shown below the menu screen while a text field has focus.
// Call every frame a focused text input box is processed (pilot callsign, chat, ...).
void openxr_quest_text_field_active();
// Keyboard state for the compositor (gropenglopenxr.cpp)
bool openxr_quest_keyboard_visible();
// Redraws the keyboard image (OPENXR_QUEST_KB_W x OPENXR_QUEST_KB_H, RGBA8, premultiplied,
// top row first) when it changed; returns false when the last image is still current
bool openxr_quest_keyboard_render(uint8_t* rgba);
// Quad pose in the OpenXR app space: centre (x, y, z), orientation quaternion (x, y, z, w), size (w, h)
void openxr_quest_keyboard_pose(float pos[3], float quat[4], float size[2]);
constexpr int OPENXR_QUEST_KB_W = 1280;
constexpr int OPENXR_QUEST_KB_H = 496;

// Name of the virtual gamepad (used to auto-select it as Joystick 0)
extern const char* const OPENXR_QUEST_PAD_NAME;
