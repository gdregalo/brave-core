/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "bat/ledger/internal/publisher/server_publisher_fetcher.h"

#include <utility>

#include "base/big_endian.h"
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
  switch (response.wallet_connected_state()) {
    case publishers_pb::UPHOLD_ACCOUNT_KYC:
      return ledger::PublisherStatus::VERIFIED;
    case publishers_pb::UPHOLD_ACCOUNT_NO_KYC:
      return ledger::PublisherStatus::CONNECTED;
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

ledger::ServerPublisherInfoPtr ServerPublisherInfoFromMessage(
    const publishers_pb::ChannelResponses& message,
    const std::string& expected_key) {
  for (auto& entry : message.channel_response()) {
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

// TODO(zenparsing): This is actually in components/brave_private_cdn
// but I'm not sure how we can use it from here.
bool RemovePadding(std::string* padded_string) {
  if (!padded_string) {
    return false;
  }

  if (padded_string->size() < sizeof(uint32_t)) {
    return false;  // Missing length field
  }

  // Read payload length from the header.
  uint32_t data_length;
  base::ReadBigEndian(padded_string->c_str(), &data_length);

  // Remove length header.
  padded_string->erase(0, sizeof(uint32_t));
  if (padded_string->size() < data_length) {
    return false;  // Payload shorter than expected length
  }

  // Remove padding.
  padded_string->resize(data_length);
  return true;
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
  bool has_entry = callback_map_.count(publisher_key) > 0;
  callback_map_.insert(std::make_pair(publisher_key, callback));
  if (has_entry) {
    LOG(INFO) << "[[zenparsing]] deduping request for " << publisher_key;
    return;
  }

  std::string url = braveledger_request_util::GetPublisherInfoUrl(
      GetHashPrefixInHex(publisher_key, kQueryHashPrefixSize));

  // TODO(zenparsing): Leave note about request length and privacy.
  ledger_->LoadURL(
      url, {}, "", "",
      ledger::UrlMethod::GET,
      std::bind(&ServerPublisherFetcher::OnFetchCompleted,
          this, publisher_key, _1, _2, _3));
}

void ServerPublisherFetcher::OnFetchCompleted(
    const std::string& publisher_key,
    int response_status_code,
    const std::string& response,
    const std::map<std::string, std::string>& headers) {
  auto server_info = ParseResponse(
      publisher_key,
      response_status_code,
      response);

  if (server_info) {
    ledger_->InsertServerPublisherInfo(*server_info, [](ledger::Result) {});
  }

  // TODO(zenparsing): If not found in the response, should we remove
  // the publisher from the prefix list so that we don't attempt to query
  // again?

  RunCallbacks(publisher_key, std::move(server_info));
}

ledger::ServerPublisherInfoPtr ServerPublisherFetcher::ParseResponse(
    const std::string& publisher_key,
    int response_status_code,
    const std::string& response) {
  if (response_status_code != net::HTTP_OK || response.empty()) {
    if (response_status_code != net::HTTP_NOT_FOUND) {
      // TODO(zenparsing): Log error - unexpected server response
    }
    return nullptr;
  }

  std::string response_data = response;
  if (!RemovePadding(&response_data)) {
    // TODO(zenparsing): Log error - invalid padding
    return nullptr;
  }

  publishers_pb::ChannelResponses message;
  if (!message.ParseFromString(response_data)) {
    // TODO(zenparsing): Log error - unable to parse protobuf
    return nullptr;
  }

  return ServerPublisherInfoFromMessage(message, publisher_key);
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
    // TODO(zenparsing): Log error
    // A negative age value indicates that either the data is
    // corrupted or that we are incorrectly storing the timestamp.
    // Pessimistically assume that we are incorrectly storing
    // the timestamp in order to avoid a case where we fetch
    // on every tab update.
  }

  return age.InSeconds() > kServerInfoExpiresSeconds;
}

ServerPublisherFetcher::CallbackVector ServerPublisherFetcher::GetCallbacks(
    const std::string& publisher_key) {
  CallbackVector callbacks;
  auto range = callback_map_.equal_range(publisher_key);
  for (auto iter = range.first; iter != range.second; ++iter) {
    callbacks.push_back(std::move(iter->second));
  }
  callback_map_.erase(range.first, range.second);
  return callbacks;
}

void ServerPublisherFetcher::RunCallbacks(
    const std::string& publisher_key,
    ledger::ServerPublisherInfoPtr server_info) {
  CallbackVector callbacks = GetCallbacks(publisher_key);
  DCHECK(!callbacks.empty());
  for (auto& callback : callbacks) {
    callback(server_info ? server_info->Clone() : nullptr);
  }
}

}  // namespace braveledger_publisher
