// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include "mailbox_holder.h"
namespace gpu {

MailboxHolder::MailboxHolder() : /*texture(NULL), egl_image(NULL), */texture_target(0){}

MailboxHolder::MailboxHolder(const gpu::Mailbox& mailbox,
                             const gpu::SyncToken& sync_token,
                             uint32_t texture_target)
     :/*texture(NULL), 
      egl_image(NULL),*/
      mailbox(mailbox),
      sync_token(sync_token),
      texture_target(texture_target) {}
}  // namespace gpu
