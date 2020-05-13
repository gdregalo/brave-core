/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "bat/ledger/internal/publisher/server_publisher_fetcher.h"

#include <utility>

#include "base/json/json_reader.h"
#include "base/strings/stringprintf.h"
#include "base/time/time.h"
#include "bat/ledger/internal/ledger_impl.h"
#include "bat/ledger/internal/publisher/channel_responses.pb.h"
#include "bat/ledger/internal/publisher/prefix_util.h"
#include "net/http/http_status_code.h"

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

namespace {

constexpr size_t kQueryHashPrefixSize = 2;

// TODO(zenparsing): This should probably be an option in option_keys.h
constexpr int64_t kServerInfoExpiresSeconds = 60 * 60 * 5;

/*
ledger::PublisherStatus PublisherStatusFromString(const std::string& str) {
  return
      str == "publisher_verified" ? ledger::PublisherStatus::CONNECTED :
      str == "wallet_connected" ? ledger::PublisherStatus::VERIFIED :
      ledger::PublisherStatus::NOT_VERIFIED;
}

ledger::PublisherBannerPtr PublisherBannerFromValue(const base::Value& value) {
  if (!value.is_dict()) {
    return nullptr;
  }

  auto banner = ledger::PublisherBanner::New();

  auto* title = value.FindStringKey("title");
  if (title) {
    banner->title = *title;
  }

  auto* description = value.FindStringKey("description");
  if (description) {
    banner->description = *description;
  }

  auto* background = value.FindStringKey("backgroundUrl");
  if (background && !background->empty()) {
    banner->background = "chrome://rewards-image/" + *background;
  }

  auto* logo = value.FindStringKey("logoUrl");
  if (logo && !logo->empty()) {
    banner->logo = "chrome://rewards-image/" + *logo;
  }

  auto* amounts = value.FindListKey("donationAmounts");
  if (amounts) {
    for (auto& item : amounts->GetList()) {
      if (item.is_int()) {
        banner->amounts.push_back(item.GetInt());
      }
    }
  }

  auto* links = value.FindDictKey("socialLinks");
  if (links) {
    for (auto item : links->DictItems()) {
      if (item.second.is_string()) {
        banner->links.insert(
            std::make_pair(item.first, item.second.GetString()));
      }
    }
  }

  return banner;
}

ledger::ServerPublisherInfoPtr ServerPublisherInfoFromValue(
    const base::Value& value,
    const std::string& expected_key) {
  if (!value.is_list()) {
    return nullptr;
  }

  auto list = value.GetList();
  if (list.size() < 5) {
    return nullptr;
  }

  auto& publisher_key = list[0];
  auto& status = list[1];
  auto& excluded = list[2];
  auto& address = list[3];
  auto& banner = list[4];

  if (!publisher_key.is_string() ||
      !status.is_string() ||
      !excluded.is_bool() ||
      !address.is_string()) {
    return nullptr;
  }

  auto server_info = ledger::ServerPublisherInfo::New();
  server_info->publisher_key = publisher_key.GetString();
  server_info->status = PublisherStatusFromString(status.GetString());
  server_info->excluded = excluded.GetBool();
  server_info->address = address.GetString();
  server_info->updated_at =
      static_cast<uint64_t>(base::Time::Now().ToDoubleT());

  if (server_info->publisher_key != expected_key) {
    return nullptr;
  }

  if (auto server_banner = PublisherBannerFromValue(banner)) {
    server_info->banner = std::move(server_banner);
  }

  return server_info;
}

ledger::ServerPublisherInfoPtr ServerPublisherInfoFromResponse(
    const std::string& response,
    const std::string& expected_key) {
  auto entries = base::JSONReader::Read(response);
  if (!entries || !entries->is_list()) {
    return nullptr;
  }

  for (auto& value : entries->GetList()) {
    auto server_info = ServerPublisherInfoFromValue(value, expected_key);
    if (server_info) {
      return server_info;
    }
  }

  return nullptr;
}
*/

ledger::PublisherStatus PublisherStatusFromMessage(
    const publishers_pb::ChannelResponse& response) {
  // TODO(zenparsing): Verify these mappings
  switch (response.wallet_connected_state()) {
    case publishers_pb::UPHOLD_ACCOUNT_KYC:
      return ledger::PublisherStatus::CONNECTED;
    case publishers_pb::UPHOLD_ACCOUNT_NO_KYC:
      return ledger::PublisherStatus::VERIFIED;
    default:
      return ledger::PublisherStatus::NOT_VERIFIED;
  }
}

ledger::PublisherBannerPtr PublisherBannerFromMessage(
    const publishers_pb::SiteBannerDetails& banner_details) {
  auto banner = ledger::PublisherBanner::New();

  banner->title = banner_details.title();
  banner->description = banner_details.description();

  if (!banner_details.background_url().empty()) {
    banner->background =
        "chrome://rewards-image/" + banner_details.background_url();
  }

  if (!banner_details.logo_url().empty()) {
    banner->logo = "chrome://rewards-image/" + banner_details.logo_url();
  }

  for (auto& amount : banner_details.donation_amounts()) {
    banner->amounts.push_back(amount);
  }

  if (banner_details.has_social_links()) {
    auto& links = banner_details.social_links();
    if (!links.youtube().empty()) {
      banner->links.insert(std::make_pair("youtube", links.youtube()));
    }
    if (!links.twitter().empty()) {
      banner->links.insert(std::make_pair("twitter", links.twitter()));
    }
    if (!links.twitch().empty()) {
      banner->links.insert(std::make_pair("twitch", links.twitch()));
    }
  }

  return banner;
}

ledger::ServerPublisherInfoPtr ServerPublisherInfoFromString(
    const std::string& response,
    const std::string& expected_key) {
  publishers_pb::ChannelResponses channel_responses;
  if (!channel_responses.ParseFromString(response)) {
    // TODO(zenparsing): Log error
    return nullptr;
  }

  for (auto& entry : channel_responses.channel_response()) {
    if (entry.channel_identifier() != expected_key) {
      continue;
    }

    auto server_info = ledger::ServerPublisherInfo::New();
    server_info->publisher_key = entry.channel_identifier();
    server_info->status = PublisherStatusFromMessage(entry);
    // TODO(zenparsing): Do we need excluded?
    server_info->address = entry.wallet_address();
    server_info->updated_at =
        static_cast<uint64_t>(base::Time::Now().ToDoubleT());

    if (entry.has_site_banner_details()) {
      server_info->banner =
          PublisherBannerFromMessage(entry.site_banner_details());
    }

    return server_info;
  }

  return nullptr;
}

}  // namespace

namespace braveledger_publisher {

ServerPublisherFetcher::ServerPublisherFetcher(
    bat_ledger::LedgerImpl* ledger)
    : ledger_(ledger) {
  DCHECK(ledger);
}

ServerPublisherFetcher::~ServerPublisherFetcher() = default;

void ServerPublisherFetcher::Fetch(
    const std::string& publisher_key,
    ledger::GetServerPublisherInfoCallback callback) {
  // TODO(zenparsing): These called need to be deduped somehow.
  // We could store publisher_keys and a list of callbacks in
  // a map.
  std::string prefix = GetHashPrefixInHex(
      publisher_key,
      kQueryHashPrefixSize);

  std::string url = base::StringPrintf(
      "http://localhost:3000/channel/%s",
      prefix.c_str());

  // TODO(zenparsing): Leave note about request length and privacy
  ledger_->LoadURL(
      url, {}, "", "",
      ledger::UrlMethod::GET,
      std::bind(&ServerPublisherFetcher::OnFetchCompleted,
          this, publisher_key, _1, _2, _3, callback));
}

void ServerPublisherFetcher::OnFetchCompleted(
    const std::string& publisher_key,
    int response_status_code,
    const std::string& response,
    const std::map<std::string, std::string>& headers,
    ledger::GetServerPublisherInfoCallback callback) {
  if (response_status_code != net::HTTP_OK || response.empty()) {
    // TODO(zenparsing): Log error? 404 is expected if there aren't
    // any channels with a matching prefix.
    callback(nullptr);
    return;
  }

  auto server_info = ServerPublisherInfoFromString(response, publisher_key);
  if (server_info) {
    ledger_->InsertServerPublisherInfo(*server_info, [](ledger::Result) {});
  }

  // TODO(zenparsing): If not found in the response, should we remove
  // the publisher from the prefix list so that we don't attempt to query
  // again?

  callback(std::move(server_info));
}

bool ServerPublisherFetcher::IsExpired(
    ledger::ServerPublisherInfo* server_info) {
  if (!server_info) {
    return true;
  }

  base::TimeDelta age =
      base::Time::Now() -
      base::Time::FromDoubleT(server_info->updated_at);

  if (age.InSeconds() < 0) {
    // TODO(zenparsing): A negative number here indicates that we
    // have a problem with how we are storing the time, but could
    // also indicate that the data is corrupted somehow. How should
    // we handle this? If the data is just corrupted, then we should
    // refresh. If we have a problem storing the data then we're
    // screwed and our refresh mechanism will never work. Is the
    // following good enough? It could lead to a situation in release
    // where we never refresh any records. Perhaps we should also
    // log the error somehow?
    NOTREACHED();
  }

  return age.InSeconds() > kServerInfoExpiresSeconds;
}

}  // namespace braveledger_publisher
