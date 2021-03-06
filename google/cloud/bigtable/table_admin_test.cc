// Copyright 2017 Google Inc.
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

#include "google/cloud/bigtable/table_admin.h"
#include "google/cloud/bigtable/testing/mock_admin_client.h"
#include "google/cloud/bigtable/testing/mock_async_failing_rpc_factory.h"
#include "google/cloud/bigtable/testing/mock_response_reader.h"
#include "google/cloud/bigtable/testing/validate_metadata.h"
#include "google/cloud/internal/make_unique.h"
#include "google/cloud/status_or.h"
#include "google/cloud/testing_util/assert_ok.h"
#include "google/cloud/testing_util/chrono_literals.h"
#include "google/cloud/testing_util/mock_completion_queue.h"
#include <google/protobuf/text_format.h>
#include <google/protobuf/util/message_differencer.h>
#include <gmock/gmock.h>
#include <chrono>

namespace {
namespace btadmin = ::google::bigtable::admin::v2;
namespace bigtable = google::cloud::bigtable;
using namespace google::cloud::testing_util::chrono_literals;
using MockAdminClient = bigtable::testing::MockAdminClient;
using google::cloud::testing_util::MockCompletionQueue;

std::string const kProjectId = "the-project";
std::string const kInstanceId = "the-instance";
std::string const kClusterId = "the-cluster";

/// A fixture for the bigtable::TableAdmin tests.
class TableAdminTest : public ::testing::Test {
 protected:
  void SetUp() override {
    using namespace ::testing;

    EXPECT_CALL(*client_, project()).WillRepeatedly(ReturnRef(kProjectId));
  }

  std::shared_ptr<MockAdminClient> client_ =
      std::make_shared<MockAdminClient>();
};

// A lambda to create lambdas.  Basically we would be rewriting the same
// lambda twice without this thing.
auto create_list_tables_lambda = [](std::string expected_token,
                                    std::string returned_token,
                                    std::vector<std::string> table_names) {
  return [expected_token, returned_token, table_names](
             grpc::ClientContext* context,
             btadmin::ListTablesRequest const& request,
             btadmin::ListTablesResponse* response) {
    EXPECT_STATUS_OK(google::cloud::bigtable::testing::IsContextMDValid(
        *context, "google.bigtable.admin.v2.BigtableTableAdmin.ListTables"));
    auto const instance_name =
        "projects/" + kProjectId + "/instances/" + kInstanceId;
    EXPECT_EQ(instance_name, request.parent());
    EXPECT_EQ(btadmin::Table::FULL, request.view());
    EXPECT_EQ(expected_token, request.page_token());

    EXPECT_NE(nullptr, response);
    for (auto const& table_name : table_names) {
      auto& table = *response->add_tables();
      table.set_name(instance_name + "/tables/" + table_name);
      table.set_granularity(btadmin::Table::MILLIS);
    }
    // Return the right token.
    response->set_next_page_token(returned_token);
    return grpc::Status::OK;
  };
};

auto create_get_policy_mock = []() {
  return [](grpc::ClientContext* context,
            ::google::iam::v1::GetIamPolicyRequest const&,
            ::google::iam::v1::Policy* response) {
    EXPECT_STATUS_OK(google::cloud::bigtable::testing::IsContextMDValid(
        *context, "google.bigtable.admin.v2.BigtableTableAdmin.GetIamPolicy"));
    EXPECT_NE(nullptr, response);
    response->set_version(3);
    response->set_etag("random-tag");
    return grpc::Status::OK;
  };
};

auto create_policy_with_params = []() {
  return [](grpc::ClientContext* context,
            ::google::iam::v1::SetIamPolicyRequest const& request,
            ::google::iam::v1::Policy* response) {
    EXPECT_STATUS_OK(google::cloud::bigtable::testing::IsContextMDValid(
        *context, "google.bigtable.admin.v2.BigtableTableAdmin.SetIamPolicy"));
    EXPECT_NE(nullptr, response);
    *response = request.policy();
    return grpc::Status::OK;
  };
};

/**
 * Helper class to create the expectations for a simple RPC call.
 *
 * Given the type of the request and responses, this struct provides a function
 * to create a mock implementation with the right signature and checks.
 *
 * @tparam RequestType the protobuf type for the request.
 * @tparam ResponseType the protobuf type for the response.
 */
template <typename RequestType, typename ResponseType>
struct MockRpcFactory {
  using SignatureType = grpc::Status(grpc::ClientContext* ctx,
                                     RequestType const& request,
                                     ResponseType* response);

  /// Refactor the boilerplate common to most tests.
  static std::function<SignatureType> Create(std::string expected_request,
                                             std::string const& method) {
    return std::function<SignatureType>(
        [expected_request, method](grpc::ClientContext* context,
                                   RequestType const& request,
                                   ResponseType* response) {
          EXPECT_STATUS_OK(google::cloud::bigtable::testing::IsContextMDValid(
              *context, method));
          if (response == nullptr) {
            return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT,
                                "invalid call to MockRpcFactory::Create()");
          }
          RequestType expected;
          // Cannot use ASSERT_TRUE() here, it has an embedded "return;"
          EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
              expected_request, &expected));
          std::string delta;
          google::protobuf::util::MessageDifferencer differencer;
          differencer.ReportDifferencesToString(&delta);
          EXPECT_TRUE(differencer.Compare(expected, request)) << delta;

          return grpc::Status::OK;
        });
  }
};

}  // anonymous namespace

/// @test Verify basic functionality in the `bigtable::TableAdmin` class.
TEST_F(TableAdminTest, Default) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_EQ("the-instance", tested.instance_id());
  EXPECT_EQ("projects/the-project/instances/the-instance",
            tested.instance_name());
}

/// @test Verify that `bigtable::TableAdmin::ListTables` works in the easy case.
TEST_F(TableAdminTest, ListTables) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, kInstanceId);
  auto mock_list_tables = create_list_tables_lambda("", "", {"t0", "t1"});
  EXPECT_CALL(*client_, ListTables(_, _, _)).WillOnce(Invoke(mock_list_tables));

  // After all the setup, make the actual call we want to test.
  auto actual = tested.ListTables(btadmin::Table::FULL);
  ASSERT_STATUS_OK(actual);
  auto const& v = *actual;
  std::string instance_name = tested.instance_name();
  ASSERT_EQ(2UL, v.size());
  EXPECT_EQ(instance_name + "/tables/t0", v[0].name());
  EXPECT_EQ(instance_name + "/tables/t1", v[1].name());
}

/// @test Verify that `bigtable::TableAdmin::ListTables` handles failures.
TEST_F(TableAdminTest, ListTablesRecoverableFailures) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  auto mock_recoverable_failure = [](grpc::ClientContext* context,
                                     btadmin::ListTablesRequest const&,
                                     btadmin::ListTablesResponse*) {
    EXPECT_STATUS_OK(google::cloud::bigtable::testing::IsContextMDValid(
        *context, "google.bigtable.admin.v2.BigtableTableAdmin.ListTables"));
    return grpc::Status(grpc::StatusCode::UNAVAILABLE, "try-again");
  };
  auto batch0 = create_list_tables_lambda("", "token-001", {"t0", "t1"});
  auto batch1 = create_list_tables_lambda("token-001", "", {"t2", "t3"});
  EXPECT_CALL(*client_, ListTables(_, _, _))
      .WillOnce(Invoke(mock_recoverable_failure))
      .WillOnce(Invoke(batch0))
      .WillOnce(Invoke(mock_recoverable_failure))
      .WillOnce(Invoke(mock_recoverable_failure))
      .WillOnce(Invoke(batch1));

  // After all the setup, make the actual call we want to test.
  auto actual = tested.ListTables(btadmin::Table::FULL);
  ASSERT_STATUS_OK(actual);
  auto const& v = *actual;
  std::string instance_name = tested.instance_name();
  ASSERT_EQ(4UL, v.size());
  EXPECT_EQ(instance_name + "/tables/t0", v[0].name());
  EXPECT_EQ(instance_name + "/tables/t1", v[1].name());
  EXPECT_EQ(instance_name + "/tables/t2", v[2].name());
  EXPECT_EQ(instance_name + "/tables/t3", v[3].name());
}

/**
 * @test Verify that `bigtable::TableAdmin::ListTables` handles unrecoverable
 * failures.
 */
TEST_F(TableAdminTest, ListTablesUnrecoverableFailures) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, ListTables(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "uh oh")));

  EXPECT_FALSE(tested.ListTables(btadmin::Table::FULL));
}

/**
 * @test Verify that `bigtable::TableAdmin::ListTables` handles too many
 * recoverable failures.
 */
TEST_F(TableAdminTest, ListTablesTooManyFailures) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;

  bigtable::TableAdmin tested(
      client_, "the-instance", bigtable::LimitedErrorCountRetryPolicy(3),
      bigtable::ExponentialBackoffPolicy(10_ms, 10_min));
  auto mock_recoverable_failure = [](grpc::ClientContext* context,
                                     btadmin::ListTablesRequest const&,
                                     btadmin::ListTablesResponse*) {
    EXPECT_STATUS_OK(google::cloud::bigtable::testing::IsContextMDValid(
        *context, "google.bigtable.admin.v2.BigtableTableAdmin.ListTables"));
    return grpc::Status(grpc::StatusCode::UNAVAILABLE, "try-again");
  };
  EXPECT_CALL(*client_, ListTables(_, _, _))
      .WillRepeatedly(Invoke(mock_recoverable_failure));

  EXPECT_FALSE(tested.ListTables(btadmin::Table::FULL));
}

/// @test Verify that `bigtable::TableAdmin::Create` works in the easy case.
TEST_F(TableAdminTest, CreateTableSimple) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;

  bigtable::TableAdmin tested(client_, "the-instance");

  std::string expected_text = R"""(
      parent: 'projects/the-project/instances/the-instance'
table_id: 'new-table'
table {
  column_families {
    key: 'f1'
    value { gc_rule { max_num_versions: 1 }}
  }
  column_families {
    key: 'f2'
    value { gc_rule { max_age { seconds: 1 }}}
  }
  granularity: TIMESTAMP_GRANULARITY_UNSPECIFIED
}
initial_splits { key: 'a' }
initial_splits { key: 'c' }
initial_splits { key: 'p' }
)""";
  auto mock_create_table =
      MockRpcFactory<btadmin::CreateTableRequest, btadmin::Table>::Create(
          expected_text,
          "google.bigtable.admin.v2.BigtableTableAdmin.CreateTable");
  EXPECT_CALL(*client_, CreateTable(_, _, _))
      .WillOnce(Invoke(mock_create_table));

  // After all the setup, make the actual call we want to test.
  using GC = bigtable::GcRule;
  bigtable::TableConfig config(
      {{"f1", GC::MaxNumVersions(1)}, {"f2", GC::MaxAge(1_s)}},
      {"a", "c", "p"});
  auto table = tested.CreateTable("new-table", std::move(config));
  EXPECT_STATUS_OK(table);
}

/**
 * @test Verify that `bigtable::TableAdmin::CreateTable` supports
 * only one try and let client know request status.
 */
TEST_F(TableAdminTest, CreateTableFailure) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, CreateTable(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "uh oh")));

  EXPECT_FALSE(tested.CreateTable("other-table", bigtable::TableConfig()));
}

/**
 * @test Verify that Copy Constructor and assignment operator
 * copies all properties.
 */
TEST_F(TableAdminTest, CopyConstructibleAssignableTest) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-copy-instance");
  bigtable::TableAdmin table_admin(tested);

  EXPECT_EQ(tested.instance_id(), table_admin.instance_id());
  EXPECT_EQ(tested.instance_name(), table_admin.instance_name());
  EXPECT_EQ(tested.project(), table_admin.project());

  bigtable::TableAdmin table_admin_assign(client_, "the-assign-instance");
  EXPECT_NE(tested.instance_id(), table_admin_assign.instance_id());
  EXPECT_NE(tested.instance_name(), table_admin_assign.instance_name());

  table_admin_assign = tested;
  EXPECT_EQ(tested.instance_id(), table_admin_assign.instance_id());
  EXPECT_EQ(tested.instance_name(), table_admin_assign.instance_name());
  EXPECT_EQ(tested.project(), table_admin_assign.project());
}

/**
 * @test Verify that Copy Constructor and assignment operator copies
 * all properties including policies applied.
 */
TEST_F(TableAdminTest, CopyConstructibleAssignablePolicyTest) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;

  bigtable::TableAdmin tested(
      client_, "the-construct-instance",
      bigtable::LimitedErrorCountRetryPolicy(3),
      bigtable::ExponentialBackoffPolicy(10_ms, 10_min));
  // Copy Constructor
  bigtable::TableAdmin table_admin(tested);
  // Create New Instance
  bigtable::TableAdmin table_admin_assign(client_, "the-assign-instance");
  // Copy assignable
  table_admin_assign = table_admin;

  EXPECT_CALL(*client_, GetTable(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::UNAVAILABLE, "try-again")));

  EXPECT_FALSE(table_admin.GetTable("other-table"));
  EXPECT_FALSE(table_admin_assign.GetTable("other-table"));
}

/// @test Verify that `bigtable::TableAdmin::GetTable` works in the easy case.
TEST_F(TableAdminTest, GetTableSimple) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;

  bigtable::TableAdmin tested(client_, "the-instance");
  std::string expected_text = R"""(
      name: 'projects/the-project/instances/the-instance/tables/the-table'
      view: SCHEMA_VIEW
)""";
  auto mock = MockRpcFactory<btadmin::GetTableRequest, btadmin::Table>::Create(
      expected_text, "google.bigtable.admin.v2.BigtableTableAdmin.GetTable");
  EXPECT_CALL(*client_, GetTable(_, _, _))
      .WillOnce(
          Return(grpc::Status(grpc::StatusCode::UNAVAILABLE, "try-again")))
      .WillOnce(Invoke(mock));

  // After all the setup, make the actual call we want to test.
  tested.GetTable("the-table");
}

/**
 * @test Verify that `bigtable::TableAdmin::GetTable` reports unrecoverable
 * failures.
 */
TEST_F(TableAdminTest, GetTableUnrecoverableFailures) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, GetTable(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::NOT_FOUND, "uh oh")));

  // After all the setup, make the actual call we want to test.
  EXPECT_FALSE(tested.GetTable("other-table"));
}

/**
 * @test Verify that `bigtable::TableAdmin::GetTable` works with too many
 * recoverable failures.
 */
TEST_F(TableAdminTest, GetTableTooManyFailures) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;

  bigtable::TableAdmin tested(
      client_, "the-instance", bigtable::LimitedErrorCountRetryPolicy(3),
      bigtable::ExponentialBackoffPolicy(10_ms, 10_min));
  EXPECT_CALL(*client_, GetTable(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::UNAVAILABLE, "try-again")));

  // After all the setup, make the actual call we want to test.
  EXPECT_FALSE(tested.GetTable("other-table"));
}

/// @test Verify that bigtable::TableAdmin::DeleteTable works as expected.
TEST_F(TableAdminTest, DeleteTable) {
  using namespace ::testing;
  using google::protobuf::Empty;

  bigtable::TableAdmin tested(client_, "the-instance");
  std::string expected_text = R"""(
      name: 'projects/the-project/instances/the-instance/tables/the-table'
)""";
  auto mock = MockRpcFactory<btadmin::DeleteTableRequest, Empty>::Create(
      expected_text, "google.bigtable.admin.v2.BigtableTableAdmin.DeleteTable");
  EXPECT_CALL(*client_, DeleteTable(_, _, _)).WillOnce(Invoke(mock));

  // After all the setup, make the actual call we want to test.
  EXPECT_STATUS_OK(tested.DeleteTable("the-table"));
}

/**
 * @test Verify that `bigtable::TableAdmin::DeleteTable` supports
 * only one try and let client know request status.
 */
TEST_F(TableAdminTest, DeleteTableFailure) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, DeleteTable(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "uh oh")));

  // After all the setup, make the actual call we want to test.
  EXPECT_FALSE(tested.DeleteTable("other-table").ok());
}

/**
 * @test Verify that bigtable::TableAdmin::ModifyColumnFamilies works as
 * expected.
 */
TEST_F(TableAdminTest, ModifyColumnFamilies) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;
  using google::protobuf::Empty;

  bigtable::TableAdmin tested(client_, "the-instance");
  std::string expected_text = R"""(
      name: 'projects/the-project/instances/the-instance/tables/the-table'
modifications {
  id: 'foo'
  create { gc_rule { max_age { seconds: 172800 }}}
}
modifications {
  id: 'bar'
  update { gc_rule { max_age { seconds: 86400 }}}
}
)""";
  auto mock = MockRpcFactory<btadmin::ModifyColumnFamiliesRequest,
                             btadmin::Table>::
      Create(
          expected_text,
          "google.bigtable.admin.v2.BigtableTableAdmin.ModifyColumnFamilies");
  EXPECT_CALL(*client_, ModifyColumnFamilies(_, _, _)).WillOnce(Invoke(mock));

  // After all the setup, make the actual call we want to test.
  using M = bigtable::ColumnFamilyModification;
  using GC = bigtable::GcRule;
  auto actual = tested.ModifyColumnFamilies(
      "the-table",
      {M::Create("foo", GC::MaxAge(48_h)), M::Update("bar", GC::MaxAge(24_h))});
}

/**
 * @test Verify that `bigtable::TableAdmin::ModifyColumnFamilies` makes only one
 * RPC attempt and reports errors on failure.
 */
TEST_F(TableAdminTest, ModifyColumnFamiliesFailure) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, ModifyColumnFamilies(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "uh oh")));

  using M = bigtable::ColumnFamilyModification;
  using GC = bigtable::GcRule;
  std::vector<M> changes{M::Create("foo", GC::MaxAge(48_h)),
                         M::Update("bar", GC::MaxAge(24_h))};

  EXPECT_FALSE(tested.ModifyColumnFamilies("other-table", std::move(changes)));
}

/// @test Verify that bigtable::TableAdmin::DropRowsByPrefix works as expected.
TEST_F(TableAdminTest, DropRowsByPrefix) {
  using namespace ::testing;
  using google::protobuf::Empty;

  bigtable::TableAdmin tested(client_, "the-instance");
  std::string expected_text = R"""(
      name: 'projects/the-project/instances/the-instance/tables/the-table'
      row_key_prefix: 'foobar'
)""";
  auto mock = MockRpcFactory<btadmin::DropRowRangeRequest, Empty>::Create(
      expected_text,
      "google.bigtable.admin.v2.BigtableTableAdmin.DropRowRange");
  EXPECT_CALL(*client_, DropRowRange(_, _, _)).WillOnce(Invoke(mock));

  // After all the setup, make the actual call we want to test.
  EXPECT_STATUS_OK(tested.DropRowsByPrefix("the-table", "foobar"));
}

/**
 * @test Verify that `bigtable::TableAdmin::DropRowsByPrefix` makes only one
 * RPC attempt and reports errors on failure.
 */
TEST_F(TableAdminTest, DropRowsByPrefixFailure) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, DropRowRange(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "uh oh")));

  EXPECT_FALSE(tested.DropRowsByPrefix("other-table", "prefix").ok());
}

/// @test Verify that bigtable::TableAdmin::DropRowsByPrefix works as expected.
TEST_F(TableAdminTest, DropAllRows) {
  using namespace ::testing;
  using google::protobuf::Empty;

  bigtable::TableAdmin tested(client_, "the-instance");
  std::string expected_text = R"""(
      name: 'projects/the-project/instances/the-instance/tables/the-table'
      delete_all_data_from_table: true
)""";
  auto mock = MockRpcFactory<btadmin::DropRowRangeRequest, Empty>::Create(
      expected_text,
      "google.bigtable.admin.v2.BigtableTableAdmin.DropRowRange");
  EXPECT_CALL(*client_, DropRowRange(_, _, _)).WillOnce(Invoke(mock));

  // After all the setup, make the actual call we want to test.
  EXPECT_STATUS_OK(tested.DropAllRows("the-table"));
}

/**
 * @test Verify that `bigtable::TableAdmin::DropAllRows` makes only one
 * RPC attempt and reports errors on failure.
 */
TEST_F(TableAdminTest, DropAllRowsFailure) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, DropRowRange(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "uh oh")));

  // After all the setup, make the actual call we want to test.
  EXPECT_FALSE(tested.DropAllRows("other-table").ok());
}

/**
 * @test Verify that `bigtagble::TableAdmin::GenerateConsistencyToken` works as
 * expected.
 */
TEST_F(TableAdminTest, GenerateConsistencyTokenSimple) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  std::string expected_text = R"""(
      name: 'projects/the-project/instances/the-instance/tables/the-table'
)""";
  auto mock = MockRpcFactory<btadmin::GenerateConsistencyTokenRequest,
                             btadmin::GenerateConsistencyTokenResponse>::
      Create(expected_text,
             "google.bigtable.admin.v2.BigtableTableAdmin."
             "GenerateConsistencyToken");
  EXPECT_CALL(*client_, GenerateConsistencyToken(_, _, _))
      .WillOnce(Invoke(mock));

  // After all the setup, make the actual call we want to test.
  tested.GenerateConsistencyToken("the-table");
}

/**
 * @test Verify that `bigtable::TableAdmin::GenerateConsistencyToken` makes only
 * one RPC attempt and reports errors on failure.
 */
TEST_F(TableAdminTest, GenerateConsistencyTokenFailure) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, GenerateConsistencyToken(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "uh oh")));

  // After all the setup, make the actual call we want to test.
  EXPECT_FALSE(tested.GenerateConsistencyToken("other-table"));
}

/**
 * @test Verify that `bigtagble::TableAdmin::CheckConsistency` works as
 * expected.
 */
TEST_F(TableAdminTest, CheckConsistencySimple) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  std::string expected_text = R"""(
      name: 'projects/the-project/instances/the-instance/tables/the-table'
      consistency_token: 'test-token'
)""";
  auto mock = MockRpcFactory<btadmin::CheckConsistencyRequest,
                             btadmin::CheckConsistencyResponse>::
      Create(expected_text,
             "google.bigtable.admin.v2.BigtableTableAdmin.CheckConsistency");
  EXPECT_CALL(*client_, CheckConsistency(_, _, _)).WillOnce(Invoke(mock));

  // After all the setup, make the actual call we want to test.
  auto result = tested.CheckConsistency("the-table", "test-token");
  ASSERT_STATUS_OK(result);
}

/**
 * @test Verify that `bigtable::TableAdmin::CheckConsistency` makes only
 * one RPC attempt and reports errors on failure.
 */
TEST_F(TableAdminTest, CheckConsistencyFailure) {
  using namespace ::testing;

  bigtable::TableAdmin tested(client_, "the-instance");
  EXPECT_CALL(*client_, CheckConsistency(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "uh oh")));

  // After all the setup, make the actual call we want to test.
  EXPECT_FALSE(tested.CheckConsistency("other-table", "test-token"));
}

/// @test Verify positive scenario for TableAdmin::GetIamPolicy.
TEST_F(TableAdminTest, GetIamPolicy) {
  using ::testing::_;
  using ::testing::Invoke;

  bigtable::TableAdmin tested(client_, "the-instance");
  auto mock_policy = create_get_policy_mock();
  EXPECT_CALL(*client_, GetIamPolicy(_, _, _)).WillOnce(Invoke(mock_policy));

  std::string resource = "test-resource";
  auto policy = tested.GetIamPolicy(resource);
  ASSERT_STATUS_OK(policy);
  EXPECT_EQ(3, policy->version());
  EXPECT_EQ("random-tag", policy->etag());
}

/// @test Verify unrecoverable errors for TableAdmin::GetIamPolicy.
TEST_F(TableAdminTest, GetIamPolicyUnrecoverableError) {
  using ::testing::_;
  using ::testing::Return;

  bigtable::TableAdmin tested(client_, "the-instance");

  EXPECT_CALL(*client_, GetIamPolicy(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "err!")));

  std::string resource = "other-resource";

  EXPECT_FALSE(tested.GetIamPolicy(resource));
}

/// @test Verify recoverable errors for TableAdmin::GetIamPolicy.
TEST_F(TableAdminTest, GetIamPolicyRecoverableError) {
  using ::testing::_;
  using ::testing::Invoke;
  namespace iamproto = ::google::iam::v1;

  bigtable::TableAdmin tested(client_, "the-instance");

  auto mock_recoverable_failure = [](grpc::ClientContext* context,
                                     iamproto::GetIamPolicyRequest const&,
                                     iamproto::Policy*) {
    EXPECT_STATUS_OK(google::cloud::bigtable::testing::IsContextMDValid(
        *context, "google.bigtable.admin.v2.BigtableTableAdmin.GetIamPolicy"));
    return grpc::Status(grpc::StatusCode::UNAVAILABLE, "try-again");
  };
  auto mock_policy = create_get_policy_mock();

  EXPECT_CALL(*client_, GetIamPolicy(_, _, _))
      .WillOnce(Invoke(mock_recoverable_failure))
      .WillOnce(Invoke(mock_policy));

  std::string resource = "test-resource";
  auto policy = tested.GetIamPolicy(resource);
  ASSERT_STATUS_OK(policy);
  EXPECT_EQ(3, policy->version());
  EXPECT_EQ("random-tag", policy->etag());
}

/// @test Verify positive scenario for TableAdmin::SetIamPolicy.
TEST_F(TableAdminTest, SetIamPolicy) {
  using ::testing::_;
  using ::testing::Invoke;

  bigtable::TableAdmin tested(client_, "the-instance");
  auto mock_policy = create_policy_with_params();
  EXPECT_CALL(*client_, SetIamPolicy(_, _, _)).WillOnce(Invoke(mock_policy));

  std::string resource = "test-resource";
  auto iam_policy = bigtable::IamPolicy(
      {bigtable::IamBinding("writer", {"abc@gmail.com", "xyz@gmail.com"})},
      "test-tag", 0);
  auto policy = tested.SetIamPolicy(resource, iam_policy);
  ASSERT_STATUS_OK(policy);

  EXPECT_EQ(1, policy->bindings().size());
  EXPECT_EQ("test-tag", policy->etag());
}

/// @test Verify unrecoverable errors for TableAdmin::SetIamPolicy.
TEST_F(TableAdminTest, SetIamPolicyUnrecoverableError) {
  using ::testing::_;
  using ::testing::Return;

  bigtable::TableAdmin tested(client_, "the-instance");

  EXPECT_CALL(*client_, SetIamPolicy(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "err!")));

  std::string resource = "test-resource";
  auto iam_policy = bigtable::IamPolicy(
      {bigtable::IamBinding("writer", {"abc@gmail.com", "xyz@gmail.com"})},
      "test-tag", 0);
  EXPECT_FALSE(tested.SetIamPolicy(resource, iam_policy));
}

/// @test Verify recoverable errors for TableAdmin::SetIamPolicy.
TEST_F(TableAdminTest, SetIamPolicyRecoverableError) {
  using ::testing::_;
  using ::testing::Invoke;
  namespace iamproto = ::google::iam::v1;

  bigtable::TableAdmin tested(client_, "the-instance");

  auto mock_recoverable_failure = [](grpc::ClientContext* context,
                                     iamproto::SetIamPolicyRequest const&,
                                     iamproto::Policy*) {
    EXPECT_STATUS_OK(google::cloud::bigtable::testing::IsContextMDValid(
        *context, "google.bigtable.admin.v2.BigtableTableAdmin.SetIamPolicy"));
    return grpc::Status(grpc::StatusCode::UNAVAILABLE, "try-again");
  };
  auto mock_policy = create_policy_with_params();

  EXPECT_CALL(*client_, SetIamPolicy(_, _, _))
      .WillOnce(Invoke(mock_recoverable_failure))
      .WillOnce(Invoke(mock_policy));

  std::string resource = "test-resource";
  auto iam_policy = bigtable::IamPolicy(
      {bigtable::IamBinding("writer", {"abc@gmail.com", "xyz@gmail.com"})},
      "test-tag", 0);
  auto policy = tested.SetIamPolicy(resource, iam_policy);
  ASSERT_STATUS_OK(policy);

  EXPECT_EQ(1, policy->bindings().size());
  EXPECT_EQ("test-tag", policy->etag());
}

/// @test Verify that TableAdmin::TestIamPermissions works in simple case.
TEST_F(TableAdminTest, TestIamPermissions) {
  using ::testing::_;
  using ::testing::Invoke;
  namespace iamproto = ::google::iam::v1;
  bigtable::TableAdmin tested(client_, "the-instance");

  auto mock_permission_set =
      [](grpc::ClientContext* context,
         iamproto::TestIamPermissionsRequest const&,
         iamproto::TestIamPermissionsResponse* response) {
        EXPECT_STATUS_OK(google::cloud::bigtable::testing::IsContextMDValid(
            *context,
            "google.bigtable.admin.v2.BigtableTableAdmin.TestIamPermissions"));
        EXPECT_NE(nullptr, response);
        std::vector<std::string> permissions = {"writer", "reader"};
        response->add_permissions(permissions[0]);
        response->add_permissions(permissions[1]);
        return grpc::Status::OK;
      };

  EXPECT_CALL(*client_, TestIamPermissions(_, _, _))
      .WillOnce(Invoke(mock_permission_set));

  std::string resource = "the-resource";
  auto permission_set =
      tested.TestIamPermissions(resource, {"reader", "writer", "owner"});
  ASSERT_STATUS_OK(permission_set);

  EXPECT_EQ(2, permission_set->size());
}

/// @test Test for unrecoverable errors for TableAdmin::TestIamPermissions.
TEST_F(TableAdminTest, TestIamPermissionsUnrecoverableError) {
  using ::testing::_;
  using ::testing::Return;

  bigtable::TableAdmin tested(client_, "the-instance");

  EXPECT_CALL(*client_, TestIamPermissions(_, _, _))
      .WillRepeatedly(
          Return(grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "err!")));

  std::string resource = "other-resource";

  EXPECT_FALSE(
      tested.TestIamPermissions(resource, {"reader", "writer", "owner"}));
}

/// @test Test for recoverable errors for TableAdmin::TestIamPermissions.
TEST_F(TableAdminTest, TestIamPermissionsRecoverableError) {
  using ::testing::_;
  using ::testing::Invoke;
  namespace iamproto = ::google::iam::v1;
  bigtable::TableAdmin tested(client_, "the-instance");

  auto mock_recoverable_failure = [](grpc::ClientContext* context,
                                     iamproto::TestIamPermissionsRequest const&,
                                     iamproto::TestIamPermissionsResponse*) {
    EXPECT_STATUS_OK(google::cloud::bigtable::testing::IsContextMDValid(
        *context,
        "google.bigtable.admin.v2.BigtableTableAdmin.TestIamPermissions"));
    return grpc::Status(grpc::StatusCode::UNAVAILABLE, "try-again");
  };

  auto mock_permission_set =
      [](grpc::ClientContext* context,
         iamproto::TestIamPermissionsRequest const&,
         iamproto::TestIamPermissionsResponse* response) {
        EXPECT_STATUS_OK(google::cloud::bigtable::testing::IsContextMDValid(
            *context,
            "google.bigtable.admin.v2.BigtableTableAdmin.TestIamPermissions"));
        EXPECT_NE(nullptr, response);
        std::vector<std::string> permissions = {"writer", "reader"};
        response->add_permissions(permissions[0]);
        response->add_permissions(permissions[1]);
        return grpc::Status::OK;
      };
  EXPECT_CALL(*client_, TestIamPermissions(_, _, _))
      .WillOnce(Invoke(mock_recoverable_failure))
      .WillOnce(Invoke(mock_permission_set));

  std::string resource = "the-resource";
  auto permission_set =
      tested.TestIamPermissions(resource, {"writer", "reader", "owner"});
  ASSERT_STATUS_OK(permission_set);

  EXPECT_EQ(2, permission_set->size());
}

using MockAsyncCheckConsistencyResponse =
    google::cloud::bigtable::testing::MockAsyncResponseReader<
        ::google::bigtable::admin::v2::CheckConsistencyResponse>;

/**
 * @test Verify that `bigtagble::TableAdmin::AsyncWaitForConsistency` works as
 * expected, with multiple asynchronous calls.
 */
TEST_F(TableAdminTest, AsyncWaitForConsistency_Simple) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;
  using google::cloud::internal::make_unique;

  bigtable::TableAdmin tested(client_, "test-instance");

  auto r1 = make_unique<MockAsyncCheckConsistencyResponse>();
  EXPECT_CALL(*r1, Finish(_, _, _))
      .WillOnce(Invoke([](btadmin::CheckConsistencyResponse* response,
                          grpc::Status* status, void*) {
        ASSERT_NE(nullptr, response);
        *status = grpc::Status(grpc::StatusCode::UNAVAILABLE, "try again");
      }));
  auto r2 = make_unique<MockAsyncCheckConsistencyResponse>();
  EXPECT_CALL(*r2, Finish(_, _, _))
      .WillOnce(Invoke([](btadmin::CheckConsistencyResponse* response,
                          grpc::Status* status, void*) {
        ASSERT_NE(nullptr, response);
        response->set_consistent(false);
        *status = grpc::Status::OK;
      }));
  auto r3 = make_unique<MockAsyncCheckConsistencyResponse>();
  EXPECT_CALL(*r3, Finish(_, _, _))
      .WillOnce(Invoke([](btadmin::CheckConsistencyResponse* response,
                          grpc::Status* status, void*) {
        ASSERT_NE(nullptr, response);
        response->set_consistent(true);
        *status = grpc::Status::OK;
      }));

  auto make_invoke = [](std::unique_ptr<MockAsyncCheckConsistencyResponse>& r) {
    return [&r](grpc::ClientContext* context,
                btadmin::CheckConsistencyRequest const& request,
                grpc::CompletionQueue*) {
      EXPECT_STATUS_OK(google::cloud::bigtable::testing::IsContextMDValid(
          *context,
          "google.bigtable.admin.v2.BigtableTableAdmin.CheckConsistency"));
      EXPECT_EQ(
          "projects/the-project/instances/test-instance/tables/test-table",
          request.name());
      // This is safe, see comments in MockAsyncResponseReader.
      return std::unique_ptr<grpc::ClientAsyncResponseReaderInterface<
          ::btadmin::CheckConsistencyResponse>>(r.get());
    };
  };

  EXPECT_CALL(*client_, project()).WillRepeatedly(ReturnRef(kProjectId));
  EXPECT_CALL(*client_, AsyncCheckConsistency(_, _, _))
      .WillOnce(Invoke(make_invoke(r1)))
      .WillOnce(Invoke(make_invoke(r2)))
      .WillOnce(Invoke(make_invoke(r3)));

  std::shared_ptr<MockCompletionQueue> cq_impl(new MockCompletionQueue);
  bigtable::CompletionQueue cq(cq_impl);

  google::cloud::future<google::cloud::StatusOr<bigtable::Consistency>> result =
      tested.AsyncWaitForConsistency(cq, "test-table", "test-async-token");

  // The future is not ready yet.
  auto future_status = result.wait_for(0_ms);
  EXPECT_EQ(std::future_status::timeout, future_status);

  // Simulate the completions for each event.

  // AsyncCheckConsistency() -> TRANSIENT
  cq_impl->SimulateCompletion(true);
  future_status = result.wait_for(0_ms);
  EXPECT_EQ(std::future_status::timeout, future_status);

  // timer
  cq_impl->SimulateCompletion(true);
  future_status = result.wait_for(0_ms);
  EXPECT_EQ(std::future_status::timeout, future_status);

  // AsyncCheckConsistency() -> !consistent
  cq_impl->SimulateCompletion(true);
  future_status = result.wait_for(0_ms);
  EXPECT_EQ(std::future_status::timeout, future_status);

  // timer
  cq_impl->SimulateCompletion(true);
  future_status = result.wait_for(0_ms);
  EXPECT_EQ(std::future_status::timeout, future_status);

  // AsyncCheckConsistency() -> consistent
  cq_impl->SimulateCompletion(true);
  future_status = result.wait_for(0_ms);
  EXPECT_EQ(std::future_status::ready, future_status);

  // The future becomes ready on the first request that completes with a
  // permanent error.
  auto consistent = result.get();
  ASSERT_STATUS_OK(consistent);

  EXPECT_EQ(bigtable::Consistency::kConsistent, *consistent);
}

/**
 * @test Verify that `bigtable::TableAdmin::AsyncWaitForConsistency` makes only
 * one RPC attempt and reports errors on failure.
 */
TEST_F(TableAdminTest, AsyncWaitForConsistency_Failure) {
  using namespace ::testing;
  using namespace google::cloud::testing_util::chrono_literals;
  using google::cloud::internal::make_unique;

  bigtable::TableAdmin tested(client_, "test-instance");
  auto reader = make_unique<MockAsyncCheckConsistencyResponse>();
  EXPECT_CALL(*reader, Finish(_, _, _))
      .WillOnce(Invoke([](btadmin::CheckConsistencyResponse* response,
                          grpc::Status* status, void*) {
        ASSERT_NE(nullptr, response);
        *status = grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "oh no");
      }));
  EXPECT_CALL(*client_, project()).WillRepeatedly(ReturnRef(kProjectId));
  EXPECT_CALL(*client_, AsyncCheckConsistency(_, _, _))
      .WillOnce(Invoke([&](grpc::ClientContext* context,
                           btadmin::CheckConsistencyRequest const& request,
                           grpc::CompletionQueue*) {
        EXPECT_STATUS_OK(google::cloud::bigtable::testing::IsContextMDValid(
            *context,
            "google.bigtable.admin.v2.BigtableTableAdmin.CheckConsistency"));
        EXPECT_EQ(
            "projects/the-project/instances/test-instance/tables/test-table",
            request.name());
        // This is safe, see comments in MockAsyncResponseReader.
        return std::unique_ptr<grpc::ClientAsyncResponseReaderInterface<
            ::btadmin::CheckConsistencyResponse>>(reader.get());
      }));

  std::shared_ptr<MockCompletionQueue> cq_impl(new MockCompletionQueue);
  bigtable::CompletionQueue cq(cq_impl);

  google::cloud::future<google::cloud::StatusOr<bigtable::Consistency>> result =
      tested.AsyncWaitForConsistency(cq, "test-table", "test-async-token");

  // The future is not ready yet.
  auto future_status = result.wait_for(0_ms);
  EXPECT_EQ(std::future_status::timeout, future_status);
  cq_impl->SimulateCompletion(true);

  // The future becomes ready on the first request that completes with a
  // permanent error.
  future_status = result.wait_for(0_ms);
  EXPECT_EQ(std::future_status::ready, future_status);

  auto consistent = result.get();
  EXPECT_FALSE(consistent.ok());

  EXPECT_EQ(google::cloud::StatusCode::kPermissionDenied,
            consistent.status().code());
}

class ValidContextMdAsyncTest : public ::testing::Test {
 public:
  ValidContextMdAsyncTest()
      : cq_impl_(new MockCompletionQueue),
        cq_(cq_impl_),
        client_(new MockAdminClient) {
    EXPECT_CALL(*client_, project())
        .WillRepeatedly(::testing::ReturnRef(kProjectId));
    table_admin_ = google::cloud::internal::make_unique<bigtable::TableAdmin>(
        client_, kInstanceId);
  }

 protected:
  template <typename ResultType>
  void FinishTest(
      google::cloud::future<google::cloud::StatusOr<ResultType>> res_future) {
    EXPECT_EQ(1U, cq_impl_->size());
    cq_impl_->SimulateCompletion(true);
    EXPECT_EQ(0U, cq_impl_->size());
    auto res = res_future.get();
    EXPECT_FALSE(res);
    EXPECT_EQ(google::cloud::StatusCode::kPermissionDenied,
              res.status().code());
  }

  void FinishTest(google::cloud::future<google::cloud::Status> res_future) {
    EXPECT_EQ(1U, cq_impl_->size());
    cq_impl_->SimulateCompletion(true);
    EXPECT_EQ(0U, cq_impl_->size());
    auto res = res_future.get();
    EXPECT_EQ(google::cloud::StatusCode::kPermissionDenied, res.code());
  }

  std::shared_ptr<MockCompletionQueue> cq_impl_;
  bigtable::CompletionQueue cq_;
  std::shared_ptr<bigtable::testing::MockAdminClient> client_;
  std::unique_ptr<bigtable::TableAdmin> table_admin_;
};

TEST_F(ValidContextMdAsyncTest, AsyncCreateTable) {
  using ::testing::_;
  bigtable::testing::MockAsyncFailingRpcFactory<btadmin::CreateTableRequest,
                                                btadmin::Table>
      rpc_factory;
  EXPECT_CALL(*client_, AsyncCreateTable(_, _, _))
      .WillOnce(::testing::Invoke(rpc_factory.Create(
          R"""(
              parent: "projects/the-project/instances/the-instance"
              table_id: "the-table"
              table: { }
          )""",
          "google.bigtable.admin.v2.BigtableTableAdmin.CreateTable")));
  FinishTest(table_admin_->AsyncCreateTable(cq_, "the-table",
                                            bigtable::TableConfig()));
}

TEST_F(ValidContextMdAsyncTest, AsyncDeleteTable) {
  using ::testing::_;
  bigtable::testing::MockAsyncFailingRpcFactory<btadmin::DeleteTableRequest,
                                                google::protobuf::Empty>
      rpc_factory;
  EXPECT_CALL(*client_, AsyncDeleteTable(_, _, _))
      .WillOnce(::testing::Invoke(rpc_factory.Create(
          R"""(
              name: "projects/the-project/instances/the-instance/tables/the-table"
          )""",
          "google.bigtable.admin.v2.BigtableTableAdmin.DeleteTable")));
  FinishTest(table_admin_->AsyncDeleteTable(cq_, "the-table"));
}

TEST_F(ValidContextMdAsyncTest, AsyncDropAllRows) {
  using ::testing::_;
  bigtable::testing::MockAsyncFailingRpcFactory<btadmin::DropRowRangeRequest,
                                                google::protobuf::Empty>
      rpc_factory;
  EXPECT_CALL(*client_, AsyncDropRowRange(_, _, _))
      .WillOnce(::testing::Invoke(rpc_factory.Create(
          R"""(
              name: "projects/the-project/instances/the-instance/tables/the-table"
              delete_all_data_from_table: true
          )""",
          "google.bigtable.admin.v2.BigtableTableAdmin.DropRowRange")));
  FinishTest(table_admin_->AsyncDropAllRows(cq_, "the-table"));
}

TEST_F(ValidContextMdAsyncTest, AsyncDropRowsByPrefix) {
  using ::testing::_;
  bigtable::testing::MockAsyncFailingRpcFactory<btadmin::DropRowRangeRequest,
                                                google::protobuf::Empty>
      rpc_factory;
  EXPECT_CALL(*client_, AsyncDropRowRange(_, _, _))
      .WillOnce(::testing::Invoke(rpc_factory.Create(
          R"""(
              name: "projects/the-project/instances/the-instance/tables/the-table"
              row_key_prefix: "prefix"
          )""",
          "google.bigtable.admin.v2.BigtableTableAdmin.DropRowRange")));
  FinishTest(table_admin_->AsyncDropRowsByPrefix(cq_, "the-table", "prefix"));
}

TEST_F(ValidContextMdAsyncTest, AsyncGenerateConsistencyToken) {
  using ::testing::_;
  bigtable::testing::MockAsyncFailingRpcFactory<
      btadmin::GenerateConsistencyTokenRequest,
      btadmin::GenerateConsistencyTokenResponse>
      rpc_factory;
  EXPECT_CALL(*client_, AsyncGenerateConsistencyToken(_, _, _))
      .WillOnce(::testing::Invoke(rpc_factory.Create(
          R"""(
              name: "projects/the-project/instances/the-instance/tables/the-table"
          )""",
          "google.bigtable.admin.v2.BigtableTableAdmin."
          "GenerateConsistencyToken")));
  FinishTest(table_admin_->AsyncGenerateConsistencyToken(cq_, "the-table"));
}

TEST_F(ValidContextMdAsyncTest, AsyncListTables) {
  using ::testing::_;
  bigtable::testing::MockAsyncFailingRpcFactory<btadmin::ListTablesRequest,
                                                btadmin::ListTablesResponse>
      rpc_factory;
  EXPECT_CALL(*client_, AsyncListTables(_, _, _))
      .WillOnce(::testing::Invoke(rpc_factory.Create(
          R"""(
              parent: "projects/the-project/instances/the-instance"
              view: SCHEMA_VIEW
          )""",
          "google.bigtable.admin.v2.BigtableTableAdmin.ListTables")));
  FinishTest(table_admin_->AsyncListTables(cq_, btadmin::Table::SCHEMA_VIEW));
}

TEST_F(ValidContextMdAsyncTest, AsyncModifyColumnFamilies) {
  using ::testing::_;
  bigtable::testing::MockAsyncFailingRpcFactory<
      btadmin::ModifyColumnFamiliesRequest, btadmin::Table>
      rpc_factory;
  EXPECT_CALL(*client_, AsyncModifyColumnFamilies(_, _, _))
      .WillOnce(::testing::Invoke(rpc_factory.Create(
          R"""(
              name: "projects/the-project/instances/the-instance/tables/the-table"
          )""",
          "google.bigtable.admin.v2.BigtableTableAdmin.ModifyColumnFamilies")));
  FinishTest(table_admin_->AsyncModifyColumnFamilies(cq_, "the-table", {}));
}

using MockAsyncIamPolicyReader =
    google::cloud::bigtable::testing::MockAsyncResponseReader<
        ::google::iam::v1::Policy>;

class AsyncGetIamPolicyTest : public ::testing::Test {
 public:
  AsyncGetIamPolicyTest()
      : cq_impl_(new MockCompletionQueue),
        cq_(cq_impl_),
        client_(new bigtable::testing::MockAdminClient),
        reader_(new MockAsyncIamPolicyReader) {
    using namespace ::testing;
    EXPECT_CALL(*client_, project()).WillRepeatedly(ReturnRef(kProjectId));
    EXPECT_CALL(*client_, AsyncGetIamPolicy(_, _, _))
        .WillOnce(Invoke([this](grpc::ClientContext* context,
                                ::google::iam::v1::GetIamPolicyRequest const&
                                    request,
                                grpc::CompletionQueue*) {
          EXPECT_STATUS_OK(google::cloud::bigtable::testing::IsContextMDValid(
              *context,
              "google.bigtable.admin.v2.BigtableTableAdmin.GetIamPolicy"));
          EXPECT_EQ(
              "projects/the-project/instances/the-instance/tables/the-table",
              request.resource());
          // This is safe, see comments in MockAsyncResponseReader.
          return std::unique_ptr<grpc::ClientAsyncResponseReaderInterface<
              ::google::iam::v1::Policy>>(reader_.get());
        }));
  }

 protected:
  void Start() {
    bigtable::TableAdmin table_admin(client_, "the-instance");
    user_future_ = table_admin.AsyncGetIamPolicy(cq_, "the-table");
  }

  std::shared_ptr<MockCompletionQueue> cq_impl_;
  bigtable::CompletionQueue cq_;
  std::shared_ptr<bigtable::testing::MockAdminClient> client_;
  google::cloud::future<google::cloud::StatusOr<google::iam::v1::Policy>>
      user_future_;
  std::unique_ptr<MockAsyncIamPolicyReader> reader_;
};

/// @test Verify that AsyncGetIamPolicy works in simple case.
TEST_F(AsyncGetIamPolicyTest, AsyncGetIamPolicy) {
  using ::testing::_;
  using ::testing::Invoke;
  namespace iamproto = ::google::iam::v1;
  bigtable::TableAdmin tested(client_, "the-instance");

  EXPECT_CALL(*reader_, Finish(_, _, _))
      .WillOnce(
          Invoke([](iamproto::Policy* response, grpc::Status* status, void*) {
            EXPECT_NE(nullptr, response);
            response->set_version(3);
            response->set_etag("random-tag");
            *status = grpc::Status::OK;
          }));

  Start();
  EXPECT_EQ(std::future_status::timeout, user_future_.wait_for(1_ms));
  EXPECT_EQ(1, cq_impl_->size());
  cq_impl_->SimulateCompletion(true);
  auto policy = user_future_.get();
  ASSERT_STATUS_OK(policy);
  EXPECT_EQ(3, policy->version());
  EXPECT_EQ("random-tag", policy->etag());
}

/// @test Test unrecoverable errors for TableAdmin::AsyncGetIamPolicy.
TEST_F(AsyncGetIamPolicyTest, AsyncGetIamPolicyUnrecoverableError) {
  using ::testing::_;
  using ::testing::Invoke;
  namespace iamproto = ::google::iam::v1;
  bigtable::TableAdmin tested(client_, "the-instance");

  EXPECT_CALL(*reader_, Finish(_, _, _))
      .WillOnce(
          Invoke([](iamproto::Policy* response, grpc::Status* status, void*) {
            EXPECT_NE(nullptr, response);
            *status = grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "nooo");
          }));

  Start();
  EXPECT_EQ(std::future_status::timeout, user_future_.wait_for(1_ms));
  EXPECT_EQ(1, cq_impl_->size());
  cq_impl_->SimulateCompletion(true);

  auto policy = user_future_.get();
  ASSERT_FALSE(policy);
  ASSERT_EQ(google::cloud::StatusCode::kPermissionDenied,
            policy.status().code());
}

using MockAsyncSetIamPolicyReader =
    google::cloud::bigtable::testing::MockAsyncResponseReader<
        ::google::iam::v1::Policy>;

class AsyncSetIamPolicyTest : public ::testing::Test {
 public:
  AsyncSetIamPolicyTest()
      : cq_impl_(new MockCompletionQueue),
        cq_(cq_impl_),
        client_(new bigtable::testing::MockAdminClient),
        reader_(new MockAsyncSetIamPolicyReader) {
    using namespace ::testing;
    EXPECT_CALL(*client_, project()).WillRepeatedly(ReturnRef(kProjectId));
    EXPECT_CALL(*client_, AsyncSetIamPolicy(_, _, _))
        .WillOnce(Invoke([this](grpc::ClientContext* context,
                                ::google::iam::v1::SetIamPolicyRequest const&
                                    request,
                                grpc::CompletionQueue*) {
          EXPECT_STATUS_OK(google::cloud::bigtable::testing::IsContextMDValid(
              *context,
              "google.bigtable.admin.v2.BigtableTableAdmin.SetIamPolicy"));
          EXPECT_EQ(
              "projects/the-project/instances/the-instance/tables/the-table",
              request.resource());
          // This is safe, see comments in MockAsyncResponseReader.
          return std::unique_ptr<grpc::ClientAsyncResponseReaderInterface<
              ::google::iam::v1::Policy>>(reader_.get());
        }));
  }

 protected:
  void Start() {
    bigtable::TableAdmin table_admin(client_, "the-instance");
    user_future_ = table_admin.AsyncSetIamPolicy(
        cq_, "the-table",
        bigtable::IamPolicy({bigtable::IamBinding(
                                "writer", {"abc@gmail.com", "xyz@gmail.com"})},
                            "test-tag", 0));
  }

  std::shared_ptr<MockCompletionQueue> cq_impl_;
  bigtable::CompletionQueue cq_;
  std::shared_ptr<bigtable::testing::MockAdminClient> client_;
  google::cloud::future<google::cloud::StatusOr<google::iam::v1::Policy>>
      user_future_;
  std::unique_ptr<MockAsyncSetIamPolicyReader> reader_;
};

/// @test Verify that AsyncSetIamPolicy works in simple case.
TEST_F(AsyncSetIamPolicyTest, AsyncSetIamPolicy) {
  using ::testing::_;
  using ::testing::Invoke;
  namespace iamproto = ::google::iam::v1;
  bigtable::TableAdmin tested(client_, "the-instance");

  EXPECT_CALL(*reader_, Finish(_, _, _))
      .WillOnce(
          Invoke([](iamproto::Policy* response, grpc::Status* status, void*) {
            EXPECT_NE(nullptr, response);

            auto new_binding = response->add_bindings();
            new_binding->set_role("writer");
            new_binding->add_members("abc@gmail.com");
            new_binding->add_members("xyz@gmail.com");
            response->set_etag("test-tag");
            *status = grpc::Status::OK;
          }));

  Start();
  EXPECT_EQ(std::future_status::timeout, user_future_.wait_for(1_ms));
  EXPECT_EQ(1, cq_impl_->size());
  cq_impl_->SimulateCompletion(true);
  auto policy = user_future_.get();
  ASSERT_STATUS_OK(policy);

  EXPECT_EQ(1, policy->bindings().size());
  EXPECT_EQ("test-tag", policy->etag());
}

/// @test Test unrecoverable errors for TableAdmin::AsyncSetIamPolicy.
TEST_F(AsyncSetIamPolicyTest, AsyncSetIamPolicyUnrecoverableError) {
  using ::testing::_;
  using ::testing::Invoke;
  namespace iamproto = ::google::iam::v1;
  bigtable::TableAdmin tested(client_, "the-instance");

  EXPECT_CALL(*reader_, Finish(_, _, _))
      .WillOnce(
          Invoke([](iamproto::Policy* response, grpc::Status* status, void*) {
            EXPECT_NE(nullptr, response);
            *status = grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "nooo");
          }));

  Start();
  EXPECT_EQ(std::future_status::timeout, user_future_.wait_for(1_ms));
  EXPECT_EQ(1, cq_impl_->size());
  cq_impl_->SimulateCompletion(true);

  auto policy = user_future_.get();
  ASSERT_FALSE(policy);
  ASSERT_EQ(google::cloud::StatusCode::kPermissionDenied,
            policy.status().code());
}

using MockAsyncTestIamPermissionsReader =
    google::cloud::bigtable::testing::MockAsyncResponseReader<
        ::google::iam::v1::TestIamPermissionsResponse>;

class AsyncTestIamPermissionsTest : public ::testing::Test {
 public:
  AsyncTestIamPermissionsTest()
      : cq_impl_(new MockCompletionQueue),
        cq_(cq_impl_),
        client_(new bigtable::testing::MockAdminClient),
        reader_(new MockAsyncTestIamPermissionsReader) {
    using namespace ::testing;
    EXPECT_CALL(*client_, project()).WillRepeatedly(ReturnRef(kProjectId));
    EXPECT_CALL(*client_, AsyncTestIamPermissions(_, _, _))
        .WillOnce(Invoke([this](
                             grpc::ClientContext* context,
                             ::google::iam::v1::TestIamPermissionsRequest const&
                                 request,
                             grpc::CompletionQueue*) {
          EXPECT_STATUS_OK(google::cloud::bigtable::testing::IsContextMDValid(
              *context,
              "google.bigtable.admin.v2.BigtableTableAdmin."
              "TestIamPermissions"));
          EXPECT_EQ(
              "projects/the-project/instances/the-instance/tables/the-table",
              request.resource());
          // This is safe, see comments in MockAsyncResponseReader.
          return std::unique_ptr<grpc::ClientAsyncResponseReaderInterface<
              ::google::iam::v1::TestIamPermissionsResponse>>(reader_.get());
        }));
  }

 protected:
  void Start(std::vector<std::string> permissions) {
    bigtable::TableAdmin table_admin(client_, "the-instance");
    user_future_ = table_admin.AsyncTestIamPermissions(cq_, "the-table",
                                                       std::move(permissions));
  }

  std::shared_ptr<MockCompletionQueue> cq_impl_;
  bigtable::CompletionQueue cq_;
  std::shared_ptr<bigtable::testing::MockAdminClient> client_;
  google::cloud::future<google::cloud::StatusOr<std::vector<std::string>>>
      user_future_;
  std::unique_ptr<MockAsyncTestIamPermissionsReader> reader_;
};

/// @test Verify that AsyncTestIamPermissions works in simple case.
TEST_F(AsyncTestIamPermissionsTest, AsyncTestIamPermissions) {
  using ::testing::_;
  using ::testing::Invoke;
  namespace iamproto = ::google::iam::v1;
  bigtable::TableAdmin tested(client_, "the-instance");

  EXPECT_CALL(*reader_, Finish(_, _, _))
      .WillOnce(Invoke([](iamproto::TestIamPermissionsResponse* response,
                          grpc::Status* status, void*) {
        EXPECT_NE(nullptr, response);
        response->add_permissions("writer");
        response->add_permissions("reader");
        *status = grpc::Status::OK;
      }));

  Start({"reader", "writer", "owner"});
  EXPECT_EQ(std::future_status::timeout, user_future_.wait_for(1_ms));
  EXPECT_EQ(1, cq_impl_->size());
  cq_impl_->SimulateCompletion(true);
  auto permission_set = user_future_.get();
  ASSERT_STATUS_OK(permission_set);
  EXPECT_EQ(2, permission_set->size());
}

/// @test Test unrecoverable errors for TableAdmin::AsyncTestIamPermissions.
TEST_F(AsyncTestIamPermissionsTest, AsyncTestIamPermissionsUnrecoverableError) {
  using ::testing::_;
  using ::testing::Invoke;
  namespace iamproto = ::google::iam::v1;
  bigtable::TableAdmin tested(client_, "the-instance");

  EXPECT_CALL(*reader_, Finish(_, _, _))
      .WillOnce(Invoke([](iamproto::TestIamPermissionsResponse* response,
                          grpc::Status* status, void*) {
        EXPECT_NE(nullptr, response);
        *status = grpc::Status(grpc::StatusCode::PERMISSION_DENIED, "nooo");
      }));

  Start({"reader", "writer", "owner"});
  EXPECT_EQ(std::future_status::timeout, user_future_.wait_for(1_ms));
  EXPECT_EQ(1, cq_impl_->size());
  cq_impl_->SimulateCompletion(true);

  auto permission_set = user_future_.get();
  ASSERT_FALSE(permission_set);
  ASSERT_EQ(google::cloud::StatusCode::kPermissionDenied,
            permission_set.status().code());
}
