/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "bat/ledger/internal/database/database_publisher_list.h"

#include <utility>

#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "bat/ledger/internal/database/database_util.h"
#include "bat/ledger/internal/publisher/prefix_util.h"
#include "bat/ledger/internal/ledger_impl.h"
#include "bat/ledger/internal/state_keys.h"

using std::placeholders::_1;
using braveledger_publisher::PrefixIterator;

namespace {

const char kTableName[] = "publisher_list";

constexpr size_t kHashPrefixSize = 4;
constexpr size_t kMaxInsertRecords = 100'000;

void DropAndCreateTableV22(ledger::DBTransaction* transaction) {
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

void AddDropAndCreateTableCommand(ledger::DBTransaction* transaction) {
  DropAndCreateTableV22(transaction);
}

std::pair<PrefixIterator, std::string> GetPrefixInsertList(
    PrefixIterator begin,
    PrefixIterator end) {
  DCHECK(begin != end);
  size_t count = 0;
  std::string values;
  PrefixIterator iter = begin;
  for (iter = begin; iter != end && count++ < kMaxInsertRecords; ++iter) {
    auto prefix = *iter;
    DCHECK(prefix.size() >= kHashPrefixSize);
    values.append(iter == begin ? "(x'" : "'),(x'");
    values.append(base::HexEncode(prefix.data(), kHashPrefixSize));
  }
  values.append("')");
  return {iter, std::move(values)};
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
    case 22:
      MigrateToV22(transaction);
      return true;
    default:
      return true;
  }
}

void DatabasePublisherList::MigrateToV22(
    ledger::DBTransaction* transaction) {
  DropAndCreateTableV22(transaction);
  ledger_->ClearState(ledger::kStateServerPublisherListStamp);
}

void DatabasePublisherList::Search(
    const std::string& publisher_key,
    ledger::SearchPublisherListCallback callback) {
  std::string hex = braveledger_publisher::GetHashPrefixInHex(
      publisher_key,
      kHashPrefixSize);

  LOG(INFO) << "[[zenparsing]] Searching for " << publisher_key;
  LOG(INFO) << "[[zenparsing]] Prefix " << hex;

  auto command = ledger::DBCommand::New();
  command->type = ledger::DBCommand::Type::READ;
  command->command = base::StringPrintf(
      "SELECT COUNT(*) as count FROM %s WHERE hash_prefix = x'%s'",
      kTableName,
      hex.c_str());

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
      LOG(INFO) << "[[zenparsing]] Result is " << count;
      callback(count > 0);
      return;
    }
  }
  // TODO(zenparsing): Log error - Unexpected database result.
  callback(false);
}

void DatabasePublisherList::ResetPrefixes(
    std::unique_ptr<braveledger_publisher::PublisherListReader> reader,
    ledger::ResultCallback callback) {
  if (reader_) {
    // TODO(zenparsing): Log error - reset in progress
    callback(ledger::Result::LEDGER_ERROR);
    return;
  }
  reader_ = std::move(reader);
  InsertNext(reader_->begin(), callback);
}

void DatabasePublisherList::InsertNext(
    PrefixIterator begin,
    ledger::ResultCallback callback) {
  DCHECK(reader_);
  DCHECK(begin != reader_->end());

  auto transaction = ledger::DBTransaction::New();

  if (begin == reader_->begin()) {
    LOG(INFO) << "[[zenparsing]] Clearing publisher_list table";
    AddDropAndCreateTableCommand(transaction.get());
  }

  auto insert_pair = GetPrefixInsertList(begin, reader_->end());
  auto command = ledger::DBCommand::New();
  command->type = ledger::DBCommand::Type::RUN;
  command->command = base::StringPrintf(
      "INSERT OR REPLACE INTO %s (hash_prefix) VALUES %s",
      kTableName,
      insert_pair.second.data());

  LOG(INFO) << "[[zenparsing]] Inserting: " << insert_pair.second.size();
  transaction->commands.push_back(std::move(command));

  ledger_->RunDBTransaction(
      std::move(transaction),
      std::bind(&DatabasePublisherList::OnInsertNextResult,
          this, _1, insert_pair.first, callback));
}

void DatabasePublisherList::OnInsertNextResult(
    ledger::DBCommandResponsePtr response,
    PrefixIterator begin,
    ledger::ResultCallback callback) {
  if (!response ||
      response->status != ledger::DBCommandResponse::Status::RESPONSE_OK) {
    reader_ = nullptr;
    callback(ledger::Result::LEDGER_ERROR);
    return;
  }

  if (begin == reader_->end()) {
    reader_ = nullptr;
    LOG(INFO) << "[[zenparsing]] Done";
    callback(ledger::Result::LEDGER_OK);
    return;
  }

  InsertNext(begin, callback);
}

}  // namespace braveledger_database
