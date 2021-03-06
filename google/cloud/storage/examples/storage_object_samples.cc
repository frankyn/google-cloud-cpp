// Copyright 2018 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "google/cloud/storage/client.h"
#include "google/cloud/storage/examples/storage_examples_common.h"
#include "google/cloud/storage/oauth2/google_credentials.h"
#include "google/cloud/storage/parallel_upload.h"
#include "google/cloud/storage/well_known_parameters.h"
#include "google/cloud/internal/getenv.h"
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>

namespace {

void ListObjects(google::cloud::storage::Client client,
                 std::vector<std::string> const& argv) {
  //! [list objects] [START storage_list_files]
  namespace gcs = google::cloud::storage;
  [](gcs::Client client, std::string bucket_name) {
    for (auto&& object_metadata : client.ListObjects(bucket_name)) {
      if (!object_metadata) {
        throw std::runtime_error(object_metadata.status().message());
      }

      std::cout << "bucket_name=" << object_metadata->bucket()
                << ", object_name=" << object_metadata->name() << "\n";
    }
  }
  //! [list objects] [END storage_list_files]
  (std::move(client), argv.at(0));
}

void ListObjectsWithPrefix(google::cloud::storage::Client client,
                           std::vector<std::string> const& argv) {
  //! [list objects with prefix] [START storage_list_files_with_prefix]
  namespace gcs = google::cloud::storage;
  [](gcs::Client client, std::string bucket_name, std::string bucket_prefix) {
    for (auto&& object_metadata :
         client.ListObjects(bucket_name, gcs::Prefix(bucket_prefix))) {
      if (!object_metadata) {
        throw std::runtime_error(object_metadata.status().message());
      }

      std::cout << "bucket_name=" << object_metadata->bucket()
                << ", object_name=" << object_metadata->name() << "\n";
    }
  }
  //! [list objects with prefix] [END storage_list_files_with_prefix]
  (std::move(client), argv.at(0), argv.at(1));
}

void ListVersionedObjects(google::cloud::storage::Client client,
                          std::vector<std::string> const& argv) {
  //! [list versioned objects] [START storage_list_file_archived_generations]
  namespace gcs = google::cloud::storage;
  [](gcs::Client client, std::string bucket_name) {
    for (auto&& object_metadata :
         client.ListObjects(bucket_name, gcs::Versions{true})) {
      if (!object_metadata) {
        throw std::runtime_error(object_metadata.status().message());
      }

      std::cout << "bucket_name=" << object_metadata->bucket()
                << ", object_name=" << object_metadata->name()
                << ", generation=" << object_metadata->generation() << "\n";
    }
  }
  //! [list versioned objects] [END storage_list_file_archived_generations]
  (std::move(client), argv.at(0));
}

void InsertObject(google::cloud::storage::Client client,
                  std::vector<std::string> const& argv) {
  //! [insert object]
  namespace gcs = google::cloud::storage;
  using ::google::cloud::StatusOr;
  [](gcs::Client client, std::string bucket_name, std::string object_name,
     std::string contents) {
    StatusOr<gcs::ObjectMetadata> object_metadata =
        client.InsertObject(bucket_name, object_name, std::move(contents));

    if (!object_metadata) {
      throw std::runtime_error(object_metadata.status().message());
    }

    std::cout << "The object " << object_metadata->name()
              << " was created in bucket " << object_metadata->bucket()
              << "\nFull metadata: " << *object_metadata << "\n";
  }
  //! [insert object]
  (std::move(client), argv.at(0), argv.at(1), argv.at(2));
}

void InsertObjectStrictIdempotency(google::cloud::storage::Client,
                                   std::vector<std::string> const& argv) {
  //! [insert object strict idempotency]
  namespace gcs = google::cloud::storage;
  using ::google::cloud::StatusOr;
  [](std::string bucket_name, std::string object_name, std::string contents) {
    // Create a client that only retries idempotent operations, the default is
    // to retry all operations.
    StatusOr<gcs::ClientOptions> options =
        gcs::ClientOptions::CreateDefaultClientOptions();

    if (!options) throw std::runtime_error(options.status().message());
    gcs::Client client{*options, gcs::StrictIdempotencyPolicy()};
    StatusOr<gcs::ObjectMetadata> object_metadata =
        client.InsertObject(bucket_name, object_name, std::move(contents),
                            gcs::IfGenerationMatch(0));

    if (!object_metadata) {
      throw std::runtime_error(object_metadata.status().message());
    }

    std::cout << "The object " << object_metadata->name()
              << " was created in bucket " << object_metadata->bucket()
              << "\nFull metadata: " << *object_metadata << "\n";
  }
  //! [insert object strict idempotency]
  (argv.at(0), argv.at(1), argv.at(2));
}

void InsertObjectModifiedRetry(google::cloud::storage::Client,
                               std::vector<std::string> const& argv) {
  //! [insert object modified retry]
  namespace gcs = google::cloud::storage;
  using ::google::cloud::StatusOr;
  [](std::string bucket_name, std::string object_name, std::string contents) {
    // Create a client that only gives up on the third error. The default policy
    // is to retry for several minutes.
    StatusOr<gcs::ClientOptions> options =
        gcs::ClientOptions::CreateDefaultClientOptions();

    if (!options) throw std::runtime_error(options.status().message());
    gcs::Client client{*options, gcs::LimitedErrorCountRetryPolicy(3)};

    StatusOr<gcs::ObjectMetadata> object_metadata =
        client.InsertObject(bucket_name, object_name, std::move(contents),
                            gcs::IfGenerationMatch(0));

    if (!object_metadata) {
      throw std::runtime_error(object_metadata.status().message());
    }

    std::cout << "The object " << object_metadata->name()
              << " was created in bucket " << object_metadata->bucket()
              << "\nFull metadata: " << *object_metadata << "\n";
  }
  //! [insert object modified retry]
  (argv.at(0), argv.at(1), argv.at(2));
}

void InsertObjectMultipart(google::cloud::storage::Client client,
                           std::vector<std::string> const& argv) {
  //! [insert object multipart]
  namespace gcs = google::cloud::storage;
  using ::google::cloud::StatusOr;
  [](gcs::Client client, std::string bucket_name, std::string object_name,
     std::string content_type, std::string contents) {
    // Setting the object metadata (via the `gcs::WithObjectMadata` option)
    // requires a multipart upload, the library prefers simple uploads unless
    // required as in this case.
    StatusOr<gcs::ObjectMetadata> object_metadata = client.InsertObject(
        bucket_name, object_name, std::move(contents),
        gcs::WithObjectMetadata(
            gcs::ObjectMetadata().set_content_type(content_type)));

    if (!object_metadata) {
      throw std::runtime_error(object_metadata.status().message());
    }

    std::cout << "The object " << object_metadata->name()
              << " was created in bucket " << object_metadata->bucket()
              << "\nThe contentType was set to "
              << object_metadata->content_type()
              << "\nFull metadata: " << *object_metadata << "\n";
  }
  //! [insert object multipart]
  (std::move(client), argv.at(0), argv.at(1), argv.at(2), argv.at(3));
}

void CopyObject(google::cloud::storage::Client client,
                std::vector<std::string> const& argv) {
  //! [copy object] [START storage_copy_file]
  namespace gcs = google::cloud::storage;
  using ::google::cloud::StatusOr;
  [](gcs::Client client, std::string source_bucket_name,
     std::string source_object_name, std::string destination_bucket_name,
     std::string destination_object_name) {
    StatusOr<gcs::ObjectMetadata> new_copy_meta =
        client.CopyObject(source_bucket_name, source_object_name,
                          destination_bucket_name, destination_object_name);

    if (!new_copy_meta) {
      throw std::runtime_error(new_copy_meta.status().message());
    }

    std::cout << "Successfully copied " << source_object_name << " in bucket "
              << source_bucket_name << " to bucket " << new_copy_meta->bucket()
              << " with name " << new_copy_meta->name()
              << ".\nThe full metadata after the copy is: " << *new_copy_meta
              << "\n";
  }
  //! [copy object] [END storage_copy_file]
  (std::move(client), argv.at(0), argv.at(1), argv.at(2), argv.at(3));
}

void GetObjectMetadata(google::cloud::storage::Client client,
                       std::vector<std::string> const& argv) {
  //! [get object metadata] [START storage_get_metadata]
  namespace gcs = google::cloud::storage;
  using ::google::cloud::StatusOr;
  [](gcs::Client client, std::string bucket_name, std::string object_name) {
    StatusOr<gcs::ObjectMetadata> object_metadata =
        client.GetObjectMetadata(bucket_name, object_name);

    if (!object_metadata) {
      throw std::runtime_error(object_metadata.status().message());
    }

    std::cout << "The metadata for object " << object_metadata->name()
              << " in bucket " << object_metadata->bucket() << " is "
              << *object_metadata << "\n";
  }
  //! [get object metadata] [END storage_get_metadata]
  (std::move(client), argv.at(0), argv.at(1));
}

void ReadObject(google::cloud::storage::Client client,
                std::vector<std::string> const& argv) {
  //! [read object] [START storage_download_file]
  namespace gcs = google::cloud::storage;
  [](gcs::Client client, std::string bucket_name, std::string object_name) {
    gcs::ObjectReadStream stream = client.ReadObject(bucket_name, object_name);

    int count = 0;
    std::string line;
    while (std::getline(stream, line, '\n')) {
      ++count;
    }

    std::cout << "The object has " << count << " lines\n";
  }
  //! [read object] [END storage_download_file]
  (std::move(client), argv.at(0), argv.at(1));
}

void ReadObjectRange(google::cloud::storage::Client client,
                     std::vector<std::string> const& argv) {
  //! [read object range] [START storage_download_byte_range]
  namespace gcs = google::cloud::storage;
  [](gcs::Client client, std::string bucket_name, std::string object_name,
     std::int64_t start, std::int64_t end) {
    gcs::ObjectReadStream stream =
        client.ReadObject(bucket_name, object_name, gcs::ReadRange(start, end));

    int count = 0;
    std::string line;
    while (std::getline(stream, line, '\n')) {
      std::cout << line << "\n";
      ++count;
    }

    std::cout << "The requested range has " << count << " lines\n";
  }
  //! [read object range] [END storage_download_byte_range]
  (std::move(client), argv.at(0), argv.at(1), std::stoll(argv.at(2)),
   std::stoll(argv.at(3)));
}

void DeleteObject(google::cloud::storage::Client client,
                  std::vector<std::string> const& argv) {
  //! [delete object] [START storage_delete_file]
  namespace gcs = google::cloud::storage;
  [](gcs::Client client, std::string bucket_name, std::string object_name) {
    google::cloud::Status status =
        client.DeleteObject(bucket_name, object_name);

    if (!status.ok()) throw std::runtime_error(status.message());
    std::cout << "Deleted " << object_name << " in bucket " << bucket_name
              << "\n";
  }
  //! [delete object] [END storage_delete_file]
  (std::move(client), argv.at(0), argv.at(1));
}

void WriteObject(google::cloud::storage::Client client,
                 std::vector<std::string> const& argv) {
  //! [write object] [START storage_stream_file_upload]
  namespace gcs = google::cloud::storage;
  using ::google::cloud::StatusOr;
  [](gcs::Client client, std::string bucket_name, std::string object_name,
     long desired_line_count) {
    std::string const text = "Lorem ipsum dolor sit amet";
    gcs::ObjectWriteStream stream =
        client.WriteObject(bucket_name, object_name);

    for (int lineno = 0; lineno != desired_line_count; ++lineno) {
      // Add 1 to the counter, because it is conventional to number lines
      // starting at 1.
      stream << (lineno + 1) << ": " << text << "\n";
    }

    stream.Close();

    StatusOr<gcs::ObjectMetadata> metadata = std::move(stream).metadata();
    if (!metadata) throw std::runtime_error(metadata.status().message());
    std::cout << "Successfully wrote to object " << metadata->name()
              << " its size is: " << metadata->size()
              << "\nFull metadata: " << *metadata << "\n";
  }
  //! [write object] [END storage_stream_file_upload]
  (std::move(client), argv.at(0), argv.at(1), std::stol(argv.at(2)));
}

void UpdateObjectMetadata(google::cloud::storage::Client client,
                          std::vector<std::string> const& argv) {
  //! [update object metadata] [START storage_set_metadata]
  namespace gcs = google::cloud::storage;
  using ::google::cloud::StatusOr;
  [](gcs::Client client, std::string bucket_name, std::string object_name,
     std::string key, std::string value) {
    StatusOr<gcs::ObjectMetadata> object_metadata =
        client.GetObjectMetadata(bucket_name, object_name);

    if (!object_metadata) {
      throw std::runtime_error(object_metadata.status().message());
    }

    gcs::ObjectMetadata desired = *object_metadata;
    desired.mutable_metadata().emplace(key, value);

    StatusOr<gcs::ObjectMetadata> updated =
        client.UpdateObject(bucket_name, object_name, desired,
                            gcs::Generation(object_metadata->generation()));

    if (!updated) throw std::runtime_error(updated.status().message());
    std::cout << "Object updated. The full metadata after the update is: "
              << *updated << "\n";
  }
  //! [update object metadata] [END storage_set_metadata]
  (std::move(client), argv.at(0), argv.at(1), argv.at(2), argv.at(3));
}

void PatchObjectDeleteMetadata(google::cloud::storage::Client client,
                               std::vector<std::string> const& argv) {
  //! [patch object delete metadata]
  namespace gcs = google::cloud::storage;
  using ::google::cloud::StatusOr;
  [](gcs::Client client, std::string bucket_name, std::string object_name,
     std::string key) {
    StatusOr<gcs::ObjectMetadata> original =
        client.GetObjectMetadata(bucket_name, object_name);

    if (!original) throw std::runtime_error(original.status().message());
    gcs::ObjectMetadata desired = *original;
    desired.mutable_metadata().erase(key);

    StatusOr<gcs::ObjectMetadata> updated =
        client.PatchObject(bucket_name, object_name, *original, desired);

    if (!updated) throw std::runtime_error(updated.status().message());
    std::cout << "Object updated. The full metadata after the update is: "
              << *updated << "\n";
  }
  //! [patch object delete metadata]
  (std::move(client), argv.at(0), argv.at(1), argv.at(2));
}

void PatchObjectContentType(google::cloud::storage::Client client,
                            std::vector<std::string> const& argv) {
  //! [patch object content type]
  namespace gcs = google::cloud::storage;
  using ::google::cloud::StatusOr;
  [](gcs::Client client, std::string bucket_name, std::string object_name,
     std::string content_type) {
    StatusOr<gcs::ObjectMetadata> updated = client.PatchObject(
        bucket_name, object_name,
        gcs::ObjectMetadataPatchBuilder().SetContentType(content_type));

    if (!updated) throw std::runtime_error(updated.status().message());
    std::cout << "Object updated. The full metadata after the update is: "
              << *updated << "\n";
  }
  //! [patch object content type]
  (std::move(client), argv.at(0), argv.at(1), argv.at(2));
}

void ComposeObject(google::cloud::storage::Client client,
                   std::vector<std::string> const& argv) {
  auto it = argv.cbegin();
  auto bucket_name = *it++;
  auto destination_object_name = *it++;
  std::vector<google::cloud::storage::ComposeSourceObject> compose_objects;
  do {
    compose_objects.push_back({*it++, {}, {}});
  } while (it != argv.cend());

  //! [compose object] [START storage_compose_file]
  namespace gcs = google::cloud::storage;
  using ::google::cloud::StatusOr;
  [](gcs::Client client, std::string bucket_name,
     std::string destination_object_name,
     std::vector<gcs::ComposeSourceObject> compose_objects) {
    StatusOr<gcs::ObjectMetadata> composed_object = client.ComposeObject(
        bucket_name, compose_objects, destination_object_name);

    if (!composed_object) {
      throw std::runtime_error(composed_object.status().message());
    }

    std::cout << "Composed new object " << composed_object->name()
              << " in bucket " << composed_object->bucket()
              << "\nFull metadata: " << *composed_object << "\n";
  }
  //! [compose object] [END storage_compose_file]
  (std::move(client), bucket_name, destination_object_name,
   std::move(compose_objects));
}

void ComposeObjectFromMany(google::cloud::storage::Client client,
                           std::vector<std::string> const& argv) {
  auto it = argv.cbegin();
  auto bucket_name = *it++;
  auto destination_object_name = *it++;
  std::vector<google::cloud::storage::ComposeSourceObject> compose_objects;
  do {
    compose_objects.push_back({*it++, {}, {}});
  } while (it != argv.cend());

  //! [compose object from many] [START storage_compose_file_from_many]
  namespace gcs = google::cloud::storage;
  using ::google::cloud::StatusOr;
  [](gcs::Client client, std::string bucket_name,
     std::string destination_object_name,
     std::vector<gcs::ComposeSourceObject> compose_objects) {
    std::string prefix = gcs::CreateRandomPrefixName(".tmpfiles");
    StatusOr<gcs::ObjectMetadata> composed_object =
        ComposeMany(client, bucket_name, compose_objects, prefix,
                    destination_object_name, false);

    if (!composed_object) {
      // If this is an effect of some transient unavailability, stray temporary
      // might be left over. You can use `DeleteByPrefix()` with `prefix` as
      // argument to delete them.
      throw std::runtime_error(composed_object.status().message());
    }

    std::cout << "Composed new object " << composed_object->name()
              << " in bucket " << composed_object->bucket()
              << "\nFull metadata: " << *composed_object << "\n";
  }
  //! [compose object from many] [END storage_compose_file_from_many]
  (std::move(client), bucket_name, destination_object_name,
   std::move(compose_objects));
}

void ChangeObjectStorageClass(google::cloud::storage::Client client,
                              std::vector<std::string> const& argv) {
  //! [change file storage class]
  // [START storage_change_file_storage_class]
  namespace gcs = google::cloud::storage;
  using ::google::cloud::StatusOr;
  [](gcs::Client client, std::string bucket_name, std::string object_name,
     std::string storage_class) {
    StatusOr<gcs::ObjectMetadata> object_metadata =
        client.RewriteObjectBlocking(
            bucket_name, object_name, bucket_name, object_name,
            gcs::WithObjectMetadata(
                gcs::ObjectMetadata().set_storage_class(storage_class)));

    if (!object_metadata) {
      throw std::runtime_error(object_metadata.status().message());
    }

    std::cout << "Changed storage class of object " << object_metadata->name()
              << " in bucket " << object_metadata->bucket() << " to "
              << object_metadata->storage_class() << "\n";
  }
  // [END storage_change_file_storage_class]
  //! [change file storage class]
  (std::move(client), argv.at(0), argv.at(1), argv.at(2));
}

void RunAll(std::vector<std::string> const& argv) {
  namespace examples = ::google::cloud::storage::examples;
  namespace gcs = ::google::cloud::storage;

  if (!argv.empty()) throw examples::Usage{"auto"};
  examples::CheckEnvironmentVariablesAreSet({
      "GOOGLE_CLOUD_PROJECT",
  });
  auto const project_id =
      google::cloud::internal::GetEnv("GOOGLE_CLOUD_PROJECT").value();
  auto generator = google::cloud::internal::DefaultPRNG(std::random_device{}());
  auto const bucket_name =
      examples::MakeRandomBucketName(generator, "cloud-cpp-test-examples-");
  auto client = gcs::Client::CreateDefaultClient().value();
  std::cout << "\nCreating bucket to run the example (" << bucket_name << ")"
            << std::endl;
  auto bucket_metadata = client
                             .CreateBucketForProject(bucket_name, project_id,
                                                     gcs::BucketMetadata{})
                             .value();

  std::string const object_media("a-string-to-serve-as-object-media");
  auto const object_name = examples::MakeRandomObjectName(generator, "object-");

  std::cout << "\nRunning InsertObject() example [1]" << std::endl;
  InsertObject(client, {bucket_name, object_name, object_media});

  std::cout << "\nRunning ListObjects() example" << std::endl;
  ListObjects(client, {bucket_name});

  std::cout << "\nRunning ListVersionedObjects() example" << std::endl;
  ListVersionedObjects(client, {bucket_name});

  std::cout << "\nRunning InsertObject() examples [with prefix]" << std::endl;
  auto const bucket_prefix =
      examples::MakeRandomObjectName(generator, "prefix-");
  InsertObject(client, {bucket_name, bucket_prefix + "/object-1.txt",
                        "media-for-object-1"});
  InsertObject(client, {bucket_name, bucket_prefix + "/object-2.txt",
                        "media-for-object-2"});

  std::cout << "\nRunning ListObjectsWithPrefix() example" << std::endl;
  ListObjectsWithPrefix(client, {bucket_name, bucket_prefix});

  std::cout << "\nRunning GetObjectMetadata() example" << std::endl;
  GetObjectMetadata(client, {bucket_name, object_name});

  std::cout << "\nRunning ChangeObjectStorageClass() example" << std::endl;
  ChangeObjectStorageClass(client, {bucket_name, object_name, "NEARLINE"});

  std::cout << "\nRunning ReadObject() example" << std::endl;
  ReadObject(client, {bucket_name, object_name});

  std::cout << "\nRunning WriteObject() example" << std::endl;
  WriteObject(client, {bucket_name, object_name, "100000"});

  std::cout << "\nRunning ReadObjectRange() example" << std::endl;
  ReadObjectRange(client, {bucket_name, object_name, "1000", "2000"});

  std::cout << "\nRunning UpdateObjectMetadata() example" << std::endl;
  UpdateObjectMetadata(client,
                       {bucket_name, object_name, "test-label", "test-value"});

  std::cout << "\nRunning PatchObjectContentType() example" << std::endl;
  PatchObjectContentType(client,
                         {bucket_name, object_name, "application/text"});

  std::cout << "\nRunning PatchObjectDeleteMetadata() example" << std::endl;
  PatchObjectDeleteMetadata(client, {bucket_name, object_name, "test-label"});

  std::cout << "\nRunning ComposeObject() example" << std::endl;
  auto const composed_object_name =
      examples::MakeRandomObjectName(generator, "composed-object-");
  ComposeObject(client,
                {bucket_name, composed_object_name, object_name, object_name});
  DeleteObject(client, {bucket_name, composed_object_name});

  std::cout << "\nRunning ComposeObjectFromMany() example" << std::endl;
  ComposeObjectFromMany(
      client, {bucket_name, composed_object_name, object_name, object_name});
  DeleteObject(client, {bucket_name, composed_object_name});

  std::cout << "\nRunning CopyObject() example" << std::endl;
  auto const copied_object_name =
      examples::MakeRandomObjectName(generator, "copied-object-");
  CopyObject(client,
             {bucket_name, object_name, bucket_name, copied_object_name});
  DeleteObject(client, {bucket_name, copied_object_name});

  std::cout << "\nRunning DeleteObject() example [1]" << std::endl;
  DeleteObject(client, {bucket_name, bucket_prefix + "/object-2.txt"});
  DeleteObject(client, {bucket_name, bucket_prefix + "/object-1.txt"});
  DeleteObject(client, {bucket_name, object_name});

  std::cout << "\nRunning InsertObjectMultipart() example" << std::endl;
  auto const multipart_object_name =
      examples::MakeRandomObjectName(generator, "multipart-object-");
  InsertObjectMultipart(
      client, {bucket_name, multipart_object_name, "text/plain", object_media});
  DeleteObject(client, {bucket_name, multipart_object_name});

  std::cout << "\nRunning InsertObjectStrictIdempotency() example" << std::endl;
  auto const object_name_strict =
      examples::MakeRandomObjectName(generator, "object-strict-");
  InsertObjectStrictIdempotency(
      client, {bucket_name, object_name_strict, object_media});
  DeleteObject(client, {bucket_name, object_name_strict});

  std::cout << "\nRunning InsertObjectModifiedRetry() example" << std::endl;
  auto const object_name_retry =
      examples::MakeRandomObjectName(generator, "object-retry-");
  InsertObjectModifiedRetry(client,
                            {bucket_name, object_name_retry, object_media});
  DeleteObject(client, {bucket_name, object_name_retry});

  (void)client.DeleteBucket(bucket_name);
}

}  // anonymous namespace

int main(int argc, char* argv[]) {
  namespace examples = ::google::cloud::storage::examples;
  auto make_entry = [](std::string const& name,
                       std::vector<std::string> arg_names,
                       examples::ClientCommand const& cmd) {
    arg_names.insert(arg_names.begin(), "<bucket-name>");
    return examples::CreateCommandEntry(name, std::move(arg_names), cmd);
  };

  google::cloud::storage::examples::Example example({
      make_entry("list-objects", {}, ListObjects),
      make_entry("list-objects-with-prefix", {"<prefix>"},
                 ListObjectsWithPrefix),
      make_entry("list-versioned-objects", {}, ListVersionedObjects),
      make_entry("insert-object",
                 {"<object-name>", "<object-contents (string)>"}, InsertObject),
      make_entry("insert-object-strict-idempotency",
                 {"<object-name>", "<object-contents (string)>"},
                 InsertObjectStrictIdempotency),
      make_entry("insert-object-modified-retry",
                 {"<object-name>", "<object-contents (string)>"},
                 InsertObjectModifiedRetry),
      make_entry(
          "insert-object-multipart",
          {"<object-name>", "<content-type>", "<object-contents (string)>"},
          InsertObjectMultipart),
      examples::CreateCommandEntry(
          "copy-object",
          {"<source-bucket-name>", "<source-object-name>",
           "<destination-bucket-name>", "<destination-object-name>"},
          CopyObject),
      make_entry("get-object-metadata", {"<object-name>"}, GetObjectMetadata),
      make_entry("read-object", {"<object-name>"}, ReadObject),
      make_entry("read-object-range", {"<object-name>", "<start>", "<end>"},
                 ReadObjectRange),
      make_entry("delete-object", {"<object-name>"}, DeleteObject),
      make_entry("write-object",
                 {"<object-name>", "<target-object-line-count>"}, WriteObject),
      make_entry("update-object-metadata",
                 {"<object-name>", "<key>", "<value>"}, UpdateObjectMetadata),
      make_entry("update-object-metadata", {"<object-name>"},
                 PatchObjectDeleteMetadata),
      make_entry("update-object-metadata", {"<object-name>", "<content-type>"},
                 PatchObjectContentType),
      make_entry("compose-object",
                 {"<destination-object-name>", "<object>", "[object...]"},
                 ComposeObject),
      make_entry("compose-object-from-many",
                 {"<destination-object-name>", "<object>", "[object...]"},
                 ComposeObjectFromMany),
      make_entry("change-object-storage-class",
                 {"<object-name>", "<storage-class>"},
                 ChangeObjectStorageClass),
      {"auto", RunAll},
  });
  return example.Run(argc, argv);
}
