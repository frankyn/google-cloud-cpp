// Copyright 2020 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Code generated by protoc-gen-go_gapic. DO NOT EDIT.

package pubsublite

import (
	"context"
	"math"

	gax "github.com/googleapis/gax-go/v2"
	"google.golang.org/api/option"
	"google.golang.org/api/option/internaloption"
	gtransport "google.golang.org/api/transport/grpc"
	pubsublitepb "google.golang.org/genproto/googleapis/cloud/pubsublite/v1"
	"google.golang.org/grpc"
	"google.golang.org/grpc/metadata"
)

var newPartitionAssignmentClientHook clientHook

// PartitionAssignmentCallOptions contains the retry settings for each method of PartitionAssignmentClient.
type PartitionAssignmentCallOptions struct {
	AssignPartitions []gax.CallOption
}

func defaultPartitionAssignmentClientOptions() []option.ClientOption {
	return []option.ClientOption{
		internaloption.WithDefaultEndpoint("pubsublite.googleapis.com:443"),
		internaloption.WithDefaultMTLSEndpoint("pubsublite.mtls.googleapis.com:443"),
		option.WithGRPCDialOption(grpc.WithDisableServiceConfig()),
		option.WithScopes(DefaultAuthScopes()...),
		option.WithGRPCDialOption(grpc.WithDefaultCallOptions(
			grpc.MaxCallRecvMsgSize(math.MaxInt32))),
	}
}

func defaultPartitionAssignmentCallOptions() *PartitionAssignmentCallOptions {
	return &PartitionAssignmentCallOptions{
		AssignPartitions: []gax.CallOption{},
	}
}

// PartitionAssignmentClient is a client for interacting with .
//
// Methods, except Close, may be called concurrently. However, fields must not be modified concurrently with method calls.
type PartitionAssignmentClient struct {
	// Connection pool of gRPC connections to the service.
	connPool gtransport.ConnPool

	// flag to opt out of default deadlines via GOOGLE_API_GO_EXPERIMENTAL_DISABLE_DEFAULT_DEADLINE
	disableDeadlines bool

	// The gRPC API client.
	partitionAssignmentClient pubsublitepb.PartitionAssignmentServiceClient

	// The call options for this service.
	CallOptions *PartitionAssignmentCallOptions

	// The x-goog-* metadata to be sent with each request.
	xGoogMetadata metadata.MD
}

// NewPartitionAssignmentClient creates a new partition assignment service client.
//
// The service that a subscriber client application uses to determine which
// partitions it should connect to.
func NewPartitionAssignmentClient(ctx context.Context, opts ...option.ClientOption) (*PartitionAssignmentClient, error) {
	clientOpts := defaultPartitionAssignmentClientOptions()

	if newPartitionAssignmentClientHook != nil {
		hookOpts, err := newPartitionAssignmentClientHook(ctx, clientHookParams{})
		if err != nil {
			return nil, err
		}
		clientOpts = append(clientOpts, hookOpts...)
	}

	disableDeadlines, err := checkDisableDeadlines()
	if err != nil {
		return nil, err
	}

	connPool, err := gtransport.DialPool(ctx, append(clientOpts, opts...)...)
	if err != nil {
		return nil, err
	}
	c := &PartitionAssignmentClient{
		connPool:         connPool,
		disableDeadlines: disableDeadlines,
		CallOptions:      defaultPartitionAssignmentCallOptions(),

		partitionAssignmentClient: pubsublitepb.NewPartitionAssignmentServiceClient(connPool),
	}
	c.setGoogleClientInfo()

	return c, nil
}

// Connection returns a connection to the API service.
//
// Deprecated.
func (c *PartitionAssignmentClient) Connection() *grpc.ClientConn {
	return c.connPool.Conn()
}

// Close closes the connection to the API service. The user should invoke this when
// the client is no longer required.
func (c *PartitionAssignmentClient) Close() error {
	return c.connPool.Close()
}

// setGoogleClientInfo sets the name and version of the application in
// the `x-goog-api-client` header passed on each request. Intended for
// use by Google-written clients.
func (c *PartitionAssignmentClient) setGoogleClientInfo(keyval ...string) {
	kv := append([]string{"gl-go", versionGo()}, keyval...)
	kv = append(kv, "gapic", versionClient, "gax", gax.Version, "grpc", grpc.Version)
	c.xGoogMetadata = metadata.Pairs("x-goog-api-client", gax.XGoogHeader(kv...))
}

// AssignPartitions assign partitions for this client to handle for the specified subscription.
//
// The client must send an InitialPartitionAssignmentRequest first.
// The server will then send at most one unacknowledged PartitionAssignment
// outstanding on the stream at a time.
// The client should send a PartitionAssignmentAck after updating the
// partitions it is connected to to reflect the new assignment.
func (c *PartitionAssignmentClient) AssignPartitions(ctx context.Context, opts ...gax.CallOption) (pubsublitepb.PartitionAssignmentService_AssignPartitionsClient, error) {
	ctx = insertMetadata(ctx, c.xGoogMetadata)
	opts = append(c.CallOptions.AssignPartitions[0:len(c.CallOptions.AssignPartitions):len(c.CallOptions.AssignPartitions)], opts...)
	var resp pubsublitepb.PartitionAssignmentService_AssignPartitionsClient
	err := gax.Invoke(ctx, func(ctx context.Context, settings gax.CallSettings) error {
		var err error
		resp, err = c.partitionAssignmentClient.AssignPartitions(ctx, settings.GRPC...)
		return err
	}, opts...)
	if err != nil {
		return nil, err
	}
	return resp, nil
}