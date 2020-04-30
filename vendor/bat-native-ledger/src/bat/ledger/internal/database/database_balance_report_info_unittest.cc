/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <memory>
#include <string>
#include <utility>

#include "base/test/task_environment.h"
#include "bat/ledger/internal/database/database_balance_report_info.h"
#include "bat/ledger/internal/database/database_util.h"
#include "bat/ledger/internal/ledger_client_mock.h"
#include "bat/ledger/internal/ledger_impl_mock.h"

// npm run test -- brave_unit_tests --filter=DatabaseBalanceReportInfoTest.*

using ::testing::_;
using ::testing::Invoke;

namespace braveledger_database {

class DatabaseBalanceReportInfoTest : public ::testing::Test {
 private:
  base::test::TaskEnvironment scoped_task_environment_;

 protected:
  std::unique_ptr<ledger::MockLedgerClient> mock_ledger_client_;
  std::unique_ptr<bat_ledger::MockLedgerImpl> mock_ledger_impl_;
  std::string execute_script_;
  std::unique_ptr<DatabaseBalanceReportInfo> balance_report_;

  DatabaseBalanceReportInfoTest() {
    mock_ledger_client_ = std::make_unique<ledger::MockLedgerClient>();
    mock_ledger_impl_ =
        std::make_unique<bat_ledger::MockLedgerImpl>(mock_ledger_client_.get());
    balance_report_ =
        std::make_unique<DatabaseBalanceReportInfo>(mock_ledger_impl_.get());
  }

  ~DatabaseBalanceReportInfoTest() override {}
};

TEST_F(DatabaseBalanceReportInfoTest, InsertOrUpdateOk) {
  auto info = ledger::BalanceReportInfo::New();
  info->id = "2020_05";
  info->grants = 1.0;
  info->earning_from_ads = 1.0;
  info->auto_contribute = 1.0;
  info->recurring_donation = 1.0;
  info->one_time_donation = 1.0;

  const std::string query =
      "INSERT OR REPLACE INTO balance_report_info "
      "(balance_report_id, grants, earning_from_ads, auto_contribute, "
      "recurring_donation, one_time_donation) "
      "VALUES (?, ?, ?, ?, ?, ?)";

  ON_CALL(*mock_ledger_impl_, RunDBTransaction(_, _))
      .WillByDefault(
        Invoke([&](
            ledger::DBTransactionPtr transaction,
            ledger::RunDBTransactionCallback callback) {
          ASSERT_TRUE(transaction);
          ASSERT_EQ(transaction->commands.size(), 1u);
          ASSERT_EQ(
              transaction->commands[0]->type,
              ledger::DBCommand::Type::RUN);
          ASSERT_EQ(transaction->commands[0]->command, query);
          ASSERT_EQ(transaction->commands[0]->bindings.size(), 6u);
        }));

  balance_report_->InsertOrUpdate(
      std::move(info),
      [](const ledger::Result){});
}

TEST_F(DatabaseBalanceReportInfoTest, GetAllRecordsOk) {
  EXPECT_CALL(*mock_ledger_impl_, RunDBTransaction(_, _)).Times(1);

  const std::string query =
    "SELECT mb.balance_report_id, mb.grants, mb.earning_from_ads, "
    "mb.auto_contribute, mb.recurring_donation, mb.one_time_donation "
    "FROM balance_report_info as mb ";

  ON_CALL(*mock_ledger_impl_, RunDBTransaction(_, _))
      .WillByDefault(
        Invoke([&](
            ledger::DBTransactionPtr transaction,
            ledger::RunDBTransactionCallback callback) {
          ASSERT_TRUE(transaction);
          ASSERT_EQ(transaction->commands.size(), 1u);
          ASSERT_EQ(
              transaction->commands[0]->type,
              ledger::DBCommand::Type::READ);
          ASSERT_EQ(transaction->commands[0]->command, query);
          ASSERT_EQ(transaction->commands[0]->record_bindings.size(), 6u);
          ASSERT_EQ(transaction->commands[0]->bindings.size(), 0u);
        }));

  balance_report_->GetAllRecords([](ledger::BalanceReportInfoList) {});
}

TEST_F(DatabaseBalanceReportInfoTest, GetRecordOk) {
  EXPECT_CALL(*mock_ledger_impl_, RunDBTransaction(_, _)).Times(1);

  const std::string query =
    "SELECT mb.balance_report_id, mb.grants, mb.earning_from_ads, "
    "mb.auto_contribute, mb.recurring_donation, mb.one_time_donation "
    "FROM balance_report_info as mb "
    "WHERE balance_report_id=?";

  ON_CALL(*mock_ledger_impl_, RunDBTransaction(_, _))
      .WillByDefault(
        Invoke([&](
            ledger::DBTransactionPtr transaction,
            ledger::RunDBTransactionCallback callback) {
          ASSERT_TRUE(transaction);
          ASSERT_EQ(transaction->commands.size(), 1u);
          ASSERT_EQ(
              transaction->commands[0]->type,
              ledger::DBCommand::Type::READ);
          ASSERT_EQ(transaction->commands[0]->command, query);
          ASSERT_EQ(transaction->commands[0]->record_bindings.size(), 6u);
          ASSERT_EQ(transaction->commands[0]->bindings.size(), 1u);
        }));

  balance_report_->GetRecord(
      ledger::ActivityMonth::MAY,
      2020,
      [](ledger::Result, ledger::BalanceReportInfoPtr) {});
}

TEST_F(DatabaseBalanceReportInfoTest, DeleteAllRecordsOk) {
  EXPECT_CALL(*mock_ledger_impl_, RunDBTransaction(_, _)).Times(1);

  const std::string query =
    "DELETE FROM balance_report_info";

  ON_CALL(*mock_ledger_impl_, RunDBTransaction(_, _))
      .WillByDefault(
        Invoke([&](
            ledger::DBTransactionPtr transaction,
            ledger::RunDBTransactionCallback callback) {
          ASSERT_TRUE(transaction);
          ASSERT_EQ(transaction->commands.size(), 1u);
          ASSERT_EQ(
              transaction->commands[0]->type,
              ledger::DBCommand::Type::RUN);
          ASSERT_EQ(transaction->commands[0]->command, query);
          ASSERT_EQ(transaction->commands[0]->record_bindings.size(), 0u);
          ASSERT_EQ(transaction->commands[0]->bindings.size(), 0u);
        }));

  balance_report_->DeleteAllRecords([](ledger::Result) {});
}

}  // namespace braveledger_database
