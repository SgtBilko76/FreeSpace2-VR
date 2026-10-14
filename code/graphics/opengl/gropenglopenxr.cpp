#include "gropenglopenxr.h"

// this block should go before the #includes otherwise MSVC will sometimes warn about APIENTRY redefinition
// (glad.h checks for redefinition of the symbol, but minwindef.h does not)
#ifdef WIN32

#define XR_USE_PLATFORM_WIN32
#include <unknwn.h>

#endif
// the other platforms do not go before the #includes because this will cause conflicts from symbols defined in XLib,
// specifically None and Always; see https://stackoverflow.com/questions/22476110/c-compiling-error-including-x11-x-h-x11-xlib-h

#include "cmdline/cmdline.h"
#include "io/cursor.h"
#include "io/mouse.h"
#include "graphics/matrix.h"
#include "graphics/opengl/gropengl.h"
#include "graphics/opengl/gropengldraw.h"
#include "graphics/opengl/gropenglshader.h"
#include "graphics/opengl/gropenglstate.h"
#include "graphics/opengl/ShaderProgram.h"
#include "osapi/osapi.h"
#ifdef USE_OPENGL_ES
#include "es_compatibility.h"
#endif

#ifndef FS_OPENXR

//Not supported

#elif defined __ANDROID__

// Meta Quest / Android: OpenGL ES 3.2 on EGL
#include <jni.h>
#include <EGL/egl.h>
#define XR_USE_PLATFORM_ANDROID
#define XR_USE_GRAPHICS_API_OPENGL_ES

#elif defined SCP_UNIX

#define XR_USE_PLATFORM_XLIB
#include<X11/Xlib.h>
#include<glad/glad_glx.h>

#endif

#ifndef __ANDROID__
#define XR_USE_GRAPHICS_API_OPENGL
#endif
#include "graphics/openxr_internal.h"
#include "graphics/openxr_quest.h"

#ifdef __ANDROID__
using XrSwapchainImageGL = XrSwapchainImageOpenGLESKHR;
static constexpr XrStructureType XR_TYPE_SWAPCHAIN_IMAGE_GL = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
#ifndef GL_FRAMEBUFFER_SRGB_EXT
#define GL_FRAMEBUFFER_SRGB_EXT 0x8DB9
#endif
static bool openxr_create_hud_swapchain();
static bool gl_has_srgb_write_control() {
	static int cached = -1;
	if (cached < 0) {
		cached = 0;
		GLint count = 0;
		glGetIntegerv(GL_NUM_EXTENSIONS, &count);
		for (GLint e = 0; e < count; e++) {
			auto name = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(e)));
			if (name && !strcmp(name, "GL_EXT_sRGB_write_control")) {
				cached = 1;
				break;
			}
		}
		mprintf(("OpenXR: GL_EXT_sRGB_write_control %s\n", cached ? "available" : "NOT available"));
	}
	return cached == 1;
}
#else
using XrSwapchainImageGL = XrSwapchainImageOpenGLKHR;
static constexpr XrStructureType XR_TYPE_SWAPCHAIN_IMAGE_GL = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
#endif

#ifdef FS_OPENXR

//SETUP FUNCTIONS OGL
#ifdef __ANDROID__
SCP_vector<const char*> gr_opengl_openxr_get_extensions() {
	return { XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME, XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME };
}

bool gr_opengl_openxr_test_capabilities() {
	XrGraphicsRequirementsOpenGLESKHR requirements{
		XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR,
		nullptr,
		0,
		0
	};

	if (openxr_callExtensionFunction<PFN_xrGetOpenGLESGraphicsRequirementsKHR>("xrGetOpenGLESGraphicsRequirementsKHR", xr_instance, xr_system, &requirements) != XR_SUCCESS) {
		mprintf(("Failed to query OpenXR graphics requirements!\n"));
		return false;
	}

	GLint major = 0, minor = 0;
	glGetIntegerv(GL_MAJOR_VERSION, &major);
	glGetIntegerv(GL_MINOR_VERSION, &minor);
	if (requirements.minApiVersionSupported > XR_MAKE_VERSION(major, minor, 0)) {
		mprintf(("System doesn't meet OpenXR graphics requirements (min " UINT64_T_ARG ", available %d.%d)!\n", requirements.minApiVersionSupported, major, minor));
		return false;
	}

	return true;
}

bool gr_opengl_openxr_create_session() {
	EGLDisplay display = eglGetCurrentDisplay();
	EGLContext context = eglGetCurrentContext();

	EGLint configId = 0;
	eglQueryContext(display, context, EGL_CONFIG_ID, &configId);
	const EGLint configAttribs[] = { EGL_CONFIG_ID, configId, EGL_NONE };
	EGLConfig config = nullptr;
	EGLint numConfigs = 0;
	if (!eglChooseConfig(display, configAttribs, &config, 1, &numConfigs) || numConfigs < 1) {
		mprintf(("Unable to find the EGL config of the current context for OpenXR!\n"));
		return false;
	}

	XrGraphicsBindingOpenGLESAndroidKHR graphicsBinding{
		XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR,
		nullptr,
		display,
		config,
		context
	};

	XrSessionCreateInfo sessionCreateInfo{
		XR_TYPE_SESSION_CREATE_INFO,
		&graphicsBinding,
		0,
		xr_system
	};

	XrResult sessionInit = xrCreateSession(xr_instance, &sessionCreateInfo, &xr_session);
	if (sessionInit != XR_SUCCESS) {
		mprintf(("Failed to create OpenXR session with code %d\n", static_cast<int>(sessionInit)));
		return false;
	}

	return true;
}
#else
SCP_vector<const char*> gr_opengl_openxr_get_extensions() {
	return { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };
}


bool gr_opengl_openxr_test_capabilities() {
	XrGraphicsRequirementsOpenGLKHR requirements{
		XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR,
		nullptr,
		0,
		0
	};

	if (openxr_callExtensionFunction<PFN_xrGetOpenGLGraphicsRequirementsKHR>("xrGetOpenGLGraphicsRequirementsKHR", xr_instance, xr_system, &requirements) != XR_SUCCESS) {
		mprintf(("Failed to query OpenXR graphics requirements!\n"));
		return false;
	}

	if (requirements.minApiVersionSupported > XR_MAKE_VERSION(GLVersion.major, GLVersion.minor, 0)) {
		mprintf(("System doesn't meet OpenXR graphics requirements (min " UINT64_T_ARG ", available " UINT64_T_ARG ")!\n", requirements.minApiVersionSupported, static_cast<XrVersion>(XR_MAKE_VERSION(GLVersion.major, GLVersion.minor, 0))));
		return false;
	}

	return true;
}

#ifdef WIN32
bool gr_opengl_openxr_create_session() {
	auto hdc = static_cast<HDC>(SDL_GetPointerProperty(SDL_GetWindowProperties(os::getSDLMainWindow()),
													   SDL_PROP_WINDOW_WIN32_HWND_POINTER,
													   nullptr));

	XrGraphicsBindingOpenGLWin32KHR graphicsBinding{
		XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR,
		nullptr,
		hdc,
		(HGLRC) SDL_GL_GetCurrentContext() //uuuuuugly, and not technically allowed by the standard, but this "opaque" SDL_GLContext type is just the hGLRC on Win32
	};
	
	XrSessionCreateInfo sessionCreateInfo{
		XR_TYPE_SESSION_CREATE_INFO,
		&graphicsBinding,
		0,
		xr_system
	};

	XrResult sessionInit = xrCreateSession(xr_instance, &sessionCreateInfo, &xr_session);
	if (sessionInit != XR_SUCCESS) {
		mprintf(("Failed to create OpenXR session with code %d\n", static_cast<int>(sessionInit)));
		return false;
	}

	return true;
}
#elif defined SCP_UNIX
bool gr_opengl_openxr_create_session() {
	std::variant<std::monostate, XrGraphicsBindingOpenGLXlibKHR> drawingSurface{};

	// for X11 version
	if (!SDL_strcmp(SDL_GetCurrentVideoDriver(), "x11")) {
		auto xdisplay = reinterpret_cast<Display *>(SDL_GetPointerProperty(SDL_GetWindowProperties(os::getSDLMainWindow()),
																	  SDL_PROP_WINDOW_X11_DISPLAY_POINTER,
																	  nullptr));
		auto xwindow = static_cast<Window>(SDL_GetNumberProperty(SDL_GetWindowProperties(os::getSDLMainWindow()),
																 SDL_PROP_WINDOW_X11_WINDOW_NUMBER,
																 0));

		XWindowAttributes wa;
		XGetWindowAttributes(xdisplay, xwindow, &wa);

		auto glxcontext = (GLXContext) SDL_GL_GetCurrentContext(); //uuuuuugly, and not technically allowed by the standard, but this "opaque" SDL_GLContext type is just the GLXContext on X11

		int glxfbconfigid, glxscreenid, nfbconfigs;
		glXQueryContext(xdisplay, glxcontext, GLX_FBCONFIG_ID, &glxfbconfigid);
		glXQueryContext(xdisplay, glxcontext, GLX_SCREEN, &glxscreenid);

		const int fbconfigattrs[] = {GLX_FBCONFIG_ID, glxfbconfigid, None};
		GLXFBConfig* fbconfigs = glXChooseFBConfig(xdisplay, glxscreenid, fbconfigattrs, &nfbconfigs);

		if (nfbconfigs < 1) {
			mprintf(("Unable to find Linux FBConfig for OpenXR!\n"));
			return false;
		}

		drawingSurface = XrGraphicsBindingOpenGLXlibKHR {
			XR_TYPE_GRAPHICS_BINDING_OPENGL_XLIB_KHR,
			nullptr,
			xdisplay,
			static_cast<uint32_t>(XVisualIDFromVisual(wa.visual)),
			fbconfigs[0],
			glXGetCurrentDrawable(),
			glxcontext
		};
	}
	else if (!SDL_strcmp(SDL_GetCurrentVideoDriver(), "wayland")) {
		//Unfortunately, XrGraphicsBindingOpenGLWaylandKHR is deprecated and effectively not implemented anywhere, so we are limited to X11 on OpenGL
		Error("VR with OpenGL cannot use wayland! Use X11 (SDL_VIDEO_DRIVER=x11) or the Vulkan renderer.");
		return false;
	}
	else {
		Error("Unsupported video driver for OpenXR on Linux! Must be X11 or wayland!");
		return false;
	}

	XrSessionCreateInfo sessionCreateInfo {
		XR_TYPE_SESSION_CREATE_INFO,
		std::visit([] (auto& surface) -> void* {
			if constexpr(std::is_same_v<std::decay_t<decltype(surface)>, std::monostate>) {
				Assertion(false, "No drawing surface found!");
				return nullptr;
			}
			else {
				return &surface;
			}
		}, drawingSurface),
		0,
		xr_system
	};

	XrResult sessionInit = xrCreateSession(xr_instance, &sessionCreateInfo, &xr_session);
	if (sessionInit != XR_SUCCESS) {
		mprintf(("Failed to create OpenXR session with code %d\n", static_cast<int>(sessionInit)));
		return false;
	}

	return true;
}
#endif
#endif // !__ANDROID__

int64_t gr_opengl_openxr_get_swapchain_format(const SCP_vector<int64_t>& allowed) {
#ifdef __ANDROID__
	// The Quest compositor expects sRGB-encoded images in an sRGB swapchain. FSO's
	// backbuffer already holds display-ready (sRGB) values on Android (no linear tonemap
	// output), so they are copied with sRGB encoding switched off (EXT_sRGB_write_control).
	if (std::find(allowed.cbegin(), allowed.cend(), GL_SRGB8_ALPHA8) != allowed.cend())
		return GL_SRGB8_ALPHA8;
	if (std::find(allowed.cbegin(), allowed.cend(), GL_RGBA8) != allowed.cend())
		return GL_RGBA8;
	return allowed.front();
#endif
	//We prefer whatever our backbuffer is
	if (std::find(allowed.cbegin(), allowed.cend(), GL_RGBA16F) != allowed.cend())
		return GL_RGBA16F;

	//Otherwise, Fallback
	return allowed.front();
}

//PIPELINE FUNCTIONS OGL
std::array<SCP_vector<XrSwapchainImageGL>, 2> swapchainImages;

GLuint xr_fbo;
GLuint xr_swap_tex;

bool gr_opengl_openxr_acquire_swapchain_buffers() {
	// create framebuffer
	glGenFramebuffers(1, &xr_fbo);
	GL_state.BindFrameBuffer(xr_fbo);
	opengl_set_object_label(GL_FRAMEBUFFER, xr_fbo, "XR Swapchain");

	//While blitting from the backbuffer is fine for the 3d-scenes, when we render a flat scene, we need to bind the backbuffer as a texture.
	//Since that doesn't work, we need an intermediary. Not pretty, but it doesn't need to be for the flat scenes
	glGenTextures(1, &xr_swap_tex);

	GL_state.Texture.SetActiveUnit(0);
	GL_state.Texture.SetTarget(GL_TEXTURE_2D);
	GL_state.Texture.Enable(xr_swap_tex);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, gr_screen.max_w, gr_screen.max_h, 0, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL);

	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, xr_swap_tex, 0);
	opengl_set_object_label(GL_TEXTURE, xr_swap_tex, "XR Swap Texture");

	for (uint32_t i = 0; i < 2; i++) {
		uint32_t imageCount = 0;

		XrResult swapchainAcq = xrEnumerateSwapchainImages(xr_swapchains[i]->swapchain, 0, &imageCount, nullptr);
		if (swapchainAcq != XR_SUCCESS) {
			mprintf(("Failed to acquire OpenXR swapchain %d with code %d\n", i, static_cast<int>(swapchainAcq)));
			return false;
		}

		swapchainImages[i] = SCP_vector<XrSwapchainImageGL>(imageCount, { XR_TYPE_SWAPCHAIN_IMAGE_GL, nullptr, 0 });

		swapchainAcq = xrEnumerateSwapchainImages(xr_swapchains[i]->swapchain, imageCount, &imageCount, (XrSwapchainImageBaseHeader*)swapchainImages[i].data());
		if (swapchainAcq != XR_SUCCESS) {
			mprintf(("Failed to acquire OpenXR swapchain %d with code %d\n", i, static_cast<int>(swapchainAcq)));
			return false;
		}
	}

#ifdef __ANDROID__
	openxr_create_hud_swapchain();
#endif

	return true;
}

#ifdef __ANDROID__
// ---- Quest: cockpit-fixed HUD composition layer --------------------------------------
// In stereo frames the HUD is not drawn into the eye images (it would be head-locked and
// distorted toward the lens edges). It is drawn once per frame with the mono ship camera,
// copied into its own swapchain and shown as a quad layer fixed in front of the ship.
static XrSwapchain xr_hud_swapchain = XR_NULL_HANDLE;
static SCP_vector<XrSwapchainImageGL> xr_hud_images;
static uint32_t xr_hud_w = 0, xr_hud_h = 0;
static bool xr_hud_ready = false; // the HUD image was written for the current frame
static constexpr float XR_HUD_DISTANCE = 3.0f; // metres

static bool openxr_create_hud_swapchain() {
	xr_hud_w = static_cast<uint32_t>(gr_screen.max_w);
	xr_hud_h = static_cast<uint32_t>(gr_screen.max_h);
	XrSwapchainCreateInfo info{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
	info.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
	info.format = xr_swapchains[0]->format;
	info.sampleCount = 1;
	info.width = xr_hud_w;
	info.height = xr_hud_h;
	info.faceCount = 1;
	info.arraySize = 1;
	info.mipCount = 1;
	if (xrCreateSwapchain(xr_session, &info, &xr_hud_swapchain) != XR_SUCCESS) {
		mprintf(("OpenXR: could not create the HUD swapchain\n"));
		xr_hud_swapchain = XR_NULL_HANDLE;
		return false;
	}
	uint32_t count = 0;
	xrEnumerateSwapchainImages(xr_hud_swapchain, 0, &count, nullptr);
	xr_hud_images.assign(count, XrSwapchainImageGL{ XR_TYPE_SWAPCHAIN_IMAGE_GL, nullptr, 0 });
	xrEnumerateSwapchainImages(xr_hud_swapchain, count, &count, (XrSwapchainImageBaseHeader*)xr_hud_images.data());
	mprintf(("OpenXR: HUD layer %ux%u, %u images\n", xr_hud_w, xr_hud_h, count));
	return true;
}

bool openxr_quest_hud_begin() {
	xr_hud_ready = false;
	if (!openxr_enabled() || xr_hud_swapchain == XR_NULL_HANDLE || xr_stage != OpenXRFBStage::FIRST)
		return false;

	// Draw the HUD alone into the (cleared, transparent) back buffer
	GL_state.BindFrameBuffer(Back_framebuffer);
	glViewport(0, 0, gr_screen.max_w, gr_screen.max_h);
	GL_state.ScissorTest(GL_FALSE);
	GL_state.ColorMask(true, true, true, true);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	return true;
}

void openxr_quest_hud_end() {
	uint32_t index = 0;
	XrSwapchainImageAcquireInfo acquireInfo{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
	if (xrAcquireSwapchainImage(xr_hud_swapchain, &acquireInfo, &index) != XR_SUCCESS)
		return;
	XrSwapchainImageWaitInfo waitInfo{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO, nullptr, XR_INFINITE_DURATION };
	xrWaitSwapchainImage(xr_hud_swapchain, &waitInfo);

	GR_DEBUG_SCOPE("XR HUD layer");
	GL_state.PushFramebufferState();
	GL_state.BindFrameBuffer(xr_fbo);
	glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, xr_hud_images[index].image, 0);
	glDrawBuffer(GL_COLOR_ATTACHMENT0);
	if (gl_has_srgb_write_control())
		glDisable(GL_FRAMEBUFFER_SRGB_EXT);
	glViewport(0, 0, xr_hud_w, xr_hud_h);
	GL_state.ScissorTest(GL_FALSE);
	GL_state.SetAlphaBlendMode(gr_alpha_blend::ALPHA_BLEND_NONE);
	GL_state.SetZbufferType(ZBUFFER_TYPE_NONE);
	GL_state.ColorMask(true, true, true, true);
	GL_state.StencilTest(GL_FALSE);

	opengl_shader_set_current(gr_opengl_maybe_create_shader(SDR_TYPE_COPY, SDR_FLAG_COPY_CLAMP01 | SDR_FLAG_COPY_LUMA_ALPHA));
	GL_state.Texture.Enable(0, GL_TEXTURE_2D, Back_texture);
	Current_shader->program->Uniforms.setTextureUniform("tex", 0);
	opengl_draw_full_screen_textured(0.0f, 0.0f, 1.0f, 1.0f);

	glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 0, 0);
	glDrawBuffer(GL_NONE);
	GL_state.PopFramebufferState();
	glViewport(0, 0, gr_screen.max_w, gr_screen.max_h);

	XrSwapchainImageReleaseInfo releaseInfo{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
	xrReleaseSwapchainImage(xr_hud_swapchain, &releaseInfo);
	xr_hud_ready = true;
}

// Quad covering exactly the mono game camera's frustum (average of both eye FOVs), so
// target brackets drawn by the HUD line up with the ships in the eye images.
static XrCompositionLayerQuad openxr_hud_layer() {
	const float tL = std::tan(-0.5f * (xr_views[0].fov.angleLeft + xr_views[1].fov.angleLeft));
	const float tR = std::tan(0.5f * (xr_views[0].fov.angleRight + xr_views[1].fov.angleRight));
	const float tU = std::tan(0.5f * (xr_views[0].fov.angleUp + xr_views[1].fov.angleUp));
	const float tD = std::tan(-0.5f * (xr_views[0].fov.angleDown + xr_views[1].fov.angleDown));
	const float D = XR_HUD_DISTANCE;

	XrCompositionLayerQuad quad{ XR_TYPE_COMPOSITION_LAYER_QUAD };
	quad.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
	quad.space = xr_space;
	quad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
	quad.subImage = { xr_hud_swapchain, { { 0, 0 }, { (int32_t)xr_hud_w, (int32_t)xr_hud_h } }, 0 };
	// xr_offset is the recentred head position in FSO axes (z flipped)
	quad.pose.orientation = XrQuaternionf{ 0.0f, 0.0f, 0.0f, 1.0f };
	quad.pose.position = XrVector3f{ xr_offset.xyz.x + D * (tR - tL) * 0.5f,
		xr_offset.xyz.y + D * (tU - tD) * 0.5f,
		-xr_offset.xyz.z - D };
	quad.size = XrExtent2Df{ D * (tL + tR), D * (tU + tD) };
	return quad;
}
#endif

bool gr_opengl_openxr_flip() {
	if (!openxr_enabled())
		return false;

	static bool was_multiframe = false;

	//If a stereoscopic frame has started, we're good. Otherwise, this is a normal frame that still needs to be OpenXR-Initialized
	//In addition, since we don't actually blit one backbuffer per eye in monoframe mode, we need to make a copy of it to a readable texture now.
	if (xr_stage == OpenXRFBStage::NONE) {
		openxr_start_frame();
		openxr_quest_update(false);

		GR_DEBUG_SCOPE("XR Blit");

		GL_state.PushFramebufferState();
		GL_state.BindFrameBuffer(Cmdline_window_res ? Back_framebuffer : 0, GL_READ_FRAMEBUFFER);
		GL_state.BindFrameBuffer(xr_fbo, GL_DRAW_FRAMEBUFFER);

		glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, xr_swap_tex, 0);

		glDrawBuffer(GL_COLOR_ATTACHMENT0);
		glReadBuffer(Cmdline_window_res ? GL_COLOR_ATTACHMENT0 : GL_BACK);
		glBlitFramebuffer(0, 0, gr_screen.max_w, gr_screen.max_h, 0, 0, gr_screen.max_w, gr_screen.max_h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
		glDrawBuffer(GL_NONE);

		GL_state.PopFramebufferState();

		if (was_multiframe) {
			openxr_reset_offset();
			was_multiframe = false; 
		}
	}
	else
		was_multiframe = true;

	uint32_t startFrame = xr_stage == OpenXRFBStage::SECOND ? 1 : 0;
	uint32_t endFrame = xr_stage == OpenXRFBStage::FIRST ? 1 : 2;

	for (uint32_t i = startFrame; i < endFrame; i++) {
		XrSwapchain swapchain = xr_swapchains[i]->swapchain;

		XrSwapchainImageAcquireInfo acquireImageInfo {
			XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO,
			nullptr
		};
		uint32_t activeIndex;
		xrAcquireSwapchainImage(swapchain, &acquireImageInfo, &activeIndex);

		XrSwapchainImageWaitInfo waitImageInfo{
			XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,
			nullptr,
			std::numeric_limits<int64_t>::max()
		};
		xrWaitSwapchainImage(swapchain, &waitImageInfo);

		const XrSwapchainImageGL& image = swapchainImages[i][activeIndex];
#ifdef __ANDROID__
		// Copy display-ready values as they are into the sRGB swapchain
		if (gl_has_srgb_write_control())
			glDisable(GL_FRAMEBUFFER_SRGB_EXT);
#endif

		//Monoframe mode: We only have one image that FSO just tried to flip. We have it in a readable texture which we'll now render as a flat plane in a fake 3D scene directly to the swapchain
		if (xr_stage == OpenXRFBStage::NONE) {
			GR_DEBUG_SCOPE("XR Plane");

			GL_state.PushFramebufferState();
			GL_state.BindFrameBuffer(xr_fbo);

			glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, image.image, 0);
			glDrawBuffer(GL_COLOR_ATTACHMENT0);
			GL_state.Texture.Enable(0, GL_TEXTURE_2D, xr_swap_tex);
			opengl_shader_set_current(gr_opengl_maybe_create_shader(SDR_TYPE_COPY_WORLD, 0));
			Current_shader->program->Uniforms.setTextureUniform("tex", 0);

			const auto& pos = xr_views[i].pose.position;
			const auto& ori = xr_views[i].pose.orientation;
			vec3d position{{ { pos.x, pos.y, -pos.z } }};
			position -= xr_offset;
			matrix orientation;
			vm_quaternion_to_matrix(&orientation, ori.x, ori.y, -ori.z, -ori.w);
			
			gr_set_clip(0, 0, xr_swapchains[i]->width, xr_swapchains[i]->height, GR_RESIZE_REPLACE);
			gr_set_proj_matrix(asymmetric_fov {
					xr_views[i].fov.angleLeft,
					xr_views[i].fov.angleRight,
					xr_views[i].fov.angleUp,
					xr_views[i].fov.angleDown
				}, i2fl(xr_swapchains[i]->width) / i2fl(xr_swapchains[i]->height), 0.01f, 1000);
			gr_set_view_matrix(&position, &orientation);

			glViewport(0, 0, xr_swapchains[i]->width, xr_swapchains[i]->height);
			GL_state.ScissorTest(GL_FALSE);
			GL_state.SetZbufferType(gr_zbuffer_type::ZBUFFER_TYPE_NONE);

			GLfloat v[4][5] = {
				{
					-2.0f * gr_screen.clip_aspect, -2.0f, 4, 0, 0
				},
				{
					2.0f * gr_screen.clip_aspect, -2.0f, 4, 1, 0
				},
				{
					2.0f * gr_screen.clip_aspect, 2.0f, 4, 1, 1
				},
				{
					-2.0f * gr_screen.clip_aspect, 2.0f, 4, 0, 1
				}
			};

			vertex_layout vert_def;
			vert_def.add_vertex_component(vertex_format_data::POSITION3, sizeof(GLfloat) * 5, 0);
			vert_def.add_vertex_component(vertex_format_data::TEX_COORD2, sizeof(GLfloat) * 5, sizeof(GLfloat) * 3);

			gr_matrix_set_uniforms();
			gr_set_clear_color(20, 20, 20);
			gr_clear();
			gr_set_clear_color(0, 0, 0);
			opengl_render_primitives_immediate(PRIM_TYPE_TRIFAN, &vert_def, 4, v, sizeof(v));

			//And if we have a curser, it also needs to be put onscreen manually
			if (io::mouse::CursorManager::get()->isCursorShown()) {
				opengl_shader_set_current(gr_opengl_maybe_create_shader(SDR_TYPE_COPY_WORLD, SDR_FLAG_COPY_FROM_ARRAY));
				Current_shader->program->Uniforms.setTextureUniform("tex", 0);
				
				float u_scale, v_scale;
				uint32_t array_index;
				int mouse_bm = io::mouse::CursorManager::get()->getCurrentCursor()->getBitmapHandle();
				int mousex, mousey, mousew, mouseh;

				bm_get_info(mouse_bm, &mousew, &mouseh);
				gr_opengl_tcache_set(mouse_bm, TCACHE_TYPE_INTERFACE, &u_scale, &v_scale, &array_index);

				mouse_get_pos(&mousex, &mousey);
				float mouse_unscaled_x = i2fl(mousex) / i2fl(gr_screen.max_w) * 2.0f - 1.0f;
				float mouse_unscaled_y = i2fl(mousey) / i2fl(gr_screen.max_h) * -2.0f + 1.0f;
				float mouse_unscaled_w = i2fl(mousew) / i2fl(gr_screen.max_w);
				float mouse_unscaled_h = i2fl(mouseh) / i2fl(gr_screen.max_h);

				float left = 2.0f * gr_screen.clip_aspect * mouse_unscaled_x;
				float right = left + 4.0f * gr_screen.clip_aspect * mouse_unscaled_w;
				float top = 2.0f * mouse_unscaled_y;
				float bottom = top - 4.0f * mouse_unscaled_h;

				GLfloat vmouse[4][5] = {
					{
						left, bottom, 4, 0, 1
					},
					{
						right, bottom, 4, 1, 1
					},
					{
						right, top, 4, 1, 0
					},
					{
						left, top, 4, 0, 0
					}
				};
				opengl_render_primitives_immediate(PRIM_TYPE_TRIFAN, &vert_def, 4, vmouse, sizeof(vmouse));
			}

			gr_end_proj_matrix();
			gr_end_view_matrix();
			gr_reset_clip();
			glViewport(0, 0, gr_screen.max_w, gr_screen.max_h);

			GL_state.Texture.Enable(0);
			glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 0, 0);
			glDrawBuffer(GL_NONE);

			GL_state.PopFramebufferState();
		}
		//If we are in stereo mode, we just need to take the current backbuffer and blit it to the proper swapchain target
		else {
			GR_DEBUG_SCOPE("XR Blit");

			GL_state.PushFramebufferState();
			GL_state.BindFrameBuffer(Cmdline_window_res ? Back_framebuffer : 0, GL_READ_FRAMEBUFFER);
			GL_state.BindFrameBuffer(xr_fbo, GL_DRAW_FRAMEBUFFER);

			glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, image.image, 0);
			glDrawBuffer(GL_COLOR_ATTACHMENT0);
			glReadBuffer(Cmdline_window_res ? GL_COLOR_ATTACHMENT0 : GL_BACK);
			glBlitFramebuffer(0, 0, gr_screen.max_w, gr_screen.max_h, 0, 0, xr_swapchains[i]->width, xr_swapchains[i]->height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
			glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 0, 0);
			glDrawBuffer(GL_NONE);

			GL_state.PopFramebufferState();
		}

		XrSwapchainImageReleaseInfo releaseImageInfo {
			XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO,
			nullptr
		};

		xrReleaseSwapchainImage(swapchain, &releaseImageInfo);
	}

	switch (xr_stage) {
	case OpenXRFBStage::FIRST:
		//We just rendered the first part of a stereo image. We now need to follow up with another one for the other eye
		xr_stage = OpenXRFBStage::SECOND;
		return true;
	case OpenXRFBStage::SECOND:
	case OpenXRFBStage::NONE:
		//We just rendered the second part of a stereo image or a mono image. Barring further instruction, the next image may be mono again.
		//Also, since that means we're done with whatever we just rendered, finish this frame
		xr_stage = OpenXRFBStage::NONE;
		break;
	}

	XrCompositionLayerProjectionView projectedViews[2];

	for (uint32_t i = 0; i < 2; i++)
	{
		projectedViews[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
		projectedViews[i].next = nullptr;
		projectedViews[i].pose = xr_views[i].pose;
		projectedViews[i].fov = xr_views[i].fov;
		projectedViews[i].subImage = {
			xr_swapchains[i]->swapchain,
			{
				{ 0, 0 },
				{ (int32_t)xr_swapchains[i]->width, (int32_t)xr_swapchains[i]->height }
			},
			0
		};
	}

	XrCompositionLayerProjection layer {
		XR_TYPE_COMPOSITION_LAYER_PROJECTION,
		nullptr,
		0,
		xr_space,
		2,
		projectedViews
	};
	auto pLayer = (const XrCompositionLayerBaseHeader*)&layer;
#ifdef __ANDROID__
	XrCompositionLayerQuad hudQuad{};
	const XrCompositionLayerBaseHeader* layers[2] = { pLayer, nullptr };
	uint32_t layerCount = 1;
	if (xr_hud_ready) {
		hudQuad = openxr_hud_layer();
		layers[layerCount++] = (const XrCompositionLayerBaseHeader*)&hudQuad;
	}
	xr_hud_ready = false;
#endif

	XrFrameEndInfo frameEndInfo {
		XR_TYPE_FRAME_END_INFO,
		nullptr,
		xr_state.predictedDisplayTime,
		XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
		0,
		nullptr
	};

	if (xr_state.shouldRender) {
#ifdef __ANDROID__
		frameEndInfo.layerCount = layerCount;
		frameEndInfo.layers = layers;
#else
		frameEndInfo.layerCount = 1;
		frameEndInfo.layers = &pLayer;
#endif
	}

	XrResult endResult = xrEndFrame(xr_session, &frameEndInfo);
#ifdef __ANDROID__
	{
		static int frameCounter = 0;
		if ((frameCounter++ % 300) == 0) {
			mprintf(("OpenXR frame %d: shouldRender %d, xrEndFrame %d, glError 0x%x, swapchain %ux%u, render %dx%d\n", frameCounter,
				(int)xr_state.shouldRender, (int)endResult, glGetError(), xr_swapchains[0]->width, xr_swapchains[0]->height,
				gr_screen.max_w, gr_screen.max_h));
		}
	}
#else
	(void)endResult;
#endif

	return false;
}

#else
//Stubs for Mac, as linking with OpenXR causes issues there.

SCP_vector<const char*> gr_opengl_openxr_get_extensions() { return SCP_vector<const char*>{}; }

bool gr_opengl_openxr_test_capabilities() { return false; }

bool gr_opengl_openxr_create_session() { return false; }

int64_t gr_opengl_openxr_get_swapchain_format(const SCP_vector<int64_t>& /*allowed*/) { return 0; }

bool gr_opengl_openxr_acquire_swapchain_buffers() { return false; }

bool gr_opengl_openxr_flip() { return false; }

#endif
