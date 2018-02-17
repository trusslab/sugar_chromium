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
 * Based on gpu_channel.cc:
 * Copyright 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpu/itc/service/gpu_thread_channel.h"

#include <utility>

#if defined(OS_WIN)
#include <windows.h>
#endif

#include <algorithm>
#include <deque>
#include <set>
#include <vector>

#include "base/atomicops.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/location.h"
#include "base/numerics/safe_conversions.h"
#include "base/single_thread_task_runner.h"
#include "base/stl_util.h"
#include "base/strings/string_util.h"
#include "base/synchronization/lock.h"
#include "base/threading/thread_task_runner_handle.h"
#include "base/timer/timer.h"
#include "base/trace_event/memory_dump_manager.h"
#include "base/trace_event/process_memory_dump.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "gpu/command_buffer/common/mailbox.h"
#include "gpu/command_buffer/service/command_executor.h"
#include "gpu/command_buffer/service/image_factory.h"
#include "gpu/command_buffer/service/mailbox_manager.h"
#include "gpu/command_buffer/service/sync_point_manager.h"
#include "gpu/ipc/common/gpu_messages.h"
#include "gpu/itc/service/gpu_thread_channel_manager.h"
#include "gpu/ipc/service/gpu_memory_buffer_factory.h"
#include "ipc/ipc_channel.h"
#include "ipc/message_filter.h"
#include "ui/gl/gl_context.h"
#include "ui/gl/gl_image_shared_memory.h"
#include "ui/gl/gl_surface.h"

#include "gpu/itc/client/gpu_thread_host.h"
#include "gpu/ipc/client/gpu_channel_host.h"

#include "base/prints.h"

namespace gpu {
namespace {

const int64_t kVsyncIntervalMs = 17;
const int64_t kPreemptWaitTimeMs = 2 * kVsyncIntervalMs;
const int64_t kMaxPreemptTimeMs = kVsyncIntervalMs;
}  
scoped_refptr<GpuThreadChannelMessageQueue> GpuThreadChannelMessageQueue::Create(
    int32_t stream_id,
    GpuStreamPriority stream_priority,
    GpuThreadChannel* channel,
    const scoped_refptr<base::SingleThreadTaskRunner>& io_task_runner,
    const scoped_refptr<PreemptionFlag>& preempting_flag,
    const scoped_refptr<PreemptionFlag>& preempted_flag,
    SyncPointManager* sync_point_manager) {
  return new GpuThreadChannelMessageQueue(stream_id, stream_priority, channel,
                                    io_task_runner, preempting_flag,
                                    preempted_flag, sync_point_manager);
}

scoped_refptr<SyncPointOrderData>
GpuThreadChannelMessageQueue::GetSyncPointOrderData() {
  return sync_point_order_data_;
}

GpuThreadChannelMessageQueue::GpuThreadChannelMessageQueue(
    int32_t stream_id,
    GpuStreamPriority stream_priority,
    GpuThreadChannel* channel,
    const scoped_refptr<base::SingleThreadTaskRunner>& io_task_runner,
    const scoped_refptr<PreemptionFlag>& preempting_flag,
    const scoped_refptr<PreemptionFlag>& preempted_flag,
    SyncPointManager* sync_point_manager)
    : stream_id_(stream_id),
      stream_priority_(stream_priority),
      enabled_(true),
      scheduled_(true),
      channel_(channel),
      preemption_state_(IDLE),
      max_preemption_time_(
          base::TimeDelta::FromMilliseconds(kMaxPreemptTimeMs)),
      timer_(new base::OneShotTimer),
      sync_point_order_data_(SyncPointOrderData::Create(true)),
      io_task_runner_(io_task_runner),
      preempting_flag_(preempting_flag),
      preempted_flag_(preempted_flag),
      sync_point_manager_(sync_point_manager) {
  timer_->SetTaskRunner(io_task_runner);
  io_thread_checker_.DetachFromThread();
}

GpuThreadChannelMessageQueue::~GpuThreadChannelMessageQueue() {
  DCHECK(!enabled_);
  DCHECK(channel_message_order_numbers_.empty());
}

void GpuThreadChannelMessageQueue::Disable() {
  {
    base::AutoLock auto_lock(channel_lock_);
    DCHECK(enabled_);
    enabled_ = false;
  }

  while (!channel_message_order_numbers_.empty()) {
    channel_message_order_numbers_.pop_front();
  }

  sync_point_order_data_->Destroy();
  sync_point_order_data_ = nullptr;

  io_task_runner_->PostTask(
      FROM_HERE, base::Bind(&GpuThreadChannelMessageQueue::DisableIO, this));
}

void GpuThreadChannelMessageQueue::DisableIO() {
  DCHECK(io_thread_checker_.CalledOnValidThread());
  timer_ = nullptr;
}

bool GpuThreadChannelMessageQueue::IsScheduled() const {
  base::AutoLock lock(channel_lock_);
  return scheduled_;
}

void GpuThreadChannelMessageQueue::OnRescheduled(bool scheduled) {
  base::AutoLock lock(channel_lock_);
  DCHECK(enabled_);
  if (scheduled_ == scheduled)
    return;
  scheduled_ = scheduled;
  if (preempting_flag_) {
    io_task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&GpuThreadChannelMessageQueue::UpdatePreemptionState, this));
  }
}

uint32_t GpuThreadChannelMessageQueue::GetUnprocessedOrderNum() const {
  return sync_point_order_data_->unprocessed_order_num();
}

uint32_t GpuThreadChannelMessageQueue::GetProcessedOrderNum() const {
  return sync_point_order_data_->processed_order_num();
}

bool GpuThreadChannelMessageQueue::PushBackMessage() {
  base::AutoLock auto_lock(channel_lock_);
  if (enabled_) {

    uint32_t order_num = sync_point_order_data_->GenerateUnprocessedOrderNumber(
        sync_point_manager_);

    if (channel_message_order_numbers_.empty()) {
      DCHECK(scheduled_);
    }

    channel_message_order_numbers_.push_back(order_num);

    if (preempting_flag_)
      UpdatePreemptionStateHelper();

    return true;
  }
  return false;
}

const GpuThreadChannelMessage* GpuThreadChannelMessageQueue::BeginMessageProcessing() {
  base::AutoLock auto_lock(channel_lock_);
  DCHECK(enabled_);
  if (preempted_flag_ && preempted_flag_->IsSet()) {
    return nullptr;
  }
  if (channel_message_order_numbers_.empty())
    return nullptr;
  sync_point_order_data_->BeginProcessingOrderNumber(
      channel_message_order_numbers_.front());
  return nullptr;
}

void GpuThreadChannelMessageQueue::PauseMessageProcessing() {
  base::AutoLock auto_lock(channel_lock_);
  DCHECK(!channel_message_order_numbers_.empty());

  sync_point_order_data_->PauseProcessingOrderNumber(
      channel_message_order_numbers_.front());
}

void GpuThreadChannelMessageQueue::FinishMessageProcessing() {
  base::AutoLock auto_lock(channel_lock_);
  DCHECK(!channel_message_order_numbers_.empty());
  DCHECK(scheduled_);

  sync_point_order_data_->FinishProcessingOrderNumber(
      channel_message_order_numbers_.front());
  channel_message_order_numbers_.pop_front();

  if (preempting_flag_) {
    io_task_runner_->PostTask(
        FROM_HERE,
        base::Bind(&GpuThreadChannelMessageQueue::UpdatePreemptionState, this));
  }
}

void GpuThreadChannelMessageQueue::UpdatePreemptionState() {
  DCHECK(io_thread_checker_.CalledOnValidThread());
  DCHECK(preempting_flag_);
  base::AutoLock lock(channel_lock_);
  UpdatePreemptionStateHelper();
}

void GpuThreadChannelMessageQueue::UpdatePreemptionStateHelper() {
  DCHECK(io_thread_checker_.CalledOnValidThread());
  DCHECK(preempting_flag_);
  channel_lock_.AssertAcquired();
  switch (preemption_state_) {
    case IDLE:
      UpdateStateIdle();
      break;
    case WAITING:
      UpdateStateWaiting();
      break;
    case CHECKING:
      UpdateStateChecking();
      break;
    case PREEMPTING:
      UpdateStatePreempting();
      break;
    case WOULD_PREEMPT_DESCHEDULED:
      UpdateStateWouldPreemptDescheduled();
      break;
    default:
      NOTREACHED();
  }
}

void GpuThreadChannelMessageQueue::UpdateStateIdle() {
  DCHECK(io_thread_checker_.CalledOnValidThread());
  DCHECK(preempting_flag_);
  channel_lock_.AssertAcquired();
  DCHECK(!timer_->IsRunning());
  if (!channel_message_order_numbers_.empty())
    TransitionToWaiting();
}

void GpuThreadChannelMessageQueue::UpdateStateWaiting() {
  DCHECK(io_thread_checker_.CalledOnValidThread());
  DCHECK(preempting_flag_);
  channel_lock_.AssertAcquired();
  if (!timer_->IsRunning())
    TransitionToChecking();
}

void GpuThreadChannelMessageQueue::UpdateStateChecking() {
}

void GpuThreadChannelMessageQueue::UpdateStatePreempting() {
  DCHECK(io_thread_checker_.CalledOnValidThread());
  DCHECK(preempting_flag_);
  channel_lock_.AssertAcquired();
  if (!timer_->IsRunning() || ShouldTransitionToIdle()) {
    TransitionToIdle();
  } else if (!scheduled_) {
    max_preemption_time_ = timer_->desired_run_time() - base::TimeTicks::Now();
    timer_->Stop();
  }
}

void GpuThreadChannelMessageQueue::UpdateStateWouldPreemptDescheduled() {
  DCHECK(io_thread_checker_.CalledOnValidThread());
  DCHECK(preempting_flag_);
  channel_lock_.AssertAcquired();
  DCHECK(!timer_->IsRunning());
  if (ShouldTransitionToIdle()) {
    TransitionToIdle();
  } else if (scheduled_) {
    TransitionToPreempting();
  }
}

bool GpuThreadChannelMessageQueue::ShouldTransitionToIdle() const {
  return false;
}

void GpuThreadChannelMessageQueue::TransitionToIdle() {
  DCHECK(io_thread_checker_.CalledOnValidThread());
  DCHECK(preempting_flag_);
  channel_lock_.AssertAcquired();
  DCHECK(preemption_state_ == PREEMPTING ||
         preemption_state_ == WOULD_PREEMPT_DESCHEDULED);

  preemption_state_ = IDLE;
  preempting_flag_->Reset();

  max_preemption_time_ = base::TimeDelta::FromMilliseconds(kMaxPreemptTimeMs);
  timer_->Stop();

  TRACE_COUNTER_ID1("gpu", "GpuThreadChannel::Preempting", this, 0);

  UpdateStateIdle();
}

void GpuThreadChannelMessageQueue::TransitionToWaiting() {
  DCHECK(io_thread_checker_.CalledOnValidThread());
  DCHECK(preempting_flag_);
  channel_lock_.AssertAcquired();
  DCHECK_EQ(preemption_state_, IDLE);
  DCHECK(!timer_->IsRunning());

  preemption_state_ = WAITING;

  timer_->Start(FROM_HERE,
                base::TimeDelta::FromMilliseconds(kPreemptWaitTimeMs), this,
                &GpuThreadChannelMessageQueue::UpdatePreemptionState);
}

void GpuThreadChannelMessageQueue::TransitionToChecking() {
  DCHECK(io_thread_checker_.CalledOnValidThread());
  DCHECK(preempting_flag_);
  channel_lock_.AssertAcquired();
  DCHECK_EQ(preemption_state_, WAITING);
  DCHECK(!timer_->IsRunning());

  preemption_state_ = CHECKING;

  UpdateStateChecking();
}

void GpuThreadChannelMessageQueue::TransitionToPreempting() {
  DCHECK(io_thread_checker_.CalledOnValidThread());
  DCHECK(preempting_flag_);
  channel_lock_.AssertAcquired();
  DCHECK(preemption_state_ == CHECKING ||
         preemption_state_ == WOULD_PREEMPT_DESCHEDULED);
  DCHECK(scheduled_);

  preemption_state_ = PREEMPTING;
  preempting_flag_->Set();
  TRACE_COUNTER_ID1("gpu", "GpuThreadChannel::Preempting", this, 1);

  DCHECK_LE(max_preemption_time_,
            base::TimeDelta::FromMilliseconds(kMaxPreemptTimeMs));
  timer_->Start(FROM_HERE, max_preemption_time_, this,
                &GpuThreadChannelMessageQueue::UpdatePreemptionState);
}

GpuThreadChannel::GpuThreadChannel(GpuThreadChannelManager* gpu_channel_manager,
                                   SyncPointManager* sync_point_manager,
                                   GpuWatchdogThread* watchdog,
                                   gl::GLShareGroup* share_group,
                                   gles2::MailboxManager* mailbox,
                                   PreemptionFlag* preempting_flag,
                                   PreemptionFlag* preempted_flag,
                                   int32_t client_id,
                                   uint64_t client_tracing_id,
                                   bool allow_view_command_buffers,
                                   bool allow_real_time_streams,
						           scoped_refptr<base::SingleThreadTaskRunner> render_task_runner)
    : gpu_channel_manager_(gpu_channel_manager),
      sync_point_manager_(sync_point_manager),
      preempting_flag_(preempting_flag),
      preempted_flag_(preempted_flag),
      client_id_(client_id),
      client_tracing_id_(client_tracing_id),
	  render_task_runner_(render_task_runner),
      share_group_(share_group),
      mailbox_manager_(mailbox),
      watchdog_(watchdog),
      allow_view_command_buffers_(allow_view_command_buffers),
      allow_real_time_streams_(allow_real_time_streams) {

  DCHECK(sync_point_manager_);

  scoped_refptr<GpuThreadChannelMessageQueue> control_queue =
      CreateStream(GPU_STREAM_DEFAULT, GpuStreamPriority::HIGH);
}

GpuThreadChannel::~GpuThreadChannel() {
  stubs_.clear();

  for (auto& kv : streams_)
    kv.second->Disable();
}

void GpuThreadChannel::Init(base::WaitableEvent* shutdown_event) {
    DCHECK(shutdown_event);
}

uint32_t GpuThreadChannel::GetProcessedOrderNum() const {
  uint32_t processed_order_num = 0;
  for (auto& kv : streams_) {
    processed_order_num =
        std::max(processed_order_num, kv.second->GetProcessedOrderNum());
  }
  return processed_order_num;
}
uint32_t GpuThreadChannel::GetUnprocessedOrderNum() const {
  uint32_t unprocessed_order_num = 0;
  for (auto& kv : streams_) {
    unprocessed_order_num =
        std::max(unprocessed_order_num, kv.second->GetUnprocessedOrderNum());
  }
  return unprocessed_order_num;
}

GpuCommandBufferStub* GpuThreadChannel::LookupCommandBuffer(int32_t route_id) {
  auto it = stubs_.find(route_id);
  if (it == stubs_.end())
    return nullptr;

  return it->second.get();
}
void GpuThreadChannel::MarkAllContextsLost() {
  for (auto& kv : stubs_)
    kv.second->MarkContextLost();
}

void GpuThreadChannel::SetGpuThreadHost(scoped_refptr<GpuThreadHost> host) {
  host_ = host;
}

scoped_refptr<GpuThreadHost> GpuThreadChannel::GetGpuThreadHost() {
  return host_;
}

scoped_refptr<SyncPointOrderData> GpuThreadChannel::GetSyncPointOrderData(
    int32_t stream_id) {
  auto it = streams_.find(stream_id);
  DCHECK(it != streams_.end());
  DCHECK(it->second);
  return it->second->GetSyncPointOrderData();
}
scoped_refptr<GpuThreadChannelMessageQueue> GpuThreadChannel::CreateStream(
    int32_t stream_id,
    GpuStreamPriority stream_priority) {
  DCHECK(streams_.find(stream_id) == streams_.end());
  scoped_refptr<GpuThreadChannelMessageQueue> queue = GpuThreadChannelMessageQueue::Create(
      stream_id, stream_priority, this, io_task_runner_,
      (stream_id == GPU_STREAM_DEFAULT) ? preempting_flag_ : nullptr,
      preempted_flag_, sync_point_manager_);
  streams_.insert(std::make_pair(stream_id, queue));
  streams_to_num_routes_.insert(std::make_pair(stream_id, 0));
  return queue;
}

scoped_refptr<GpuThreadChannelMessageQueue> GpuThreadChannel::LookupStream(
    int32_t stream_id) {
  auto stream_it = streams_.find(stream_id);
  if (stream_it != streams_.end())
    return stream_it->second;
  return nullptr;
}

void GpuThreadChannel::DestroyStreamIfNecessary(
    const scoped_refptr<GpuThreadChannelMessageQueue>& queue) {
  int32_t stream_id = queue->stream_id();
  if (streams_to_num_routes_[stream_id] == 0) {
    queue->Disable();
    streams_to_num_routes_.erase(stream_id);
    streams_.erase(stream_id);
  }
}

void GpuThreadChannel::OnCreateCommandBuffer(
       base::WaitableEvent* complete,
       const GPUCreateCommandBufferConfig& init_params,  
       int32_t route_id,
       gpu::CommandBufferSharedState* shared_state,
       bool* result,
       gpu::Capabilities* capabilities,
	   scoped_refptr<GpuChannelHost> compositor_channel,
	   int32_t compositor_route_id) {
  TRACE_EVENT2("gpu", "GpuThreadChannel::OnCreateCommandBuffer", "route_id", route_id,
               "offscreen", (init_params.surface_handle == kNullSurfaceHandle));
  std::unique_ptr<GpuCommandBufferStub> stub =
      CreateCommandBuffer(init_params, route_id, compositor_route_id, shared_state, compositor_channel);
  if (stub) {
    *result = true;
    *capabilities = stub->decoder()->GetCapabilities();
    stubs_[route_id] = std::move(stub);
  } else {
    *result = false;
    *capabilities = gpu::Capabilities();
  }
  complete->Signal();
}

std::unique_ptr<GpuCommandBufferStub> GpuThreadChannel::CreateCommandBuffer(
    const GPUCreateCommandBufferConfig& init_params,
    int32_t route_id,
	int32_t compositor_route_id,
	gpu::CommandBufferSharedState* shared_state,
	scoped_refptr<GpuChannelHost> compositor_channel) {  

  int32_t share_group_id = init_params.share_group_id;
  GpuCommandBufferStub* share_group = LookupCommandBuffer(share_group_id);

  if (!share_group && share_group_id != MSG_ROUTING_NONE) {
    DLOG(ERROR)
        << "GpuThreadChannel::CreateCommandBuffer(): invalid share group id";
    return nullptr;
  }

  int32_t stream_id = init_params.stream_id;
  if (share_group && stream_id != share_group->stream_id()) {
    DLOG(ERROR) << "GpuThreadChannel::CreateCommandBuffer(): stream id does not "
                   "match share group stream id";
    return nullptr;
  }

  GpuStreamPriority stream_priority = init_params.stream_priority;
  if (!allow_real_time_streams_ &&
      stream_priority == GpuStreamPriority::REAL_TIME) {
    DLOG(ERROR) << "GpuThreadChannel::CreateCommandBuffer(): real time stream "
                   "priority not allowed";
    return nullptr;
  }

  if (share_group && !share_group->decoder()) {
    DLOG(ERROR) << "GpuThreadChannel::CreateCommandBuffer(): shared context was "
                   "not initialized";
    return nullptr;
  }

  if (share_group && share_group->decoder()->WasContextLost()) {
    DLOG(ERROR) << "GpuThreadChannel::CreateCommandBuffer(): shared context was "
                   "already lost";
    return nullptr;
  }

  scoped_refptr<GpuThreadChannelMessageQueue> queue = LookupStream(stream_id);
  if (!queue)
    queue = CreateStream(stream_id, stream_priority);

  std::unique_ptr<GpuCommandBufferStub> stub(GpuCommandBufferStub::Create(
      this, share_group, init_params, route_id, compositor_route_id, shared_state, compositor_channel));

  if (!stub) {
    DestroyStreamIfNecessary(queue);
    return nullptr;
  }

  return stub;
}

void GpuThreadChannel::OnDestroyCommandBuffer(int32_t route_id) {
  TRACE_EVENT1("gpu", "GpuThreadChannel::OnDestroyCommandBuffer",
               "route_id", route_id);

  std::unique_ptr<GpuCommandBufferStub> stub;
  auto it = stubs_.find(route_id);
  if (it != stubs_.end()) {
    stub = std::move(it->second);
    stubs_.erase(it);
  }
  if (stub && !stub->IsScheduled()) {
  }

}

void GpuThreadChannel::Nop(bool* res, base::WaitableEvent* complete) {
  *res = true;
  complete->Signal();
}

void GpuThreadChannel::OnSetGetBuffer(base::WaitableEvent* complete,
                                      int32_t route_id,
                                      int32_t shm_id) {
    stubs_[route_id]->OnSetGetBufferForWebgl(shm_id);
	complete->Signal();
}

void GpuThreadChannel::OnTakeFrontBuffer(int32_t route_id, const Mailbox& mailbox) {
  stubs_[route_id]->OnTakeFrontBuffer(mailbox);
}

void GpuThreadChannel::OnReturnFrontBuffer(int32_t route_id, const Mailbox& mailbox, bool is_lost){
  stubs_[route_id]->OnReturnFrontBuffer(mailbox, is_lost);
}

void GpuThreadChannel::OnGetState(base::WaitableEvent* complete, int32_t route_id) {
}

void GpuThreadChannel::OnWaitForTokenInRange(base::WaitableEvent* complete,
                                             int32_t route_id,
                                             int32_t start,
                                             int32_t end,
	                                         gpu::CommandBuffer::State* state) {
  stubs_[route_id]->OnWaitForTokenInRangeForWebgl(start, end, state);
  complete->Signal();
}									  

void GpuThreadChannel::OnWaitForGetOffsetInRange(
    base::WaitableEvent* complete,
	int32_t route_id,
    int32_t start,
    int32_t end,
	gpu::CommandBuffer::State* state) {
  stubs_[route_id]->OnWaitForGetOffsetInRangeForWebgl(start, end, state);
  complete->Signal();
}

void GpuThreadChannel::OnAsyncFlush(
    base::WaitableEvent* complete,
    int32_t route_id,
    int32_t put_offset,
    uint32_t flush_count,
    const std::vector<ui::LatencyInfo>& latency_info) {
	stubs_[route_id]->OnAsyncFlush(put_offset, flush_count, latency_info);
	complete->Signal();
}

void GpuThreadChannel::OnRegisterTransferBuffer(
    int32_t route_id,
    int32_t id,
    scoped_refptr<gpu::Buffer> buffer,
    uint32_t size) {
  TRACE_EVENT0("gpu", "GpuCommandBufferStub::OnRegisterTransferBuffer");

  if (stubs_[route_id]) {
   stubs_[route_id]->OnRegisterTransferBufferForWebgl(
        id, buffer, size);
  }
}

void GpuThreadChannel::OnDestroyTransferBuffer(int32_t route_id, int32_t id) {
  stubs_[route_id]->OnDestroyTransferBuffer(id);
}

void GpuThreadChannel::OnGetTransferBuffer(base::WaitableEvent* complete, int32_t route_id, int32_t id, IPC::Message* reply_message) {
}

void GpuThreadChannel::OnWaitSyncToken(int32_t route_id, const SyncToken& sync_token) {
  stubs_[route_id]->OnWaitSyncToken(sync_token);
}

void GpuThreadChannel::OnSignalSyncToken(int32_t route_id, const SyncToken& sync_token, uint32_t id) {
  stubs_[route_id]->OnSignalSyncToken(sync_token, id);
}

void GpuThreadChannel::OnSignalAck(int32_t route_id, uint32_t id) {
  stubs_[route_id]->OnSignalAck(id);
}

void GpuThreadChannel::OnSignalQuery(int32_t route_id, uint32_t query, uint32_t id) {
  stubs_[route_id]->OnSignalQuery(query, id);
}
                                                                          
void GpuThreadChannel::OnFenceSyncRelease(int32_t route_id, uint64_t release) {
  stubs_[route_id]->OnFenceSyncRelease(release);
}

void GpuThreadChannel::OnWaitFenceSync(base::WaitableEvent* complete,
                                       int32_t route_id,
                                       CommandBufferNamespace namespace_id,
                                       CommandBufferId command_buffer_id,
                                       uint64_t release,
					                   bool* res) {
  *res = stubs_[route_id]->OnWaitFenceSync(namespace_id, command_buffer_id, release);
  complete->Signal();
}

void GpuThreadChannel::OnWaitFenceSyncCompleted(int32_t route_id,
                              CommandBufferNamespace namespace_id,
                              CommandBufferId command_buffer_id,
                              uint64_t release) {
  stubs_[route_id]->OnWaitFenceSyncCompleted(namespace_id, command_buffer_id, release);
}							  
                                                                          
void GpuThreadChannel::OnDescheduleUntilFinished(int32_t route_id) {
  stubs_[route_id]->OnDescheduleUntilFinished();
}

void GpuThreadChannel::OnRescheduleAfterFinished(int32_t route_id) {
  stubs_[route_id]->OnRescheduleAfterFinished();
}
                                                                          
void GpuThreadChannel::OnCreateImage(int32_t route_id, const GpuCommandBufferMsg_CreateImage_Params& params) {
  stubs_[route_id]->OnCreateImage(params);
}

void GpuThreadChannel::OnDestroyImage(int32_t route_id, int32_t id) {
  stubs_[route_id]->OnDestroyImage(id);
}

void GpuThreadChannel::OnCreateStreamTexture(base::WaitableEvent* complete,
                                             int32_t route_id,
                                             uint32_t texture_id,
                                             int32_t stream_id,
                                             bool* succeeded) {
  stubs_[route_id]->OnCreateStreamTexture(texture_id, stream_id, succeeded);
  complete->Signal();
}						   

void GpuThreadChannel::OnCommandProcessed(int32_t route_id) {
  stubs_[route_id]->OnCommandProcessed();
}

void GpuThreadChannel::OnParseError(int32_t route_id) {
  stubs_[route_id]->OnParseError();
}

}  
