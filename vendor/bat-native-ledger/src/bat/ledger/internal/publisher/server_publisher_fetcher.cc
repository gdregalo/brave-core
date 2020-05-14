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
#include "bat/ledger/internal/request/request_publisher.h"
#include "net/http/http_status_code.h"

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

namespace {

constexpr size_t kQueryHashPrefixSize = 2;

// TODO(zenparsing): This should probably be an option in option_keys.h
constexpr int64_t kServerInfoExpiresSeconds = 60 * 60 * 5;

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
    // TODO(zenparsing): Do we need "excluded" field anymore?
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
  // TODO(zenparsing): These calls need to be deduped somehow.
  // We could store publisher_keys and a list of callbacks in
  // a map.
  std::string url = braveledger_request_util::GetPublisherInfoUrl(
      GetHashPrefixInHex(publisher_key, kQueryHashPrefixSize));

  // TODO(zenparsing): Leave note about request length and privacy.
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
