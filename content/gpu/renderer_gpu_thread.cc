/* Copyright (C) 2016-2018 University of California, Irvine
 *
 * Authors:
 * Zhihao Yao <z.yao@uci.edu>
 * Ardalan Amiri Sani <arrdalan@gmail.com>
 * 
 * All rights reserved. Use of this source code is governed
 * by a BSD-style license that can be found in the LICENSE file.
 */

#include "content/gpu/renderer_gpu_thread.h"

#include <errno.h>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/compiler_specific.h"
#include "base/files/file_util.h"
#include "base/location.h"
#include "base/process/process.h"
#include "base/single_thread_task_runner.h"
#include "build/build_config.h"
#include "content/public/common/content_switches.h"
#include "content/public/common/result_codes.h"
#include <sys/syscall.h>

#include "base/run_loop.h"

namespace content {
namespace {

}  

int GpuMain(const MainFunctionParams& parameters);
int RendererGpuMain(const MainFunctionParams& parameters, base::MessageLoop* main_message_loop);

RendererGpuThread::RendererGpuThread(base::CommandLine* __cmd_line)
    : base::Thread("renderer-gpu-thread"),
      cmd_line(__cmd_line)
{
  fprintf(stderr, "%s [1]\n", __PRETTY_FUNCTION__);
  fprintf(stderr, "%s [2]: isRendererGpuThread (__cmd_line) = %d\n",
				__PRETTY_FUNCTION__,
				__cmd_line->HasSwitch("renderer-gpu-thread"));
  fprintf(stderr, "%s [3]: isRendererGpuThread (cmd_line) = %d\n",
				__PRETTY_FUNCTION__,
				cmd_line->HasSwitch("renderer-gpu-thread"));
}

void RendererGpuThread::Init() {
}

void RendererGpuThread::CleanUp() {
  fprintf(stderr, "%s [1]\n", __PRETTY_FUNCTION__);
}

RendererGpuThread::~RendererGpuThread() {
  fprintf(stderr, "%s [1]\n", __PRETTY_FUNCTION__);
  fprintf(stderr, "%s [2]: GetThreadId() = %d\n", __PRETTY_FUNCTION__, GetThreadId());
  fprintf(stderr, "%s [3]: PlatformThread::CurrentId() = %d\n",
					__PRETTY_FUNCTION__, base::PlatformThread::CurrentId());
}

void RendererGpuThread::InitGpu()
{
  fprintf(stderr, "success[0]:%s\n", __PRETTY_FUNCTION__);
}

void RendererGpuThread::PostInitGpu()
{

  task_runner()->PostTask(FROM_HERE,
                          base::Bind(&RendererGpuThread::Init, this));
}

}  
