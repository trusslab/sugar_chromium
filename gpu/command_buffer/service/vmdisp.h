/* Copyright (C) 2016-2018 University of California, Irvine
 *
 * Authors:
 * Zhihao Yao <z.yao@uci.edu>
 * Zongheng Ma <zonghenm@uci.edu>
 * Ardalan Amiri Sani <arrdalan@gmail.com>
 * 
 * All rights reserved. Use of this source code is governed
 * by a BSD-style license that can be found in the LICENSE file.
 *
 * based on:
 * QEMU Intel GVT-g indirect display support
 * Copyright (c) Intel
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory
 */

#ifndef GPU_COMMAND_BUFFER_SERVICE_VMDISP_H
#define GPU_COMMAND_BUFFER_SERVICE_VMDISP_H

#include <limits.h>

#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "ui/gl/gl_context.h"

#include <GL/gl.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <xf86drm.h>

#define EGL_EGLEXT_PROTOTYPES
#define __user

#define I915_VGT_PLANE_PRIMARY 1
#define I915_VGT_PLANE_SPRITE 2
#define I915_VGT_PLANE_CURSOR 3
#define I915_VGTBUFFER_READ_ONLY (1<<0)
#define I915_VGTBUFFER_QUERY_ONLY (1<<1)
#define I915_VGTBUFFER_CHECK_CAPABILITY (1<<2)
#define I915_VGTBUFFER_UNSYNCHRONIZED 0x80000000

#define DRM_I915_GEM_VGTBUFFER          0x36
#define DRM_IOCTL_I915_GEM_VGTBUFFER    DRM_IOWR(DRM_COMMAND_BASE + DRM_I915_GEM_VGTBUFFER, struct drm_i915_gem_vgtbuffer)
  
struct buffer_rec{
    uint32_t start;
    GLuint textureId;
    int age;
    uint8_t tiled;
    uint32_t size;
};

struct buffer_list{
    struct buffer_rec *l;
    int len;
    bool firstcreated;
};
  
struct drm_i915_gem_vgtbuffer {
        __u32 vmid;
        __u32 plane_id;
        __u32 pipe_id;
        __u32 phys_pipe_id;
        __u8  enabled;
        __u8  tiled;
        __u32 bpp;
        __u32 hw_format;
        __u32 drm_format;
        __u32 start;
        __u32 x_pos;
        __u32 y_pos;
        __u32 x_offset;
        __u32 y_offset;
        __u32 size;
        __u32 width;
        __u32 height;
        __u32 stride;
        __u64 user_ptr;
        __u32 user_size;
        __u32 flags;
        /**
         * Returned handle for the object.
         *
         * Object handles are nonzero.
         */
        __u32 handle;
};

struct egl_manager {
   EGLNativeDisplayType xdpy;
   EGLNativeWindowType xwin;
   EGLNativePixmapType xpix;

   EGLDisplay dpy;
   EGLConfig conf;

   EGLSurface pix;
   EGLSurface pbuf;
   EGLImageKHR image;

   EGLBoolean verbose;
   EGLint major, minor;

   GC gc;
   GLuint fbo;
};

class gl_vmdisp
{
public:
  gl_vmdisp(int renderer_pid);
  ~gl_vmdisp();
  void set_vmdisp_egl_context(scoped_refptr<gl::GLContext> context_);
  void initialize_vmdisp(void);
  void vmdisp_event_loop(void);
  
  EGLContext ctx;
  GLuint current_textureId;

protected:
  int find_rec(struct buffer_list *l, struct drm_i915_gem_vgtbuffer *vgtbuffer);
  int oldest_rec(struct buffer_list *l);
  void age_list(struct buffer_list *l);
  void clear_rec(struct buffer_list *l, int i);
  void create_primary_buffer(void);
  bool intel_vgt_check_composite_display(void);
  void vmdisp_init(void);
  struct egl_manager *
  egl_manager_new(EGLNativeDisplayType xdpy, 
                  const EGLint *attrib_list,
                  EGLBoolean verbose);
  void egl_manager_destroy(struct egl_manager *eman);
  void check_for_new_primary_buffer(void);
  bool check_egl(void);

  typedef int (*drm_ioctl_proc)(int, unsigned long, void*);
  typedef void (*gl_color_3f_proc)(GLfloat, GLfloat, GLfloat);
  typedef void (*gl_ortho_proc)(GLdouble, GLdouble, GLdouble, GLdouble, GLdouble, GLdouble); 
  typedef void (*gl_matrix_mode_proc)(GLenum);
  typedef EGLBoolean (*eglBindAPI_proc)(EGLenum);
  typedef EGLDisplay (*eglGetDisplay_proc)(NativeDisplayType);
  typedef void (*glEGLImageTargetTexture2DOES_proc)(GLenum, GLeglImageOES);
  typedef __eglMustCastToProperFunctionPointerType (*eglGetProcAddress_proc)(const char*);
  typedef EGLBoolean (*eglInitialize_proc)(EGLDisplay, EGLint*, EGLint*);
  typedef Display* (*XOpenDisplay_proc)(char*);

  typedef EGLBoolean (*eglMakeCurrent_proc)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
  typedef EGLDisplay (*eglGetCurrentDisplay_proc)();

  typedef EGLContext (*eglCreateContext_proc)(EGLDisplay dpy, EGLConfig config, EGLContext share_list,
                                              const EGLint *attrib_list);

  struct buffer_list primary_list;
  struct buffer_list cursor_list;

  struct egl_manager* eman_;

  int vmid = 0;

  GLfloat view_transz = 0.0;
  GLfloat g_angle = 180.0;

  int wire = 0;
  int sphere = 0;
  GLuint textureId = 0;
  GLuint cursortextureId = 0;
  GLfloat widthoverstride = 0.;
  int fbWidth = 0;
  int fbHeight = 0;

  int vm_pipe = UINT_MAX;
  bool dma_buf_mode = false;

  int winWidth = 1024;
  int winHeight = 768;

  int fd = 0;

  uint32_t current_primary_fb_addr = 0;
  uint32_t current_cursor_fb_addr = 0;
  GLuint current_cursor_textureId;
  bool cursor_ready = false;
  
  EGLDisplay dpy;
  EGLSurface sur;

  const int PRIMARY_LIST_LEN = 3;
  const int CURSOR_LIST_LEN = 4;

  drm_ioctl_proc drmIoctl_;
  gl_color_3f_proc glColor3f_;
  gl_ortho_proc glOrtho_;
  gl_matrix_mode_proc glMatrixMode_;

  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES_func;
  PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR_func;
  PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR_func;
  glEGLImageTargetTexture2DOES_proc glEGLImageTargetTexture2DOES_func_;
  eglBindAPI_proc                   eglBindAPI_func;
  eglGetDisplay_proc                eglGetDisplay_func;
  eglGetProcAddress_proc            eglGetProcAddress_func;
  eglGetProcAddress_proc            _glapi_get_proc_address_func;
  eglInitialize_proc                eglInitialize_func;
  XOpenDisplay_proc                 XOpenDisplay_func;
  eglMakeCurrent_proc               eglMakeCurrent_func;
  eglGetCurrentDisplay_proc         eglGetCurrentDisplay_func;
  eglCreateContext_proc             eglCreateContext_func;

};

#endif 
