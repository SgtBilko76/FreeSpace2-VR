# FreeSpace 2 VR (Meta Quest)

FreeSpace 2 running natively on the Meta Quest 3, built on the
[FreeSpace Open](https://github.com/scp-fs2open/fs2open.github.com) engine with
OpenXR on OpenGL ES. Branch: `fs2-vr`.

The APK contains only the engine. You need your own copy of FreeSpace 2
([GOG](https://www.gog.com/game/freespace_2) or
[Steam](https://store.steampowered.com/app/273620/Freespace_2/)); its game files
(about 1.8 GB) go on the headset.

## Install

1. Install `FreeSpace2-VR-<version>.apk` with SideQuest or `adb install -r -g`.
2. Copy the game files to the folder `/sdcard/FS2` on the headset:
   - From an installed game (GOG or Steam install folder on your PC): copy all
     `.vp` files (`root_fs2.vp`, `smarty_fs2.vp`, `sparky_fs2.vp`,
     `sparky_hi_fs2.vp`, `stu_fs2.vp`, `tango1_fs2.vp`, `tango2_fs2.vp`,
     `tango3_fs2.vp`, `warble_fs2.vp`) and the folders `data2` and `data3`
     (cutscenes) to `/sdcard/FS2` with SideQuest. The app moves the cutscenes
     into place on the first start.
   - Or, on a PC with Python 3 and the Quest connected by USB:
     `python3 tools/quest/get_fs2_data.py <game folder or GOG setup_freespace_2_*.exe> --push`
     (the GOG offline installer is unpacked with
     [innoextract](https://constexpr.org/innoextract/)).
3. Start **FreeSpace 2 VR** from *Unknown Sources* and allow
   "access to all files" when asked.

## Controls

Menus, briefings and cutscenes are shown on a large screen in front of you.

| Input | Action |
|---|---|
| Laser (either controller) + trigger | Click |
| A | Enter |
| B / left Menu button | Escape (back / pause) |
| A, B, Menu or trigger during a cutscene | Skip it |

Text fields (pilot callsign, chat): a keyboard appears below the menu screen while the
field is active; point at a key and pull the trigger. Hold Back to keep deleting.

Flight (the Touch controllers act as one gamepad; every binding can be changed
in Options > Controls, "Reset to default" restores this layout):

| Input | Action |
|---|---|
| Left stick | Pitch and turn |
| Right stick left/right | Roll |
| Right stick up/down | Throttle (stays where you leave it) |
| Right stick click | Match target speed |
| Left stick click (tap) | Autopilot |
| Left stick click (hold 1 s) | Warp out (end mission) |
| Right trigger | Guns |
| Left trigger | Missiles |
| Right grip (hold) | Afterburner |
| A | Target nearest enemy (next ship if there is no enemy) |
| B | Target ship in crosshair |
| X | Next missile type |
| Y | Next gun type |
| Left Menu button | Pause menu |
| Left grip + A | Next target |
| Left grip + B | Target your target's attacker |
| Left grip + X | Next subsystem on target |
| Left grip + Y | Comms menu |
| Left grip + right stick up / down | Full speed / full stop |
| Left grip + left stick click | Look behind |
| Left grip + right stick click | Chase view on / off |

Comms menu open: left stick up/down picks an order, A or a trigger confirms,
Y closes the menu.

## Building

Linux host with the Android SDK/NDK: `tools/quest/build_apk.sh --install`.
`tools/quest/run.sh` launches it on a connected headset and
`tools/quest/fps_test.sh` flies the first mission and prints the frame rate.

## License

The FreeSpace Open engine is released under the Volition source code license
(non-commercial use only); see `Copying.md`. FreeSpace 2 game data is not
included; you need your own copy of the game.
