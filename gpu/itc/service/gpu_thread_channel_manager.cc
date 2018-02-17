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
 * Based on gpu_channel_manager.cc:
 * Copyright 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpu/itc/service/gpu_thread_channel_manager.h"

#include <algorithm>
#include <utility>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/location.h"
#include "base/memory/ptr_util.h"
#include "base/message_loop/message_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "gpu/command_buffer/common/sync_token.h"
#include "gpu/command_buffer/service/feature_info.h"
#include "gpu/command_buffer/service/mailbox_manager.h"
#include "gpu/command_buffer/service/memory_program_cache.h"
#include "gpu/command_buffer/service/shader_translator_cache.h"
#include "gpu/command_buffer/service/sync_point_manager.h"
#include "gpu/ipc/common/gpu_messages.h"
#include "gpu/itc/service/gpu_thread_channel.h"
#include "gpu/itc/service/gpu_thread_channel_manager_delegate.h"
#include "gpu/ipc/service/gpu_memory_buffer_factory.h"
#include "gpu/ipc/service/gpu_memory_manager.h"
#include "ui/gl/gl_bindings.h"
#include "ui/gl/gl_share_group.h"
#include "ui/gl/init/gl_factory.h"

#include "gpu/itc/client/gpu_thread_host.h"
#include "gpu/ipc/service/gpu_init.h"
#include "base/prints.h"

namespace gpu {

namespace {
#if defined(OS_ANDROID)
const int kMaxGpuIdleTimeMs = 40;
const int kMaxKeepAliveTimeMs = 200;
#endif

}

GpuThreadChannelManager::GpuThreadChannelManager(
    const GpuPreferences& gpu_preferences,
    GpuThreadChannelManagerDelegate* delegate,
    GpuWatchdogThread* watchdog,
    SyncPointManager* sync_point_manager,
    GpuMemoryBufferFactory* gpu_memory_buffer_factory,
    const GpuFeatureInfo& gpu_feature_info,
	const GPUInfo& gpu_info,
    scoped_refptr<base::SingleThreadTaskRunner> render_task_runner)
    : 
      render_task_runner_(render_task_runner),
      gpu_preferences_(gpu_preferences),
      gpu_driver_bug_workarounds_(base::CommandLine::ForCurrentProcess()),
      delegate_(delegate),
      watchdog_(watchdog),
      share_group_(new gl::GLShareGroup()),
      mailbox_manager_(gles2::MailboxManager::Create(gpu_preferences, false)),
      gpu_memory_manager_(this),
      sync_point_manager_(sync_point_manager),
      sync_point_client_waiter_(
          sync_point_manager->CreateSyncPointClientWaiter()),
      gpu_memory_buffer_factory_(gpu_memory_buffer_factory),
      gpu_feature_info_(gpu_feature_info),
      gpu_info_(gpu_info),
      exiting_for_lost_context_(false),
      weak_factory_(this) {
  if (gpu_preferences_.ui_prioritize_in_gpu_process)
    preemption_flag_ = new PreemptionFlag;
}

GpuThreadChannelManager::~GpuThreadChannelManager() {
  gpu_channels_.clear();
  if (default_offscreen_surface_.get()) {
    default_offscreen_surface_->Destroy();
    default_offscreen_surface_ = NULL;
  }
}

gles2::ProgramCache* GpuThreadChannelManager::program_cache() {
  if (!program_cache_.get() &&
      !gpu_preferences_.disable_gpu_program_cache) {
    const GpuDriverBugWorkarounds& workarounds = gpu_driver_bug_workarounds_;
    bool disable_disk_cache =
        gpu_preferences_.disable_gpu_shader_disk_cache ||
        workarounds.disable_program_disk_cache;
    program_cache_.reset(new gles2::MemoryProgramCache(
        gpu_preferences_.gpu_program_cache_size,
        disable_disk_cache,
        workarounds.disable_program_caching_for_transform_feedback));
  }
  return program_cache_.get();
}

gles2::ShaderTranslatorCache*
GpuThreadChannelManager::shader_translator_cache() {
  if (!shader_translator_cache_.get()) {
    shader_translator_cache_ =
        new gles2::ShaderTranslatorCache(gpu_preferences_);
  }
  return shader_translator_cache_.get();
}

gles2::FramebufferCompletenessCache*
GpuThreadChannelManager::framebuffer_completeness_cache() {
  if (!framebuffer_completeness_cache_.get())
    framebuffer_completeness_cache_ =
        new gles2::FramebufferCompletenessCache;
  return framebuffer_completeness_cache_.get();
}

void GpuThreadChannelManager::RemoveChannel(int client_id) {
  gpu_channels_.erase(client_id);
}

scoped_refptr<GpuThreadChannel> GpuThreadChannelManager::CreateGpuThreadChannel(
    int client_id,
    uint64_t client_tracing_id,
    bool preempts,
    bool allow_view_command_buffers,
    bool allow_real_time_streams) {
  PrintPtr();
  return new GpuThreadChannel(
      this, new SyncPointManager(false), watchdog_, share_group(), mailbox_manager(),
      preempts ? preemption_flag() : nullptr,
      preempts ? nullptr : preemption_flag(), /*task_runner_.get(),
      io_task_runner_.get(), */client_id, client_tracing_id,
      allow_view_command_buffers, allow_real_time_streams, render_task_runner_);
}

scoped_refptr<GpuThreadHost> GpuThreadChannelManager::EstablishThreadChannel(
    uint64_t client_tracing_id,
    bool preempts,
    bool allow_view_command_buffers,
    bool allow_real_time_streams) {
  scoped_refptr<GpuThreadChannel> channel = 
      CreateGpuThreadChannel(next_client_id_, client_tracing_id, preempts,
                       allow_view_command_buffers, allow_real_time_streams);
  gpu_channels_[next_client_id_] = channel;
  scoped_refptr<GpuThreadHost> host = GpuThreadHost::Create(next_client_id_ ++, gpu_info_, channel);
  channel->SetGpuThreadHost(host);
  return host;
}

void GpuThreadChannelManager::PopulateShaderCache(const std::string& program_proto) {
  if (program_cache())
    program_cache()->LoadProgram(program_proto);
}

uint32_t GpuThreadChannelManager::GetUnprocessedOrderNum() const {
  uint32_t unprocessed_order_num = 0;
  for (auto& kv : gpu_channels_) {
    unprocessed_order_num =
        std::max(unprocessed_order_num, kv.second->GetUnprocessedOrderNum());
  }
  return unprocessed_order_num;
}

uint32_t GpuThreadChannelManager::GetProcessedOrderNum() const {
  uint32_t processed_order_num = 0;
  for (auto& kv : gpu_channels_) {
    processed_order_num =
        std::max(processed_order_num, kv.second->GetProcessedOrderNum());
  }
  return processed_order_num;
}

void GpuThreadChannelManager::LoseAllContexts() {
  for (auto& kv : gpu_channels_) {
    kv.second->MarkAllContextsLost();
  }
  task_runner_->PostTask(FROM_HERE,
                         base::Bind(&GpuThreadChannelManager::DestroyAllChannels,
                                    weak_factory_.GetWeakPtr()));
}

void GpuThreadChannelManager::MaybeExitOnContextLost() {
  if (!gpu_preferences().single_process && !gpu_preferences().in_process_gpu) {
    LOG(ERROR) << "Exiting GPU process because some drivers cannot recover"
               << " from problems.";
    base::MessageLoop::current()->QuitNow();
    exiting_for_lost_context_ = true;
  }
}

void GpuThreadChannelManager::DestroyAllChannels() {
  gpu_channels_.clear();
}

gl::GLSurface* GpuThreadChannelManager::GetDefaultOffscreenSurface() {
  if (!default_offscreen_surface_.get()) {
    default_offscreen_surface_ =
        gl::init::CreateOffscreenGLSurface(gfx::Size());
  }
  return default_offscreen_surface_.get();
}

void GpuThreadChannelManager::PrintPtr() {
}

#if defined(OS_ANDROID)
void GpuThreadChannelManager::DidAccessGpu() {
  last_gpu_access_time_ = base::TimeTicks::Now();
}

void GpuThreadChannelManager::WakeUpGpu() {
  begin_wake_up_time_ = base::TimeTicks::Now();
  ScheduleWakeUpGpu();
}

void GpuThreadChannelManager::ScheduleWakeUpGpu() {
  base::TimeTicks now = base::TimeTicks::Now();
  TRACE_EVENT2("gpu", "GpuThreadChannelManager::ScheduleWakeUp",
               "idle_time", (now - last_gpu_access_time_).InMilliseconds(),
               "keep_awake_time", (now - begin_wake_up_time_).InMilliseconds());
  if (now - last_gpu_access_time_ <
      base::TimeDelta::FromMilliseconds(kMaxGpuIdleTimeMs))
    return;
  if (now - begin_wake_up_time_ >
      base::TimeDelta::FromMilliseconds(kMaxKeepAliveTimeMs))
    return;

  DoWakeUpGpu();

  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, base::Bind(&GpuThreadChannelManager::ScheduleWakeUpGpu,
                            weak_factory_.GetWeakPtr()),
      base::TimeDelta::FromMilliseconds(kMaxGpuIdleTimeMs));
}

#endif

}  
