// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "ipc/ipc_channel_factory.h"
#include "ipc/ipc_channel_mojo.h"

namespace IPC {

namespace {

class PlatformChannelFactory : public ChannelFactory {
 public:
  bool webgl__ = false;
  
  PlatformChannelFactory(
      ChannelHandle handle,
      Channel::Mode mode,
      const scoped_refptr<base::SingleThreadTaskRunner>& ipc_task_runner)
      : handle_(handle), mode_(mode), ipc_task_runner_(ipc_task_runner) {}

  PlatformChannelFactory(
      bool webgl,
	  ChannelHandle handle,
      Channel::Mode mode,
      const scoped_refptr<base::SingleThreadTaskRunner>& ipc_task_runner)
      : webgl__(webgl), handle_(handle), mode_(mode), ipc_task_runner_(ipc_task_runner) {}

  std::unique_ptr<Channel> BuildChannel(Listener* listener) override {
	fprintf(stderr, "success[1]: %s, webgl__=%d\n", __PRETTY_FUNCTION__, webgl__);
#if defined(OS_NACL_SFI)
    return Channel::Create(handle_, mode_, listener);
#else
    DCHECK(handle_.is_mojo_channel_handle());
    if (webgl__) {
	  return ChannelMojo::CreateForWebgl(
          mojo::ScopedMessagePipeHandle(handle_.mojo_handle), mode_, listener,
          ipc_task_runner_);
	
	} else {
	  return ChannelMojo::Create(
          mojo::ScopedMessagePipeHandle(handle_.mojo_handle), mode_, listener,
          ipc_task_runner_);
	}
#endif
	fprintf(stderr, "success[2]: %s\n", __PRETTY_FUNCTION__);
  }

  scoped_refptr<base::SingleThreadTaskRunner> GetIPCTaskRunner() override {
    return ipc_task_runner_;
  }

 private:
  ChannelHandle handle_;
  Channel::Mode mode_;
  scoped_refptr<base::SingleThreadTaskRunner> ipc_task_runner_;

  DISALLOW_COPY_AND_ASSIGN(PlatformChannelFactory);
};

} // namespace

// static
std::unique_ptr<ChannelFactory> ChannelFactory::Create(
    const ChannelHandle& handle,
    Channel::Mode mode,
    const scoped_refptr<base::SingleThreadTaskRunner>& ipc_task_runner) {
  return base::MakeUnique<PlatformChannelFactory>(handle, mode,
                                                  ipc_task_runner);
}

std::unique_ptr<ChannelFactory> ChannelFactory::Create(
    bool webgl,
	const ChannelHandle& handle,
    Channel::Mode mode,
    const scoped_refptr<base::SingleThreadTaskRunner>& ipc_task_runner) {
  fprintf(stderr, "success[1]: %s, webgl=%d\n", __PRETTY_FUNCTION__, webgl);
  return base::MakeUnique<PlatformChannelFactory>(webgl, handle, mode,
                                                  ipc_task_runner);
}

}  // namespace IPC
