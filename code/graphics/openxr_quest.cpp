#include "openxr_quest.h"

const char* const OPENXR_QUEST_PAD_NAME = "Quest Touch (VR)";

#if defined(FS_OPENXR) && defined(__ANDROID__)

#include <jni.h>
#include <android/log.h>
#include <SDL3/SDL.h>

#define XR_USE_PLATFORM_ANDROID
#include "openxr_internal.h"

// OpenXR structs are initialised with their type only; the rest is zeroed on purpose
#pragma clang diagnostic ignored "-Wmissing-field-initializers"

#include "graphics/2d.h"
#include "graphics/openxr_quest_keyboard.h"
#include "io/key.h"
#include "io/mouse.h"
#include "osapi/osapi.h"
#include "playerman/player.h"
#include "popup/popupdead.h"

#define QLOG(...) __android_log_print(ANDROID_LOG_INFO, "QuestVR", __VA_ARGS__)

namespace {

enum Hand { LEFT = 0, RIGHT = 1 };

XrActionSet actionSet = XR_NULL_HANDLE;
XrAction aimPose = XR_NULL_HANDLE;
XrAction trigger = XR_NULL_HANDLE;
XrAction grip = XR_NULL_HANDLE;
XrAction stick = XR_NULL_HANDLE;
XrAction stickClick = XR_NULL_HANDLE;
XrAction btnA = XR_NULL_HANDLE, btnB = XR_NULL_HANDLE, btnX = XR_NULL_HANDLE, btnY = XR_NULL_HANDLE;
XrAction btnMenu = XR_NULL_HANDLE;
XrPath handPath[2] = { XR_NULL_PATH, XR_NULL_PATH };
XrSpace aimSpace[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
bool actionsReady = false;

SDL_Joystick* pad = nullptr;

// previous button states for edge detection
bool prevTrigger = false;
bool prevA = false, prevB = false, prevMenu = false;
int pointerHand = RIGHT;

// keys injected this frame that must be released on the next update
SCP_vector<int> pendingKeyUps;

// Throttle lever (0 = stop .. 1 = full), driven by the right stick Y and held where it is left
float throttleLever = 0.0f;
bool prevShiftStickUp = false, prevShiftStickDown = false;
bool prevCommsUp = false, prevCommsDown = false;
float lastUpdateSeconds = -1.0f;
// left stick click: tap = autopilot (sent on release), hold = warp out
constexpr float WARP_HOLD_SECONDS = 1.0f;
float clickLDownSince = -1.0f;   // < 0: not held
bool clickLWarpSent = false;
int autopilotPulse = 0, warpPulse = 0; // frames left to hold the pad button down

// Virtual keyboard (shown below the menu screen while a text field has focus)
static_assert(OPENXR_QUEST_KB_W == VR_KB_WIDTH_PX && OPENXR_QUEST_KB_H == VR_KB_HEIGHT_PX, "keyboard image size");
Uint64 textFieldMs = 0;       // last frame a focused text input box was processed
bool kbVisible = false;
bool kbDirty = true;          // image must be redrawn
VrKeyboard kb;
XrVector3f kbPos{ 0.0f, 0.0f, 0.0f };
XrQuaternionf kbQuat{ 0.0f, 0.0f, 0.0f, 1.0f };
float kbW = 1.1f, kbH = 1.1f * VR_KB_HEIGHT_PX / VR_KB_WIDTH_PX;
bool kbTrig[2] = { false, false };    // trigger state per hand (with hysteresis)
bool kbHeld[2] = { false, false };    // this trigger press started on the keyboard
Uint64 kbRepeatMs = 0;                // next Backspace repeat

XrPath path(const char* s)
{
	XrPath p = XR_NULL_PATH;
	xrStringToPath(xr_instance, s, &p);
	return p;
}

XrAction makeAction(const char* name, const char* localized, XrActionType type, bool perHand)
{
	XrActionCreateInfo info{ XR_TYPE_ACTION_CREATE_INFO };
	strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
	strncpy(info.localizedActionName, localized, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
	info.actionType = type;
	if (perHand) {
		info.countSubactionPaths = 2;
		info.subactionPaths = handPath;
	}
	XrAction a = XR_NULL_HANDLE;
	XrResult r = xrCreateAction(actionSet, &info, &a);
	if (r != XR_SUCCESS)
		QLOG("xrCreateAction(%s) failed: %d", name, (int)r);
	return a;
}

float getFloat(XrAction a, int hand)
{
	XrActionStateGetInfo gi{ XR_TYPE_ACTION_STATE_GET_INFO, nullptr, a, handPath[hand] };
	XrActionStateFloat st{ XR_TYPE_ACTION_STATE_FLOAT };
	if (xrGetActionStateFloat(xr_session, &gi, &st) != XR_SUCCESS || !st.isActive)
		return 0.0f;
	return st.currentState;
}

bool getBool(XrAction a, XrPath sub = XR_NULL_PATH)
{
	XrActionStateGetInfo gi{ XR_TYPE_ACTION_STATE_GET_INFO, nullptr, a, sub };
	XrActionStateBoolean st{ XR_TYPE_ACTION_STATE_BOOLEAN };
	if (xrGetActionStateBoolean(xr_session, &gi, &st) != XR_SUCCESS || !st.isActive)
		return false;
	return st.currentState == XR_TRUE;
}

XrVector2f getVec2(XrAction a, int hand)
{
	XrActionStateGetInfo gi{ XR_TYPE_ACTION_STATE_GET_INFO, nullptr, a, handPath[hand] };
	XrActionStateVector2f st{ XR_TYPE_ACTION_STATE_VECTOR2F };
	if (xrGetActionStateVector2f(xr_session, &gi, &st) != XR_SUCCESS || !st.isActive)
		return XrVector2f{ 0.0f, 0.0f };
	return st.currentState;
}

Sint16 axisValue(float v)
{
	v = std::max(-1.0f, std::min(1.0f, v));
	return static_cast<Sint16>(v * 32767.0f);
}

Sint16 triggerValue(float v)
{
	// SDL gamepad triggers: 0 (released) .. 32767 (full)
	v = std::max(0.0f, std::min(1.0f, v));
	return static_cast<Sint16>(v * 32767.0f);
}

void pressKey(int key)
{
	key_mark(key, 1, 0);
	pendingKeyUps.push_back(key);
}

void createPad()
{
	if (!SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD)) {
		QLOG("SDL joystick init failed: %s", SDL_GetError());
		return;
	}

	SDL_VirtualJoystickDesc desc;
	SDL_INIT_INTERFACE(&desc);
	desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
	desc.vendor_id = 0x2833;  // Oculus
	desc.product_id = 0x5157; // "WQ"
	desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
	desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
	// consecutive masks: joystick axis/button index == SDL gamepad enum
	desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_COUNT) - 1;
	desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
	desc.name = OPENXR_QUEST_PAD_NAME;

	SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
	if (id == 0) {
		QLOG("SDL_AttachVirtualJoystick failed: %s", SDL_GetError());
		return;
	}
	pad = SDL_OpenJoystick(id);
	QLOG("virtual gamepad '%s' attached (id %u, open %s)", OPENXR_QUEST_PAD_NAME, (unsigned)id, pad ? "ok" : SDL_GetError());

	// rest position: triggers released
	if (pad) {
		SDL_SetJoystickVirtualAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 0);
		SDL_SetJoystickVirtualAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 0);
	}
}

void setPadNeutral()
{
	if (!pad)
		return;
	for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++)
		SDL_SetJoystickVirtualAxis(pad, a, 0);
	// keep the throttle lever where it is (a centred axis would mean half speed)
	SDL_SetJoystickVirtualAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY, axisValue(1.0f - 2.0f * throttleLever));
	for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++)
		SDL_SetJoystickVirtualButton(pad, b, false);
}

// Rotate v by unit quaternion q
XrVector3f rotate(const XrQuaternionf& q, const XrVector3f& v)
{
	// t = 2 * cross(q.xyz, v); v' = v + w*t + cross(q.xyz, t)
	XrVector3f t{ 2.0f * (q.y * v.z - q.z * v.y), 2.0f * (q.z * v.x - q.x * v.z), 2.0f * (q.x * v.y - q.y * v.x) };
	return XrVector3f{ v.x + q.w * t.x + (q.y * t.z - q.z * t.y),
		v.y + q.w * t.y + (q.z * t.x - q.x * t.z),
		v.z + q.w * t.z + (q.x * t.y - q.y * t.x) };
}

// Laser pointer: intersect the aim ray with the virtual screen drawn by
// gr_opengl_openxr_flip() (plane z = 4, height 4, width 4 * clip_aspect, centred on the
// recentre point xr_offset; FSO axes = OpenXR axes with z flipped).
bool pointerHit(int hand, float& mx, float& my)
{
	if (aimSpace[hand] == XR_NULL_HANDLE)
		return false;
	XrSpaceLocation loc{ XR_TYPE_SPACE_LOCATION };
	if (xrLocateSpace(aimSpace[hand], xr_space, xr_state.predictedDisplayTime, &loc) != XR_SUCCESS)
		return false;
	const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
	if ((loc.locationFlags & need) != need)
		return false;

	XrVector3f d = rotate(loc.pose.orientation, XrVector3f{ 0.0f, 0.0f, -1.0f });
	float px = loc.pose.position.x - xr_offset.xyz.x;
	float py = loc.pose.position.y - xr_offset.xyz.y;
	float pz = -loc.pose.position.z - xr_offset.xyz.z;
	float dx = d.x, dy = d.y, dz = -d.z;
	if (dz < 0.05f)
		return false;

	const float planeZ = 4.0f;
	float t = (planeZ - pz) / dz;
	float hx = px + t * dx;
	float hy = py + t * dy;

	float aspect = gr_screen.clip_aspect;
	float u = (hx / (2.0f * aspect) + 1.0f) * 0.5f;
	float v = (1.0f - hy / 2.0f) * 0.5f;
	if (u < -0.1f || u > 1.1f || v < -0.1f || v > 1.1f)
		return false;

	mx = std::max(0.0f, std::min(1.0f, u)) * (gr_screen.max_w - 1);
	my = std::max(0.0f, std::min(1.0f, v)) * (gr_screen.max_h - 1);
	return true;
}

// Same for the cockpit-fixed HUD layer (a 4:3 quad OPENXR_QUEST_HUD_DISTANCE ahead, centred on
// the recentred head); returns HUD canvas pixels (max_w x max_w / OPENXR_QUEST_HUD_ASPECT)
bool pointerHitHud(int hand, float& mx, float& my)
{
	if (aimSpace[hand] == XR_NULL_HANDLE)
		return false;
	XrSpaceLocation loc{ XR_TYPE_SPACE_LOCATION };
	if (xrLocateSpace(aimSpace[hand], xr_space, xr_state.predictedDisplayTime, &loc) != XR_SUCCESS)
		return false;
	const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
	if ((loc.locationFlags & need) != need)
		return false;

	XrVector3f d = rotate(loc.pose.orientation, XrVector3f{ 0.0f, 0.0f, -1.0f });
	const float px = loc.pose.position.x - xr_offset.xyz.x;
	const float py = loc.pose.position.y - xr_offset.xyz.y;
	const float pz = -loc.pose.position.z - xr_offset.xyz.z;
	const float dx = d.x, dy = d.y, dz = -d.z;
	if (dz < 0.05f)
		return false;

	const float D = OPENXR_QUEST_HUD_DISTANCE;
	const float t = (D - pz) / dz;
	const float hx = px + t * dx;
	const float hy = py + t * dy;

	const float tV = std::tan(openxr_quest_hud_half_fov());
	const float tH = tV * OPENXR_QUEST_HUD_ASPECT;
	if (tV <= 0.0f)
		return false;
	const float u = (hx / (D * tH) + 1.0f) * 0.5f;
	const float v = (1.0f - hy / (D * tV)) * 0.5f;
	if (u < -0.1f || u > 1.1f || v < -0.1f || v > 1.1f)
		return false;

	const float canvas_h = std::round(gr_screen.max_w / OPENXR_QUEST_HUD_ASPECT);
	mx = std::max(0.0f, std::min(1.0f, u)) * (gr_screen.max_w - 1);
	my = std::max(0.0f, std::min(1.0f, v)) * (canvas_h - 1);
	return true;
}

XrQuaternionf conjugate(const XrQuaternionf& q)
{
	return XrQuaternionf{ -q.x, -q.y, -q.z, q.w };
}

// Desk-like keyboard quad: 1.4 m ahead of and 0.55 m below the recentred head, tilted
// 35 degrees toward the player. App-space (OpenXR) coordinates; xr_offset is in FSO axes.
void keyboardPlace()
{
	kbPos = XrVector3f{ xr_offset.xyz.x, xr_offset.xyz.y - 0.55f, -xr_offset.xyz.z - 1.4f };
	const float half = -35.0f * 0.5f * 3.14159265f / 180.0f;
	kbQuat = XrQuaternionf{ std::sin(half), 0.0f, 0.0f, std::cos(half) };
}

// Aim ray of `hand` against the keyboard quad; u/v in 0..1 (v down)
bool keyboardHit(int hand, float& u, float& v)
{
	if (aimSpace[hand] == XR_NULL_HANDLE)
		return false;
	XrSpaceLocation loc{ XR_TYPE_SPACE_LOCATION };
	if (xrLocateSpace(aimSpace[hand], xr_space, xr_state.predictedDisplayTime, &loc) != XR_SUCCESS)
		return false;
	const XrSpaceLocationFlags need = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
	if ((loc.locationFlags & need) != need)
		return false;

	const XrQuaternionf inv = conjugate(kbQuat);
	const XrVector3f o = rotate(inv, XrVector3f{ loc.pose.position.x - kbPos.x, loc.pose.position.y - kbPos.y, loc.pose.position.z - kbPos.z });
	const XrVector3f d = rotate(inv, rotate(loc.pose.orientation, XrVector3f{ 0.0f, 0.0f, -1.0f }));
	if (d.z > -0.05f) // must point into the front face
		return false;
	const float t = -o.z / d.z;
	if (t <= 0.0f)
		return false;
	u = (o.x + t * d.x) / kbW + 0.5f;
	v = 0.5f - (o.y + t * d.y) / kbH;
	return u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f;
}

void typeText(const char* text)
{
	// static strings from the keyboard layout, so SDL may keep the pointer
	SDL_Event e{};
	e.type = SDL_EVENT_TEXT_INPUT;
	e.text.windowID = SDL_GetWindowID(os::getSDLMainWindow());
	e.text.text = text;
	SDL_PushEvent(&e);
}

void keyboardPress(int key, Uint64 now)
{
	const VrKeyKind kind = VrKeyboard_KeyKind(key);
	const char* text = VrKeyboard_Press(&kb, key, now);
	if (kind == VR_KEY_KIND_BACKSPACE) {
		pressKey(KEY_BACKSP);
		kbRepeatMs = now + 450;
	} else if (kind == VR_KEY_KIND_ENTER) {
		pressKey(KEY_ENTER);
	} else if (text) {
		typeText(text);
	}
	kbDirty = true;
}

// Menus: show/hide the keyboard, hover, typing. onKb[h] = that hand's ray is on the keyboard;
// held[h] = that hand's trigger press belongs to the keyboard (no mouse click for it).
void keyboardUpdate(float trigL, float trigR, bool onKb[2])
{
	const Uint64 now = SDL_GetTicks();
	const bool want = textFieldMs != 0 && now - textFieldMs < 300;
	if (want && !kbVisible) {
		VrKeyboard_Reset(&kb);
		keyboardPlace();
		kbDirty = true;
		QLOG("virtual keyboard shown");
	} else if (!want && kbVisible) {
		QLOG("virtual keyboard hidden");
	}
	kbVisible = want;

	const float trig[2] = { trigL, trigR };
	for (int h = 0; h < 2; h++) {
		int hover = VR_KB_NO_KEY;
		float u, v;
		onKb[h] = kbVisible && keyboardHit(h, u, v);
		if (onKb[h])
			hover = VrKeyboard_KeyAt(u, v);
		if (hover != kb.hover[h]) {
			kb.hover[h] = hover;
			kbDirty = true;
		}

		const bool down = trig[h] > (kbTrig[h] ? 0.4f : 0.6f);
		if (down && !kbTrig[h]) {
			kbHeld[h] = onKb[h];
			if (onKb[h] && hover != VR_KB_NO_KEY)
				keyboardPress(hover, now);
		} else if (!down) {
			kbHeld[h] = false;
		}
		kbTrig[h] = down;

		// hold Backspace to keep deleting
		if (down && kbHeld[h] && hover != VR_KB_NO_KEY && VrKeyboard_KeyKind(hover) == VR_KEY_KIND_BACKSPACE && now >= kbRepeatMs) {
			pressKey(KEY_BACKSP);
			kbRepeatMs = now + 70;
		}
	}

	if (kb.flash_key != VR_KB_NO_KEY && now >= kb.flash_until_ms) {
		kb.flash_key = VR_KB_NO_KEY;
		kbDirty = true;
	}
}

} // namespace

void openxr_quest_text_field_active()
{
	textFieldMs = SDL_GetTicks();
	if (textFieldMs == 0)
		textFieldMs = 1;
}

bool openxr_quest_keyboard_visible()
{
	return kbVisible;
}

bool openxr_quest_keyboard_render(uint8_t* rgba)
{
	if (!kbDirty)
		return false;
	VrKeyboard_Render(&kb, rgba, 0, SDL_GetTicks());
	kbDirty = false;
	return true;
}

void openxr_quest_keyboard_pose(float pos[3], float quat[4], float size[2])
{
	pos[0] = kbPos.x;
	pos[1] = kbPos.y;
	pos[2] = kbPos.z;
	quat[0] = kbQuat.x;
	quat[1] = kbQuat.y;
	quat[2] = kbQuat.z;
	quat[3] = kbQuat.w;
	size[0] = kbW;
	size[1] = kbH;
}

bool hasPerfSettings = false;

SCP_vector<const char*> openxr_quest_instance_extensions()
{
	// Only request what the runtime offers; an unknown extension fails xrCreateInstance
	SCP_vector<const char*> wanted = { XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME };
	SCP_vector<const char*> out;

	uint32_t count = 0;
	if (xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr) != XR_SUCCESS)
		return out;
	SCP_vector<XrExtensionProperties> props(count, XrExtensionProperties{ XR_TYPE_EXTENSION_PROPERTIES });
	xrEnumerateInstanceExtensionProperties(nullptr, count, &count, props.data());

	for (const char* w : wanted) {
		for (const auto& p : props) {
			if (!strcmp(p.extensionName, w)) {
				out.push_back(w);
				if (!strcmp(w, XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME))
					hasPerfSettings = true;
				break;
			}
		}
	}
	QLOG("optional OpenXR extensions: performance_settings %d", hasPerfSettings ? 1 : 0);
	return out;
}

static void setPerformanceLevels()
{
	if (!hasPerfSettings)
		return;
	PFN_xrPerfSettingsSetPerformanceLevelEXT setLevel = nullptr;
	if (xrGetInstanceProcAddr(xr_instance, "xrPerfSettingsSetPerformanceLevelEXT", (PFN_xrVoidFunction*)&setLevel) != XR_SUCCESS || !setLevel)
		return;
	// FSO renders the whole scene twice per frame on one thread: ask for the high clocks
	XrResult c = setLevel(xr_session, XR_PERF_SETTINGS_DOMAIN_CPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT);
	XrResult g = setLevel(xr_session, XR_PERF_SETTINGS_DOMAIN_GPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT);
	QLOG("performance levels: CPU %d, GPU %d", (int)c, (int)g);
}

void openxr_quest_init()
{
	handPath[LEFT] = path("/user/hand/left");
	handPath[RIGHT] = path("/user/hand/right");

	XrActionSetCreateInfo setInfo{ XR_TYPE_ACTION_SET_CREATE_INFO };
	strcpy(setInfo.actionSetName, "fso");
	strcpy(setInfo.localizedActionSetName, "FreeSpace Open");
	setInfo.priority = 0;
	XrResult r = xrCreateActionSet(xr_instance, &setInfo, &actionSet);
	if (r != XR_SUCCESS) {
		QLOG("xrCreateActionSet failed: %d", (int)r);
		return;
	}

	aimPose = makeAction("aim", "Aim", XR_ACTION_TYPE_POSE_INPUT, true);
	trigger = makeAction("trigger", "Trigger", XR_ACTION_TYPE_FLOAT_INPUT, true);
	grip = makeAction("grip", "Grip", XR_ACTION_TYPE_FLOAT_INPUT, true);
	stick = makeAction("stick", "Thumbstick", XR_ACTION_TYPE_VECTOR2F_INPUT, true);
	stickClick = makeAction("stick_click", "Thumbstick click", XR_ACTION_TYPE_BOOLEAN_INPUT, true);
	btnA = makeAction("a", "A", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
	btnB = makeAction("b", "B", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
	btnX = makeAction("x", "X", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
	btnY = makeAction("y", "Y", XR_ACTION_TYPE_BOOLEAN_INPUT, false);
	btnMenu = makeAction("menu", "Menu", XR_ACTION_TYPE_BOOLEAN_INPUT, false);

	const XrActionSuggestedBinding bindings[] = {
		{ aimPose, path("/user/hand/left/input/aim/pose") },
		{ aimPose, path("/user/hand/right/input/aim/pose") },
		{ trigger, path("/user/hand/left/input/trigger/value") },
		{ trigger, path("/user/hand/right/input/trigger/value") },
		{ grip, path("/user/hand/left/input/squeeze/value") },
		{ grip, path("/user/hand/right/input/squeeze/value") },
		{ stick, path("/user/hand/left/input/thumbstick") },
		{ stick, path("/user/hand/right/input/thumbstick") },
		{ stickClick, path("/user/hand/left/input/thumbstick/click") },
		{ stickClick, path("/user/hand/right/input/thumbstick/click") },
		{ btnA, path("/user/hand/right/input/a/click") },
		{ btnB, path("/user/hand/right/input/b/click") },
		{ btnX, path("/user/hand/left/input/x/click") },
		{ btnY, path("/user/hand/left/input/y/click") },
		{ btnMenu, path("/user/hand/left/input/menu/click") },
	};
	XrInteractionProfileSuggestedBinding suggested{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
	suggested.interactionProfile = path("/interaction_profiles/oculus/touch_controller");
	suggested.countSuggestedBindings = sizeof(bindings) / sizeof(bindings[0]);
	suggested.suggestedBindings = bindings;
	r = xrSuggestInteractionProfileBindings(xr_instance, &suggested);
	if (r != XR_SUCCESS)
		QLOG("xrSuggestInteractionProfileBindings failed: %d", (int)r);

	for (int h = 0; h < 2; h++) {
		XrActionSpaceCreateInfo si{ XR_TYPE_ACTION_SPACE_CREATE_INFO, nullptr, aimPose, handPath[h], XrPosef{ XrQuaternionf{ 0, 0, 0, 1 }, XrVector3f{ 0, 0, 0 } } };
		xrCreateActionSpace(xr_session, &si, &aimSpace[h]);
	}

	XrSessionActionSetsAttachInfo attach{ XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO, nullptr, 1, &actionSet };
	r = xrAttachSessionActionSets(xr_session, &attach);
	if (r != XR_SUCCESS) {
		QLOG("xrAttachSessionActionSets failed: %d", (int)r);
		return;
	}

	actionsReady = true;
	createPad();
	setPerformanceLevels();
	QLOG("Touch controller input ready");
}

void openxr_quest_close()
{
	for (auto& s : aimSpace) {
		if (s != XR_NULL_HANDLE)
			xrDestroySpace(s);
		s = XR_NULL_HANDLE;
	}
	if (actionSet != XR_NULL_HANDLE)
		xrDestroyActionSet(actionSet);
	actionSet = XR_NULL_HANDLE;
	actionsReady = false;
}

void openxr_quest_update(bool stereo_frame)
{
	for (int k : pendingKeyUps)
		key_mark(k, 0, 0);
	pendingKeyUps.clear();

	if (!actionsReady)
		return;

	XrActiveActionSet active{ actionSet, XR_NULL_PATH };
	XrActionsSyncInfo sync{ XR_TYPE_ACTIONS_SYNC_INFO, nullptr, 1, &active };
	if (xrSyncActions(xr_session, &sync) != XR_SUCCESS)
		return; // not focused (system menu open etc.)

	const float trigL = getFloat(trigger, LEFT), trigR = getFloat(trigger, RIGHT);
	const float gripL = getFloat(grip, LEFT), gripR = getFloat(grip, RIGHT);
	const XrVector2f stL = getVec2(stick, LEFT), stR = getVec2(stick, RIGHT);
	const bool clickL = getBool(stickClick, handPath[LEFT]), clickR = getBool(stickClick, handPath[RIGHT]);
	const bool a = getBool(btnA), b = getBool(btnB), x = getBool(btnX), y = getBool(btnY);
	const bool menu = getBool(btnMenu);

	// Left menu button = Escape (pause / back) in every mode
	if (menu && !prevMenu)
		pressKey(KEY_ESC);
	prevMenu = menu;

	// death popup in flight: it is on the HUD layer, so point at that instead of flying
	const bool hudPointer = stereo_frame && popupdead_is_active();

	if (stereo_frame && !hudPointer) {
		// Flight: Touch -> virtual gamepad. Bindings: controlconfig (control_config_quest_touch_defaults)
		const float now = static_cast<float>(SDL_GetTicks()) / 1000.0f;
		if (lastUpdateSeconds < 0.0f && Player) {
			// entering flight: start the lever at the ship's current throttle
			throttleLever = std::max(0.0f, std::min(1.0f, Player->ci.forward_cruise_percent / 100.0f));
		}
		const float dt = (lastUpdateSeconds < 0.0f) ? 0.0f : std::min(0.1f, now - lastUpdateSeconds);
		lastUpdateSeconds = now;

		const bool shift = gripL > 0.5f;
		const bool comms = Player && (Player->flags & PLAYER_FLAGS_MSG_MODE);
		auto btn = [](SDL_GamepadButton b, bool down) {
			if (pad)
				SDL_SetJoystickVirtualButton(pad, b, down);
		};

		// right stick Y: throttle lever (full sweep in about one second); with the grip: full / stop
		if (shift) {
			const bool up = stR.y > 0.7f, downs = stR.y < -0.7f;
			if (up && !prevShiftStickUp)
				throttleLever = 1.0f;
			if (downs && !prevShiftStickDown)
				throttleLever = 0.0f;
			prevShiftStickUp = up;
			prevShiftStickDown = downs;
		} else {
			prevShiftStickUp = prevShiftStickDown = false;
			if (std::fabs(stR.y) > 0.15f)
				throttleLever = std::max(0.0f, std::min(1.0f, throttleLever + stR.y * dt));
		}

		// comms menu open: left stick up/down moves the selection, a trigger or A picks it
		bool commsUp = false, commsDown = false;
		if (comms) {
			commsUp = stL.y > 0.6f && !prevCommsUp;
			commsDown = stL.y < -0.6f && !prevCommsDown;
		}
		prevCommsUp = comms && stL.y > 0.6f;
		prevCommsDown = comms && stL.y < -0.6f;

		if (pad) {
			SDL_SetJoystickVirtualAxis(pad, SDL_GAMEPAD_AXIS_LEFTX, comms ? 0 : axisValue(stL.x));
			SDL_SetJoystickVirtualAxis(pad, SDL_GAMEPAD_AXIS_LEFTY, comms ? 0 : axisValue(-stL.y));
			SDL_SetJoystickVirtualAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX, axisValue(stR.x));
			// absolute throttle axis: up (-32767) = full speed, down (+32767) = stop
			SDL_SetJoystickVirtualAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY, axisValue(1.0f - 2.0f * throttleLever));
			SDL_SetJoystickVirtualAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, comms ? 0 : triggerValue(trigL));
			SDL_SetJoystickVirtualAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, comms ? 0 : triggerValue(trigR));
		}

		const bool normal = !shift && !comms;
		btn(SDL_GAMEPAD_BUTTON_SOUTH, normal && a);
		btn(SDL_GAMEPAD_BUTTON_EAST, normal && b);
		btn(SDL_GAMEPAD_BUTTON_WEST, normal && x);
		btn(SDL_GAMEPAD_BUTTON_NORTH, normal && y);
		// left stick click: short tap = autopilot, held for WARP_HOLD_SECONDS = warp out
		if (normal && clickL) {
			if (clickLDownSince < 0.0f) {
				clickLDownSince = now;
				clickLWarpSent = false;
			} else if (!clickLWarpSent && now - clickLDownSince >= WARP_HOLD_SECONDS) {
				clickLWarpSent = true;
				warpPulse = 2;
			}
		} else {
			if (clickLDownSince >= 0.0f && !clickLWarpSent && !clickL)
				autopilotPulse = 2;
			clickLDownSince = -1.0f;
		}
		btn(SDL_GAMEPAD_BUTTON_LEFT_STICK, autopilotPulse > 0);
		btn(SDL_GAMEPAD_BUTTON_MISC3, warpPulse > 0);
		if (autopilotPulse > 0)
			autopilotPulse--;
		if (warpPulse > 0)
			warpPulse--;
		btn(SDL_GAMEPAD_BUTTON_RIGHT_STICK, normal && clickR);
		btn(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, gripR > 0.5f);
		btn(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, false);

		// second layer (left grip held)
		btn(SDL_GAMEPAD_BUTTON_MISC1, shift && a);
		btn(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1, shift && b);
		btn(SDL_GAMEPAD_BUTTON_LEFT_PADDLE1, shift && x);
		btn(SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2, (shift || comms) && y); // also closes the comms menu
		btn(SDL_GAMEPAD_BUTTON_LEFT_PADDLE2, shift && clickL);
		btn(SDL_GAMEPAD_BUTTON_MISC2, shift && clickR); // chase view on / off

		// comms menu
		btn(SDL_GAMEPAD_BUTTON_DPAD_UP, commsUp);
		btn(SDL_GAMEPAD_BUTTON_DPAD_DOWN, commsDown);
		btn(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, comms && (a || trigL > 0.6f || trigR > 0.6f));

		if (prevTrigger) {
			mouse_mark_button(MOUSE_LEFT_BUTTON, 0);
			prevTrigger = false;
		}
		kbVisible = false;
	} else {
		// Menus: neutral pad, laser pointer drives the mouse
		lastUpdateSeconds = -1.0f;
		clickLDownSince = -1.0f;
		autopilotPulse = warpPulse = 0;
		setPadNeutral();

		// the hand that pulls its trigger takes over the pointer
		if (trigL > 0.6f && trigR < 0.3f)
			pointerHand = LEFT;
		else if (trigR > 0.6f && trigL < 0.3f)
			pointerHand = RIGHT;

		// virtual keyboard first: a ray on the keyboard neither moves the mouse nor clicks
		bool onKb[2] = { false, false };
		if (hudPointer)
			kbVisible = false;
		else
			keyboardUpdate(trigL, trigR, onKb);

		float mx, my;
		if (!onKb[pointerHand] && (hudPointer ? pointerHitHud(pointerHand, mx, my) : pointerHit(pointerHand, mx, my)))
			mouse_event(mx, my, 0.0f, 0.0f);

		const float mouseTrig = std::max(kbHeld[LEFT] ? 0.0f : trigL, kbHeld[RIGHT] ? 0.0f : trigR);
		const bool down = mouseTrig > (prevTrigger ? 0.4f : 0.6f);
		if (down != prevTrigger)
			mouse_mark_button(MOUSE_LEFT_BUTTON, down ? 1 : 0);
		prevTrigger = down;

		if (a && !prevA)
			pressKey(KEY_ENTER);
		if (b && !prevB)
			pressKey(KEY_ESC);
	}
	prevA = a;
	prevB = b;
}

#else

SCP_vector<const char*> openxr_quest_instance_extensions() { return {}; }
bool openxr_quest_hud_begin() { return false; }
float openxr_quest_hud_half_fov() { return 0.0f; }
void openxr_quest_hud_end() {}
void openxr_quest_init() {}
void openxr_quest_close() {}
void openxr_quest_update(bool) {}
void openxr_quest_text_field_active() {}
bool openxr_quest_keyboard_visible() { return false; }
bool openxr_quest_keyboard_render(uint8_t*) { return false; }
void openxr_quest_keyboard_pose(float[3], float[4], float[2]) {}

#endif
