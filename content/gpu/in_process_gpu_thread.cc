// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/gpu/in_process_gpu_thread.h"

#include "base/time/time.h"
#include "build/build_config.h"
#include "content/gpu/gpu_child_thread.h"
#include "content/gpu/gpu_process.h"
#include "gpu/config/gpu_info_collector.h"
#include "gpu/config/gpu_util.h"
#include "gpu/ipc/common/gpu_memory_buffer_support.h"
#include "gpu/ipc/service/gpu_memory_buffer_factory.h"
#include "ui/gl/init/gl_factory.h"

#if defined(OS_ANDROID)
#include "base/android/jni_android.h"
#endif

#include "base/debug/stack_trace.h"
#include "gpu/command_buffer/service/mailbox_manager.h"
#include "base/prints.h"
namespace content {

InProcessGpuThread::InProcessGpuThread(
    const InProcessChildThreadParams& params,
    const gpu::GpuPreferences& gpu_preferences)
    : base::Thread("Chrome_InProcGpuThread"),
      params_(params),
      gpu_process_(NULL),
      gpu_preferences_(gpu_preferences),
      mailbox_manager_(NULL),
      gpu_memory_buffer_factory_(
          gpu::GetNativeGpuMemoryBufferType() != gfx::EMPTY_BUFFER
          ? gpu::GpuMemoryBufferFactory::CreateNativeType()
          : nullptr) {
    fprintf(stderr, "in_process_gpu_thread: %s\n", __PRETTY_FUNCTION__);
  if (params_.webgl()){
    mailbox_manager_ = gpu::gles2::MailboxManager::Create(gpu_preferences_, true);
    fprintf(stderr, "creating webgl in_process_gpu_thread: %s\n", __PRETTY_FUNCTION__);
  }
}

InProcessGpuThread::~InProcessGpuThread() {
  Stop();
}

void InProcessGpuThread::Init() {
  base::ThreadPriority io_thread_priority = base::ThreadPriority::NORMAL;

#if defined(OS_ANDROID)
  // Call AttachCurrentThreadWithName, before any other AttachCurrentThread()
  // calls. The latter causes Java VM to assign Thread-??? to the thread name.
  // Please note calls to AttachCurrentThreadWithName after AttachCurrentThread
  // will not change the thread name kept in Java VM.
  base::android::AttachCurrentThreadWithName(thread_name());
  // Up the priority of the |io_thread_| on Android.
  io_thread_priority = base::ThreadPriority::DISPLAY;
#endif

  fprintf(stderr, "success[1]:%s\n", __PRETTY_FUNCTION__);
  gpu_process_ = new GpuProcess(io_thread_priority);
 fprintf(stderr, "success[2]:%s\n", __PRETTY_FUNCTION__);

  gpu::GPUInfo gpu_info;
  if (!gl::init::InitializeGLOneOff())
    VLOG(1) << "gl::init::InitializeGLOneOff failed";
  else
    gpu::CollectContextGraphicsInfo(&gpu_info);
  fprintf(stderr, "success[2.5]:%s\n", __PRETTY_FUNCTION__);
  
  gpu::GpuFeatureInfo gpu_feature_info =
      gpu::GetGpuFeatureInfo(gpu_info, *base::CommandLine::ForCurrentProcess());

  // The process object takes ownership of the thread object, so do not
  // save and delete the pointer.
 fprintf(stderr, "success[3]:%s\n", __PRETTY_FUNCTION__);

  GpuChildThread* child_thread;
  if(params_.webgl()){
    fprintf(stderr, "success[3.1]:, webgl true in: %s\n", __PRETTY_FUNCTION__);
    child_thread = new GpuChildThread(
	    params_, gpu_info, gpu_feature_info, gpu_memory_buffer_factory_.get(), mailbox_manager_.get());  
  } else {
    fprintf(stderr, "success[3.1]:, webgl false in: %s\n", __PRETTY_FUNCTION__);
    child_thread = new GpuChildThread(
        params_, gpu_info, gpu_feature_info, gpu_memory_buffer_factory_.get());  	
  }

  // Since we are in the browser process, use the thread start time as the
 fprintf(stderr, "success[5]:%s\n", __PRETTY_FUNCTION__);
  child_thread->Init(base::Time::Now());
  fprintf(stderr, "success[6]:%s\n", __PRETTY_FUNCTION__);

  gpu_process_->set_main_thread(child_thread);
fprintf(stderr, "success[7]:%s\n", __PRETTY_FUNCTION__);
}

void InProcessGpuThread::CleanUp() {
  SetThreadWasQuitProperly(true);
  delete gpu_process_;
}

base::Thread* CreateInProcessGpuThread(
    const InProcessChildThreadParams& params,
    const gpu::GpuPreferences& gpu_preferences) {
  return new InProcessGpuThread(params, gpu_preferences);
}

}  // namespace content
