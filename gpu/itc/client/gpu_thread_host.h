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
 * Based on gpu_channel_host.h:
 * Copyright 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GPU_ITC_CLIENT_GPU_THREAD_HOST_H_
#define GPU_ITC_CLIENT_GPU_THREAD_HOST_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <vector>

#include "base/atomic_sequence_num.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process.h"
#include "base/synchronization/lock.h"
#include "gpu/config/gpu_info.h"
#include "gpu/gpu_export.h"
#include "gpu/ipc/common/gpu_stream_constants.h"
#include "gpu/ipc/service/gpu_memory_manager.h"
#include "ipc/ipc_channel_handle.h"
#include "ipc/ipc_sync_channel.h"
#include "ipc/message_filter.h"
#include "ipc/message_router.h"
#include "ui/events/latency_info.h"
#include "ui/gfx/gpu_memory_buffer.h"

#include "gpu/command_buffer/common/constants.h"

namespace base {
class WaitableEvent;
}
namespace IPC {
class SyncMessageFilter;
}
struct GPUCommandBufferConsoleMessage;
struct GPUCreateCommandBufferConfig;
struct GpuCommandBufferMsg_SwapBuffersCompleted_Params;

namespace gpu {
class GpuMemoryBufferManager;
class CommandBufferProxyImpl;
class GpuThreadChannel;
}
namespace gpu {

class GpuThreadHost;
using GpuThreadEstablishedCallback =
    base::Callback<void(scoped_refptr<GpuThreadHost>)>;

class GPU_EXPORT GpuThreadHost
    : public base::RefCountedThreadSafe<GpuThreadHost> {
 public:
  static scoped_refptr<GpuThreadHost> Create(
      int channel_id,
      const gpu::GPUInfo& gpu_info,
      const scoped_refptr<gpu::GpuThreadChannel> channel);

  int channel_id() const { return channel_id_; }

  gpu::GpuThreadChannel* channel() const { return channel_.get(); };

  const gpu::GPUInfo& gpu_info() const { return gpu_info_; }

  bool Send(IPC::Message* msg);

  uint32_t OrderingBarrier(int32_t route_id,
                           int32_t stream_id,
                           int32_t put_offset,
                           uint32_t flush_count,
                           const std::vector<ui::LatencyInfo>& latency_info,
                           bool put_offset_changed,
                           bool do_flush,
                           uint32_t* highest_verified_flush_id);

  void SetGpuThreadTaskRunner(scoped_refptr<base::SingleThreadTaskRunner> gpu_thread);

  void FlushPendingStream(int32_t stream_id);

  void DestroyChannel();

  base::SharedMemoryHandle ShareToGpuProcess(
      const base::SharedMemoryHandle& source_handle);

  int32_t ReserveTransferBufferId();

  int32_t ReserveImageId();

  int32_t GenerateRouteID();

  int32_t GenerateStreamID();

  uint32_t ValidateFlushIDReachedServer(int32_t stream_id, bool force_validate);

  uint32_t GetHighestValidatedFlushID(int32_t stream_id);

  void SetCommandBufferProxy(CommandBufferProxyImpl* command_buffer);

  CommandBufferProxyImpl* GetCommandBufferProxy();

 private:
  friend class base::RefCountedThreadSafe<GpuThreadHost>;
  friend class gpu::CommandBufferProxyImpl;
  friend class GpuCommandBufferStub;

  struct StreamFlushInfo {
    StreamFlushInfo();
    StreamFlushInfo(const StreamFlushInfo& other);
    ~StreamFlushInfo();

    uint32_t next_stream_flush_id;
    uint32_t flushed_stream_flush_id;
    uint32_t verified_stream_flush_id;

    bool flush_pending;
    int32_t route_id;
    int32_t put_offset;
    uint32_t flush_count;
    uint32_t flush_id;
    std::vector<ui::LatencyInfo> latency_info;
  };

  GpuThreadHost(
                 int channel_id,
                 const gpu::GPUInfo& gpu_info,
				 scoped_refptr<GpuThreadChannel> channel);
  ~GpuThreadHost();
  bool InternalSend(IPC::Message* msg);
  void InternalFlush(StreamFlushInfo* flush_info);
  void InternalFlush(StreamFlushInfo* flush_info, bool webgl);
  void DestroyCommandBuffer(int32_t route_id);

  void OnDestroyed(gpu::error::ContextLostReason reason,
                   gpu::error::Error error);
  void OnConsoleMessage(const GPUCommandBufferConsoleMessage& message);
  void OnSignalAck(uint32_t id);
  void OnSwapBuffersCompleted(
      const GpuCommandBufferMsg_SwapBuffersCompleted_Params& params);
  void OnUpdateVSyncParameters(base::TimeTicks timebase,
                               base::TimeDelta interval);

  const int channel_id_;
  const gpu::GPUInfo gpu_info_;

  scoped_refptr<IPC::SyncMessageFilter> sync_filter_;

  base::AtomicSequenceNumber next_image_id_;

  base::AtomicSequenceNumber next_route_id_;

  base::AtomicSequenceNumber next_stream_id_;

  mutable base::Lock context_lock_;
  base::hash_map<int32_t, StreamFlushInfo> stream_flush_info_;
  
  scoped_refptr<GpuThreadChannel> channel_;
  scoped_refptr<base::SingleThreadTaskRunner> gpu_thread_;
  CommandBufferProxyImpl* command_buffer_;

  DISALLOW_COPY_AND_ASSIGN(GpuThreadHost);
};

}  

#endif  
