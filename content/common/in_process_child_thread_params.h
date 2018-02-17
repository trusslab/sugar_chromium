// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_COMMON_IN_PROCESS_CHILD_THREAD_PARAMS_H_
#define CONTENT_COMMON_IN_PROCESS_CHILD_THREAD_PARAMS_H_

#include <string>

#include "base/memory/ref_counted.h"
#include "base/single_thread_task_runner.h"
#include "content/common/content_export.h"

namespace content {

// Tells ChildThreadImpl to run in in-process mode. There are a couple of
// parameters to run in the mode: An emulated io task runner used by
// ChnanelMojo, an IPC channel name to open.
class CONTENT_EXPORT InProcessChildThreadParams {
 public:
  InProcessChildThreadParams(
      scoped_refptr<base::SingleThreadTaskRunner> io_runner,
      const std::string& service_request_token);
  InProcessChildThreadParams(const InProcessChildThreadParams& other);
  ~InProcessChildThreadParams();

  scoped_refptr<base::SingleThreadTaskRunner> io_runner() const {
    return io_runner_;
  }
  InProcessChildThreadParams(
      bool webgl,
      scoped_refptr<base::SingleThreadTaskRunner> io_runner,
      const std::string& service_request_token);

  bool webgl() const {return webgl_;}
  const std::string& channel_name() const { return channel_name_; }
  const std::string& service_request_token() const {
    return service_request_token_;
  }

 private:
  scoped_refptr<base::SingleThreadTaskRunner> io_runner_;
  std::string service_request_token_;
  bool webgl_;
  std::string channel_name_;
};

}  // namespace content

#endif  // CONTENT_COMMON_IN_PROCESS_CHILD_THREAD_PARAMS_H_
