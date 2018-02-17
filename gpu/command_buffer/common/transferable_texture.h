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
 * Based on texture_manager.h:
 * Copyright 2012 The Chromium Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GPU_COMMON_TRANSFERABLE_TEXTURE_H_
#define GPU_COMMON_TRANSFERABLE_TEXTURE_H_

#include <set>
#include <string>
#include <vector>
#include "gpu/gpu_export.h"

namespace gpu{

class GPU_EXPORT TransferableTexture {
public:
 TransferableTexture();
  ~TransferableTexture();

  struct LevelInfo {
    LevelInfo();
    ~LevelInfo();

    int level;
    unsigned int internal_format;
    int width;
    int height;
    int depth;
    int border;
    unsigned int format;
    unsigned int type;
    uint32_t estimated_size;
  };

  struct FaceInfo {
      FaceInfo();
      FaceInfo(const FaceInfo& r);
      ~FaceInfo();

	  int num_mip_levels;
      std::vector<LevelInfo> level_infos;
  };

  std::vector<FaceInfo> face_infos_;

  bool cleared_;

  int num_uncleared_mips_;
  int num_npot_faces_;

  unsigned int min_filter_;
  unsigned int mag_filter_;
  unsigned int wrap_r_;
  unsigned int wrap_s_;
  unsigned int wrap_t_;
  unsigned int usage_;
  unsigned int compare_func_;
  unsigned int compare_mode_;
  float max_lod_;
  float min_lod_;
  int base_level_;
  int max_level_;

  int max_level_set_;

  bool texture_complete_;

  bool texture_mips_dirty_;

  bool cube_complete_;

  bool texture_level0_dirty_;

  bool npot_;

  bool has_been_bound_;

  int framebuffer_attachment_count_;

  bool immutable_;

  bool has_images_;

  uint32_t estimated_size_;

  bool texture_max_anisotropy_initialized_;

  };
} 

#endif 

