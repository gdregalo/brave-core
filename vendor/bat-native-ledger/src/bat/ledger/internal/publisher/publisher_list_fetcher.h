/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BRAVELEDGER_PUBLISHER_PUBLISHER_LIST_FETCHER_H_
#define BRAVELEDGER_PUBLISHER_PUBLISHER_LIST_FETCHER_H_

#include <functional>

#include "base/timer/timer.h"
#include "bat/ledger/ledger.h"

namespace bat_ledger {
class LedgerImpl;
}

namespace braveledger_publisher {

class PublisherListFetcher {
 public:
  explicit PublisherListFetcher(bat_ledger::LedgerImpl* ledger);

  PublisherListFetcher(const PublisherListFetcher&) = delete;
  PublisherListFetcher& operator=(const PublisherListFetcher&) = delete;

  ~PublisherListFetcher();

  using FetchCallback = std::function<void()>;

  void Fetch(FetchCallback callback);

  void StartAutoUpdate();

  void StopAutoUpdate();

 private:
  void StartFetchTimer(
      const base::Location& posted_from,
      base::TimeDelta delay);

  void OnFetchTimerElapsed();

  void OnFetchCompleted(
      int response_status_code,
      const std::string& response,
      const std::map<std::string, std::string>& headers,
      FetchCallback callback);

  void OnDatabaseUpdated(ledger::Result result, FetchCallback callback);

  base::TimeDelta GetAutoUpdateDelay();

  bat_ledger::LedgerImpl* ledger_;  // NOT OWNED
  base::OneShotTimer timer_;
  bool auto_update_ = false;
};

}  // namespace braveledger_publisher

#endif  // BRAVELEDGER_PUBLISHER_PUBLISHER_LIST_FETCHER_H_
