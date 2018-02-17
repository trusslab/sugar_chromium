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
 * Based on browser_gpu_channel_host_factory.h:
 * Copyright 2014 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CONTENT_RENDERER_GPU_RENDER_GPU_CHANNEL_HOST_FACTORY_H_
#define CONTENT_RENDERER_GPU_RENDER_GPU_CHANNEL_HOST_FACTORY_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <vector>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "build/build_config.h"
#include "content/common/content_export.h"
#include "gpu/ipc/client/gpu_channel_host.h"
#include "ipc/message_filter.h"

#include "content/browser/gpu/browser_gpu_memory_buffer_manager.h"

namespace content {

class CONTENT_EXPORT RenderGpuChannelHostFactory
    : public gpu::GpuChannelHostFactory,
      public gpu::GpuChannelEstablishFactory {
 public:
  static void Initialize(bool establish_gpu_channel);
  static void Terminate();
  static RenderGpuChannelHostFactory* instance() { return instance_; }

  bool IsMainThread() override;
  scoped_refptr<base::SingleThreadTaskRunner> GetIOThreadTaskRunner() override;
  std::unique_ptr<base::SharedMemory> AllocateSharedMemory(
      size_t size) override;

  int GpuProcessHostId() { return gpu_host_id_; }
  gpu::GpuChannelHost* GetGpuChannel();
  int GetGpuChannelId() { return gpu_client_id_; }

  void CloseChannel();

  static bool CanUseForTesting();

  void EstablishGpuChannel(
      const gpu::GpuChannelEstablishedCallback& callback) override;
  scoped_refptr<gpu::GpuChannelHost> EstablishGpuChannelSync() override;
  gpu::GpuMemoryBufferManager* GetGpuMemoryBufferManager() override;

 private:
  struct CreateRequest;
  class EstablishRequest;

  RenderGpuChannelHostFactory();
  ~RenderGpuChannelHostFactory() override;
  
  void GpuChannelEstablished();

  static void AddFilterOnIO(int gpu_host_id,
                            scoped_refptr<IPC::MessageFilter> filter);
  static void InitializeShaderDiskCacheOnIO(int gpu_client_id,
                                            const base::FilePath& cache_dir);

  const int gpu_client_id_;
  const uint64_t gpu_client_tracing_id_;
  std::unique_ptr<base::WaitableEvent> shutdown_event_;
  scoped_refptr<gpu::GpuChannelHost> gpu_channel_;
  std::unique_ptr<BrowserGpuMemoryBufferManager> gpu_memory_buffer_manager_;
  int gpu_host_id_;
  scoped_refptr<EstablishRequest> pending_request_;
  std::vector<gpu::GpuChannelEstablishedCallback> established_callbacks_;

  static RenderGpuChannelHostFactory* instance_;

  DISALLOW_COPY_AND_ASSIGN(RenderGpuChannelHostFactory);
};

}  

#endif  
