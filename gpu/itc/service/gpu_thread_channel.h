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
 * Based on gpu_channel.h:
 * Copyright 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GPU_ITC_SERVICE_GPU_THREAD_CHANNEL_H_
#define GPU_ITC_SERVICE_GPU_THREAD_CHANNEL_H_

#include <stddef.h>
#include <stdint.h>

#include <memory>
#include <string>
#include <unordered_map>

#include "base/containers/hash_tables.h"
#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/process/process.h"
#include "base/threading/thread_checker.h"
#include "base/trace_event/memory_dump_provider.h"
#include "build/build_config.h"
#include "gpu/gpu_export.h"
#include "gpu/ipc/common/gpu_stream_constants.h"
#include "gpu/ipc/service/gpu_command_buffer_stub.h"
#include "gpu/ipc/service/gpu_memory_manager.h"
#include "ipc/ipc_sender.h"
#include "ipc/ipc_sync_channel.h"
#include "ipc/message_router.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gl/gl_share_group.h"
#include "ui/gl/gpu_preference.h"

#include "gpu/command_buffer/common/buffer.h"
#include "base/synchronization/waitable_event.h"

struct GPUCreateCommandBufferConfig;

namespace base {
class WaitableEvent;
}

namespace gpu {

class PreemptionFlag;
class SyncPointOrderData;
class SyncPointManager;
class GpuThreadChannelManager;
class GpuThreadChannelMessageQueue;
class GpuWatchdogThread;
class GpuThreadHost;

class GPU_EXPORT GpuThreadChannel
        : public base::RefCountedThreadSafe<GpuThreadChannel> {
 public:
  GpuThreadChannel(GpuThreadChannelManager* gpu_channel_manager,
             SyncPointManager* sync_point_manager,
             GpuWatchdogThread* watchdog,
             gl::GLShareGroup* share_group,
             gles2::MailboxManager* mailbox_manager,
             PreemptionFlag* preempting_flag,
             PreemptionFlag* preempted_flag,
             int32_t client_id,
             uint64_t client_tracing_id,
             bool allow_view_command_buffers,
             bool allow_real_time_streams,
	         scoped_refptr<base::SingleThreadTaskRunner> render_task_runner
			 );

  void Init(base::WaitableEvent* shutdown_event);

  GpuThreadChannelManager* gpu_thread_channel_manager() const {
    return gpu_channel_manager_;
  }

  SyncPointManager* sync_point_manager() const { return sync_point_manager_; }

  GpuWatchdogThread* watchdog() const { return watchdog_; }

  const scoped_refptr<gles2::MailboxManager>& mailbox_manager() const {
    return mailbox_manager_;
  }

  const scoped_refptr<base::SingleThreadTaskRunner>& render_task_runner() const {
	DCHECK(render_task_runner_);
    return render_task_runner_;
  }

  const scoped_refptr<PreemptionFlag>& preempted_flag() const {
    return preempted_flag_;
  }

  int client_id() const { return client_id_; }

  uint64_t client_tracing_id() const { return client_tracing_id_; }

  gl::GLShareGroup* share_group() const { return share_group_.get(); }

  GpuCommandBufferStub* LookupCommandBuffer(int32_t route_id);

  void MarkAllContextsLost();

  void SetGpuThreadHost(scoped_refptr<GpuThreadHost> host);
  scoped_refptr<GpuThreadHost> GetGpuThreadHost();

  uint32_t GetProcessedOrderNum() const;

  uint32_t GetUnprocessedOrderNum() const;

  scoped_refptr<SyncPointOrderData> GetSyncPointOrderData(
      int32_t stream_id);

#if defined(OS_ANDROID)
#endif

 protected:

  std::unordered_map<int32_t, std::unique_ptr<GpuCommandBufferStub>> stubs_;

 private:
  ~GpuThreadChannel();
  friend class base::RefCountedThreadSafe<GpuThreadChannel>;
  friend class CommandBufferProxyImpl;
  friend class GpuThreadHost;
  friend class std::unique_ptr<GpuThreadChannel>;
  friend class GpuThreadChannelManager;

  scoped_refptr<GpuThreadChannelMessageQueue> CreateStream(
      int32_t stream_id,
      GpuStreamPriority stream_priority);

  scoped_refptr<GpuThreadChannelMessageQueue> LookupStream(int32_t stream_id);

  void DestroyStreamIfNecessary(
      const scoped_refptr<GpuThreadChannelMessageQueue>& queue);

  void OnCreateCommandBuffer(base::WaitableEvent* complete,
                             const GPUCreateCommandBufferConfig& init_params,
                             int32_t route_id,
							 gpu::CommandBufferSharedState* shared_state,
                             bool* result,
                             gpu::Capabilities* capabilities,
	                         scoped_refptr<GpuChannelHost> compositor_channel,
							 int32_t compositor_route_id);
	void OnDestroyCommandBuffer(int32_t route_id);

  void Nop(bool* res, 
           base::WaitableEvent* complete);

  std::unique_ptr<GpuCommandBufferStub> CreateCommandBuffer(
      const GPUCreateCommandBufferConfig& init_params,
      int32_t route_id,
	  int32_t compositor_route_id,
      gpu::CommandBufferSharedState* shared_state,
	  scoped_refptr<GpuChannelHost> compositor_channel);

  void OnSetGetBuffer(base::WaitableEvent* complete,
                      int32_t route_id,
                      int32_t shm_id);
  void OnTakeFrontBuffer(int32_t route_id, const Mailbox& mailbox);
  void OnReturnFrontBuffer(int32_t route_id, const Mailbox& mailbox, bool is_lost);
  void OnGetState(base::WaitableEvent* complete, int32_t route_id);
  void OnWaitForTokenInRange(base::WaitableEvent* complete,
                             int32_t route_id,
                             int32_t start,
                             int32_t end,
	                         gpu::CommandBuffer::State* state);
  void OnWaitForGetOffsetInRange(base::WaitableEvent* complete,
                                 int32_t route_id,
                                 int32_t start,
                                 int32_t end,
                                 gpu::CommandBuffer::State* state);
  void OnAsyncFlush(base::WaitableEvent* complete,
                    int32_t route_id,
                    int32_t put_offset,
                    uint32_t flush_count,
                    const std::vector<ui::LatencyInfo>& latency_info);

  void OnRegisterTransferBuffer(
      int32_t route_id,
      int32_t id,
  	  scoped_refptr<gpu::Buffer> buffer,
      uint32_t size);
  void OnDestroyTransferBuffer(int32_t route_id, int32_t id);
  void OnGetTransferBuffer(base::WaitableEvent* complete, int32_t route_id, int32_t id, IPC::Message* reply_message);
  void OnEnsureBackbuffer(int32_t route_id);
  void OnWaitSyncToken(int32_t route_id, const SyncToken& sync_token);
  void OnSignalSyncToken(int32_t route_id, const SyncToken& sync_token, uint32_t id);
  void OnSignalAck(int32_t route_id, uint32_t id);
  void OnSignalQuery(int32_t route_id, uint32_t query, uint32_t id);

  void OnFenceSyncRelease(int32_t route_id, uint64_t release);
  void OnWaitFenceSync(base::WaitableEvent* complete,
                       int32_t route_id,
                       CommandBufferNamespace namespace_id,
                       CommandBufferId command_buffer_id,
                       uint64_t release,
					   bool* res);
  void OnWaitFenceSyncCompleted(int32_t route_id,
                                CommandBufferNamespace namespace_id,
                                CommandBufferId command_buffer_id,
                                uint64_t release);

  void OnDescheduleUntilFinished(int32_t route_id);
  void OnRescheduleAfterFinished(int32_t route_id);

  void OnCreateImage(int32_t route_id, const GpuCommandBufferMsg_CreateImage_Params& params);
  void OnDestroyImage(int32_t route_id, int32_t id);
  void OnCreateStreamTexture(base::WaitableEvent* complete,
                             int32_t route_id, 
                             uint32_t texture_id,
                             int32_t stream_id,
                             bool* succeeded);
  void OnCommandProcessed(int32_t route_id);
  void OnParseError(int32_t route_id);

  GpuThreadChannelManager* const gpu_channel_manager_;

  SyncPointManager* const sync_point_manager_;

  scoped_refptr<PreemptionFlag> preempting_flag_;

  scoped_refptr<PreemptionFlag> preempted_flag_;

  const int32_t client_id_;

  const uint64_t client_tracing_id_;

  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;
  scoped_refptr<base::SingleThreadTaskRunner> io_task_runner_;
  scoped_refptr<base::SingleThreadTaskRunner> gpu_task_runner_;
  scoped_refptr<base::SingleThreadTaskRunner> render_task_runner_;

  scoped_refptr<gl::GLShareGroup> share_group_;

  scoped_refptr<gles2::MailboxManager> mailbox_manager_;

  GpuWatchdogThread* const watchdog_;

  base::hash_map<int32_t, scoped_refptr<GpuThreadChannelMessageQueue>> streams_;

  base::hash_map<int32_t, int> streams_to_num_routes_;

  base::hash_map<int32_t, int32_t> routes_to_streams_;

  const bool allow_view_command_buffers_;

  const bool allow_real_time_streams_;

  scoped_refptr<GpuThreadHost> host_;

  DISALLOW_COPY_AND_ASSIGN(GpuThreadChannel);
};

struct GpuThreadChannelMessage {
  IPC::Message message;
  uint32_t order_number;
  base::TimeTicks time_received;

  GpuThreadChannelMessage(const IPC::Message& msg,
                    uint32_t order_num,
                    base::TimeTicks ts)
      : message(msg), order_number(order_num), time_received(ts) {}

};
class GpuThreadChannelMessageQueue
    : public base::RefCountedThreadSafe<GpuThreadChannelMessageQueue> {
 public:
  static scoped_refptr<GpuThreadChannelMessageQueue> Create(
      int32_t stream_id,
      GpuStreamPriority stream_priority,
      GpuThreadChannel* channel,
      const scoped_refptr<base::SingleThreadTaskRunner>& io_task_runner,
      const scoped_refptr<PreemptionFlag>& preempting_flag,
      const scoped_refptr<PreemptionFlag>& preempted_flag,
      SyncPointManager* sync_point_manager);

  void Disable();
  void DisableIO();

  int32_t stream_id() const { return stream_id_; }
  GpuStreamPriority stream_priority() const { return stream_priority_; }

  bool IsScheduled() const;
  void OnRescheduled(bool scheduled);

  bool HasQueuedMessages() const;

  base::TimeTicks GetNextMessageTimeTick() const;

  scoped_refptr<SyncPointOrderData> GetSyncPointOrderData();

  uint32_t GetUnprocessedOrderNum() const;

  uint32_t GetProcessedOrderNum() const;

  const GpuThreadChannelMessage* BeginMessageProcessing();
  void PauseMessageProcessing();
  void FinishMessageProcessing();

  bool PushBackMessage();

 private:
  enum PreemptionState {
    IDLE,
    WAITING,
    CHECKING,
    PREEMPTING,
    WOULD_PREEMPT_DESCHEDULED,
  };

  friend class base::RefCountedThreadSafe<GpuThreadChannelMessageQueue>;

  GpuThreadChannelMessageQueue(
      int32_t stream_id,
      GpuStreamPriority stream_priority,
      GpuThreadChannel* channel,
      const scoped_refptr<base::SingleThreadTaskRunner>& io_task_runner,
      const scoped_refptr<PreemptionFlag>& preempting_flag,
      const scoped_refptr<PreemptionFlag>& preempted_flag,
      SyncPointManager* sync_point_manager);
  ~GpuThreadChannelMessageQueue();

  void UpdatePreemptionState();
  void UpdatePreemptionStateHelper();

  void UpdateStateIdle();
  void UpdateStateWaiting();
  void UpdateStateChecking();
  void UpdateStatePreempting();
  void UpdateStateWouldPreemptDescheduled();

  void TransitionToIdle();
  void TransitionToWaiting();
  void TransitionToChecking();
  void TransitionToPreempting();
  void TransitionToWouldPreemptDescheduled();

  bool ShouldTransitionToIdle() const;

  const int32_t stream_id_;
  const GpuStreamPriority stream_priority_;

  bool enabled_;
  bool scheduled_;
  GpuThreadChannel* const channel_;
  std::deque<uint32_t> channel_message_order_numbers_;
  mutable base::Lock channel_lock_;

  PreemptionState preemption_state_;
  base::TimeDelta max_preemption_time_;
  std::unique_ptr<base::OneShotTimer> timer_;
  base::ThreadChecker io_thread_checker_;

  scoped_refptr<SyncPointOrderData> sync_point_order_data_;

  scoped_refptr<base::SingleThreadTaskRunner> io_task_runner_;
  scoped_refptr<PreemptionFlag> preempting_flag_;
  scoped_refptr<PreemptionFlag> preempted_flag_;
  SyncPointManager* const sync_point_manager_;

  DISALLOW_COPY_AND_ASSIGN(GpuThreadChannelMessageQueue);
};

}  

#endif  
