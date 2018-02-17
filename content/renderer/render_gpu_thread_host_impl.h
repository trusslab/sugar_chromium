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
 * Based on browser_child_process_host_impl.h:
 * Copyright 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CONTENT_RENDERER_RENDER_GPU_THREAD_HOST_IMPL_H_
#define CONTENT_RENDERER_RENDER_GPU_THREAD_HOST_IMPL_H_

#include <stdint.h>

#include <list>
#include <memory>

#include "base/compiler_specific.h"
#include "base/memory/ref_counted.h"
#include "base/memory/shared_memory.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process.h"
#include "base/single_thread_task_runner.h"
#include "base/synchronization/waitable_event_watcher.h"
#include "build/build_config.h"
#include "content/browser/child_process_launcher.h"
#include "content/public/browser/browser_child_process_host.h"
#include "content/public/browser/child_process_data.h"
#include "content/public/common/child_process_host_delegate.h"
#include "mojo/edk/embedder/pending_process_connection.h"

#if defined(OS_WIN)
#include "base/win/object_watcher.h"
#endif

namespace base {
class CommandLine;
}

namespace service_manager {
class InterfaceProvider;
}

namespace content {

class BrowserMessageFilter;
class ChildConnection;

class CONTENT_EXPORT RenderGpuThreadHostImpl
    : public BrowserChildProcessHost,
      public NON_EXPORTED_BASE(ChildProcessHostDelegate),
#if defined(OS_WIN)
      public base::win::ObjectWatcher::Delegate,
#endif
      public ChildProcessLauncher::Client {
 public:
  RenderGpuThreadHostImpl(content::ProcessType process_type,
                              BrowserChildProcessHostDelegate* delegate,
                              const std::string& service_name);
  ~RenderGpuThreadHostImpl() override;

  static void TerminateAll();

  bool Send(IPC::Message* message) override;
  void Launch(std::unique_ptr<SandboxedProcessLauncherDelegate> delegate,
              std::unique_ptr<base::CommandLine> cmd_line,
              bool terminate_on_shutdown) override;
  const ChildProcessData& GetData() const override;
  ChildProcessHost* GetHost() const override;
  base::TerminationStatus GetTerminationStatus(bool known_dead,
                                               int* exit_code) override;
  std::unique_ptr<base::SharedPersistentMemoryAllocator> TakeMetricsAllocator()
      override;
  void SetName(const base::string16& name) override;
  void SetHandle(base::ProcessHandle handle) override;
  std::string GetServiceRequestChannelToken() override;

  bool CanShutdown() override;
  void OnChannelInitialized(IPC::Channel* channel) override;
  void OnChildDisconnected() override;
  const base::Process& GetProcess() const override;
  service_manager::InterfaceProvider* GetRemoteInterfaces() override;
  bool OnMessageReceived(const IPC::Message& message) override;
  void OnChannelConnected(int32_t peer_pid) override;
  void OnChannelError() override;
  void OnBadMessageReceived(const IPC::Message& message) override;

  void TerminateOnBadMessageReceived(const std::string& error);

  void AddFilter(BrowserMessageFilter* filter);

  ChildConnection* child_connection() const {
    return child_connection_.get();
  }
  IPC::Channel* child_channel() const { return channel_; }
    void OnProcessLaunched() override;

  ChildProcessData data_;
  BrowserChildProcessHostDelegate* delegate_;
  std::unique_ptr<ChildProcessHost> child_process_host_;

  std::unique_ptr<mojo::edk::PendingProcessConnection> pending_connection_;
  std::unique_ptr<ChildConnection> child_connection_;

  std::unique_ptr<ChildProcessLauncher> child_process_;

#if defined(OS_WIN)
  base::win::ObjectWatcher early_exit_watcher_;
#endif

  std::unique_ptr<base::SharedPersistentMemoryAllocator> metrics_allocator_;

  IPC::Channel* channel_ = nullptr;
  bool is_channel_connected_;
  bool notify_child_disconnected_;

  base::WeakPtrFactory<RenderGpuThreadHostImpl> weak_factory_;
};

}  

#endif  
