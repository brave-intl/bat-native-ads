/* This Source Code Form is subject to the terms of the Mozilla Public
* License, v. 2.0. If a copy of the MPL was not distributed with this
* file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "../include/url_request_handler.h"
#include "../include/ads.h"

namespace bat_ads {

URLRequestHandler::URLRequestHandler() {}

URLRequestHandler::~URLRequestHandler() {
  Clear();
}

void URLRequestHandler::Clear() {
  request_handlers_.clear();
}

void URLRequestHandler::OnURLRequestResponse(uint64_t request_id,
    const std::string& url, int response_code, const std::string& response,
    const std::map<std::string, std::string>& headers) {
  if (!RunRequestHandler(request_id, response_code == 200, response, headers)) {
    // LOG(ERROR) << "no request handler found for " << request_id;
    return;
  }

  if (ads::is_verbose) {
    // LOG(ERROR) << "[ RESPONSE ]";
    // LOG(ERROR) << "> url: " << url;
    // LOG(ERROR) << "> response: " << response;
    for (std::pair<std::string, std::string> const& value: headers) {
      // LOG(ERROR) << "> header: " << value.first << " | " << value.second;
    }
    // LOG(ERROR) << "[ END RESPONSE ]";
  }
}

bool URLRequestHandler::AddRequestHandler(
    std::unique_ptr<ads::AdsURLLoader> loader, URLRequestCallback callback) {
  uint64_t request_id = loader->request_id();
  if (request_handlers_.find(request_id) != request_handlers_.end())
    return false;

  request_handlers_[request_id] = callback;
  loader->Start();
  return true;
}

bool URLRequestHandler::RunRequestHandler(uint64_t request_id,
    bool success, const std::string& response,
    const std::map<std::string, std::string>& headers) {
  if (request_handlers_.find(request_id) == request_handlers_.end())
    return false;

  auto callback = request_handlers_[request_id];
  request_handlers_.erase(request_id);
  callback(success, response, headers);
  return true;
}

}  // namespace bat_ads
