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

#ifndef UI_GL_GL_SURFACE_EGL_GBM_H_
#define UI_GL_GL_SURFACE_EGL_GBM_H_

#include <gbm2.h>

#include "ui/gl/gl_surface_egl.h"

#include <xf86drm2.h>
#include <xf86drm2Mode.h>

#include <stdint.h>
#include <string>

namespace gl {

typedef int (*isol_open_proc)(const char *, int, mode_t);
typedef int (*isol_poll_proc)(int, int);
typedef void (*isol_flip_mode_proc)(int);

typedef struct gbm_device *(*gbm_create_device_proc)(int);
typedef struct gbm_surface *(*gbm_surface_create_proc)(struct gbm_device *, uint32_t,
						       uint32_t, uint32_t , uint32_t);
typedef void *(*gbm_bo_get_user_data_proc)(struct gbm_bo *);
typedef unsigned int (*gbm_bo_get_width_proc)(struct gbm_bo *);
typedef unsigned int (*gbm_bo_get_height_proc)(struct gbm_bo *);
typedef uint32_t (*gbm_bo_get_stride_proc)(struct gbm_bo *);
typedef union gbm_bo_handle (*gbm_bo_get_handle_proc)(struct gbm_bo *);
typedef void (*gbm_bo_set_user_data_proc)(struct gbm_bo *, void *,
                                  void (*destroy_user_data)(struct gbm_bo *, void *));
typedef struct gbm_bo *(*gbm_surface_lock_front_buffer_proc)(struct gbm_surface *);
typedef void (*gbm_surface_release_buffer_proc)(struct gbm_surface *, struct gbm_bo *);

typedef drmModeEncoderPtr (*drmModeGetEncoderProc)(int, uint32_t);
typedef void (*drmModeFreeEncoderProc)(drmModeEncoderPtr);
typedef drmModeResPtr (*drmModeGetResourcesProc)(int);
typedef drmModeConnectorPtr (*drmModeGetConnectorProc)(int, uint32_t);
typedef void (*drmModeFreeConnectorProc)(drmModeConnectorPtr);
typedef int (*drmModeRmFBProc)(int, uint32_t);
typedef int (*drmModeAddFBProc)(int, uint32_t, uint32_t, uint8_t,
                                uint8_t, uint32_t, uint32_t, uint32_t *);
typedef int (*drmModeSetCrtcProc)(int, uint32_t, uint32_t, uint32_t, uint32_t,
				  uint32_t *, int, drmModeModeInfoPtr);
typedef int (*drmModePageFlipProc)(int, uint32_t, uint32_t, uint32_t, void *);
typedef int (*drmHandleEventProc)(int, drmEventContextPtr);

/* FIXME: rename the class */
class GL_EXPORT NativeViewGLSurfaceEGLGBM : public GLSurfaceEGL {
 public:
  explicit NativeViewGLSurfaceEGLGBM();

  EGLConfig GetConfig() override;
  bool Initialize(GLSurfaceFormat format) override;
  void Destroy() override;
  bool IsOffscreen() override;
  bool IsSurfaceless() const override;
  gfx::SwapResult SwapBuffers() override;
  gfx::Size GetSize() override;
  bool Resize(const gfx::Size& size,
              float scale_factor,
              bool has_alpha) override;
  EGLSurface GetHandle() override;
  void* GetShareHandle() override;
  static EGLDisplay GetGBMDisplay();
  static bool InitializeSurface(const gfx::Size& size);
  static EGLSurface GetSurface();
  static void flipBuffer(GLuint texture_id);
  static struct drm_fb *drm_fb_get_from_bo(struct gbm_bo *bo);
  static void drm_fb_destroy_callback(struct gbm_bo *bo, void *data);
  static bool setContext(EGLContext context);

 private:
  ~NativeViewGLSurfaceEGLGBM() override;

  static uint32_t find_crtc_for_encoder(const drmModeRes *resources,
			         const drmModeEncoder *encoder);
  static uint32_t find_crtc_for_connector(const drmModeRes *resources,
				   const drmModeConnector *connector);
  static int init_drm();
  static int init_gbm();
  static int init_quad_program();
 
  DISALLOW_COPY_AND_ASSIGN(NativeViewGLSurfaceEGLGBM);
};

}  

#endif  
