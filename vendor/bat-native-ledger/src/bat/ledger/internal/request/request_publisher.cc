/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "base/strings/stringprintf.h"
#include "bat/ledger/internal/request/request_publisher.h"
#include "bat/ledger/internal/request/request_util.h"

namespace braveledger_request_util {

std::string GetPublisherListUrl() {
  // TODO(zenparsing): Use BuildUrl
  return "http://localhost:3000/publisher_list.pb";
}

std::string GetPublisherInfoUrl(const std::string& hash_prefix) {
  // TODO(zenparsing): Use BuildUrl
  return base::StringPrintf(
      "http://localhost:3000/channel/%s",
      hash_prefix.c_str());
}

}  // namespace braveledger_request_util
