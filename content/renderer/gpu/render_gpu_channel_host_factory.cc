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
 * Based on browser_gpu_channel_host_factory.cc:
 * Copyright 2014 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "content/renderer/gpu/render_gpu_channel_host_factory.h"

#include "base/bind.h"
#include "base/command_line.h"
#include "base/location.h"
#include "base/profiler/scoped_tracker.h"
#include "base/single_thread_task_runner.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/thread_restrictions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/trace_event/memory_dump_manager.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "content/browser/gpu/browser_gpu_memory_buffer_manager.h"
#include "content/browser/gpu/gpu_data_manager_impl.h"
#include "content/browser/gpu/gpu_process_host.h"
#include "content/browser/gpu/shader_cache_factory.h"
#include "content/common/child_process_host_impl.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/browser/gpu_data_manager.h"
#include "content/public/common/content_client.h"
#include "gpu/command_buffer/service/gpu_switches.h"
#include "gpu/ipc/common/gpu_messages.h"
#include "ipc/ipc_channel_handle.h"
#include "ipc/message_filter.h"
#include "services/service_manager/runner/common/client_util.h"

#include "content/child/child_process.h"
#include "chrome/common/chrome_paths.h"
#include "chrome/common/chrome_switches.h"
#include "base/path_service.h"
#include "base/debug/stack_trace.h"
#include "base/prints.h"

namespace content {

RenderGpuChannelHostFactory* RenderGpuChannelHostFactory::instance_ = NULL;

class RenderGpuChannelHostFactory::EstablishRequest
    : public base::RefCountedThreadSafe<EstablishRequest> {
 public:
  static scoped_refptr<EstablishRequest> Create(int gpu_client_id,
                                                uint64_t gpu_client_tracing_id,
                                                int gpu_host_id);
  void Wait();
  void Cancel();

  int gpu_host_id() { return gpu_host_id_; }
  IPC::ChannelHandle& channel_handle() { return channel_handle_; }
  gpu::GPUInfo gpu_info() { return gpu_info_; }

 private:
  friend class base::RefCountedThreadSafe<EstablishRequest>;
  explicit EstablishRequest(int gpu_client_id,
                            uint64_t gpu_client_tracing_id,
                            int gpu_host_id);
  ~EstablishRequest() {}
  void EstablishOnIO();
  void OnEstablishedOnIO(const IPC::ChannelHandle& channel_handle,
                         const gpu::GPUInfo& gpu_info);
  void FinishOnIO();
  void FinishOnMain();

  base::WaitableEvent event_;
  const int gpu_client_id_;
  const uint64_t gpu_client_tracing_id_;
  int gpu_host_id_;
  bool reused_gpu_process_;
  IPC::ChannelHandle channel_handle_;
  gpu::GPUInfo gpu_info_;
  bool finished_;
  scoped_refptr<base::SingleThreadTaskRunner> main_task_runner_;
};

scoped_refptr<RenderGpuChannelHostFactory::EstablishRequest>
RenderGpuChannelHostFactory::EstablishRequest::Create(
    int gpu_client_id,
    uint64_t gpu_client_tracing_id,
    int gpu_host_id) {
  scoped_refptr<EstablishRequest> establish_request =
      new EstablishRequest(gpu_client_id, gpu_client_tracing_id, gpu_host_id);
  scoped_refptr<base::SingleThreadTaskRunner> task_runner =
      ChildProcess::current()->io_task_runner();
  task_runner->PostTask(
      FROM_HERE,
      base::Bind(&RenderGpuChannelHostFactory::EstablishRequest::EstablishOnIO,
                 establish_request));
  return establish_request;
}

RenderGpuChannelHostFactory::EstablishRequest::EstablishRequest(
    int gpu_client_id,
    uint64_t gpu_client_tracing_id,
    int gpu_host_id)
    : event_(base::WaitableEvent::ResetPolicy::AUTOMATIC,
             base::WaitableEvent::InitialState::NOT_SIGNALED),
      gpu_client_id_(gpu_client_id),
      gpu_client_tracing_id_(gpu_client_tracing_id),
      gpu_host_id_(gpu_host_id),
      reused_gpu_process_(false),
      finished_(false),
      main_task_runner_(base::ThreadTaskRunnerHandle::Get()) {}

void RenderGpuChannelHostFactory::EstablishRequest::EstablishOnIO() {
  tracked_objects::ScopedTracker tracking_profile(
      FROM_HERE_WITH_EXPLICIT_FUNCTION(
          "477117 "
          "RenderGpuChannelHostFactory::EstablishRequest::EstablishOnIO"));
  GpuProcessHost* host = GpuProcessHost::FromID(gpu_host_id_);
  if (!host) {
    host = GpuProcessHost::GetForRender(GpuProcessHost::GPU_PROCESS_KIND_SANDBOXED, true, true);
    if (!host) {
      LOG(ERROR) << "Failed to launch GPU process.";
      FinishOnIO();
      return;
    }
    gpu_host_id_ = host->host_id();
    reused_gpu_process_ = false;
  } else {
    if (reused_gpu_process_) {
      LOG(ERROR) << "Failed to create channel.";
      FinishOnIO();
      return;
    }
    reused_gpu_process_ = true;
  }

  bool preempts = true;
  bool allow_view_command_buffers = true;
  bool allow_real_time_streams = true;
  host->EstablishGpuChannel(
      gpu_client_id_, gpu_client_tracing_id_, preempts,
      allow_view_command_buffers, allow_real_time_streams,
      base::Bind(
          &RenderGpuChannelHostFactory::EstablishRequest::OnEstablishedOnIO,
          this));
}

void RenderGpuChannelHostFactory::EstablishRequest::OnEstablishedOnIO(
    const IPC::ChannelHandle& channel_handle,
    const gpu::GPUInfo& gpu_info) {
  if (!channel_handle.mojo_handle.is_valid() && reused_gpu_process_) {
    DVLOG(1) << "Failed to create channel on existing GPU process. Trying to "
                "restart GPU process.";
    EstablishOnIO();
  } else {
    channel_handle_ = channel_handle;
    gpu_info_ = gpu_info;
    FinishOnIO();
  }
}

void RenderGpuChannelHostFactory::EstablishRequest::FinishOnIO() {
  event_.Signal();
  main_task_runner_->PostTask(
      FROM_HERE,
      base::Bind(&RenderGpuChannelHostFactory::EstablishRequest::FinishOnMain,
                 this));
}

void RenderGpuChannelHostFactory::EstablishRequest::FinishOnMain() {
  if (!finished_) {
    RenderGpuChannelHostFactory* factory =
        RenderGpuChannelHostFactory::instance();
    factory->GpuChannelEstablished();
    finished_ = true;
  }
}

void RenderGpuChannelHostFactory::EstablishRequest::Wait() {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  {
    tracked_objects::ScopedTracker tracking_profile(
        FROM_HERE_WITH_EXPLICIT_FUNCTION(
            "125248 RenderGpuChannelHostFactory::EstablishRequest::Wait"));

    TRACE_EVENT0("browser",
                 "RenderGpuChannelHostFactory::EstablishGpuChannelSync");
    event_.Wait();
  }
  FinishOnMain();
}

void RenderGpuChannelHostFactory::EstablishRequest::Cancel() {
  DCHECK(main_task_runner_->BelongsToCurrentThread());
  finished_ = true;
}

void RenderGpuChannelHostFactory::CloseChannel() {
  DCHECK(instance_);
  if (instance_->gpu_channel_) {
    instance_->gpu_channel_->DestroyChannel();
    instance_->gpu_channel_ = nullptr;
  }
}

bool RenderGpuChannelHostFactory::CanUseForTesting() {
  return GpuDataManager::GetInstance()->GpuAccessAllowed(NULL);
}

void RenderGpuChannelHostFactory::Initialize(bool establish_gpu_channel) {
  if(!instance_){
    instance_ = new RenderGpuChannelHostFactory();
    if (establish_gpu_channel) {
      instance_->EstablishGpuChannel(gpu::GpuChannelEstablishedCallback());
    }
  }
}

void RenderGpuChannelHostFactory::Terminate() {
  DCHECK(instance_);
  delete instance_;
  instance_ = NULL;
}

RenderGpuChannelHostFactory::RenderGpuChannelHostFactory()
    : gpu_client_id_(ChildProcessHostImpl::GenerateChildProcessUniqueId()),
      gpu_client_tracing_id_(ChildProcessHost::kBrowserTracingProcessId),
      shutdown_event_(new base::WaitableEvent(
          base::WaitableEvent::ResetPolicy::MANUAL,
          base::WaitableEvent::InitialState::NOT_SIGNALED)),
      gpu_memory_buffer_manager_(
          new BrowserGpuMemoryBufferManager(gpu_client_id_,
                                            gpu_client_tracing_id_)),
      gpu_host_id_(0) {
  if (!base::CommandLine::ForCurrentProcess()->HasSwitch(
          switches::kDisableGpuShaderDiskCache)) {
    DCHECK(GetContentClient());
  base::FilePath cache_dir;
  PathService::Get(chrome::DIR_USER_DATA, &cache_dir);
  cache_dir = cache_dir.Append(FILE_PATH_LITERAL("ShaderCache"));
    if (!cache_dir.empty()) {
      GetIOThreadTaskRunner()->PostTask(
          FROM_HERE,
          base::Bind(
              &RenderGpuChannelHostFactory::InitializeShaderDiskCacheOnIO,
              gpu_client_id_, cache_dir));
    }
  }
}

RenderGpuChannelHostFactory::~RenderGpuChannelHostFactory() {
  DCHECK(IsMainThread());
  if (pending_request_.get())
    pending_request_->Cancel();
  for (size_t n = 0; n < established_callbacks_.size(); n++)
    established_callbacks_[n].Run(nullptr);
  shutdown_event_->Signal();
  if (gpu_channel_) {
    gpu_channel_->DestroyChannel();
    gpu_channel_ = NULL;
  }
}

bool RenderGpuChannelHostFactory::IsMainThread() {
  return BrowserThread::CurrentlyOn(BrowserThread::UI);
}

scoped_refptr<base::SingleThreadTaskRunner>
RenderGpuChannelHostFactory::GetIOThreadTaskRunner() {
  return BrowserThread::GetTaskRunnerForThread(BrowserThread::IO);
}

std::unique_ptr<base::SharedMemory>
RenderGpuChannelHostFactory::AllocateSharedMemory(size_t size) {
  std::unique_ptr<base::SharedMemory> shm(new base::SharedMemory());
  if (!shm->CreateAnonymous(size))
    return std::unique_ptr<base::SharedMemory>();
  return shm;
}

void RenderGpuChannelHostFactory::EstablishGpuChannel(
    const gpu::GpuChannelEstablishedCallback& callback) {
  if (gpu_channel_.get() && gpu_channel_->IsLost()) {
    DCHECK(!pending_request_.get());
    gpu_channel_->DestroyChannel();
    gpu_channel_ = NULL;
  }

  if (!gpu_channel_.get() && !pending_request_.get()) {
    pending_request_ = EstablishRequest::Create(
        gpu_client_id_, gpu_client_tracing_id_, gpu_host_id_);
  }

  if (!callback.is_null()) {
    if (gpu_channel_.get())
      callback.Run(gpu_channel_);
    else
      established_callbacks_.push_back(callback);
  }
}

scoped_refptr<gpu::GpuChannelHost>
RenderGpuChannelHostFactory::EstablishGpuChannelSync() {
#if defined(OS_ANDROID)
  NOTREACHED();
  return nullptr;
#endif
  EstablishGpuChannel(gpu::GpuChannelEstablishedCallback());
  
  if (pending_request_.get()){
    pending_request_->Wait();
  }

  return gpu_channel_;
}

gpu::GpuMemoryBufferManager*
RenderGpuChannelHostFactory::GetGpuMemoryBufferManager() {
  return gpu_memory_buffer_manager_.get();
}

gpu::GpuChannelHost* RenderGpuChannelHostFactory::GetGpuChannel() {
  if (gpu_channel_.get() && !gpu_channel_->IsLost())
    return gpu_channel_.get();

  return NULL;
}

void RenderGpuChannelHostFactory::GpuChannelEstablished() {
  DCHECK(IsMainThread());
  DCHECK(pending_request_.get());
  if (!pending_request_->channel_handle().mojo_handle.is_valid()) {
    DCHECK(!gpu_channel_.get());
  } else {
    tracked_objects::ScopedTracker tracking_profile1(
        FROM_HERE_WITH_EXPLICIT_FUNCTION(
            "466866 RenderGpuChannelHostFactory::GpuChannelEstablished1"));
    GetContentClient()->SetGpuInfo(pending_request_->gpu_info());
    gpu_channel_ = gpu::GpuChannelHost::CreateWebglChannel(
        this, gpu_client_id_, pending_request_->gpu_info(),
        pending_request_->channel_handle(), shutdown_event_.get(),
        gpu_memory_buffer_manager_.get());
  }
  gpu_host_id_ = pending_request_->gpu_host_id();
  pending_request_ = NULL;

  tracked_objects::ScopedTracker tracking_profile2(
      FROM_HERE_WITH_EXPLICIT_FUNCTION(
          "466866 RenderGpuChannelHostFactory::GpuChannelEstablished2"));

  std::vector<gpu::GpuChannelEstablishedCallback> established_callbacks;
  established_callbacks_.swap(established_callbacks);
  for (auto& callback : established_callbacks)
    callback.Run(gpu_channel_);
}

void RenderGpuChannelHostFactory::AddFilterOnIO(
    int host_id,
    scoped_refptr<IPC::MessageFilter> filter) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  GpuProcessHost* host = GpuProcessHost::FromID(host_id);
  if (host)
    host->AddFilter(filter.get());
}

void RenderGpuChannelHostFactory::InitializeShaderDiskCacheOnIO(
    int gpu_client_id,
    const base::FilePath& cache_dir) {
  GetShaderCacheFactorySingleton()->SetCacheInfo(gpu_client_id, cache_dir);
}

}  
