/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <string>
#include <vector>

#include "catalog_geo_target_info.h"
#include "catalog_creative_set_info.h"

namespace catalog {

struct CampaignInfo {
  CampaignInfo() :
    campaign_id(""),
    name(""),
    start_at(""),
    end_at(""),
    daily_cap(0),
    budget(0),
    advertiser_id(""),
    geo_targets({}),
    creative_sets({}) {}

  explicit CampaignInfo(const std::string& campaign_id) :
    campaign_id(campaign_id),
    name(""),
    start_at(""),
    end_at(""),
    daily_cap(0),
    budget(0),
    advertiser_id(""),
    geo_targets({}),
    creative_sets({}) {}

  CampaignInfo(const CampaignInfo& info) :
    campaign_id(info.campaign_id),
    name(info.name),
    start_at(info.start_at),
    end_at(info.end_at),
    daily_cap(info.daily_cap),
    budget(info.budget),
    advertiser_id(info.advertiser_id),
    geo_targets(info.geo_targets),
    creative_sets(info.creative_sets) {}

  ~CampaignInfo() {}

  std::string campaign_id;
  std::string name;
  std::string start_at;
  std::string end_at;
  int64_t daily_cap;
  int64_t budget;
  std::string advertiser_id;
  std::vector<GeoTargetInfo> geo_targets;
  std::vector<CreativeSetInfo> creative_sets;
};

struct CampaignInfoFilter {
  CampaignInfoFilter() :
      campaign_id("") {}

  CampaignInfoFilter(const CampaignInfoFilter& filter) :
      campaign_id(filter.campaign_id) {}

  ~CampaignInfoFilter() {}

  std::string campaign_id;
};

}  // namespace catalog