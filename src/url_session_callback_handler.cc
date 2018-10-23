/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "url_session_callback_handler.h"
#include "ads.h"

namespace ads {

URLSessionCallbackHandler::URLSessionCallbackHandler() {}

URLSessionCallbackHandler::~URLSessionCallbackHandler() {
  Clear();
}

void URLSessionCallbackHandler::Clear() {
  url_session_callback_handlers_.clear();
}

bool URLSessionCallbackHandler::AddCallbackHandler(
    const std::unique_ptr<URLSession> url_session,
    const URLSessionCallbackHandlerCallback callback) {
  auto session_id = url_session->GetSessionId();
  if (url_session_callback_handlers_.find(session_id) !=
      url_session_callback_handlers_.end()) {
    return false;
  }

  url_session_callback_handlers_[session_id] = callback;
  url_session->Start();

  return true;
}

bool URLSessionCallbackHandler::RunCallbackHandler(
    const uint64_t session_id,
    const int response_status_code,
    const std::string& response,
    const std::map<std::string, std::string>& headers) {
  if (url_session_callback_handlers_.find(session_id) ==
      url_session_callback_handlers_.end()) {
    return false;
  }

  auto callback = url_session_callback_handlers_[session_id];
  url_session_callback_handlers_.erase(session_id);

  callback(response_status_code, response, headers);

  return true;
}

bool URLSessionCallbackHandler::OnURLSessionReceivedResponse(
    const uint64_t session_id,
    const std::string& url,
    const int response_status_code,
    const std::string& response,
    const std::map<std::string, std::string>& headers) {
  if (!RunCallbackHandler(session_id, response_status_code,
      response, headers)) {
    return false;
  }

  return true;
}

}  // namespace ads