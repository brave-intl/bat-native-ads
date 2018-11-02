/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <string>
#include <vector>

#include "catalog_segment_info.h"
#include "catalog_creative_info.h"

namespace ads {

struct CreativeSetInfo {
  CreativeSetInfo() :
    creative_set_id(""),
    execution(""),
    per_day(0),
    total_max(0),
    segments({}),
    creatives({}) {}

  explicit CreativeSetInfo(const std::string& creative_set_id) :
    creative_set_id(creative_set_id),
    execution(""),
    per_day(0),
    total_max(0),
    segments({}),
    creatives({}) {}

  CreativeSetInfo(const CreativeSetInfo& info) :
    creative_set_id(info.creative_set_id),
    execution(info.execution),
    per_day(info.per_day),
    total_max(info.total_max),
    segments(info.segments),
    creatives(info.creatives) {}

  ~CreativeSetInfo() {}

  std::string creative_set_id;
  std::string execution;
  uint64_t per_day;
  uint64_t total_max;
  std::vector<SegmentInfo> segments;
  std::vector<CreativeInfo> creatives;
};

}  // namespace ads