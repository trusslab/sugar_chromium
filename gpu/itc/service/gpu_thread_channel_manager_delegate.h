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
 * Based on gpu_channel_manager_delegate.h:
 * Copyright 2016 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GPU_ITC_SERVICE_GPU_THREAD_CHANNEL_MANAGER_DELEGATE_H_
#define GPU_ITC_SERVICE_GPU_THREAD_CHANNEL_MANAGER_DELEGATE_H_

#include "gpu/command_buffer/common/constants.h"
#include "gpu/ipc/common/surface_handle.h"

class GURL;

namespace content {
  class RenderGpuThreadHost;
}

namespace gpu {

class GpuThreadChannelManagerDelegate {
 public:
  GpuThreadChannelManagerDelegate(content::RenderGpuThreadHost* host);
  void DidCreateOffscreenContext(const GURL& active_url);

  void DidDestroyChannel(int client_id);

  void DidDestroyOffscreenContext(const GURL& active_url);

  void DidLoseContext(bool offscreen,
                      error::ContextLostReason reason,
                      const GURL& active_url);

  void StoreShaderToDisk(int32_t client_id,
                         const std::string& key,
                         const std::string& shader);

#if defined(OS_WIN)
  void SendAcceleratedSurfaceCreatedChildWindow(
      SurfaceHandle parent_window,
      SurfaceHandle child_window);
#endif

  void SetActiveURL(const GURL& url);

 protected:
  ~GpuThreadChannelManagerDelegate();

 private:
  content::RenderGpuThreadHost* host_;
};

}  

#endif  
