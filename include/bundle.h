/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <string>
#include <memory>

#include "ads_impl.h"
#include "ads_client.h"
#include "callback_handler.h"
#include "bundle_state.h"
#include "catalog_state.h"


namespace rewards_ads {
class AdsImpl;
}  // namespace rewards_ads

namespace state {

class Bundle: public ads::CallbackHandler {
 public:
  Bundle(rewards_ads::AdsImpl* ads, ads::AdsClient* ads_client);
  ~Bundle();

  bool LoadJson(const std::string& json);  // Deserialize
  void SaveJson();  // Serialize
  void Save();

  bool GenerateFromCatalog(const std::shared_ptr<CATALOG_STATE> catalog_state);

  void Reset();

 private:
  void OnBundleSaved(const ads::Result result);

  rewards_ads::AdsImpl* ads_;  // NOT OWNED
  ads::AdsClient* ads_client_;  // NOT OWNED

  std::shared_ptr<BUNDLE_STATE> bundle_state_;
};

}  // namespace state