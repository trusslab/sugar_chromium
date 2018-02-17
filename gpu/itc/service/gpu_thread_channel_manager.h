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
 * Based on gpu_channel_manager.h:
 * Copyright 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GPU_ITC_SERVICE_GPU_THREAD_CHANNEL_MANAGER_H_
#define GPU_ITC_SERVICE_GPU_THREAD_CHANNEL_MANAGER_H_

#include <stdint.h>

#include <deque>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "build/build_config.h"
#include "gpu/command_buffer/common/constants.h"
#include "gpu/command_buffer/service/gpu_preferences.h"
#include "gpu/config/gpu_driver_bug_workarounds.h"
#include "gpu/config/gpu_feature_info.h"
#include "gpu/gpu_export.h"
#include "gpu/ipc/service/gpu_memory_manager.h"
#include "ui/gfx/gpu_memory_buffer.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gl/gl_surface.h"
#include "url/gurl.h"

#include "gpu/config/gpu_info.h"

namespace base {
class WaitableEvent;
}

namespace gl {
class GLShareGroup;
}

namespace gpu {
struct GpuPreferences;
class PreemptionFlag;
class SyncPointClient;
class SyncPointManager;
struct SyncToken;
namespace gles2 {
class FramebufferCompletenessCache;
class MailboxManager;
class ProgramCache;
class ShaderTranslatorCache;
}
}

namespace IPC {
struct ChannelHandle;
}

namespace gpu {
class GpuThreadChannel;
class GpuThreadHost;
class GpuThreadChannelManagerDelegate;
class GpuMemoryBufferFactory;
class GpuWatchdogThread;

class GPU_EXPORT GpuThreadChannelManager {
 public:
  GpuThreadChannelManager(const GpuPreferences& gpu_preferences,
                    GpuThreadChannelManagerDelegate* delegate,
                    GpuWatchdogThread* watchdog,
                    SyncPointManager* sync_point_manager,
                    GpuMemoryBufferFactory* gpu_memory_buffer_factory,
                    const GpuFeatureInfo& gpu_feature_info,
					const GPUInfo& gpu_info,
                    scoped_refptr<base::SingleThreadTaskRunner> render_task_runner);
  virtual ~GpuThreadChannelManager();

  GpuThreadChannelManagerDelegate* delegate() const { return delegate_; }

  scoped_refptr<GpuThreadHost> EstablishThreadChannel(
                                    uint64_t client_tracing_id,
                                    bool preempts,
                                    bool allow_view_command_buffers,
                                    bool allow_real_time_streams);

  void PopulateShaderCache(const std::string& shader);
  void DestroyGpuMemoryBuffer(gfx::GpuMemoryBufferId id,
                              int client_id,
                              const SyncToken& sync_token);
#if defined(OS_ANDROID)
  void WakeUpGpu();
#endif
  void DestroyAllChannels();

  void RemoveChannel(int client_id);

  void LoseAllContexts();
  void MaybeExitOnContextLost();

  const GpuPreferences& gpu_preferences() const {
    return gpu_preferences_;
  }
  const GpuDriverBugWorkarounds& gpu_driver_bug_workarounds() const {
    return gpu_driver_bug_workarounds_;
  }
  const GpuFeatureInfo& gpu_feature_info() const { return gpu_feature_info_; }
  gles2::ProgramCache* program_cache();
  gles2::ShaderTranslatorCache* shader_translator_cache();
  gles2::FramebufferCompletenessCache* framebuffer_completeness_cache();

  GpuMemoryManager* gpu_memory_manager() { return &gpu_memory_manager_; }

  gl::GLSurface* GetDefaultOffscreenSurface();

  GpuMemoryBufferFactory* gpu_memory_buffer_factory() {
    return gpu_memory_buffer_factory_;
  }

  uint32_t GetUnprocessedOrderNum() const;

  uint32_t GetProcessedOrderNum() const;

#if defined(OS_ANDROID)
  void DidAccessGpu();
#endif

  bool is_exiting_for_lost_context() {
    return exiting_for_lost_context_;
  }

  gles2::MailboxManager* mailbox_manager() const {
	return mailbox_manager_.get();
  }

  gl::GLShareGroup* share_group() const { return share_group_.get(); }

  void PrintPtr();

 protected:
  scoped_refptr<GpuThreadChannel> CreateGpuThreadChannel(
      int client_id,
      uint64_t client_tracing_id,
      bool preempts,
      bool allow_view_command_buffers,
      bool allow_real_time_streams);

  SyncPointManager* sync_point_manager() const {
    return sync_point_manager_;
  }

  PreemptionFlag* preemption_flag() const { return preemption_flag_.get(); }

  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
  scoped_refptr<base::SingleThreadTaskRunner> io_task_runner_;
  scoped_refptr<base::SingleThreadTaskRunner> render_task_runner_;

  std::unordered_map<int32_t, scoped_refptr<GpuThreadChannel>> gpu_channels_;

 private:
  void InternalDestroyGpuMemoryBuffer(gfx::GpuMemoryBufferId id, int client_id);
  void InternalDestroyGpuMemoryBufferOnIO(gfx::GpuMemoryBufferId id,
                                          int client_id);
#if defined(OS_ANDROID)
  void ScheduleWakeUpGpu();
#endif

  const GpuPreferences gpu_preferences_;
  GpuDriverBugWorkarounds gpu_driver_bug_workarounds_;

  GpuThreadChannelManagerDelegate* const delegate_;

  GpuWatchdogThread* watchdog_;

  scoped_refptr<gl::GLShareGroup> share_group_;
  scoped_refptr<gles2::MailboxManager> mailbox_manager_;
  scoped_refptr<PreemptionFlag> preemption_flag_;
  GpuMemoryManager gpu_memory_manager_;
  SyncPointManager* sync_point_manager_;
  std::unique_ptr<SyncPointClient> sync_point_client_waiter_;
  std::unique_ptr<gles2::ProgramCache> program_cache_;
  scoped_refptr<gles2::ShaderTranslatorCache> shader_translator_cache_;
  scoped_refptr<gles2::FramebufferCompletenessCache>
      framebuffer_completeness_cache_;
  scoped_refptr<gl::GLSurface> default_offscreen_surface_;
  GpuMemoryBufferFactory* const gpu_memory_buffer_factory_;
  GpuFeatureInfo gpu_feature_info_;
  GPUInfo gpu_info_;
#if defined(OS_ANDROID)
  base::TimeTicks last_gpu_access_time_;
  base::TimeTicks begin_wake_up_time_;
#endif

  bool exiting_for_lost_context_;

  int next_client_id_ = 0;

  base::WeakPtrFactory<GpuThreadChannelManager> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(GpuThreadChannelManager);
};

}  

#endif  
