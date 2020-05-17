/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BRAVELEDGER_PUBLISHER_SERVER_PUBLISHER_FETCHER_H_
#define BRAVELEDGER_PUBLISHER_SERVER_PUBLISHER_FETCHER_H_

#include <string>
#include <map>
#include <vector>

#include "bat/ledger/ledger.h"

namespace bat_ledger {
class LedgerImpl;
}

namespace braveledger_publisher {

class ServerPublisherFetcher {
 public:
  explicit ServerPublisherFetcher(bat_ledger::LedgerImpl* ledger);

  ServerPublisherFetcher(const ServerPublisherFetcher&) = delete;
  ServerPublisherFetcher& operator=(const ServerPublisherFetcher&) = delete;

  ~ServerPublisherFetcher();

  bool IsExpired(ledger::ServerPublisherInfo* server_info);

  void Fetch(
      const std::string& publisher_key,
      ledger::GetServerPublisherInfoCallback callback);

 private:
  using CallbackMap =
      std::multimap<std::string, ledger::GetServerPublisherInfoCallback>;

  using CallbackVector =
      std::vector<ledger::GetServerPublisherInfoCallback>;

  void OnFetchCompleted(
      const std::string& publisher_key,
      int response_status_code,
      const std::string& response,
      const std::map<std::string, std::string>& headers);

  ledger::ServerPublisherInfoPtr ParseResponse(
      const std::string& publisher_key,
      int response_status_code,
      const std::string& response);

  CallbackVector GetCallbacks(const std::string& publisher_key);

  void RunCallbacks(
      const std::string& publisher_key,
      ledger::ServerPublisherInfoPtr server_info);

  bat_ledger::LedgerImpl* ledger_;  // NOT OWNED
  CallbackMap callback_map_;
};

}  // namespace braveledger_publisher

#endif  // BRAVELEDGER_PUBLISHER_SERVER_PUBLISHER_FETCHER_H_
