/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "bat/ledger/internal/publisher/publisher_list_fetcher.h"

#include "bat/ledger/internal/ledger_impl.h"
#include "bat/ledger/internal/publisher/publisher_list_reader.h"
#include "bat/ledger/internal/state_keys.h"
#include "bat/ledger/option_keys.h"
#include "brave_base/random.h"
#include "net/http/http_status_code.h"

using std::placeholders::_1;
using std::placeholders::_2;
using std::placeholders::_3;

namespace {

constexpr double kRetryAfterFailureDelay = 150.0;

base::TimeDelta GetRetryAfterFailureDelay() {
  uint64_t seconds = brave_base::random::Geometric(kRetryAfterFailureDelay);
  return base::TimeDelta::FromSeconds(seconds);
}

}  // namespace

namespace braveledger_publisher {

PublisherListFetcher::PublisherListFetcher(bat_ledger::LedgerImpl* ledger)
    : ledger_(ledger) {}

PublisherListFetcher::~PublisherListFetcher() = default;

void PublisherListFetcher::Fetch(FetchCallback callback) {
  LOG(INFO) << "[[zenparsing]] Fetching list!";
  ledger_->SetUint64State(
      ledger::kStateServerPublisherListStamp,
      static_cast<uint64_t>(base::Time::Now().ToDoubleT()));

  // TODO(zenparsing): Get a real URL
  std::string url = "http://localhost:3000/publisher_list.pb";

  ledger_->LoadURL(
      url, {}, "", "",
      ledger::UrlMethod::GET,
      std::bind(&PublisherListFetcher::OnFetchCompleted,
          this, _1, _2, _3, callback));
}

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
  Fetch([]() {});
}

void PublisherListFetcher::OnFetchCompleted(
    int response_status_code,
    const std::string& response,
    const std::map<std::string, std::string>& headers,
    FetchCallback callback) {
  LOG(INFO) << "[[zenparsing]] Fetching completed!";
  if (response_status_code != net::HTTP_OK || response.empty()) {
    StartFetchTimer(FROM_HERE, GetRetryAfterFailureDelay());
    callback();
    return;
  }
  PublisherListReader reader;
  auto parse_error = reader.Parse(response);
  if (parse_error == PublisherListReader::ParseError::None) {
    LOG(INFO) << "[[zenparsing]] Updating database!";
    ledger_->ResetPublisherList(
      reader.begin(),
      reader.end(),
      std::bind(&PublisherListFetcher::OnDatabaseUpdated,
          this, _1, callback));
  }
  // TODO(zenparsing): Should we set this timer after the database
  // is updated, or here? What should the behavior be when the
  // database update fails for some reason? If we do a failure retry,
  // then we risk flooding the server. Instead, we should probably
  // leave the following as is and log the error.
  if (auto_update_) {
    StartFetchTimer(FROM_HERE, GetAutoUpdateDelay());
  }
  callback();
}

void PublisherListFetcher::OnDatabaseUpdated(
    ledger::Result result,
    FetchCallback callback) {
  // TODO(zenparsing)
  LOG(INFO) << "[[zenparsing]] Database updated!";
  callback();
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

}  // namespace braveledger_publisher
