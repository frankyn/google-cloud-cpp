// Copyright 2018 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "google/cloud/storage/internal/service_account_requests.h"
#include <iostream>

namespace google {
namespace cloud {
namespace storage {
inline namespace STORAGE_CLIENT_NS {
namespace internal {
StatusOr<ServiceAccount> ServiceAccountParser::FromJson(
    internal::nl::json const& json) {
  if (!json.is_object()) {
    return Status(StatusCode::kInvalidArgument, __func__);
  }
  ServiceAccount result{};
  result.kind_ = json.value("kind", "");
  result.email_address_ = json.value("email_address", "");
  return result;
}

StatusOr<ServiceAccount> ServiceAccountParser::FromString(
    std::string const& payload) {
  auto json = internal::nl::json::parse(payload, nullptr, false);
  return FromJson(json);
}

std::ostream& operator<<(std::ostream& os,
                         GetProjectServiceAccountRequest const& r) {
  os << "GetProjectServiceAccountRequest={project_id=" << r.project_id();
  r.DumpOptions(os, ", ");
  return os << "}";
}
}  // namespace internal
}  // namespace STORAGE_CLIENT_NS
}  // namespace storage
}  // namespace cloud
}  // namespace google