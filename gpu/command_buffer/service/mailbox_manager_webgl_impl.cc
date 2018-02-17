/* Copyright (C) 2016-2018 University of California, Irvine
 *
 * Authors:
 * Zhihao Yao <z.yao@uci.edu>
 * Zongheng Ma <zonghenm@uci.edu>
 * Yingtong Liu <yingtong@uci.edu>
 * Ardalan Amiri Sani <arrdalan@gmail.com>
 * 
 * All rights reserved. Use of this source code is governed
 * by a BSD-style license that can be found in the LICENSE file.
 * 
 * Based on mailbox_manager_impl.cc:
 * Copyright 2014 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpu/command_buffer/service/mailbox_manager_webgl_impl.h"

#include <stddef.h>

#include <algorithm>

#include "gpu/command_buffer/service/texture_manager.h"
#if !defined(OS_MACOSX)
#include "ui/gl/gl_surface_egl.h"
#endif
#include "base/debug/stack_trace.h"
namespace gpu {
namespace gles2 {

MailboxManagerWebglImpl* MailboxManagerWebglImpl::instance_ = NULL;

MailboxManagerWebglImpl::MailboxManagerWebglImpl() {
  fprintf(stderr, "yingtongtest, success[1]:%s\n", __PRETTY_FUNCTION__);
  DCHECK(!instance_);
  instance_ = this;
}

scoped_refptr<MailboxManager> Create() {
  if (MailboxManagerWebglImpl::instance() != nullptr)
    return MailboxManagerWebglImpl::instance();
  return scoped_refptr<MailboxManager>(new MailboxManagerWebglImpl); 
}

MailboxManagerWebglImpl::~MailboxManagerWebglImpl() {
  DCHECK(mailbox_to_eglimages_.empty());
  DCHECK(eglimages_to_mailboxes_.empty());
  DCHECK(mailbox_to_textures_.empty());
  DCHECK(textures_to_mailboxes_.empty());
  DCHECK(instance_);
  delete instance_;
  instance_ = NULL;
}

bool MailboxManagerWebglImpl::UsesSync() {
  return false;
}

TextureBase* MailboxManagerWebglImpl::ConsumeTexture(const Mailbox& mailbox) {
  MailboxToTextureMap::iterator it =
      mailbox_to_textures_.find(mailbox);
  if (it != mailbox_to_textures_.end())
    return it->second->first;

  return NULL;
}

void MailboxManagerWebglImpl::ProduceTexture(const Mailbox& mailbox,
                                        TextureBase* texture) {
  fprintf(stderr, "success[0]:%s\n", __PRETTY_FUNCTION__);
  MailboxToTextureMap::iterator it = mailbox_to_textures_.find(mailbox);
  MailboxToEGLImageKHRMap::iterator it1 = mailbox_to_eglimages_.find(mailbox);
  
  fprintf(stderr, "success[1]:%s\n", __PRETTY_FUNCTION__);
  if (it != mailbox_to_textures_.end()) {
    fprintf(stderr, "success[2]:%s\n", __PRETTY_FUNCTION__);
    if (it->second->first == texture)
      return;
    fprintf(stderr, "success[21]:%s\n", __PRETTY_FUNCTION__);
    EGLImageKHRToMailboxMap::iterator image_it = it1->second;
    mailbox_to_eglimages_.erase(it1);
    eglimages_to_mailboxes_.erase(image_it);
    TextureToMailboxMap::iterator texture_it = it->second;
    mailbox_to_textures_.erase(it);
    textures_to_mailboxes_.erase(texture_it);
  }
  fprintf(stderr, "success[3]:%s\n", __PRETTY_FUNCTION__);
  TextureToEGLImageKHRMap::iterator it2 = texture_to_eglimage_.find(texture);
  if(it2 != texture_to_eglimage_.end()) {
    fprintf(stderr, "success[411]:%s\n", __PRETTY_FUNCTION__);
    InsertTexture(mailbox, texture);
    InsertEGLImage(mailbox, it2->second);
    fprintf(stderr, "success[412]:%s\n", __PRETTY_FUNCTION__);
  } else {
    fprintf(stderr, "success[421]:%s\n", __PRETTY_FUNCTION__);
    GLuint texture_id = texture->service_id();
    fprintf(stderr, "success[422]:%s\n", __PRETTY_FUNCTION__);
    EGLDisplay egl_display = gl::GLSurfaceEGL::GetHardwareDisplay();
    fprintf(stderr, "success[423]:%s\n", __PRETTY_FUNCTION__);
    EGLContext egl_context = eglGetCurrentContext();
    fprintf(stderr, "success[424]:%s\n", __PRETTY_FUNCTION__);

    DCHECK_NE(EGL_NO_CONTEXT, egl_context);
    DCHECK_NE(EGL_NO_DISPLAY, egl_display);
    DCHECK(glIsTexture(texture_id));
    fprintf(stderr, "success[425]:%s\n", __PRETTY_FUNCTION__);

    const EGLint egl_attrib_list[] = {
        EGL_GL_TEXTURE_LEVEL_KHR, 0, EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
    EGLClientBuffer egl_buffer = reinterpret_cast<EGLClientBuffer>(texture_id);
    fprintf(stderr, "success[421]:%s\n", __PRETTY_FUNCTION__);
    EGLenum egl_target = EGL_GL_TEXTURE_2D_KHR;
    fprintf(stderr, "success[421]:%s\n", __PRETTY_FUNCTION__);
    EGLImageKHR egl_image = eglCreateImageKHR(
        egl_display, egl_context, egl_target, egl_buffer, egl_attrib_list);
    fprintf(stderr, "success[421]:%s\n", __PRETTY_FUNCTION__);
    if (egl_image == EGL_NO_IMAGE_KHR) {
      LOG(ERROR) << "eglCreateImageKHR for cross-thread sharing failed: 0x"
                 << std::hex << eglGetError(); 
    }
    fprintf(stderr, "success[421]:%s\n", __PRETTY_FUNCTION__);
    static int i = 0;
    i++;
    fprintf(stderr,"insert texture and eglimage i:%d, creat eglimage:%p\n",i, egl_image);
    InsertEGLImage(mailbox, egl_image);
    fprintf(stderr, "success[421]:%s\n", __PRETTY_FUNCTION__);
    InsertTextureEGLImage(texture, egl_image);
    fprintf(stderr, "success[421]:%s\n", __PRETTY_FUNCTION__);
    fprintf(stderr, "success[422]:%s\n", __PRETTY_FUNCTION__);
	if (texture){
	  fprintf(stderr, "success[4221]:%s\n", __PRETTY_FUNCTION__);
      InsertTexture(mailbox, texture);
	} else {
	  fprintf(stderr, "success[4222]:%s\n", __PRETTY_FUNCTION__);
    }
  }
}

EGLImageKHR MailboxManagerWebglImpl::GetEGLImageKHR(const Mailbox& mailbox) {
  fprintf(stderr, "success[1]:%s\n", __PRETTY_FUNCTION__);
  MailboxToEGLImageKHRMap::iterator it =
      mailbox_to_eglimages_.find(mailbox);
  if (it != mailbox_to_eglimages_.end())
    return it->second->first;
  fprintf(stderr, "success[2]:%s\n", __PRETTY_FUNCTION__);
  return NULL;
}

void MailboxManagerWebglImpl::InsertEGLImage(const Mailbox& mailbox,
                                       EGLImageKHR eglimage) {
  fprintf(stderr, "success[1]:%s\n", __PRETTY_FUNCTION__);
  EGLImageKHRToMailboxMap::iterator image_it =
      eglimages_to_mailboxes_.insert(std::make_pair(eglimage, mailbox));
  mailbox_to_eglimages_.insert(std::make_pair(mailbox, image_it));
  fprintf(stderr, "yingtong1:%lu,%lu\n",mailbox_to_eglimages_.size(),eglimages_to_mailboxes_.size());
  DCHECK_EQ(mailbox_to_eglimages_.size(), eglimages_to_mailboxes_.size());
}

void MailboxManagerWebglImpl::InsertTextureEGLImage(TextureBase* texture, EGLImageKHR eglimage)
{
 fprintf(stderr, "success[0]:%s\n", __PRETTY_FUNCTION__);
 texture_to_eglimage_.insert(std::make_pair(texture, eglimage));
  fprintf(stderr, "insert texture eglimage:%lu\n",texture_to_eglimage_.size());
}

void MailboxManagerWebglImpl::InsertTexture(const Mailbox& mailbox,
                                       TextureBase* texture) {
  fprintf(stderr, "success[0]:%s\n", __PRETTY_FUNCTION__);
  texture->SetMailboxManager(this);
  TextureToMailboxMap::iterator texture_it =
      textures_to_mailboxes_.insert(std::make_pair(texture, mailbox));
  mailbox_to_textures_.insert(std::make_pair(mailbox, texture_it));
  DCHECK_EQ(mailbox_to_textures_.size(), textures_to_mailboxes_.size());
}

void MailboxManagerWebglImpl::TextureDeleted(TextureBase* texture) {
  std::pair<TextureToMailboxMap::iterator,
            TextureToMailboxMap::iterator> range =
      textures_to_mailboxes_.equal_range(texture);
  TextureToEGLImageKHRMap::iterator it1 = texture_to_eglimage_.find(texture);

  std::pair<EGLImageKHRToMailboxMap::iterator,
            EGLImageKHRToMailboxMap::iterator> range1 =
      eglimages_to_mailboxes_.equal_range(it1->second);
  for (TextureToMailboxMap::iterator it = range.first;
       it != range.second; ++it) {
    size_t count = mailbox_to_textures_.erase(it->second);
    DCHECK(count == 1);
  }
  
  for (EGLImageKHRToMailboxMap::iterator it2 = range1.first; 
	it2 != range1.second; ++it2) {
    size_t count1 = mailbox_to_eglimages_.erase(it2->second);
    DCHECK(count1 == 1);
  }
  size_t count0 = texture_to_eglimage_.erase(texture);
  DCHECK(count0 == 1);

  eglimages_to_mailboxes_.erase(range1.first, range1.second);
  DCHECK_EQ(mailbox_to_eglimages_.size(), eglimages_to_mailboxes_.size());
  textures_to_mailboxes_.erase(range.first, range.second);
  DCHECK_EQ(mailbox_to_textures_.size(), textures_to_mailboxes_.size());
}

}  
}  
