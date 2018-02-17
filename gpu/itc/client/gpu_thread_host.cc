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
 * Based on gpu_channel_host.cc:
 * Copyright 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpu/itc/client/gpu_thread_host.h"

#include <algorithm>
#include <utility>

#include "base/atomic_sequence_num.h"
#include "base/bind.h"
#include "base/single_thread_task_runner.h"
#include "base/threading/thread_restrictions.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "gpu/ipc/common/gpu_messages.h"
#include "gpu/ipc/common/gpu_param_traits_macros.h"
#include "ipc/ipc_sync_message_filter.h"
#include "url/gurl.h"

#include "base/debug/stack_trace.h"

#include "gpu/itc/service/gpu_thread_channel_manager.h"
#include "base/synchronization/waitable_event.h"
#include "gpu/ipc/client/command_buffer_proxy_impl.h"
#include "gpu/itc/service/gpu_thread_channel.h"
#include <sys/syscall.h>
#include "base/prints.h"

using base::AutoLock;

namespace gpu {
namespace {

base::StaticAtomicSequenceNumber g_next_transfer_buffer_id;

}  

GpuThreadHost::StreamFlushInfo::StreamFlushInfo()
    : next_stream_flush_id(1),
      flushed_stream_flush_id(0),
      verified_stream_flush_id(0),
      flush_pending(false),
      route_id(MSG_ROUTING_NONE),
      put_offset(0),
      flush_count(0),
      flush_id(0) {}

GpuThreadHost::StreamFlushInfo::StreamFlushInfo(const StreamFlushInfo& other) =
    default;

GpuThreadHost::StreamFlushInfo::~StreamFlushInfo() {}

scoped_refptr<GpuThreadHost> GpuThreadHost::Create(
    int channel_id,
    const gpu::GPUInfo& gpu_info,
    const scoped_refptr<gpu::GpuThreadChannel> channel)
  {
  scoped_refptr<GpuThreadHost> host = new GpuThreadHost(
      channel_id,
	  gpu_info, 
	  channel);
  return host;
}

GpuThreadHost::GpuThreadHost(
    int channel_id,
    const gpu::GPUInfo& gpu_info,
	scoped_refptr<GpuThreadChannel> channel)
    : 
      channel_id_(channel_id),
      gpu_info_(gpu_info),
	  channel_(std::move(channel)) 
	  {
  next_image_id_.GetNext();
  next_route_id_.GetNext();
  next_stream_id_.GetNext();
}

bool GpuThreadHost::Send(IPC::Message* msg) {
  std::unique_ptr<IPC::Message> message(msg);
  message->set_unblock(false);

  bool result = sync_filter_->Send(message.release());
  return result;
}

void GpuThreadHost::SetGpuThreadTaskRunner(scoped_refptr<base::SingleThreadTaskRunner> gpu_thread) {
  gpu_thread_ = gpu_thread;
}

uint32_t GpuThreadHost::OrderingBarrier(
    int32_t route_id,
    int32_t stream_id,
    int32_t put_offset,
    uint32_t flush_count,
    const std::vector<ui::LatencyInfo>& latency_info,
    bool put_offset_changed,
    bool do_flush,
    uint32_t* highest_verified_flush_id) {
  AutoLock lock(context_lock_);
  StreamFlushInfo& flush_info = stream_flush_info_[stream_id];
  if (flush_info.flush_pending && flush_info.route_id != route_id)
    InternalFlush(&flush_info);

  *highest_verified_flush_id = flush_info.verified_stream_flush_id;

  if (put_offset_changed) {
    const uint32_t flush_id = flush_info.next_stream_flush_id++;
    flush_info.flush_pending = true;
    flush_info.route_id = route_id;
    flush_info.put_offset = put_offset;
    flush_info.flush_count = flush_count;
    flush_info.flush_id = flush_id;
    flush_info.latency_info.insert(flush_info.latency_info.end(),
                                   latency_info.begin(), latency_info.end());

    if (do_flush)
      InternalFlush(&flush_info);
    return flush_id;
  }
  return 0;
}

void GpuThreadHost::FlushPendingStream(int32_t stream_id) {
}

void GpuThreadHost::InternalFlush(StreamFlushInfo* flush_info) {
  context_lock_.AssertAcquired();
  DCHECK(flush_info);
  DCHECK(flush_info->flush_pending);
  DCHECK_LT(flush_info->flushed_stream_flush_id, flush_info->flush_id);
  base::WaitableEvent complete(base::WaitableEvent::ResetPolicy::MANUAL, base::WaitableEvent::InitialState::NOT_SIGNALED);
  DCHECK(channel());
  DCHECK(gpu_thread_);
  bool res = gpu_thread_->PostTask(FROM_HERE, base::Bind(&GpuThreadChannel::OnAsyncFlush, channel(), &complete, flush_info->route_id, flush_info->put_offset, flush_info->flush_count, flush_info->latency_info));
  DCHECK(res);
  complete.Wait();
  flush_info->latency_info.clear();
  flush_info->flush_pending = false;

  flush_info->flushed_stream_flush_id = flush_info->flush_id;
}

base::SharedMemoryHandle GpuThreadHost::ShareToGpuProcess(
    const base::SharedMemoryHandle& source_handle) {

  return base::SharedMemory::DuplicateHandle(source_handle);
}

int32_t GpuThreadHost::ReserveTransferBufferId() {
  return g_next_transfer_buffer_id.GetNext() + 1;
}

int32_t GpuThreadHost::ReserveImageId() {
  return next_image_id_.GetNext();
}

int32_t GpuThreadHost::GenerateRouteID() {
  return next_route_id_.GetNext();
}

int32_t GpuThreadHost::GenerateStreamID() {
  const int32_t stream_id = next_stream_id_.GetNext();
  DCHECK_NE(gpu::GPU_STREAM_INVALID, stream_id);
  DCHECK_NE(gpu::GPU_STREAM_DEFAULT, stream_id);
  return stream_id;
}

uint32_t GpuThreadHost::ValidateFlushIDReachedServer(int32_t stream_id,
                                                     bool force_validate) {
  base::hash_map<int32_t, uint32_t> validate_flushes;
  uint32_t flushed_stream_flush_id = 0;
  uint32_t verified_stream_flush_id = 0;
  {
    AutoLock lock(context_lock_);
    for (const auto& iter : stream_flush_info_) {
      const int32_t iter_stream_id = iter.first;
      const StreamFlushInfo& flush_info = iter.second;
      if (iter_stream_id == stream_id) {
        flushed_stream_flush_id = flush_info.flushed_stream_flush_id;
        verified_stream_flush_id = flush_info.verified_stream_flush_id;
      }

      if (flush_info.flushed_stream_flush_id >
          flush_info.verified_stream_flush_id) {
        validate_flushes.insert(
            std::make_pair(iter_stream_id, flush_info.flushed_stream_flush_id));
      }
    }
  }

  if (!force_validate && flushed_stream_flush_id == verified_stream_flush_id) {
    return verified_stream_flush_id;
  }

    uint32_t highest_flush_id = 0;
    AutoLock lock(context_lock_);
    for (const auto& iter : validate_flushes) {
      const int32_t validated_stream_id = iter.first;
      const uint32_t validated_flush_id = iter.second;
      StreamFlushInfo& flush_info = stream_flush_info_[validated_stream_id];
      if (flush_info.verified_stream_flush_id < validated_flush_id) {
        flush_info.verified_stream_flush_id = validated_flush_id;
      }

      if (validated_stream_id == stream_id)
        highest_flush_id = flush_info.verified_stream_flush_id;
    }

    return highest_flush_id;

  return 0;
}

uint32_t GpuThreadHost::GetHighestValidatedFlushID(int32_t stream_id) {
  AutoLock lock(context_lock_);
  StreamFlushInfo& flush_info = stream_flush_info_[stream_id];
  return flush_info.verified_stream_flush_id;
}

void GpuThreadHost::SetCommandBufferProxy(CommandBufferProxyImpl* command_buffer) {
  command_buffer_ = command_buffer;
}

CommandBufferProxyImpl* GpuThreadHost::GetCommandBufferProxy() {
  return command_buffer_;
}

GpuThreadHost::~GpuThreadHost() {
#if DCHECK_IS_ON()
  AutoLock lock(context_lock_);
#endif
}

void GpuThreadHost::DestroyCommandBuffer(int32_t route_id) {
  gpu_thread_->PostTask(FROM_HERE, base::Bind(&GpuThreadChannel::OnDestroyCommandBuffer, channel(), route_id));
}

void GpuThreadHost::OnDestroyed(gpu::error::ContextLostReason reason, gpu::error::Error error) {
  command_buffer_->OnDestroyed(reason, error);
}

void GpuThreadHost::OnConsoleMessage(const GPUCommandBufferConsoleMessage& message) {
  command_buffer_->OnConsoleMessage(message);
}

void GpuThreadHost::OnSignalAck(uint32_t id) {
  command_buffer_->OnSignalAck(id);
}

void GpuThreadHost::OnSwapBuffersCompleted(const GpuCommandBufferMsg_SwapBuffersCompleted_Params& params) {
  command_buffer_->OnSwapBuffersCompleted(params);
}

void GpuThreadHost::OnUpdateVSyncParameters(base::TimeTicks timebase, 
                                            base::TimeDelta interval) {
  command_buffer_->OnUpdateVSyncParameters(timebase, interval);
}

}  
