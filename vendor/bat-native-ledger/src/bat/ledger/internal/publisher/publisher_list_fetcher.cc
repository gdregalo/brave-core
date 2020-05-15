/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "bat/ledger/internal/publisher/publisher_list_fetcher.h"

#include "bat/ledger/internal/ledger_impl.h"
#include "bat/ledger/internal/publisher/publisher_list_reader.h"
#include "bat/ledger/internal/request/request_publisher.h"
#include "bat/ledger/internal/state_keys.h"
#include "bat/ledger/option_keys.h"
#include "brave_base/random.h"
#include "net/http/http_status_code.h"

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

namespace {

constexpr double kRetryAfterFailureDelay = 150.0;
constexpr uint64_t kMaxRetryAfterFailureDelay = 60 * 60 * 4;

}  // namespace

namespace braveledger_publisher {

PublisherListFetcher::PublisherListFetcher(bat_ledger::LedgerImpl* ledger)
    : ledger_(ledger) {}

PublisherListFetcher::~PublisherListFetcher() = default;

void PublisherListFetcher::StartAutoUpdate() {
  auto_update_ = true;
  if (!timer_.IsRunning()) {
    StartFetchTimer(FROM_HERE, GetAutoUpdateDelay());
  }
}

void PublisherListFetcher::StopAutoUpdate() {
  auto_update_ = false;
  timer_.Stop();
}

void PublisherListFetcher::StartFetchTimer(
    const base::Location& posted_from,
    base::TimeDelta delay) {
  timer_.Start(posted_from, delay, base::BindOnce(
      &PublisherListFetcher::OnFetchTimerElapsed,
      base::Unretained(this)));
}

void PublisherListFetcher::OnFetchTimerElapsed() {
  std::string url = braveledger_request_util::GetPublisherListUrl();
  ledger_->LoadURL(
      url, {}, "", "",
      ledger::UrlMethod::GET,
      std::bind(&PublisherListFetcher::OnFetchCompleted, this, _1, _2, _3));
}

void PublisherListFetcher::OnFetchCompleted(
    int response_status_code,
    const std::string& response,
    const std::map<std::string, std::string>& headers) {
  if (response_status_code != net::HTTP_OK || response.empty()) {
    // TODO(zenparsing): Log error - invalid server response
    StartFetchTimer(FROM_HERE, GetRetryAfterFailureDelay());
    return;
  }

  PublisherListReader reader;
  auto parse_error = reader.Parse(response);
  if (parse_error != PublisherListReader::ParseError::None) {
    // TODO(zenparsing): Log error - invalid protobuf message.
    // This could be a problem on the client or the server, but
    // optimistically assume that it's a server issue and retry
    // with back-off.
    StartFetchTimer(FROM_HERE, GetRetryAfterFailureDelay());
    return;
  }

  // At this point we have received a valid response from the server.
  // Store last successful fetch time for calculation of next refresh
  // interval.
  ledger_->SetUint64State(
      ledger::kStateServerPublisherListStamp,
      static_cast<uint64_t>(base::Time::Now().ToDoubleT()));

  retry_count_ = 0;

  ledger_->ResetPublisherList(
      reader.begin(),
      reader.end(),
      std::bind(&PublisherListFetcher::OnDatabaseUpdated, this, _1));

  if (auto_update_) {
    StartFetchTimer(FROM_HERE, GetAutoUpdateDelay());
  }
}

void PublisherListFetcher::OnDatabaseUpdated(ledger::Result result) {
  // TODO(zenparsing): Log error - Unable to update database
}

base::TimeDelta PublisherListFetcher::GetAutoUpdateDelay() {
  uint64_t last_fetch_sec =
      ledger_->GetUint64State(ledger::kStateServerPublisherListStamp);
  uint64_t interval_sec =
      ledger_->GetUint64Option(ledger::kOptionPublisherListRefreshInterval);

  auto now = base::Time::Now();
  auto fetch_time = base::Time::FromDoubleT(
      static_cast<double>(last_fetch_sec));

  if (fetch_time > now) {
    fetch_time = now;
  }

  fetch_time += base::TimeDelta::FromSeconds(interval_sec);
  return fetch_time < now
      ? base::TimeDelta::FromSeconds(0)
      : fetch_time - now;
}

base::TimeDelta PublisherListFetcher::GetRetryAfterFailureDelay() {
  uint64_t seconds = brave_base::random::Geometric(kRetryAfterFailureDelay);
  seconds <<= retry_count_;
  retry_count_ += 1;
  if (seconds > kMaxRetryAfterFailureDelay) {
    seconds = kMaxRetryAfterFailureDelay;
  }
  return base::TimeDelta::FromSeconds(seconds);
}

}  // namespace braveledger_publisher
