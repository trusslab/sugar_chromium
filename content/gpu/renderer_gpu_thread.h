/* Copyright (C) 2016-2018 University of California, Irvine
 *
 * Authors:
 * Zhihao Yao <z.yao@uci.edu>
 * Ardalan Amiri Sani <arrdalan@gmail.com>
 * 
 * All rights reserved. Use of this source code is governed
 * by a BSD-style license that can be found in the LICENSE file.
 */

#ifndef CONTENT_GPU_RENDERER_GPU_THREAD_H_
#define CONTENT_GPU_RENDERER_GPU_THREAD_H_

#include "base/memory/ref_counted.h"
#include "base/memory/weak_ptr.h"
#include "base/message_loop/message_loop.h"
#include "base/threading/thread.h"
#include "base/time/time.h"
#include "ui/gfx/native_widget_types.h"
#include "base/command_line.h"
#include "content/public/common/main_function_params.h"

namespace content {

class RendererGpuThread : public base::Thread,
                          public base::RefCountedThreadSafe<RendererGpuThread> {
 public:
  explicit RendererGpuThread(base::CommandLine* cmd_line);
  void PostInitGpu();
  
 protected:
  void Init() override;
  void CleanUp() override;
  
 private:
  ~RendererGpuThread() override;
  base::CommandLine* cmd_line;
  void InitGpu();
  friend class base::RefCountedThreadSafe<RendererGpuThread>;

  };

}  

#endif  
