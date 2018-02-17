/* Copyright (C) 2016-2018 University of California, Irvine
 *
 * Authors:
 * Zhihao Yao <z.yao@uci.edu>
 * Zongheng Ma <zonghenm@uci.edu>
 * Yingtong Liu <yingtong@uci.edu>
 * Ardalan Amiri Sani <arrdalan@gmail.com>
 * 
 * All rights reserved. Use of this source code is governed
 * by a BSD-style license that can be found in the LICENSE file.
 *
 * Based on texture_manager.cc:
 * Copyright 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpu/command_buffer/common/transferable_texture.h"
namespace gpu {

 TransferableTexture::TransferableTexture()
      :cleared_(true),
      num_uncleared_mips_(0),
      num_npot_faces_(0),
      min_filter_(0x2702),
      mag_filter_(0x2601),
      wrap_r_(0x2901),
      wrap_s_(0x2901),
      wrap_t_(0x2901),
      usage_(0x0),
      compare_func_(0x0203),
      compare_mode_(0x0),
      max_lod_(1000.0f),
      min_lod_(-1000.0f),
      base_level_(0),
      max_level_(1000),
      max_level_set_(-1),
      texture_complete_(false),
      texture_mips_dirty_(false),
      cube_complete_(false),
      texture_level0_dirty_(false),
      npot_(false),
      has_been_bound_(false),
      framebuffer_attachment_count_(0),
      immutable_(false),
      has_images_(false),
      estimated_size_(0),
      texture_max_anisotropy_initialized_(false) {
}

TransferableTexture::~TransferableTexture() {
}

TransferableTexture::LevelInfo::LevelInfo()
      : level(-1),
      internal_format(0),
      width(0),
      height(0),
      depth(0),
      border(0),
      format(0),
      type(0),
      estimated_size(0) {}

TransferableTexture::LevelInfo::~LevelInfo() {
}

TransferableTexture::FaceInfo::FaceInfo() {
  num_mip_levels = 0;
}

TransferableTexture::FaceInfo::FaceInfo(const FaceInfo& r) = default;
TransferableTexture::FaceInfo::~FaceInfo() {
}

} 
