/* Copyright (C) 2016-2018 University of California, Irvine
 *
 * Authors:
 * Zhihao Yao <z.yao@uci.edu>
 * Zongheng Ma <zonghenm@uci.edu>
 * Ardalan Amiri Sani <arrdalan@gmail.com>
 * 
 * All rights reserved. Use of this source code is governed
 * by a BSD-style license that can be found in the LICENSE file.
 */

#include "gpu_thread_channel_manager_delegate.h"
#include "content/renderer/gpu/render_gpu_thread_host.h"
#include "base/debug/crash_logging.h"

#include "base/prints.h"

namespace gpu {

GpuThreadChannelManagerDelegate::GpuThreadChannelManagerDelegate(content::RenderGpuThreadHost* host) : host_(host) {
  DCHECK(host_);
}

GpuThreadChannelManagerDelegate::~GpuThreadChannelManagerDelegate() {}

void GpuThreadChannelManagerDelegate::DidCreateOffscreenContext(const GURL& active_url) {
}

void GpuThreadChannelManagerDelegate::DidDestroyChannel(int client_id) {
}

void GpuThreadChannelManagerDelegate::DidDestroyOffscreenContext(const GURL& active_url) {
}

void GpuThreadChannelManagerDelegate::DidLoseContext(bool offscreen,
                                gpu::error::ContextLostReason reason,
                                const GURL& active_url) {
}

void GpuThreadChannelManagerDelegate::StoreShaderToDisk(int client_id,
                                   const std::string& key,
                                   const std::string& shader) {
}

#if defined(OS_WIN)
void GpuThreadChannelManagerDelegate::SendAcceleratedSurfaceCreatedChildWindow(
    gpu::SurfaceHandle parent_window,
    gpu::SurfaceHandle child_window) {
}
#endif

void GpuThreadChannelManagerDelegate::SetActiveURL(const GURL& url) {
  constexpr char kActiveURL[] = "url-chunk";
  base::debug::SetCrashKeyValue(kActiveURL, url.possibly_invalid_spec());
}

} 
