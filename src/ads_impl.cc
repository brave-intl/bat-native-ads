/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <fstream>
#include <algorithm>

#include "rapidjson/document.h"
#include "rapidjson/error/en.h"
#include "rapidjson/stringbuffer.h"
#include "rapidjson/writer.h"

#include "ads_impl.h"
#include "bat/ads/ads_client.h"
#include "bat/ads/notification_info.h"
#include "logging.h"
#include "search_providers.h"
#include "math_helper.h"
#include "string_helper.h"
#include "locale_helper.h"
#include "time_helper.h"
#include "uri_helper.h"
#include "bat/ads/url_components.h"
#include "static_values.h"

using namespace std::placeholders;

namespace ads {

AdsImpl::AdsImpl(AdsClient* ads_client) :
    is_first_run_(true),
    is_foreground_(false),
    media_playing_({}),
    last_shown_tab_url_(""),
    last_page_classification_(""),
    page_score_cache_({}),
    last_shown_notification_info_(NotificationInfo()),
    collect_activity_timer_id_(0),
    delivering_notifications_timer_id_(0),
    sustained_ad_interaction_timer_id_(0),
    next_easter_egg_(0),
    client_(std::make_unique<Client>(this, ads_client)),
    bundle_(std::make_unique<Bundle>(ads_client)),
    ads_serve_(std::make_unique<AdsServe>(this, ads_client, bundle_.get())),
    user_model_(nullptr),
    is_initialized_(false),
    ads_client_(ads_client) {
}

AdsImpl::~AdsImpl() = default;

void AdsImpl::Initialize() {
  if (!ads_client_->IsAdsEnabled()) {
    LOG(INFO) << "Deinitializing as Ads are disabled";

    Deinitialize();
    return;
  }

  if (IsInitialized()) {
    LOG(WARNING) << "Already initialized";
    return;
  }

  client_->LoadState();
}

void AdsImpl::InitializeStep2() {
  client_->SetLocales(ads_client_->GetLocales());

  LoadUserModel();
}

void AdsImpl::InitializeStep3() {
  is_initialized_ = true;

  LOG(INFO) << "Successfully initialized";

  is_foreground_ = ads_client_->IsForeground();

  ads_client_->SetIdleThreshold(kIdleThresholdInSeconds);

  NotificationAllowedCheck(false);

  RetrieveSSID();

  if (IsMobile()) {
    StartDeliveringNotifications(kDeliverNotificationsAfterSeconds);
  }

  ConfirmAdUUIDIfAdEnabled();

  ads_serve_->DownloadCatalog();
}

void AdsImpl::Deinitialize() {
  if (!IsInitialized()) {
    LOG(WARNING) << "Failed to deinitialize as not initialized";
    return;
  }

  LOG(INFO) << "Deinitializing";

  ads_serve_->Reset();

  StopDeliveringNotifications();

  StopSustainingAdInteraction();

  RemoveAllHistory();

  bundle_->Reset();
  user_model_.reset();

  last_shown_notification_info_ = NotificationInfo();

  last_page_classification_ = "";
  page_score_cache_.clear();

  is_first_run_ = true;
  is_initialized_ = false;
  is_foreground_ = false;
}

bool AdsImpl::IsInitialized() {
  if (!is_initialized_ ||
      !ads_client_->IsAdsEnabled() ||
      !user_model_->IsInitialized()) {
    return false;
  }

  return true;
}

void AdsImpl::LoadUserModel() {
  auto locale = client_->GetLocale();
  auto callback = std::bind(&AdsImpl::OnUserModelLoaded, this, _1, _2);
  ads_client_->LoadUserModelForLocale(locale, callback);
}

void AdsImpl::OnUserModelLoaded(const Result result, const std::string& json) {
  if (result == FAILED) {
    LOG(ERROR) << "Failed to load user model";

    return;
  }

  LOG(INFO) << "Successfully loaded user model";

  InitializeUserModel(json);

  if (!IsInitialized()) {
    InitializeStep3();
  }
}

void AdsImpl::InitializeUserModel(const std::string& json) {
  // TODO(Terry Mancey): Refactor function to use callbacks

  LOG(INFO) << "Initializing user model";

  user_model_.reset(usermodel::UserModel::CreateInstance());
  user_model_->InitializePageClassifier(json);

  LOG(INFO) << "Initialized user model";
}

bool AdsImpl::IsMobile() const {
  ClientInfo client_info;
  ads_client_->GetClientInfo(&client_info);

  if (client_info.platform != ANDROID_OS && client_info.platform != IOS) {
    return false;
  }

  return true;
}

void AdsImpl::OnForeground() {
  is_foreground_ = true;
  GenerateAdReportingForegroundEvent();
}

void AdsImpl::OnBackground() {
  is_foreground_ = false;
  GenerateAdReportingBackgroundEvent();
}

bool AdsImpl::IsForeground() const {
  return is_foreground_;
}

void AdsImpl::OnIdle() {
  // TODO(Terry Mancey): Implement Log (#44)
  // 'Idle state changed', { idleState: action.get('idleState') }

  LOG(INFO) << "Browser state changed to idle";
}

void AdsImpl::OnUnIdle() {
  // TODO(Terry Mancey): Implement Log (#44)
  // 'Idle state changed', { idleState: action.get('idleState') }

  LOG(INFO) << "Browser state changed to unidle";

  client_->UpdateLastUserIdleStopTime();

  if (IsMobile()) {
    return;
  }

  NotificationAllowedCheck(true);
}

void AdsImpl::OnMediaPlaying(const int32_t tab_id) {
  auto tab = media_playing_.find(tab_id);
  if (tab != media_playing_.end()) {
    // Media is already playing for this tab
    return;
  }

  LOG(INFO) << "OnMediaPlaying for tab id: " << tab_id;

  media_playing_.insert({tab_id, true});
}

void AdsImpl::OnMediaStopped(const int32_t tab_id) {
  auto tab = media_playing_.find(tab_id);
  if (tab == media_playing_.end()) {
    // Media is not playing for this tab
    return;
  }

  LOG(INFO) << "OnMediaStopped for tab id: " << tab_id;

  media_playing_.erase(tab_id);
}

bool AdsImpl::IsMediaPlaying() const {
  if (media_playing_.empty()) {
    return false;
  }

  return true;
}

void AdsImpl::TabUpdated(
    const int32_t tab_id,
    const std::string& url,
    const bool is_active,
    const bool is_incognito) {
  if (is_incognito) {
    return;
  }

  client_->UpdateLastUserActivity();

  LoadInfo load_info;
  load_info.tab_id = tab_id;
  load_info.tab_url = url;
  GenerateAdReportingLoadEvent(load_info);

  if (is_active) {
    LOG(INFO) << "TabUpdated.IsFocused for tab id: " << tab_id
      << " and url:" << url;

    last_shown_tab_url_ = url;

    TestShoppingData(url);
    TestSearchState(url);

    FocusInfo focus_info;
    focus_info.tab_id = tab_id;
    GenerateAdReportingFocusEvent(focus_info);
  } else {
    LOG(INFO) << "TabUpdated.IsBlurred for tab id: " << tab_id
      << " and url:" << url;

    BlurInfo blur_info;
    blur_info.tab_id = tab_id;
    GenerateAdReportingBlurEvent(blur_info);
  }
}

void AdsImpl::TabClosed(const int32_t tab_id) {
  LOG(INFO) << "TabClosed for tab id: " << tab_id;

  OnMediaStopped(tab_id);

  DestroyInfo destroy_info;
  destroy_info.tab_id = tab_id;
  GenerateAdReportingDestroyEvent(destroy_info);
}

void AdsImpl::RemoveAllHistory() {
  client_->RemoveAllHistory();

  ConfirmAdUUIDIfAdEnabled();
}

void AdsImpl::RetrieveSSID() {
  std::string ssid = ads_client_->GetSSID();
  if (ssid.empty()) {
    ssid = kUnknownSSID;
  }

  client_->SetCurrentSSID(ssid);
}

void AdsImpl::ConfirmAdUUIDIfAdEnabled() {
  if (!ads_client_->IsAdsEnabled()) {
    StopCollectingActivity();
    return;
  }

  client_->UpdateAdUUID();

  if (_is_debug) {
    StartCollectingActivity(kDebugOneHourInSeconds);
  } else {
    StartCollectingActivity(kOneHourInSeconds);
  }
}

void AdsImpl::ChangeLocale(const std::string& locale) {
  if (!IsInitialized()) {
    return;
  }

  auto locales = ads_client_->GetLocales();

  if (std::find(locales.begin(), locales.end(), locale) != locales.end()) {
    LOG(INFO) << "Change Localed to " << locale;
    client_->SetLocale(locale);
  } else {
    std::string closest_match_for_locale = "";
    auto language_code = helper::Locale::GetLanguageCode(locale);
    if (std::find(locales.begin(), locales.end(),
        language_code) != locales.end()) {
      closest_match_for_locale = language_code;
    } else {
      closest_match_for_locale = kDefaultLanguageCode;
    }

    LOG(INFO) << "Locale not found, so changed Locale to closest match: "
      << closest_match_for_locale;
    client_->SetLocale(closest_match_for_locale);
  }

  LoadUserModel();
}

void AdsImpl::ClassifyPage(const std::string& url, const std::string& html) {
  if (!IsInitialized()) {
    return;
  }

  if (!IsValidScheme(url)) {
    return;
  }

  TestShoppingData(url);
  TestSearchState(url);

  auto page_score = user_model_->ClassifyPage(html);
  last_page_classification_ = GetWinningCategory(page_score);

  client_->AppendPageScoreToPageScoreHistory(page_score);

  // TODO(Terry Mancey): Implement Log (#44)
  // 'Site visited', { url, immediateWinner, winnerOverTime }

  auto winner_over_time_category = GetWinnerOverTimeCategory();

  LOG(INFO) << "Site visited " << url << ", immediateWinner is "
    << last_page_classification_ << " and winnerOverTime is "
    << winner_over_time_category;
}

std::string AdsImpl::GetWinnerOverTimeCategory() {
  auto page_score_history = client_->GetPageScoreHistory();
  if (page_score_history.size() == 0) {
    return "";
  }

  uint64_t count = page_score_history.front().size();

  std::vector<double> winner_over_time_page_scores(count);
  std::fill(winner_over_time_page_scores.begin(),
    winner_over_time_page_scores.end(), 0);

  for (const auto& page_score : page_score_history) {
    if (page_score.size() != count) {
      return "";
    }

    for (size_t i = 0; i < page_score.size(); i++) {
      winner_over_time_page_scores[i] += page_score[i];
    }
  }

  return GetWinningCategory(winner_over_time_page_scores);
}

std::string AdsImpl::GetWinningCategory(const std::vector<double>& page_score) {
  return user_model_->WinningCategory(page_score);
}

void AdsImpl::CachePageScore(
    const std::string& url,
    const std::vector<double>& page_score) {
  auto cached_page_score = page_score_cache_.find(url);

  if (cached_page_score == page_score_cache_.end()) {
    page_score_cache_.insert({url, page_score});
  } else {
    cached_page_score->second = page_score;
  }
}

void AdsImpl::TestShoppingData(const std::string& url) {
  if (!IsInitialized()) {
    return;
  }

  UrlComponents components;
  if (!ads_client_->GetUrlComponents(url, &components)) {
    return;
  }

  // TODO(Terry Mancey): Confirm with product if this list should be expanded
  // to include amazon.co.uk and other territories
  if (components.hostname == "www.amazon.com") {
    client_->FlagShoppingState(url, 1.0);
  } else {
    client_->UnflagShoppingState();
  }
}

void AdsImpl::TestSearchState(const std::string& url) {
  if (!IsInitialized()) {
    return;
  }

  UrlComponents components;
  if (!ads_client_->GetUrlComponents(url, &components)) {
    return;
  }

  if (SearchProviders::IsSearchEngine(components)) {
    client_->FlagSearchState(url, 1.0);
  } else {
    client_->UnflagSearchState(url);
  }
}

void AdsImpl::ServeSampleAd() {
  if (!IsInitialized()) {
    return;
  }

  auto callback = std::bind(&AdsImpl::OnLoadSampleBundle, this, _1, _2);
  ads_client_->LoadSampleBundle(callback);
}

void AdsImpl::OnLoadSampleBundle(
    const Result result,
    const std::string& json) {
  if (result == FAILED) {
    LOG(ERROR) << "Failed to load sample bundle";
    return;
  }

  LOG(INFO) << "Successfully loaded sample bundle";

  BundleState sample_bundle_state;
  if (!sample_bundle_state.FromJson(json,
      ads_client_->LoadJsonSchema(_bundle_schema_name))) {
    LOG(ERROR) << "Failed to parse sample bundle: " << json;
    return;
  }

  // TODO(Terry Mancey): Sample bundle state should be persisted on the Client
  // in a database so that sample ads can be fetched from the database rather
  // than parsing the JSON each time, and be consistent with GetAds, therefore
  // the below code should be abstracted into GetAdForSampleCategory once the
  // necessary changes have been made in Brave Core by Brian Johnson

  auto categories = sample_bundle_state.categories.begin();
  auto categories_count = sample_bundle_state.categories.size();
  if (categories_count == 0) {
    // TODO(Terry Mancey): Implement Log (#44)
    // 'Notification not made', { reason: 'no categories' }

    LOG(INFO) << "Notification not made: No sample bundle categories";

    return;
  }

  auto category_rand = helper::Math::Random(categories_count - 1);
  std::advance(categories, static_cast<int64_t>(category_rand));

  auto category = categories->first;
  auto ads = categories->second;

  auto ads_count = ads.size();
  if (ads_count == 0) {
    // TODO(Terry Mancey): Implement Log (#44)
    // 'Notification not made', { reason: 'no ads for category', category }

    LOG(INFO) << "Notification not made: No sample bundle ads found for \""
      << category << "\" sample category";

    return;
  }

  auto ad_rand = helper::Math::Random(ads_count - 1);
  auto ad = ads.at(ad_rand);

  ShowAd(ad, category);
}

void AdsImpl::CheckEasterEgg(const std::string& url) {
  if (!_is_testing) {
    return;
  }

  UrlComponents components;
  if (!ads_client_->GetUrlComponents(url, &components)) {
    return;
  }

  auto now = helper::Time::Now();
  if (components.hostname == kEasterEggUrl && next_easter_egg_ < now) {
    LOG(INFO) << "Collect easter egg";

    CheckReadyAdServe(true);

    next_easter_egg_ = now + kNextEasterEggStartsInSeconds;
    LOG(INFO) << "Next easter egg available in " << next_easter_egg_
      << " seconds";
  }
}

void AdsImpl::CheckReadyAdServe(const bool forced) {
  if (!IsInitialized() || !bundle_->IsReady()) {
    LOG(INFO) << "Notification not made: Not initialized";

    return;
  }

  if (!forced) {
    if (!IsMobile() && !IsForeground()) {
      // TODO(Terry Mancey): Implement Log (#44)
      // 'Notification not made', { reason: 'not in foreground' }

      LOG(INFO) << "Notification not made: Not in foreground";

      return;
    }

    if (IsMediaPlaying()) {
      // TODO(Terry Mancey): Implement Log (#44)
      // 'Notification not made', { reason: 'media playing in browser' }

      LOG(INFO) << "Notification not made: Media playing in browser";

      return;
    }

    if (!IsAllowedToShowAds()) {
      // TODO(Terry Mancey): Implement Log (#44)
      // 'Notification not made', { reason: 'not allowed based on history' }

      LOG(INFO) << "Notification not made: Not allowed based on history";

      return;
    }
  }

  auto category = GetWinnerOverTimeCategory();
  ServeAdFromCategory(category);
}

void AdsImpl::ServeAdFromCategory(const std::string& category) {
  std::string catalog_id = bundle_->GetCatalogId();
  if (catalog_id.empty()) {
    // TODO(Terry Mancey): Implement Log (#44)
    // 'Notification not made', { reason: 'no ad catalog' }

    LOG(INFO) << "Notification not made: No ad catalog";

    return;
  }

  if (category.empty()) {
    // TODO(Terry Mancey): Implement Log (#44)
    // 'Notification not made', { reason: 'no ad (or permitted ad) for
    // winnerOverTime', category, winnerOverTime, arbitraryKey }

    LOG(INFO) << "Notification not made: No ad (or permitted ad) for \""
      << category << "\" category";

    return;
  }

  auto locale = ads_client_->GetAdsLocale();
  auto region = helper::Locale::GetCountryCode(locale);

  auto callback = std::bind(&AdsImpl::OnGetAds, this, _1, _2, _3, _4);
  ads_client_->GetAds(region, category, callback);
}

void AdsImpl::OnGetAds(
    const Result result,
    const std::string& region,
    const std::string& category,
    const std::vector<AdInfo>& ads) {
  if (result == FAILED) {
    auto pos = category.find_last_of('-');
    if (pos != std::string::npos) {
      std::string new_category = category.substr(0, pos);

      LOG(INFO) << "Notification not made: No ads found for \"" << category
        << "\" category, trying again with \"" << new_category << "\" category";

      auto callback = std::bind(&AdsImpl::OnGetAds, this, _1, _2, _3, _4);
      ads_client_->GetAds(region, new_category, callback);

      return;
    }

    if (ads.empty()) {
      // TODO(Terry Mancey): Implement Log (#44)
      // 'Notification not made', { reason: 'no ads for category', category }

      LOG(INFO) << "Notification not made: No ads found for \"" << category
        << "\" category";

      return;
    }
  }

  auto ads_unseen = GetUnseenAds(ads);
  if (ads_unseen.empty()) {
    // TODO(Terry Mancey): Implement Log (#44)
    // 'Ad round-robin', { category, adsSeen, adsNotSeen }

    LOG(INFO) << "Ad round-robin for \"" << category << "\" category";

    client_->ResetAdsUUIDSeen(ads);

    ads_unseen = GetUnseenAds(ads);
    if (ads_unseen.empty()) {
      // TODO(Terry Mancey): Implement Log (#44)
      // 'Notification not made', { reason: 'no ads for category', category }

      LOG(INFO) << "Notification not made: No ads found for \""
        << category << "\" category";

      return;
    }
  }

  auto rand = helper::Math::Random(ads_unseen.size() - 1);
  auto ad = ads_unseen.at(rand);
  ShowAd(ad, category);
}

std::vector<AdInfo> AdsImpl::GetUnseenAds(
    const std::vector<AdInfo>& ads) {
  auto ads_unseen = ads;
  auto ads_seen = client_->GetAdsUUIDSeen();

  auto iterator = std::remove_if(
    ads_unseen.begin(),
    ads_unseen.end(),
    [&](AdInfo &info) {
      return ads_seen.find(info.uuid) != ads_seen.end();
    });

  ads_unseen.erase(iterator, ads_unseen.end());

  return ads_unseen;
}

bool AdsImpl::IsAdValid(const AdInfo& ad_info) {
  if (ad_info.advertiser.empty() ||
      ad_info.notification_text.empty() ||
      ad_info.notification_url.empty()) {
    // TODO(Terry Mancey): Implement Log (#44)
    // 'Notification not made', { reason: 'incomplete ad information',
    // category, winnerOverTime, arbitraryKey, notificationUrl,
    // notificationText, advertiser

    LOG(INFO) << "Notification not made: Incomplete ad information for:"
      << std::endl << "  advertiser: " << ad_info.advertiser << std::endl
      << std::endl << "  notificationText: " << ad_info.notification_text
      << std::endl << "  notificationUrl: " << ad_info.notification_url
      << std::endl << "  creativeSetId: " << ad_info.notification_url
      << std::endl << "  uuid: " << ad_info.notification_url;

    return false;
  }

  return true;
}

bool AdsImpl::ShowAd(
    const AdInfo& ad_info,
    const std::string& category) {
  if (!IsAdValid(ad_info)) {
    return false;
  }

  auto notification_info = std::make_unique<NotificationInfo>();
  notification_info->advertiser = ad_info.advertiser;
  notification_info->category = category;
  notification_info->text = ad_info.notification_text;
  notification_info->url = helper::Uri::GetUri(ad_info.notification_url);
  notification_info->creative_set_id = ad_info.creative_set_id;
  notification_info->uuid = ad_info.uuid;

  last_shown_notification_info_ = NotificationInfo(*notification_info);

  // TODO(Terry Mancey): Implement Log (#44)
  // 'Notification shown', {category, winnerOverTime, arbitraryKey,
  // notificationUrl, notificationText, advertiser, uuid, hierarchy}

  LOG(INFO) << "Notification shown:"
    << std::endl << "  category: " << category
    << std::endl << "  winnerOverTime: " << GetWinnerOverTimeCategory()
    << std::endl << "  notificationUrl: " << notification_info->url
    << std::endl << "  notificationText: " << notification_info->text
    << std::endl << "  advertiser: " << notification_info->advertiser
    << std::endl << "  uuid: " << notification_info->uuid;

  ads_client_->ShowNotification(std::move(notification_info));

  client_->AppendCurrentTimeToAdsShownHistory();

  return true;
}

bool AdsImpl::AdsShownHistoryRespectsRollingTimeConstraint(
    const uint64_t seconds_window,
    const uint64_t allowable_ad_count) const {
  auto ads_shown_history = client_->GetAdsShownHistory();

  uint64_t recent_count = 0;
  auto now = helper::Time::Now();

  for (auto ad_shown : ads_shown_history) {
    if (now - ad_shown < seconds_window) {
      recent_count++;
    }
  }

  if (recent_count <= allowable_ad_count) {
    return true;
  }

  return false;
}

bool AdsImpl::IsAllowedToShowAds() {
  auto hour_window = kOneHourInSeconds;
  auto hour_allowed = ads_client_->GetAdsPerHour();
  auto respects_hour_limit = AdsShownHistoryRespectsRollingTimeConstraint(
    hour_window, hour_allowed);

  auto day_window = kOneHourInSeconds;
  auto day_allowed = ads_client_->GetAdsPerDay();
  auto respects_day_limit = AdsShownHistoryRespectsRollingTimeConstraint(
    day_window, day_allowed);

  auto minimum_wait_time = hour_window / hour_allowed;
  bool respects_minimum_wait_time =
    AdsShownHistoryRespectsRollingTimeConstraint(minimum_wait_time, 0);

  return respects_hour_limit &&
    respects_day_limit &&
    respects_minimum_wait_time;
}

void AdsImpl::StartCollectingActivity(const uint64_t start_timer_in) {
  StopCollectingActivity();

  collect_activity_timer_id_ = ads_client_->SetTimer(start_timer_in);
  if (collect_activity_timer_id_ == 0) {
    LOG(ERROR) << "Failed to start collecting activity due to an invalid timer";
    return;
  }

  LOG(INFO) << "Start collecting activity in " << start_timer_in << " seconds";
}

void AdsImpl::CollectActivity() {
  if (!IsInitialized()) {
    return;
  }

  LOG(INFO) << "Collect activity";

  ads_serve_->DownloadCatalog();
}

void AdsImpl::StopCollectingActivity() {
  if (!IsCollectingActivity()) {
    return;
  }

  LOG(INFO) << "Stopped collecting activity";

  ads_client_->KillTimer(collect_activity_timer_id_);
  collect_activity_timer_id_ = 0;
}

bool AdsImpl::IsCollectingActivity() const {
  if (collect_activity_timer_id_ == 0) {
    return false;
  }

  return true;
}


void AdsImpl::StartDeliveringNotifications(const uint64_t start_timer_in) {
  StopDeliveringNotifications();

  delivering_notifications_timer_id_ = ads_client_->SetTimer(start_timer_in);
  if (delivering_notifications_timer_id_ == 0) {
    LOG(ERROR) <<
      "Failed to start delivering notifications due to an invalid timer";
    return;
  }

  LOG(INFO) << "Start delivering notifications in "
    << start_timer_in << " seconds";
}

void AdsImpl::DeliverNotification() {
  NotificationAllowedCheck(true);

  if (IsMobile()) {
    StartDeliveringNotifications(kDeliverNotificationsAfterSeconds);
  }
}

void AdsImpl::StopDeliveringNotifications() {
  if (!IsDeliveringNotifications()) {
    return;
  }

  LOG(INFO) << "Stopped delivering notifications";

  ads_client_->KillTimer(delivering_notifications_timer_id_);
  delivering_notifications_timer_id_ = 0;
}

bool AdsImpl::IsDeliveringNotifications() const {
  if (delivering_notifications_timer_id_ == 0) {
    return false;
  }

  return true;
}

bool AdsImpl::IsCatalogOlderThanOneDay() {
  auto now = helper::Time::Now();

  auto catalog_last_updated_timestamp =
    bundle_->GetCatalogLastUpdatedTimestamp();

  if (catalog_last_updated_timestamp != 0 &&
      now > catalog_last_updated_timestamp + kOneDayInSeconds) {
    return true;
  }

  return false;
}

void AdsImpl::NotificationAllowedCheck(const bool serve) {
  auto ok = ads_client_->IsNotificationsAvailable();

  // TODO(Terry Mancey): Implement Log (#44)
  // appConstants.APP_ON_NATIVE_NOTIFICATION_AVAILABLE_CHECK, {err, result}

  auto previous = client_->GetAvailable();

  if (ok != previous) {
    client_->SetAvailable(ok);
  }

  if (!serve || ok != previous) {
    GenerateAdReportingSettingsEvent();
  }

  if (!serve) {
    return;
  }

  if (!ok) {
    // TODO(Terry Mancey): Implement Log (#44)
    // 'Ad not served', { reason: 'notifications not presently allowed' }

    LOG(INFO) << "Ad not served: Notifications not presently allowed";

    return;
  }

  if (!ads_client_->IsNetworkConnectionAvailable()) {
    // TODO(Terry Mancey): Implement Log (#44)
    // 'Ad not served', { reason: 'network connection not availaable' }

    LOG(INFO) << "Ad not served: Network connection not available";

    return;
  }

  if (IsCatalogOlderThanOneDay()) {
    // TODO(Terry Mancey): Implement Log (#44)
    // 'Ad not served', { reason: 'catalog older than one day' }

    LOG(INFO) << "Ad not served: Catalog older than one day";

    return;
  }

  CheckReadyAdServe(false);
}

void AdsImpl::StartSustainingAdInteraction(const uint64_t start_timer_in) {
  StopSustainingAdInteraction();

  sustained_ad_interaction_timer_id_ = ads_client_->SetTimer(start_timer_in);
  if (sustained_ad_interaction_timer_id_ == 0) {
    LOG(ERROR) <<
      "Failed to start sustaining ad interaction due to an invalid timer";
    return;
  }

  LOG(INFO) << "Start sustaining ad interaction in "
    << start_timer_in << " seconds";
}

void AdsImpl::SustainAdInteraction() {
  if (!IsStillViewingAd()) {
    return;
  }

  GenerateAdReportingSustainEvent(last_shown_notification_info_);
}

void AdsImpl::StopSustainingAdInteraction() {
  if (!IsSustainingAdInteraction()) {
    return;
  }

  LOG(INFO) << "Stopped sustaining ad interaction";

  ads_client_->KillTimer(sustained_ad_interaction_timer_id_);
  sustained_ad_interaction_timer_id_ = 0;
}

bool AdsImpl::IsSustainingAdInteraction() const {
  if (sustained_ad_interaction_timer_id_ == 0) {
    return false;
  }

  return true;
}

bool AdsImpl::IsStillViewingAd() const {
  if (last_shown_notification_info_.url != last_shown_tab_url_) {
    return false;
  }

  return true;
}

void AdsImpl::OnTimer(const uint32_t timer_id) {
  if (timer_id == collect_activity_timer_id_) {
    CollectActivity();
  } else if (timer_id == delivering_notifications_timer_id_) {
    DeliverNotification();
  } else if (timer_id == sustained_ad_interaction_timer_id_) {
    SustainAdInteraction();
  }
}

void AdsImpl::GenerateAdReportingNotificationShownEvent(
    const NotificationInfo& info) {
  if (is_first_run_) {
    is_first_run_ = false;

    GenerateAdReportingRestartEvent();
  }

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

  writer.StartObject();

  writer.String("data");
  writer.StartObject();

  writer.String("type");
  writer.String("notify");

  writer.String("stamp");
  std::string time_stamp = helper::Time::TimeStamp();
  writer.String(time_stamp.c_str());

  writer.String("notificationType");
  writer.String("generated");

  writer.String("notificationClassification");
  writer.StartArray();
  std::vector<std::string> classifications;
  helper::String::Split(info.category, '-', &classifications);
  for (const auto& classification : classifications) {
    writer.String(classification.c_str());
  }
  writer.EndArray();

  writer.String("notificationCatalog");
  if (info.creative_set_id.empty()) {
    writer.String("sample-catalog");
  } else {
    writer.String(info.creative_set_id.c_str());
  }

  writer.String("notificationUrl");
  writer.String(info.url.c_str());

  writer.EndObject();

  auto json = buffer.GetString();
  ads_client_->EventLog(json);
}

void AdsImpl::GenerateAdReportingNotificationResultEvent(
    const NotificationInfo& info,
    const NotificationResultInfoResultType type) {
  if (is_first_run_) {
    is_first_run_ = false;

    GenerateAdReportingRestartEvent();
  }

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

  writer.StartObject();

  writer.String("data");
  writer.StartObject();

  writer.String("type");
  writer.String("notify");

  writer.String("stamp");
  std::string time_stamp = helper::Time::TimeStamp();
  writer.String(time_stamp.c_str());

  writer.String("notificationType");
  switch (type) {
    case CLICKED: {
      writer.String("clicked");
      client_->UpdateAdsUUIDSeen(info.uuid, 1);

      StartSustainingAdInteraction(kSustainAdInteractionAfterSeconds);
      break;
    }

    case DISMISSED: {
      writer.String("dismissed");
      client_->UpdateAdsUUIDSeen(info.uuid, 1);
      break;
    }

    case TIMEOUT: {
      writer.String("timeout");
      break;
    }
  }

  writer.String("notificationClassification");
  writer.StartArray();
  std::vector<std::string> classifications;
  helper::String::Split(info.category, '-', &classifications);
  for (const auto& classification : classifications) {
    writer.String(classification.c_str());
  }
  writer.EndArray();

  writer.String("notificationCatalog");
  if (info.creative_set_id.empty()) {
    writer.String("sample-catalog");
  } else {
    writer.String(info.creative_set_id.c_str());
  }

  writer.String("notificationUrl");
  writer.String(info.url.c_str());

  writer.EndObject();

  auto json = buffer.GetString();
  ads_client_->EventLog(json);
}

void AdsImpl::GenerateAdReportingSustainEvent(
    const NotificationInfo& info) {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

  writer.StartObject();

  writer.String("data");
  writer.StartObject();

  writer.String("type");
  writer.String("sustain");

  writer.String("stamp");
  std::string time_stamp = helper::Time::TimeStamp();
  writer.String(time_stamp.c_str());

  writer.String("notificationId");
  writer.String(info.uuid.c_str());

  writer.String("notificationType");
  writer.String("viewed");

  writer.EndObject();

  auto json = buffer.GetString();
  ads_client_->EventLog(json);
}

void AdsImpl::GenerateAdReportingLoadEvent(
    const LoadInfo& info) {
  if (!IsValidScheme(info.tab_url)) {
    return;
  }

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

  writer.StartObject();

  writer.String("data");
  writer.StartObject();

  writer.String("type");
  writer.String("load");

  writer.String("stamp");
  std::string time_stamp = helper::Time::TimeStamp();
  writer.String(time_stamp.c_str());

  writer.String("tabId");
  writer.Int(info.tab_id);

  writer.String("tabType");
  if (client_->GetSearchState()) {
    writer.String("search");
  } else {
    writer.String("click");
  }

  writer.String("tabUrl");
  writer.String(info.tab_url.c_str());

  writer.String("tabClassification");
  writer.StartArray();
  std::vector<std::string> classifications;
  helper::String::Split(last_page_classification_, '-', &classifications);
  for (const auto& classification : classifications) {
    writer.String(classification.c_str());
  }
  writer.EndArray();

  auto cached_page_score = page_score_cache_.find(info.tab_url);
  if (cached_page_score != page_score_cache_.end()) {
    writer.String("pageScore");
    writer.StartArray();
    for (const auto& page_score : cached_page_score->second) {
      writer.Double(page_score);
    }
    writer.EndArray();
  }

  writer.EndObject();

  auto json = buffer.GetString();
  ads_client_->EventLog(json);

  CheckEasterEgg(info.tab_url);
}

void AdsImpl::GenerateAdReportingBackgroundEvent() {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

  writer.StartObject();

  writer.String("data");
  writer.StartObject();

  writer.String("type");
  writer.String("background");

  writer.String("stamp");
  std::string time_stamp = helper::Time::TimeStamp();
  writer.String(time_stamp.c_str());

  writer.String("place");
  auto place = client_->GetCurrentPlace();
  writer.String(place.c_str());

  writer.EndObject();

  auto json = buffer.GetString();
  ads_client_->EventLog(json);
}

void AdsImpl::GenerateAdReportingForegroundEvent() {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

  writer.StartObject();

  writer.String("data");
  writer.StartObject();

  writer.String("type");
  writer.String("foreground");

  writer.String("stamp");
  std::string time_stamp = helper::Time::TimeStamp();
  writer.String(time_stamp.c_str());

  writer.String("place");
  auto place = client_->GetCurrentPlace();
  writer.String(place.c_str());

  writer.EndObject();

  auto json = buffer.GetString();
  ads_client_->EventLog(json);
}

void AdsImpl::GenerateAdReportingBlurEvent(
    const BlurInfo& info) {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

  writer.StartObject();

  writer.String("data");
  writer.StartObject();

  writer.String("type");
  writer.String("blur");

  writer.String("stamp");
  std::string time_stamp = helper::Time::TimeStamp();
  writer.String(time_stamp.c_str());

  writer.String("tabId");
  writer.Int(info.tab_id);

  writer.EndObject();

  auto json = buffer.GetString();
  ads_client_->EventLog(json);
}

void AdsImpl::GenerateAdReportingDestroyEvent(
    const DestroyInfo& info) {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

  writer.StartObject();

  writer.String("data");
  writer.StartObject();

  writer.String("type");
  writer.String("destroy");

  writer.String("stamp");
  std::string time_stamp = helper::Time::TimeStamp();
  writer.String(time_stamp.c_str());

  writer.String("tabId");
  writer.Int(info.tab_id);

  writer.EndObject();

  auto json = buffer.GetString();
  ads_client_->EventLog(json);
}

void AdsImpl::GenerateAdReportingFocusEvent(
    const FocusInfo& info) {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

  writer.StartObject();

  writer.String("data");
  writer.StartObject();

  writer.String("type");
  writer.String("focus");

  writer.String("stamp");
  std::string time_stamp = helper::Time::TimeStamp();
  writer.String(time_stamp.c_str());

  writer.String("tabId");
  writer.Int(info.tab_id);

  writer.EndObject();

  auto json = buffer.GetString();
  ads_client_->EventLog(json);
}

void AdsImpl::GenerateAdReportingRestartEvent() {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

  writer.StartObject();

  writer.String("data");
  writer.StartObject();

  writer.String("type");
  writer.String("restart");

  writer.String("stamp");
  std::string time_stamp = helper::Time::TimeStamp();
  writer.String(time_stamp.c_str());

  writer.String("place");
  auto place = client_->GetCurrentPlace();
  writer.String(place.c_str());

  writer.EndObject();

  auto json = buffer.GetString();
  ads_client_->EventLog(json);
}

void AdsImpl::GenerateAdReportingSettingsEvent() {
  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);

  writer.StartObject();

  writer.String("data");
  writer.StartObject();

  writer.String("type");
  writer.String("settings");

  writer.String("stamp");
  std::string time_stamp = helper::Time::TimeStamp();
  writer.String(time_stamp.c_str());

  writer.String("settings");
  writer.StartObject();

  writer.String("notifications");
  writer.StartObject();

  writer.String("available");
  auto configured = ads_client_->IsNotificationsAvailable();
  writer.Bool(configured);

  writer.EndObject();

  writer.String("place");
  auto place = client_->GetCurrentPlace();
  writer.String(place.c_str());

  writer.String("locale");
  auto locale = client_->GetLocale();
  writer.String(locale.c_str());

  writer.String("adsPerDay");
  auto ads_per_day = ads_client_->GetAdsPerDay();
  writer.Uint64(ads_per_day);

  writer.String("adsPerHour");
  auto ads_per_hour = ads_client_->GetAdsPerHour();
  writer.Uint64(ads_per_hour);

  writer.EndObject();

  writer.EndObject();

  auto json = buffer.GetString();
  ads_client_->EventLog(json);
}

bool AdsImpl::IsValidScheme(const std::string& url) {
  UrlComponents components;
  if (!ads_client_->GetUrlComponents(url, &components)) {
    return false;
  }

  if (components.scheme != "http" && components.scheme != "https") {
    return false;
  }

  return true;
}

}  // namespace ads
