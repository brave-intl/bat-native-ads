/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "bat/ads/issuers_info.h"

#include "json_helper.h"

namespace ads {

IssuersInfo::IssuersInfo() :
    public_key(""),
    issuers({}) {}

IssuersInfo::IssuersInfo(const IssuersInfo& info) :
    public_key(info.public_key),
    issuers(info.issuers) {}

IssuersInfo::~IssuersInfo() = default;

const std::string IssuersInfo::ToJson() const {
  std::string json;
  SaveToJson(*this, &json);
  return json;
}

bool IssuersInfo::FromJson(const std::string& json) {
  rapidjson::Document document;
  document.Parse(json.c_str());

  if (document.HasParseError()) {
    return false;
  }

  // Public key
  if (!document.HasMember("public_key")) {
    return false;
  }

  public_key = document["public_key"].GetString();

  // Issuers
  if (!document.HasMember("issuers")) {
    return false;
  }

  std::vector<IssuerInfo> issuers = {};
  for (const auto& issuer : document["issuers"].GetArray()) {
    IssuerInfo info;
    info.name = issuer["name"].GetString();
    info.public_key = issuer["public_key"].GetString();

    issuers.push_back(info);
  }

  return true;
}

void SaveToJson(JsonWriter* writer, const IssuersInfo& info) {
  writer->StartObject();

  // Public key
  writer->String("public_key");
  writer->String(info.public_key.c_str());

  // Issuers
  writer->String("issuers");
  writer->StartArray();
  for (const auto& issuer : info.issuers) {
    writer->StartObject();

    writer->String("name");
    writer->String(issuer.name.c_str());

    writer->String("public_key");
    writer->String(issuer.public_key.c_str());

    writer->EndObject();
  }
  writer->EndArray();

  writer->EndObject();
}

}  // namespace ads
