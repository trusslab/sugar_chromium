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
 * Based on browser_child_process_host_impl.cc:
 * Copyright 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "content/renderer/render_gpu_thread_host_impl.h"

#include "base/base_switches.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/debug/alias.h"
#include "base/debug/dump_without_crashing.h"
#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/metrics/field_trial.h"
#include "base/metrics/histogram_macros.h"
#include "base/metrics/persistent_histogram_allocator.h"
#include "base/metrics/persistent_memory_allocator.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/synchronization/waitable_event.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "components/tracing/common/tracing_switches.h"
#include "content/browser/histogram_message_filter.h"
#include "content/browser/loader/resource_message_filter.h"
#include "content/browser/memory/memory_message_filter.h"
#include "content/browser/profiler_message_filter.h"
#include "content/browser/service_manager/service_manager_context.h"
#include "content/browser/tracing/trace_message_filter.h"
#include "content/common/child_process_host_impl.h"
#include "content/common/child_process_messages.h"
#include "content/common/service_manager/child_connection.h"
#include "content/public/browser/browser_child_process_host_delegate.h"
#include "content/public/browser/browser_child_process_observer.h"
#include "content/public/browser/browser_thread.h"
#include "content/public/browser/child_process_data.h"
#include "content/public/browser/content_browser_client.h"
#include "content/public/common/connection_filter.h"
#include "content/public/common/content_features.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/mojo_channel_switches.h"
#include "content/public/common/process_type.h"
#include "content/public/common/result_codes.h"
#include "content/public/common/sandboxed_process_launcher_delegate.h"
#include "content/public/common/service_manager_connection.h"
#include "mojo/edk/embedder/embedder.h"
#include "services/service_manager/public/cpp/interface_registry.h"

#if defined(OS_MACOSX)
#include "content/browser/mach_broker_mac.h"
#endif

#include "base/prints.h"

namespace content {

RenderGpuThreadHostImpl::RenderGpuThreadHostImpl(
    content::ProcessType process_type,
    BrowserChildProcessHostDelegate* delegate,
    const std::string& service_name)
    : data_(process_type),
      delegate_(delegate),
      pending_connection_(new mojo::edk::PendingProcessConnection),
      channel_(nullptr),
      is_channel_connected_(false),
      notify_child_disconnected_(false),
      weak_factory_(this) {
  data_.id = ChildProcessHostImpl::GenerateChildProcessUniqueId();

  child_process_host_.reset(ChildProcessHost::Create(this));
  AddFilter(new TraceMessageFilter(data_.id));
  AddFilter(new ProfilerMessageFilter(process_type));
  AddFilter(new HistogramMessageFilter);
  AddFilter(new MemoryMessageFilter(this, process_type));

  if (!service_name.empty()) {
    child_connection_.reset(
        new ChildConnection(service_name, base::StringPrintf("%d", data_.id),
                            pending_connection_.get(),
                            ServiceManagerContext::GetConnectorForWebglIOThread(),
							base::ThreadTaskRunnerHandle::Get()));
  }

}

RenderGpuThreadHostImpl::~RenderGpuThreadHostImpl() {

}

void RenderGpuThreadHostImpl::Launch(
    std::unique_ptr<SandboxedProcessLauncherDelegate> delegate,
    std::unique_ptr<base::CommandLine> cmd_line,
    bool terminate_on_shutdown) {
}

const ChildProcessData& RenderGpuThreadHostImpl::GetData() const {
  return data_;
}

ChildProcessHost* RenderGpuThreadHostImpl::GetHost() const {
  return child_process_host_.get();
}

const base::Process& RenderGpuThreadHostImpl::GetProcess() const {
  DCHECK(child_process_.get())
      << "Requesting a child process handle before launching.";
  DCHECK(child_process_->GetProcess().IsValid())
      << "Requesting a child process handle before launch has completed OK.";
  return child_process_->GetProcess();
}

std::unique_ptr<base::SharedPersistentMemoryAllocator>
RenderGpuThreadHostImpl::TakeMetricsAllocator() {
  return std::move(metrics_allocator_);
}

void RenderGpuThreadHostImpl::SetName(const base::string16& name) {
  data_.name = name;
}

void RenderGpuThreadHostImpl::SetHandle(base::ProcessHandle handle) {
  data_.handle = handle;
}

std::string RenderGpuThreadHostImpl::GetServiceRequestChannelToken() {
  return child_connection_->service_token();
}

void RenderGpuThreadHostImpl::AddFilter(BrowserMessageFilter* filter) {
  child_process_host_->AddFilter(filter->GetFilter());
}
service_manager::InterfaceProvider*
RenderGpuThreadHostImpl::GetRemoteInterfaces() {
  if (!child_connection_)
    return nullptr;

  return child_connection_->GetRemoteInterfaces();
}

base::TerminationStatus RenderGpuThreadHostImpl::GetTerminationStatus(
    bool known_dead, int* exit_code) {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);
  if (!child_process_)  
    return base::GetTerminationStatus(data_.handle, exit_code);
  return child_process_->GetChildTerminationStatus(known_dead,
                                                   exit_code);
}

bool RenderGpuThreadHostImpl::OnMessageReceived(
    const IPC::Message& message) {
  return delegate_->OnMessageReceived(message);
}

void RenderGpuThreadHostImpl::OnChannelConnected(int32_t peer_pid) {

  is_channel_connected_ = true;
  delegate_->OnChannelConnected(peer_pid);

}

void RenderGpuThreadHostImpl::OnChannelError() {
  delegate_->OnChannelError();
}

void RenderGpuThreadHostImpl::OnBadMessageReceived(
    const IPC::Message& message) {
}

bool RenderGpuThreadHostImpl::CanShutdown() {
  return delegate_->CanShutdown();
}

void RenderGpuThreadHostImpl::OnChannelInitialized(IPC::Channel* channel) {
  channel_ = channel;
}

void RenderGpuThreadHostImpl::OnChildDisconnected() {
}

bool RenderGpuThreadHostImpl::Send(IPC::Message* message) {
  return child_process_host_->Send(message);
}

void RenderGpuThreadHostImpl::OnProcessLaunched() {
  DCHECK_CURRENTLY_ON(BrowserThread::IO);

  const base::Process& process = child_process_->GetProcess();
  DCHECK(process.IsValid());

}

}  
