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

#include "gpu/command_buffer/service/vmdisp.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include "error.h"

#include "base/files/file_path.h"
#include "base/native_library.h"

#include "ui/gfx/x/x11_types.h"
#include "ui/gl/gl_surface_egl.h"
#include "ui/gl/init/gl_factory.h"
#include "ui/gl/gl_implementation.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <xf86drm.h>
#include <libdrm/drm_fourcc.h>

#include "base/prints.h"

int gl_vmdisp::find_rec(struct buffer_list *l, struct drm_i915_gem_vgtbuffer
                    *vgtbuffer)
{
    int i, r;

    r = -1;
    for (i = 0; i < l->len; i++) {
        if (l->l[i].start == vgtbuffer->start &&
            l->l[i].tiled == vgtbuffer->tiled &&
            l->l[i].size == vgtbuffer->size) {
            r = i;
            break;
		} else if ((l->l[i].tiled != vgtbuffer->tiled) ||
					(l->l[i].size != vgtbuffer->size)) {
			l->firstcreated = false;
			break;
        }
    }

    return r;
}

int gl_vmdisp::oldest_rec(struct buffer_list *l)
{
    int i = 1, a = l->l[0].age, r = 0;

    for (i = 1; i < l->len; i++) {
        if (l->l[i].age > a) {
            a = l->l[i].age;
            r = i;
        }
    }

    return r;
}

void gl_vmdisp::age_list(struct buffer_list *l)
{
    int i;

    for (i = 0; i < l->len; i++) {
        if (l->l[i].age != INT_MAX) {
            l->l[i].age++;
        }
    }
}

void gl_vmdisp::clear_rec(struct buffer_list *l, int i)
{
    l->l[i].age = INT_MAX;
    l->l[i].start = 0;
    l->l[i].textureId = 0;
}

void gl_vmdisp::create_primary_buffer(void)
{
    struct drm_i915_gem_vgtbuffer vcreate;
    int width = 0, height = 0 ,stride = 0;
	int i, r, ret;
    EGLImageKHR namedimage;
    unsigned long name;

    memset(&vcreate, 0, sizeof(struct drm_i915_gem_vgtbuffer));
    width = 0;
    height = 0;
    stride = width * 4;
    vcreate.vmid = vmid;
    vcreate.plane_id = I915_VGT_PLANE_PRIMARY;
    vcreate.phys_pipe_id = vm_pipe;

    ret = (*drmIoctl_)(fd, DRM_IOCTL_I915_GEM_VGTBUFFER, &vcreate);
    width = vcreate.width;
    height = vcreate.height;
    stride = vcreate.stride;
    widthoverstride = (float)width / (float)(stride / 4);
    fbWidth = width;
    fbHeight = height;
    current_primary_fb_addr = vcreate.start;

	if (!primary_list.firstcreated) {
		for (i = 0; i < primary_list.len; i++) {
			clear_rec(&primary_list, i);
			primary_list.l[i].tiled = vcreate.tiled;
			primary_list.l[i].size  = vcreate.size;
		}
		primary_list.firstcreated = true;
	}

    r = oldest_rec(&primary_list);

    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);

    primary_list.l[r].start = current_primary_fb_addr;
    primary_list.l[r].textureId = textureId;
    primary_list.l[r].age = 0;
    primary_list.l[r].tiled = vcreate.tiled;
    primary_list.l[r].size = vcreate.size;
    current_textureId = textureId;

    if (dma_buf_mode) {
        struct drm_prime_handle prime;
        prime.handle = vcreate.handle;
        prime.flags = DRM_CLOEXEC;
        ret = (*drmIoctl_)(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &prime);
        name = prime.fd;
    } else {
        struct drm_gem_flink flink;
        flink.handle = vcreate.handle;
        ret = (*drmIoctl_)(fd, DRM_IOCTL_GEM_FLINK, &flink);
        name = flink.name;
    }

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);

    if (dma_buf_mode) {
        EGLint attribs[] = {
            EGL_WIDTH, width,
            EGL_HEIGHT, height,
            EGL_LINUX_DRM_FOURCC_EXT,
            vcreate.drm_format > 0 ? vcreate.drm_format : DRM_FORMAT_ARGB8888,
            EGL_DMA_BUF_PLANE0_FD_EXT, name,
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
            EGL_DMA_BUF_PLANE0_PITCH_EXT, stride,
            EGL_NONE
        };
        namedimage = eglCreateImageKHR_func(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
                                       NULL, attribs);
    } else {
        /* Only EGL_DRM_BUFFER_FORMAT_ARGB32_MESA format is supported so if
           some app(like Heaven) use their own buffer format rather than
           the ARGB32 then the color display maybe go wrong.
        */
        EGLint attribs[] = {
            EGL_WIDTH, width,
            EGL_HEIGHT, height,
            EGL_DRM_BUFFER_STRIDE_MESA, stride / 4,
            EGL_DRM_BUFFER_FORMAT_MESA, EGL_DRM_BUFFER_FORMAT_ARGB32_MESA,
            EGL_NONE
        };
        namedimage = eglCreateImageKHR_func(dpy, ctx, EGL_DRM_BUFFER_MESA,
                                       (EGLClientBuffer)name, attribs);
    }
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, namedimage);
    eglDestroyImageKHR_func(dpy, namedimage);

    if (dma_buf_mode) {
        close(name);
    }
}

bool gl_vmdisp::intel_vgt_check_composite_display(void)
{
    struct drm_i915_gem_vgtbuffer vcreate;

    fd = open(INTEL_GPU_DEV_FILE_NAME, O_RDWR);
    memset(&vcreate, 0, sizeof(struct drm_i915_gem_vgtbuffer));
    vcreate.flags = I915_VGTBUFFER_CHECK_CAPABILITY;
    if (!(*drmIoctl_)(fd, DRM_IOCTL_I915_GEM_VGTBUFFER, &vcreate)) {
        return true;
    } else {
        return false;
    }
}

gl_vmdisp::gl_vmdisp(int renderer_pid):
vmid(renderer_pid)
{
  base::FilePath drm_path("/usr/lib/x86_64-linux-gnu/libdrm.so");
  base::NativeLibrary drm_library = gl::LoadLibraryAndPrintError(drm_path);
  DCHECK(drm_library);
  drmIoctl_ = reinterpret_cast<drm_ioctl_proc>(
      base::GetFunctionPointerFromNativeLibrary(drm_library, "drmIoctl"));
  DCHECK(drmIoctl_);
}

gl_vmdisp::~gl_vmdisp(void)
{
    free(primary_list.l);

    free(cursor_list.l);

    egl_manager_destroy(eman_);

    if (ctx && dpy) {
	  eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
	  eglDestroyContext(dpy, ctx);
    }

}

void gl_vmdisp::set_vmdisp_egl_context(scoped_refptr<gl::GLContext> context_)
{
  
    EGLint attribs[] = {
       EGL_SURFACE_TYPE, EGL_WINDOW_BIT, 
       EGL_RED_SIZE, 1,
       EGL_GREEN_SIZE, 1,
       EGL_BLUE_SIZE, 1,
       EGL_DEPTH_SIZE, 1,
       EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
       EGL_NONE
    };

    GLboolean printInfo = GL_FALSE;
    XDisplay* xdpy = gfx::GetXDisplay();
    eglBindAPI(EGL_OPENGL_API);
    
    DCHECK(xdpy);
    eman_ = egl_manager_new(xdpy, attribs, printInfo);
    ctx = eglCreateContext(dpy, eman_->conf, context_.get(), NULL);
    if (ctx == EGL_NO_CONTEXT) {
      eglTerminate(dpy);
      free(eman_);
          DCHECK(false);
    }
    DCHECK(eman_);
}

void gl_vmdisp::initialize_vmdisp(void)
{  
  check_egl();
  vmdisp_init();
  intel_vgt_check_composite_display();
}

void gl_vmdisp::vmdisp_init(void)
{
    glEGLImageTargetTexture2DOES_func = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)
    eglGetProcAddress("glEGLImageTargetTexture2DOES");
    eglCreateImageKHR_func = (PFNEGLCREATEIMAGEKHRPROC)
    eglGetProcAddress("eglCreateImageKHR");
    eglDestroyImageKHR_func = (PFNEGLDESTROYIMAGEKHRPROC)
    eglGetProcAddress("eglDestroyImageKHR");
    int i;

    primary_list.l = (struct buffer_rec *) malloc(PRIMARY_LIST_LEN*sizeof(struct buffer_rec));
    if (primary_list.l == NULL) {
        exit(1);
    }
    primary_list.len = PRIMARY_LIST_LEN;
	primary_list.firstcreated = false;
    cursor_list.l = (struct buffer_rec *) malloc(CURSOR_LIST_LEN*sizeof(struct buffer_rec));
    if (cursor_list.l == NULL) {
        exit(1);
    }
    cursor_list.len = CURSOR_LIST_LEN;
	cursor_list.firstcreated = false;
    for (i = 0; i < primary_list.len; i++) {
        primary_list.l[i].start = 0;
        primary_list.l[i].textureId = 0;
        primary_list.l[i].age = INT_MAX;
    }
    for (i = 0; i < cursor_list.len; i++) {
        cursor_list.l[i].start = 0;
        cursor_list.l[i].textureId = 0;
        cursor_list.l[i].age = INT_MAX;
    }

}

struct egl_manager *
gl_vmdisp::egl_manager_new(EGLNativeDisplayType xdpy, 
                const EGLint *attrib_list,
                EGLBoolean verbose)
{
   struct egl_manager *eman;

   eman = (struct egl_manager *) calloc(1, sizeof(*eman));
   if (!eman)
      return NULL;

   eman->verbose = verbose;
   eman->xdpy = xdpy;

   dpy = eglGetDisplay(eman->xdpy);

   if (dpy == EGL_NO_DISPLAY) {
      free(eman);
      return NULL;
   }

   if (!eglInitialize(dpy, &eman->major, &eman->minor)) {
      free(eman);
      return NULL;
   }

   return eman;
}

void gl_vmdisp::egl_manager_destroy(struct egl_manager *eman)
{

}

void gl_vmdisp::check_for_new_primary_buffer(void)
{
    struct drm_i915_gem_vgtbuffer vcreate;
	int r = 0;
    uint32_t start;

    memset(&vcreate, 0, sizeof(struct drm_i915_gem_vgtbuffer));
    vcreate.plane_id = I915_VGT_PLANE_PRIMARY;
    vcreate.vmid = vmid;
    vcreate.phys_pipe_id = vm_pipe;
    vcreate.flags = I915_VGTBUFFER_QUERY_ONLY;

    r = (*drmIoctl_)(fd, DRM_IOCTL_I915_GEM_VGTBUFFER, &vcreate);
    if (r != 0 || vcreate.start == 0) {
        current_primary_fb_addr = 0;
		primary_list.firstcreated = false;
        return;
    }

    start = vcreate.start;
    if ((start != current_primary_fb_addr)) {
        r = find_rec(&primary_list, &vcreate);
        age_list(&primary_list);

        if (r >= 0) {
            primary_list.l[r].age = 0;
            current_textureId = primary_list.l[r].textureId;
            current_primary_fb_addr = start;
        } else {
            create_primary_buffer();
        }
    }
}

void gl_vmdisp::vmdisp_event_loop(void)
{

    check_for_new_primary_buffer();

}

bool gl_vmdisp::check_egl(void)
{
    bool ret = true;

    dma_buf_mode = true;

    return ret;
}

