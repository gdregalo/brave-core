/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BRAVELEDGER_DATABASE_DATABASE_PUBLISHER_LIST_H_
#define BRAVELEDGER_DATABASE_DATABASE_PUBLISHER_LIST_H_

#include <functional>
#include <string>

#include "bat/ledger/internal/database/database_table.h"
#include "bat/ledger/internal/publisher/prefix_iterator.h"

namespace braveledger_database {

class DatabasePublisherList: public DatabaseTable {
 public:
  explicit DatabasePublisherList(bat_ledger::LedgerImpl* ledger);
  ~DatabasePublisherList() override;

  bool Migrate(ledger::DBTransaction* transaction, int target) override;

  void InsertPrefixes(
      braveledger_publisher::PrefixIterator begin,
      braveledger_publisher::PrefixIterator end,
      ledger::ResultCallback callback);

  void RemovePrefixes(
      braveledger_publisher::PrefixIterator begin,
      braveledger_publisher::PrefixIterator end,
      ledger::ResultCallback callback);

  void ResetPrefixes(
      braveledger_publisher::PrefixIterator begin,
      braveledger_publisher::PrefixIterator end,
      ledger::ResultCallback callback);

  void Search(
      const std::string& prefix,
      ledger::SearchPublisherListCallback callback);

 private:
  void MigrateToV21(ledger::DBTransaction* transaction);

  void OnSearchResult(
      ledger::DBCommandResponsePtr response,
      ledger::SearchPublisherListCallback callback);
};

}  // namespace braveledger_database

#endif  // BRAVELEDGER_DATABASE_DATABASE_PUBLISHER_LIST_H_
