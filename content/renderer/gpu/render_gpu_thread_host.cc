/* Copyright (C) 2016-2018 University of California, Irvine
 *
 * Authors:
 * Zhihao Yao <z.yao@uci.edu>
 * Ardalan Amiri Sani <arrdalan@gmail.com>
 * 
 * All rights reserved. Use of this source code is governed
 * by a BSD-style license that can be found in the LICENSE file.
 */

#include "render_gpu_thread_host.h"
#include "gpu/ipc/service/gpu_init.h"
#include "content/public/browser/gpu_utils.h"
#include "gpu/ipc/common/gpu_memory_buffer_support.h"
#include "gpu/ipc/service/gpu_memory_buffer_factory.h"
#include "gpu/command_buffer/service/sync_point_manager.h"
#include "content/public/gpu/content_gpu_client.h"
#include "gpu/itc/service/gpu_thread_channel_manager_delegate.h"

namespace content {

RenderGpuThreadHost::RenderGpuThreadHost() {

}

void RenderGpuThreadHost::Initialize(const base::CommandLine& command_line, scoped_refptr<base::SingleThreadTaskRunner> render_thread_task_runner, base::WaitableEvent* complete) {
  gpu::GpuInit gpu_init(true);
  bool init_success = gpu_init.InitializeAndStartSandbox(command_line);
  std::unique_ptr<gpu::GpuMemoryBufferFactory> gpu_memory_buffer_factory;
  if (init_success &&
      gpu::GetNativeGpuMemoryBufferType() != gfx::EMPTY_BUFFER)
    gpu_memory_buffer_factory = gpu::GpuMemoryBufferFactory::CreateNativeType();
  GetContentClient()->SetGpuInfo(gpu_init.gpu_info());
  
  gpu::SyncPointManager* sync_point_manager = new gpu::SyncPointManager(false);

  gpu::GpuPreferences preferences = GetGpuPreferencesFromCommandLine();
  gpu_thread_channel_manager_.reset(new gpu::GpuThreadChannelManager(
                                    preferences,
				    new gpu::GpuThreadChannelManagerDelegate(this), 
				    gpu_init.TakeWatchdogThread().get(), 
				    sync_point_manager/*.get()*/, 
				    gpu_memory_buffer_factory.get(), 
				    gpu_init.gpu_feature_info(), 
				    gpu_init.gpu_info(), 
				    render_thread_task_runner));
  gpu_thread_channel_manager_->PrintPtr(); 
  complete->Signal();
}

gpu::GpuThreadChannelManager* RenderGpuThreadHost::gpu_thread_channel_manager() {
  return gpu_thread_channel_manager_.get();
}

RenderGpuThreadHost::~RenderGpuThreadHost() {}

} 
