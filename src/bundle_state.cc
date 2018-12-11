/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "bat/ads/bundle_state.h"

#include "json_helper.h"

namespace ads {

BundleState::BundleState() :
    catalog_id(""),
    catalog_version(0),
    catalog_ping(0),
    catalog_last_updated_timestamp(0),
    categories({}) {}

BundleState::BundleState(const BundleState& state):
    catalog_id(state.catalog_id),
    catalog_version(state.catalog_version),
    catalog_ping(state.catalog_ping),
    catalog_last_updated_timestamp(state.catalog_last_updated_timestamp),
    categories(state.categories) {}

BundleState::~BundleState() = default;

const std::string BundleState::ToJson() const {
  std::string json;
  SaveToJson(*this, &json);
  return json;
}

bool BundleState::FromJson(
    const std::string& json,
    const std::string& jsonSchema) {
  rapidjson::Document bundle;
  bundle.Parse(json.c_str());

  if (!helper::JSON::Validate(&bundle, jsonSchema)) {
    return false;
  }

  std::map<std::string, std::vector<AdInfo>> new_categories = {};

  if (bundle.HasMember("categories")) {
    for (const auto& category : bundle["categories"].GetObject()) {
      for (const auto& info : category.value.GetArray()) {
        AdInfo ad_info;

        if (info.HasMember("creativeSetId")) {
          ad_info.creative_set_id = info["creativeSetId"].GetString();
        }

        if (info.HasMember("startTimestamp")) {
          ad_info.start_timestamp = info["startTimestamp"].GetString();
        }

        if (info.HasMember("endTimestamp")) {
          ad_info.end_timestamp = info["endTimestamp"].GetString();
        }

        std::vector<std::string> regions = {};
        if (info.HasMember("regions")) {
          for (const auto& region : info["regions"].GetArray()) {
            regions.push_back(region.GetString());
          }
        }
        ad_info.regions = regions;

        ad_info.advertiser = info["advertiser"].GetString();
        ad_info.notification_text = info["notificationText"].GetString();
        std::string url = info["notificationURL"].GetString();
        if (url.find("http://") != 0 && url.find("https://") != 0) {
          url = "http://" + url;
        }
        ad_info.notification_url = url;
        ad_info.uuid = info["uuid"].GetString();

        if (new_categories.find(category.name.GetString()) ==
            new_categories.end()) {
          new_categories.insert({category.name.GetString(), {}});
        }
        new_categories.at(category.name.GetString()).push_back(ad_info);
      }
    }
  }

  categories = new_categories;

  return true;
}

void SaveToJson(JsonWriter* writer, const BundleState& state) {
  writer->StartObject();

  writer->String("categories");
  writer->StartObject();

  for (const auto& category : state.categories) {
    writer->String(category.first.c_str());
    writer->StartArray();

    for (const auto& ad : category.second) {
      writer->StartObject();

      writer->String("creativeSetId");
      writer->String(ad.creative_set_id.c_str());

      writer->String("regions");
      writer->StartArray();
      for (const auto& region : ad.regions) {
        writer->String(region.c_str());
      }
      writer->EndArray();

      writer->String("startTimestamp");
      writer->String(ad.start_timestamp.c_str());

      writer->String("endTimestamp");
      writer->String(ad.end_timestamp.c_str());

      writer->String("advertiser");
      writer->String(ad.advertiser.c_str());

      writer->String("notificationText");
      writer->String(ad.notification_text.c_str());

      writer->String("notificationURL");
      writer->String(ad.notification_url.c_str());

      writer->String("uuid");
      writer->String(ad.uuid.c_str());

      writer->EndObject();
    }

    writer->EndArray();
  }

  writer->EndObject();

  writer->EndObject();
}

}  // namespace ads
