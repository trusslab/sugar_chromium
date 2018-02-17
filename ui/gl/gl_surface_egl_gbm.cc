/* Copyright (C) 2016-2018 University of California, Irvine
 *
 * Authors:
 * Zhihao Yao <z.yao@uci.edu>
 * Ardalan Amiri Sani <arrdalan@gmail.com>
 * 
 * All rights reserved. Use of this source code is governed
 * by a BSD-style license that can be found in the LICENSE file.
 *  
 * Based on gl_surface_egl_x11:
 * Copyright (c) 2015 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *  
 * Also, based on kmscube:
 *
 * Copyright (c) 2012 Arvin Schnell <arvin.schnell@gmail.com>
 * Copyright (c) 2012 Rob Clark <rob@ti.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/* Based on a egl cube test app originally written by Arvin Schnell */

#include "ui/gl/gl_surface_egl_gbm.h"

#include "ui/gl/egl_util.h"

#include <fcntl.h>
#include <sugar/isol_file_ops.h>
#include "base/prints.h"

using ui::GetLastEGLErrorString;

namespace gl {

const char kDriverLibraryNameSugar[] = "/usr/lib/x86_64-linux-gnu/libsugar_driver.so";
const char kDRMLibraryNameSugar[] = "/usr/lib/x86_64-linux-gnu/lib/libdrm2.so";
const char kGBMLibraryNameSugar[] = "/usr/lib/x86_64-linux-gnu/lib/libgbm2.so";

EGLDisplay g_display = nullptr;

/* driver */
isol_open_proc isol_open;
isol_poll_proc isol_poll;
isol_flip_mode_proc isol_flip_mode;

/* gbm */
struct gbm_device *g_gbm_dev;
gbm_create_device_proc gbm_create_device;
gbm_surface_create_proc gbm_surface_create;
gbm_bo_get_user_data_proc gbm_bo_get_user_data;
gbm_bo_get_width_proc gbm_bo_get_width;
gbm_bo_get_height_proc gbm_bo_get_height;
gbm_bo_get_stride_proc gbm_bo_get_stride;
gbm_bo_get_handle_proc gbm_bo_get_handle;
gbm_bo_set_user_data_proc gbm_bo_set_user_data;
gbm_surface_lock_front_buffer_proc gbm_surface_lock_front_buffer;
gbm_surface_release_buffer_proc gbm_surface_release_buffer;

/* drm */
int g_fd;
drmModeModeInfo *g_mode;
uint32_t g_crtc_id;
uint32_t g_connector_id;
drmModeGetEncoderProc drmModeGetEncoder;
drmModeFreeEncoderProc drmModeFreeEncoder;
drmModeGetResourcesProc drmModeGetResources;
drmModeGetConnectorProc drmModeGetConnector;
drmModeFreeConnectorProc drmModeFreeConnector;
drmModeRmFBProc drmModeRmFB;
drmModeAddFBProc drmModeAddFB;
drmModeSetCrtcProc drmModeSetCrtc;
drmModePageFlipProc drmModePageFlip;
drmHandleEventProc drmHandleEvent;

/* surface */
gfx::Size g_size;
EGLSurface g_surface;
EGLContext g_context, g_flip_context;
EGLConfig g_config;
struct gbm_surface *g_gbm_surface;

struct gbm_bo *g_bo;
struct drm_fb *g_fb;

struct drm_fb {
	struct gbm_bo *bo;
	uint32_t fb_id;
};

/* quad program */
GLuint g_quad_program;
GLuint g_quad_program_texID;
GLuint g_quad_vertexbuffer;

NativeViewGLSurfaceEGLGBM::NativeViewGLSurfaceEGLGBM()
    {
}

void NativeViewGLSurfaceEGLGBM::drm_fb_destroy_callback(struct gbm_bo *bo, void *data)
{
	struct drm_fb *fb = (struct drm_fb *) data;

	if (fb->fb_id)
		(*drmModeRmFB)(g_fd, fb->fb_id);

	free(fb);
}

struct drm_fb *NativeViewGLSurfaceEGLGBM::drm_fb_get_from_bo(struct gbm_bo *bo)
{
	struct drm_fb *fb = (struct drm_fb *) (*gbm_bo_get_user_data)(bo);
	uint32_t width, height, stride, handle;
	int ret;

	if (fb)
		return fb;

	fb = (struct drm_fb *) calloc(1, sizeof *fb);
	fb->bo = bo;

	width = (*gbm_bo_get_width)(bo);
	height = (*gbm_bo_get_height)(bo);
	stride = (*gbm_bo_get_stride)(bo);
	handle = (*gbm_bo_get_handle)(bo).u32;

	ret = (*drmModeAddFB)(g_fd, width, height, 24, 32, stride, handle, &fb->fb_id);
	if (ret) {
		free(fb);
		return NULL;
	}

	(*gbm_bo_set_user_data)(bo, fb, drm_fb_destroy_callback);

	return fb;
}

/* FIXME: this func does a lot more than initializing the surface. */
bool NativeViewGLSurfaceEGLGBM::InitializeSurface(const gfx::Size& size) {
  EGLint n;

  static const EGLint config_attribs[] = {
  	EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
  	EGL_RED_SIZE, 1,
  	EGL_GREEN_SIZE, 1,
  	EGL_BLUE_SIZE, 1,
  	EGL_ALPHA_SIZE, 0,
  	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
  	EGL_NONE
  };

  g_size = size;

  g_gbm_surface = (*gbm_surface_create)(g_gbm_dev,
		g_mode->hdisplay, g_mode->vdisplay,
		GBM_FORMAT_XRGB8888,
		GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
  if (!g_gbm_surface) {
    return false;
  }

  if (!eglChooseConfig(g_display, config_attribs, &g_config, 1, &n) || n != 1) {
    return false;
  }

  g_surface = eglCreateWindowSurface(g_display, g_config, (EGLNativeWindowType) g_gbm_surface, NULL);
  if (g_surface == EGL_NO_SURFACE) {
    return false;
  }

  /* connect the context to the surface */

  return true;
}

uint32_t NativeViewGLSurfaceEGLGBM::find_crtc_for_encoder(const drmModeRes *resources,
			                                  const drmModeEncoder *encoder) {
	int i;

	for (i = 0; i < resources->count_crtcs; i++) {
		/* possible_crtcs is a bitmask as described here:
		 * https://dvdhrm.wordpress.com/2012/09/13/linux-drm-mode-setting-api
		 */
		const uint32_t crtc_mask = 1 << i;
		const uint32_t crtc_id = resources->crtcs[i];
		if (encoder->possible_crtcs & crtc_mask) {
			return crtc_id;
		}
	}

	/* no match found */
	return -1;
}

uint32_t NativeViewGLSurfaceEGLGBM::find_crtc_for_connector(const drmModeRes *resources,
				                            const drmModeConnector *connector) {
	int i;

	for (i = 0; i < connector->count_encoders; i++) {
		const uint32_t encoder_id = connector->encoders[i];
		drmModeEncoder *encoder = (*drmModeGetEncoder)(g_fd, encoder_id);

		if (encoder) {
			const uint32_t crtc_id = find_crtc_for_encoder(resources, encoder);

			(*drmModeFreeEncoder)(encoder);
			if (crtc_id != 0) {
				return crtc_id;
			}
		}
	}

	/* no match found */
	return -1;
}

int NativeViewGLSurfaceEGLGBM::init_drm()
{
	drmModeRes *resources;
	drmModeConnector *connector = NULL;
	drmModeEncoder *encoder = NULL;
	int i, area;

	g_fd = (*isol_open)(INTEL_GPU_DEV_FILE_NAME, O_RDWR, 0);

	if (g_fd < 0) {
		printf("could not open drm device\n");
		return -1;
	}

	resources = (*drmModeGetResources)(g_fd);
	if (!resources) {
		printf("drmModeGetResources failed: %s\n", strerror(errno));
		return -1;
	}

	/* find a connected connector: */
	for (i = 0; i < resources->count_connectors; i++) {
		connector = (*drmModeGetConnector)(g_fd, resources->connectors[i]);
		if (connector->connection == DRM_MODE_CONNECTED) {
			/* it's connected, let's use this! */
			break;
		}
		(*drmModeFreeConnector)(connector);
		connector = NULL;
	}

	if (!connector) {
		/* we could be fancy and listen for hotplug events and wait for
		 * a connector..
		 */
		return -1;
	}

	g_mode = nullptr;

	/* find prefered mode or the highest resolution mode: */
	for (i = 0, area = 0; i < connector->count_modes; i++) {
		drmModeModeInfo *current_mode = &connector->modes[i];

		if (i == 0) 
			g_mode = current_mode;

		if (current_mode->type & DRM_MODE_TYPE_PREFERRED) {
			g_mode = current_mode;
		}

		int current_area = current_mode->hdisplay * current_mode->vdisplay;
		if (current_area > area) {
			g_mode = current_mode;
			area = current_area;
		}
	}

	if (!g_mode) {
		return -1;
	}

	/* find encoder: */
	for (i = 0; i < resources->count_encoders; i++) {
		encoder = (*drmModeGetEncoder)(g_fd, resources->encoders[i]);
		if (encoder->encoder_id == connector->encoder_id)
			break;
		(*drmModeFreeEncoder)(encoder);
		encoder = NULL;
	}

	if (encoder) {
		g_crtc_id = encoder->crtc_id;
	} else {
		uint32_t crtc_id = find_crtc_for_connector(resources, connector);
		if (crtc_id == 0) {
			return -1;
		}

		g_crtc_id = crtc_id;
	}

	g_connector_id = connector->connector_id;

	return 0;
}

int NativeViewGLSurfaceEGLGBM::init_gbm()
{
	g_gbm_dev = (*gbm_create_device)(g_fd);

	return 0;
}

static const char *vertex_shader_source =
	"attribute vec3 vertexPosition_modelspace;                 \n"
	"                                                          \n"
	"// Output data ; will be interpolated for each fragment.  \n"
	"varying vec2 UV;                                          \n"
	"                                                          \n"
	"void main(){                                              \n"
	"	gl_Position =  vec4(vertexPosition_modelspace[0], vertexPosition_modelspace[1]*(-1.0), vertexPosition_modelspace[2],1);  \n"
	"	UV = (vertexPosition_modelspace.xy+vec2(1,1))/2.0; \n"
	"}                                                         \n";

static const char *fragment_shader_source =
	"precision mediump float;                                  \n"
	"                                                          \n"
	"uniform sampler2D renderedTexture;                        \n"
	"                                                          \n"
	"varying vec2 UV;                                          \n"
	"                                                          \n"
	"void main(){                                              \n"
	"	gl_FragColor = texture2D(renderedTexture, UV);\n"
	"}                                                         \n";

int NativeViewGLSurfaceEGLGBM::init_quad_program()
{
	GLuint vertex_shader, fragment_shader;
	GLint ret;

	/* The fullscreen quad's FBO */
	GLfloat quad_vertex_buffer_data[] = { 
		-1.0f, -1.0f, 0.0f,
		 1.0f, -1.0f, 0.0f,
		-1.0f,  1.0f, 0.0f,
		-1.0f,  1.0f, 0.0f,
		 1.0f, -1.0f, 0.0f,
		 1.0f,  1.0f, 0.0f,
	};

	if (!eglBindAPI(EGL_OPENGL_ES_API)) {
		return -1;
	}

	vertex_shader = glCreateShader(GL_VERTEX_SHADER);

	glShaderSource(vertex_shader, 1, &vertex_shader_source, NULL);
	glCompileShader(vertex_shader);

	glGetShaderiv(vertex_shader, GL_COMPILE_STATUS, &ret);
	if (!ret) {
		char *log;

		glGetShaderiv(vertex_shader, GL_INFO_LOG_LENGTH, &ret);
		if (ret > 1) {
			log = (char *) malloc(ret);
			glGetShaderInfoLog(vertex_shader, ret, NULL, log);
		}

		return -1;
	}

	fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);

	glShaderSource(fragment_shader, 1, &fragment_shader_source, NULL);
	glCompileShader(fragment_shader);

	glGetShaderiv(fragment_shader, GL_COMPILE_STATUS, &ret);
	if (!ret) {
		char *log;

		glGetShaderiv(fragment_shader, GL_INFO_LOG_LENGTH, &ret);

		if (ret > 1) {
			log = (char *) malloc(ret);
			glGetShaderInfoLog(fragment_shader, ret, NULL, log);
		}

		return -1;
	}

	g_quad_program = glCreateProgram();

	glAttachShader(g_quad_program, vertex_shader);
	glAttachShader(g_quad_program, fragment_shader);

	glLinkProgram(g_quad_program);

	glGetProgramiv(g_quad_program, GL_LINK_STATUS, &ret);
	if (!ret) {
		char *log;

		glGetProgramiv(g_quad_program, GL_INFO_LOG_LENGTH, &ret);

		if (ret > 1) {
			log = (char *) malloc(ret);
			glGetProgramInfoLog(g_quad_program, ret, NULL, log);
		}

		return -1;
	}

	glGenBuffersARB(1, &g_quad_vertexbuffer);
	glBindBuffer(GL_ARRAY_BUFFER, g_quad_vertexbuffer);
	glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertex_buffer_data),
				quad_vertex_buffer_data, GL_STATIC_DRAW);

	g_quad_program_texID = glGetUniformLocation(g_quad_program, "renderedTexture");

	return 0;
}

EGLDisplay NativeViewGLSurfaceEGLGBM::GetGBMDisplay() {
  int ret;
  base::FilePath driver_path(kDriverLibraryNameSugar);
  base::FilePath drm_path(kDRMLibraryNameSugar);
  base::FilePath gbm_path(kGBMLibraryNameSugar);
  
  if (g_display)
    return g_display;

  base::NativeLibrary driver_library = LoadLibraryAndPrintError(driver_path);
  if (!driver_library) {
    return nullptr;
  }

  base::NativeLibrary drm_library = LoadLibraryAndPrintError(drm_path);
  if (!drm_library) {
    base::UnloadNativeLibrary(driver_library);
    return nullptr;
  }
  
  base::NativeLibrary gbm_library = LoadLibraryAndPrintError(gbm_path);
  if (!gbm_library) {
    base::UnloadNativeLibrary(drm_library);
    base::UnloadNativeLibrary(driver_library);
    return nullptr;
  }

  isol_open = reinterpret_cast<isol_open_proc>(
		base::GetFunctionPointerFromNativeLibrary(driver_library, "isol_open"));
  isol_poll = reinterpret_cast<isol_poll_proc>(
		base::GetFunctionPointerFromNativeLibrary(driver_library, "isol_poll"));
  isol_flip_mode = reinterpret_cast<isol_flip_mode_proc>(
		base::GetFunctionPointerFromNativeLibrary(driver_library, "isol_flip_mode"));
      
  if (!isol_open || !isol_poll || !isol_flip_mode) {
    base::UnloadNativeLibrary(gbm_library);
    base::UnloadNativeLibrary(drm_library);
    base::UnloadNativeLibrary(driver_library);
    return nullptr;
  }

  gbm_create_device = reinterpret_cast<gbm_create_device_proc>(
		base::GetFunctionPointerFromNativeLibrary(gbm_library, "gbm_create_device"));
  gbm_surface_create = reinterpret_cast<gbm_surface_create_proc>(
		base::GetFunctionPointerFromNativeLibrary(gbm_library, "gbm_surface_create"));
  gbm_bo_get_user_data = reinterpret_cast<gbm_bo_get_user_data_proc>(
		base::GetFunctionPointerFromNativeLibrary(gbm_library, "gbm_bo_get_user_data"));
  gbm_bo_get_width = reinterpret_cast<gbm_bo_get_width_proc>(
		base::GetFunctionPointerFromNativeLibrary(gbm_library, "gbm_bo_get_width"));
  gbm_bo_get_height = reinterpret_cast<gbm_bo_get_height_proc>(
		base::GetFunctionPointerFromNativeLibrary(gbm_library, "gbm_bo_get_height"));
  gbm_bo_get_stride = reinterpret_cast<gbm_bo_get_stride_proc>(
		base::GetFunctionPointerFromNativeLibrary(gbm_library, "gbm_bo_get_stride"));
  gbm_bo_get_handle = reinterpret_cast<gbm_bo_get_handle_proc>(
		base::GetFunctionPointerFromNativeLibrary(gbm_library, "gbm_bo_get_handle"));
  gbm_bo_set_user_data = reinterpret_cast<gbm_bo_set_user_data_proc>(
		base::GetFunctionPointerFromNativeLibrary(gbm_library, "gbm_bo_set_user_data"));
  gbm_surface_lock_front_buffer = reinterpret_cast<gbm_surface_lock_front_buffer_proc>(
		base::GetFunctionPointerFromNativeLibrary(gbm_library, "gbm_surface_lock_front_buffer"));
  gbm_surface_release_buffer = reinterpret_cast<gbm_surface_release_buffer_proc>(
		base::GetFunctionPointerFromNativeLibrary(gbm_library, "gbm_surface_release_buffer"));

  if (!gbm_create_device || !gbm_surface_create || !gbm_bo_get_user_data || !gbm_bo_get_width ||
      !gbm_bo_get_height || !gbm_bo_get_stride || !gbm_bo_get_handle || !gbm_bo_set_user_data ||
      !gbm_surface_lock_front_buffer || !gbm_surface_release_buffer) {
    base::UnloadNativeLibrary(gbm_library);
    base::UnloadNativeLibrary(drm_library);
    base::UnloadNativeLibrary(driver_library);
    return nullptr;
  }

  drmModeGetEncoder = reinterpret_cast<drmModeGetEncoderProc>(
		base::GetFunctionPointerFromNativeLibrary(drm_library, "drmModeGetEncoder"));
  drmModeFreeEncoder = reinterpret_cast<drmModeFreeEncoderProc>(
		base::GetFunctionPointerFromNativeLibrary(drm_library, "drmModeFreeEncoder"));
  drmModeGetResources = reinterpret_cast<drmModeGetResourcesProc>(
		base::GetFunctionPointerFromNativeLibrary(drm_library, "drmModeGetResources"));
  drmModeGetConnector = reinterpret_cast<drmModeGetConnectorProc>(
		base::GetFunctionPointerFromNativeLibrary(drm_library, "drmModeGetConnector"));
  drmModeFreeConnector = reinterpret_cast<drmModeFreeConnectorProc>(
		base::GetFunctionPointerFromNativeLibrary(drm_library, "drmModeFreeConnector"));
  drmModeRmFB = reinterpret_cast<drmModeRmFBProc>(
		base::GetFunctionPointerFromNativeLibrary(drm_library, "drmModeRmFB"));
  drmModeAddFB = reinterpret_cast<drmModeAddFBProc>(
		base::GetFunctionPointerFromNativeLibrary(drm_library, "drmModeAddFB"));
  drmModeSetCrtc = reinterpret_cast<drmModeSetCrtcProc>(
		base::GetFunctionPointerFromNativeLibrary(drm_library, "drmModeSetCrtc"));
  drmModePageFlip = reinterpret_cast<drmModePageFlipProc>(
		base::GetFunctionPointerFromNativeLibrary(drm_library, "drmModePageFlip"));
  drmHandleEvent = reinterpret_cast<drmHandleEventProc>(
		base::GetFunctionPointerFromNativeLibrary(drm_library, "drmHandleEvent"));

  if (!drmModeGetEncoder || !drmModeFreeEncoder || !drmModeGetResources ||
      !drmModeGetConnector || !drmModeFreeConnector || !drmModeRmFB || !drmModeAddFB ||
      !drmModeSetCrtc || !drmModePageFlip || !drmHandleEvent) {
    base::UnloadNativeLibrary(gbm_library);
    base::UnloadNativeLibrary(drm_library);
    base::UnloadNativeLibrary(driver_library);
    return nullptr;
  }

  ret = init_drm();
  if (ret) {
    printf("failed to initialize DRM\n");
    return nullptr;
  }
  
  ret = init_gbm();
  if (ret) {
    printf("failed to initialize GBM\n");
    return nullptr;
  }

  /* FIXME: set window_ and size_ */

  g_display = eglGetDisplay((EGLNativeDisplayType) g_gbm_dev);

  return g_display;
}

void NativeViewGLSurfaceEGLGBM::Destroy() {

}

EGLConfig NativeViewGLSurfaceEGLGBM::GetConfig() {
	
  return g_config;

}

bool NativeViewGLSurfaceEGLGBM::Resize(const gfx::Size& size,
                                       float scale_factor,
                                       bool has_alpha) {

  return true;
}

bool NativeViewGLSurfaceEGLGBM::Initialize(GLSurfaceFormat format) {
  return true;
}

bool NativeViewGLSurfaceEGLGBM::IsOffscreen() {
  return false;
}

bool NativeViewGLSurfaceEGLGBM::IsSurfaceless() const {
  return false;
}

gfx::SwapResult NativeViewGLSurfaceEGLGBM::SwapBuffers() {
  return gfx::SwapResult::SWAP_ACK;
}

gfx::Size NativeViewGLSurfaceEGLGBM::GetSize() {
  return g_size;
}

EGLSurface NativeViewGLSurfaceEGLGBM::GetHandle() {
  return g_surface;
}

void* NativeViewGLSurfaceEGLGBM::GetShareHandle() {
  return NULL;
}

EGLSurface NativeViewGLSurfaceEGLGBM::GetSurface() {
  return g_surface;
}

static void page_flip_handler(int fd, unsigned int frame,
		  unsigned int sec, unsigned int usec, void *data)
{
	int *waiting_for_flip = (int *)data;
	*waiting_for_flip = 0;
}

void NativeViewGLSurfaceEGLGBM::flipBuffer(GLuint texture_id) {
  int waiting_for_flip = 1;
  int counter;
  struct gbm_bo *next_bo;
  drmEventContext evctx = {
  		.version = DRM_EVENT_CONTEXT_VERSION,
  		.page_flip_handler = page_flip_handler,
  };
  int ret;

  (*isol_flip_mode)(1);

  /* Create a texture for testing */
   /* end of creating the texture */

  eglMakeCurrent(g_display, g_surface, g_surface, g_flip_context);

  glBindFramebufferEXT(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, g_mode->hdisplay, g_mode->vdisplay);
  
  /* FIXME: do we need this if we will render to the screen shortly? */
  glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glUseProgram(g_quad_program);
  
  glActiveTexture(GL_TEXTURE0);

  glBindTexture(GL_TEXTURE_2D, texture_id);
  glUniform1i(g_quad_program_texID, 0);
  
  glEnableVertexAttribArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, g_quad_vertexbuffer);
  glVertexAttribPointer(
  	0,                  
  	3,                  
  	GL_FLOAT,           
  	GL_FALSE,           
  	0,                  
  	(void*)0            
  );
  
  glDrawArrays(GL_TRIANGLES, 0, 6); 
  
  glDisableVertexAttribArray(0);

  /* Then, we'll swap/flip */

  eglSwapBuffers(g_display, g_surface);

  next_bo = (*gbm_surface_lock_front_buffer)(g_gbm_surface);
  if (!next_bo) {
    goto end;
  }

  g_fb = drm_fb_get_from_bo(next_bo);
  
  /*
   * Here you could also update drm plane layers if you want
   * hw composition
   */
  
  ret = (*drmModePageFlip)(g_fd, g_crtc_id, g_fb->fb_id,
  		DRM_MODE_PAGE_FLIP_EVENT, &waiting_for_flip);
  if (ret) {
    goto end;
  }
  
  counter = 15;
  while (waiting_for_flip && counter > 0) {
  	counter--;
  	ret = (*isol_poll)(g_fd, 0); /* 0 -> no blocking */
  	if (ret < 0) {
  		goto end;
  	} else if (ret == 0) {
  		sleep(1);
  		continue;
  	}
  	(*drmHandleEvent)(g_fd, &evctx);
  }
  
  if (counter <= 0) {
  }
  
  /* release last buffer to render on again: */
  (*gbm_surface_release_buffer)(g_gbm_surface, g_bo);
  g_bo = next_bo;

  eglMakeCurrent(g_display, g_surface, g_surface, g_context);

end:
  (*isol_flip_mode)(0);
}

bool NativeViewGLSurfaceEGLGBM::setContext(EGLContext context) {
  int ret;

  static const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
  };

  g_context = context;

  g_flip_context = eglCreateContext(g_display, g_config,
        		g_context, context_attribs);
  if (g_flip_context == NULL) {
    return false;
  }

  eglSwapBuffers(g_display, g_surface);

  g_bo = (*gbm_surface_lock_front_buffer)(g_gbm_surface);
  if (!g_bo) {
    return false;
  }

  g_fb = drm_fb_get_from_bo(g_bo);
  
  /* set mode: */
  ret = (*drmModeSetCrtc)(g_fd, g_crtc_id, g_fb->fb_id, 0, 0,
  		&g_connector_id, 1, g_mode);
  if (ret) {
    return false;
  }

  ret = init_quad_program();
  if (ret) {
    return false;
  }

  /* FIXME */

  return true;
}

NativeViewGLSurfaceEGLGBM::~NativeViewGLSurfaceEGLGBM() {
  Destroy();
}

}  
