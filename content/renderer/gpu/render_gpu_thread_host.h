/* Copyright (C) 2016-2018 University of California, Irvine
 *
 * Authors:
 * Zhihao Yao <z.yao@uci.edu>
 * Ardalan Amiri Sani <arrdalan@gmail.com>
 * 
 * All rights reserved. Use of this source code is governed
 * by a BSD-style license that can be found in the LICENSE file.
 */

#ifndef CONTENT_RENDERER_GPU_GPU_THREAD_HOST_H_
#define CONTENT_RENDERER_GPU_GPU_THREAD_HOST_H_

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/synchronization/waitable_event.h"

#include "gpu/config/gpu_feature_info.h"
#include "gpu/command_buffer/service/gpu_preferences.h"

#include "gpu/itc/service/gpu_thread_channel_manager.h"
#include "gpu/itc/service/gpu_thread_channel_manager_delegate.h"

namespace content {

class RenderGpuThreadHost : public base::RefCountedThreadSafe<RenderGpuThreadHost> {
  public:
   RenderGpuThreadHost();
   void Initialize(const base::CommandLine& command_line, scoped_refptr<base::SingleThreadTaskRunner> render_thread_task_runner, base::WaitableEvent* complete);
   
   gpu::GpuThreadChannelManager* gpu_thread_channel_manager();
   const gpu::GpuFeatureInfo& gpu_feature_info() { return gpu_feature_info_; }
   const gpu::GpuPreferences& gpu_prefenrences() { return gpu_preferences_; }

  private:
   friend class base::RefCountedThreadSafe<RenderGpuThreadHost>;
   
   ~RenderGpuThreadHost();

   std::unique_ptr<gpu::GpuThreadChannelManager> gpu_thread_channel_manager_;
   gpu::GpuFeatureInfo gpu_feature_info_;
   gpu::GpuPreferences gpu_preferences_;

};

} 

#endif 
