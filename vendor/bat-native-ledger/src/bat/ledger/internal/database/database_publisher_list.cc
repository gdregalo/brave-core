/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "bat/ledger/internal/database/database_publisher_list.h"

#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "bat/ledger/internal/database/database_util.h"
#include "bat/ledger/internal/ledger_impl.h"
#include "bat/ledger/internal/state_keys.h"

using std::placeholders::_1;
using braveledger_publisher::PrefixIterator;

namespace {

const char kTableName[] = "publisher_list";

void DropAndCreateTableV21(ledger::DBTransaction* transaction) {
  DCHECK(transaction);

  if (!braveledger_database::DropTable(transaction, kTableName)) {
    NOTREACHED();
  }

  auto command = ledger::DBCommand::New();
  command->type = ledger::DBCommand::Type::EXECUTE;
  command->command = base::StringPrintf(
      "CREATE TABLE %s "
      "(hash_prefix BLOB PRIMARY KEY NOT NULL)",
      kTableName);

  transaction->commands.push_back(std::move(command));
}

void AddInsertCommand(
    const std::string& value_list,
    ledger::DBTransaction* transaction) {
  DCHECK(transaction);

  auto command = ledger::DBCommand::New();
  command->type = ledger::DBCommand::Type::RUN;
  command->command = base::StringPrintf(
      "INSERT OR REPLACE INTO %s (hash_prefix) VALUES %s",
      kTableName,
      value_list.data());

  transaction->commands.push_back(std::move(command));
}

void AddDeleteCommand(
    const std::string& value_list,
    ledger::DBTransaction* transaction) {
  DCHECK(transaction);

  auto command = ledger::DBCommand::New();
  command->type = ledger::DBCommand::Type::RUN;
  command->command = base::StringPrintf(
      "DELETE FROM %s WHERE hash_prefix IN %s",
      kTableName,
      value_list.data());

  transaction->commands.push_back(std::move(command));
}

std::string GetPrefixInsertList(PrefixIterator begin, PrefixIterator end) {
  std::string values;
  for (auto iter = begin; iter != end; ++iter) {
    auto prefix = *iter;
    values.append(iter == begin ? "(x'" : "'),(x'");
    values.append(base::HexEncode(prefix.data(), prefix.length()));
  }
  values.append("')");
  return values;
}

std::string GetPrefixSearchList(PrefixIterator begin, PrefixIterator end) {
  std::string values;
  for (auto iter = begin; iter != end; ++iter) {
    auto prefix = *iter;
    values.append(iter == begin ? "(x'" : "',x'");
    values.append(base::HexEncode(prefix.data(), prefix.length()));
  }
  values.append("')");
  return values;
}

}  // namespace

namespace braveledger_database {

DatabasePublisherList::DatabasePublisherList(bat_ledger::LedgerImpl* ledger)
    : DatabaseTable(ledger) {}

DatabasePublisherList::~DatabasePublisherList() = default;

bool DatabasePublisherList::Migrate(
    ledger::DBTransaction* transaction,
    int target) {
  switch (target) {
    case 21:
      MigrateToV21(transaction);
      return true;
    default:
      return true;
  }
}

void DatabasePublisherList::MigrateToV21(
    ledger::DBTransaction* transaction) {
  DropAndCreateTableV21(transaction);
  ledger_->ClearState(ledger::kStateServerPublisherListStamp);
}

void DatabasePublisherList::Search(
    const std::string& prefix,
    ledger::SearchPublisherListCallback callback) {
  auto command = ledger::DBCommand::New();
  command->type = ledger::DBCommand::Type::READ;
  command->command = base::StringPrintf(
      "SELECT COUNT(*) as count FROM %s WHERE hash_prefix = ?",
      kTableName);

  BindString(command.get(), 0, prefix);
  command->record_bindings = {
    ledger::DBCommand::RecordBindingType::INT_TYPE
  };

  auto transaction = ledger::DBTransaction::New();
  transaction->commands.push_back(std::move(command));

  ledger_->RunDBTransaction(
      std::move(transaction),
      std::bind(&DatabasePublisherList::OnSearchResult, this, _1, callback));
}

void DatabasePublisherList::OnSearchResult(
    ledger::DBCommandResponsePtr response,
    ledger::SearchPublisherListCallback callback) {
  if (response && response->result) {
    for (auto& record : response->result->get_records()) {
      int count = GetIntColumn(record.get(), 0);
      callback(count > 0);
      return;
    }
  }
  // TODO(zenparsing): Log error?
  callback(false);
}

void DatabasePublisherList::InsertPrefixes(
    PrefixIterator begin,
    PrefixIterator end,
    ledger::ResultCallback callback) {
  auto transaction = ledger::DBTransaction::New();
  AddInsertCommand(GetPrefixInsertList(begin, end), transaction.get());
  ledger_->RunDBTransaction(
      std::move(transaction),
      std::bind(&OnResultCallback, _1, callback));
}

void DatabasePublisherList::RemovePrefixes(
    PrefixIterator begin,
    PrefixIterator end,
    ledger::ResultCallback callback) {
  auto transaction = ledger::DBTransaction::New();
  AddDeleteCommand(GetPrefixSearchList(begin, end), transaction.get());
  ledger_->RunDBTransaction(
      std::move(transaction),
      std::bind(&OnResultCallback, _1, callback));
}

void DatabasePublisherList::ResetPrefixes(
    PrefixIterator begin,
    PrefixIterator end,
    ledger::ResultCallback callback) {
  auto transaction = ledger::DBTransaction::New();
  DropAndCreateTableV21(transaction.get());
  AddInsertCommand(GetPrefixInsertList(begin, end), transaction.get());
  ledger_->RunDBTransaction(
      std::move(transaction),
      std::bind(&OnResultCallback, _1, callback));
}

}  // namespace braveledger_database
