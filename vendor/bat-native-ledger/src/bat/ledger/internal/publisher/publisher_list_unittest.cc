/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <algorithm>

#include "bat/ledger/internal/publisher/publisher_list_reader.h"
#include "testing/gtest/include/gtest/gtest.h"

// npm run test -- brave_unit_tests --filter='PublisherListReaderTest.*'

namespace braveledger_publisher {

class PublisherListReaderTest : public testing::Test {};

TEST_F(PublisherListReaderTest, Basics) {
  using publishers_pb::PublisherList;

  size_t prefix_size = 4;

  // A sorted list of prefixes. Note that actual prefixes
  // are raw bytes and not chars.
  std::string prefix_data =
    "andy"
    "bear"
    "cake"
    "dear";

  PublisherList list;
  list.set_prefix_size(prefix_size);
  list.set_compression_type(PublisherList::NO_COMPRESSION);
  list.set_uncompressed_size(prefix_data.length());
  list.set_prefixes(prefix_data);

  std::string serialized;
  ASSERT_TRUE(list.SerializeToString(&serialized));

  PublisherListReader reader;

  // Basic successful parsing
  ASSERT_EQ(
      reader.Parse(serialized),
      PublisherListReader::ParseError::None);

  // Iteration
  size_t offset = 0;
  for (auto prefix : reader) {
    EXPECT_EQ(prefix, prefix_data.substr(offset, prefix_size));
    offset += prefix_size;
  }

  // Binary searching
  EXPECT_TRUE(std::binary_search(reader.begin(), reader.end(), "cake"));
  EXPECT_FALSE(std::binary_search(reader.begin(), reader.end(), "pool"));
}

// TODO(zenparsing): Add tests for parse errors

}  // namespace braveledger_publisher
