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
 * Based on mailbox_manager_impl.h:
 * Copyright 2014 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GPU_COMMAND_BUFFER_SERVICE_MAILBOX_MANAGER_WEBGL_IMPL_H_
#define GPU_COMMAND_BUFFER_SERVICE_MAILBOX_MANAGER_WEBGL_IMPL_H_

#include <map>
#include <utility>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "gpu/command_buffer/common/constants.h"
#include "gpu/command_buffer/common/mailbox.h"
#include "gpu/command_buffer/service/mailbox_manager.h"
#include "gpu/gpu_export.h"

#if !defined(OS_MACOSX)
#include "third_party/khronos/EGL/eglplatform.h"
#include "third_party/khronos/EGL/egl.h"
#include "third_party/khronos/EGL/eglext.h"
#endif

namespace gpu {
namespace gles2 {

class GPU_EXPORT MailboxManagerWebglImpl : public MailboxManager {
 public:
  MailboxManagerWebglImpl();

  static MailboxManagerWebglImpl* instance() {
    fprintf(stderr, "yingtongtest, success[1]:%s\n", __PRETTY_FUNCTION__);
    if(instance_)
	  return instance_;
    return new MailboxManagerWebglImpl();
  }

  TextureBase* ConsumeTexture(const Mailbox& mailbox) override;
  void ProduceTexture(const Mailbox& mailbox, TextureBase* texture) override;
  EGLImageKHR GetEGLImageKHR(const Mailbox& mailbox);
  bool UsesSync() override;
  void PushTextureUpdates(const SyncToken& token) override {}
  void PullTextureUpdates(const SyncToken& token) override {}
  void TextureDeleted(TextureBase* texture) override;

 protected:
  ~MailboxManagerWebglImpl() override;

 private:
  friend class base::RefCounted<MailboxManager>;
  
  static MailboxManagerWebglImpl* instance_;

  void InsertTexture(const Mailbox& mailbox, TextureBase* texture);
  void InsertEGLImage(const Mailbox& mailbox, EGLImageKHR eglimage);
  void InsertTextureEGLImage(TextureBase* texture, EGLImageKHR eglimage);

  typedef std::multimap<TextureBase*, Mailbox> TextureToMailboxMap;
  typedef std::map<Mailbox, TextureToMailboxMap::iterator>
      MailboxToTextureMap;

  MailboxToTextureMap mailbox_to_textures_;
  typedef std::multimap<EGLImageKHR, Mailbox> EGLImageKHRToMailboxMap;
  typedef std::map<Mailbox, EGLImageKHRToMailboxMap::iterator>
      MailboxToEGLImageKHRMap;

  MailboxToEGLImageKHRMap mailbox_to_eglimages_;
  EGLImageKHRToMailboxMap eglimages_to_mailboxes_;
  typedef std::map<TextureBase*, EGLImageKHR> TextureToEGLImageKHRMap;
  TextureToEGLImageKHRMap texture_to_eglimage_;

  TextureToMailboxMap textures_to_mailboxes_;

  DISALLOW_COPY_AND_ASSIGN(MailboxManagerWebglImpl);
};

}  
}  

#endif  

