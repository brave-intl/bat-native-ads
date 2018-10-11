/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <any>
#include <ctime>

#include "../include/json_bat_helper.h"

namespace ads_bat_client {

namespace settings {

const char enabled[] = "ads.enabled";
const char locale[] = "ads.locale";
const char perDay[] = "ads.amount.day";
const char perHour[] = "ads.amount.hour";
const char place[] = "ads.place";
const char operatingMode[] = "ads.operating-mode";

}  // namespace settings

struct CLIENT_STATE_ST {
  CLIENT_STATE_ST();
  CLIENT_STATE_ST(const CLIENT_STATE_ST& state);
  ~CLIENT_STATE_ST();

  bool LoadFromJson(const std::string& json);

  bool ads_enabled;
  bool ad_served;
  std::vector<std::string> adsShownHistory;
  std::string adUUID;
  std::map<std::string, bool> adsUUIDSeen;
  bool available;
  bool allowed;
  std::map<std::string, std::any> catalog;
  bool configured;
  std::string currentSSID;
  bool expired;
  std::time_t finalContactTimestamp;
  std::time_t firstContactTimestamp;
  std::time_t lastAdTime;
  std::time_t lastSearchTime;
  std::time_t lastShopTime;
  std::string lastUrl;
  std::time_t lastUserActivity;
  std::time_t lastUserIdleStopTime;
  std::string locale;
  std::vector<std::string> locales;
  std::vector<double> pageScoreHistory;
  std::map<std::string, std::string> places;
  bool purchaseActive;
  std::time_t purchaseTime;
  std::string purchaseUrl;
  std::map<std::string, std::any> reportingEventQueue;
  double score;
  bool searchActivity;
  std::string searchUrl;
  std::map<std::string, std::any> settings;
  bool shopActivity;
  std::string shopUrl;
  bool status;
  std::time_t updated;
  std::string url;
};

}  // namespace ads_bat_client
