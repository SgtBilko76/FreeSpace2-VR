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

// Name of the virtual gamepad (used to auto-select it as Joystick 0)
extern const char* const OPENXR_QUEST_PAD_NAME;
