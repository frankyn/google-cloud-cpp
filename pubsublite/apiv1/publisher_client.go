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

var newPublisherClientHook clientHook

// PublisherCallOptions contains the retry settings for each method of PublisherClient.
type PublisherCallOptions struct {
	Publish []gax.CallOption
}

func defaultPublisherClientOptions() []option.ClientOption {
	return []option.ClientOption{
		internaloption.WithDefaultEndpoint("pubsublite.googleapis.com:443"),
		internaloption.WithDefaultMTLSEndpoint("pubsublite.mtls.googleapis.com:443"),
		option.WithGRPCDialOption(grpc.WithDisableServiceConfig()),
		option.WithScopes(DefaultAuthScopes()...),
		option.WithGRPCDialOption(grpc.WithDefaultCallOptions(
			grpc.MaxCallRecvMsgSize(math.MaxInt32))),
	}
}

func defaultPublisherCallOptions() *PublisherCallOptions {
	return &PublisherCallOptions{
		Publish: []gax.CallOption{},
	}
}

// PublisherClient is a client for interacting with .
//
// Methods, except Close, may be called concurrently. However, fields must not be modified concurrently with method calls.
type PublisherClient struct {
	// Connection pool of gRPC connections to the service.
	connPool gtransport.ConnPool

	// flag to opt out of default deadlines via GOOGLE_API_GO_EXPERIMENTAL_DISABLE_DEFAULT_DEADLINE
	disableDeadlines bool

	// The gRPC API client.
	publisherClient pubsublitepb.PublisherServiceClient

	// The call options for this service.
	CallOptions *PublisherCallOptions

	// The x-goog-* metadata to be sent with each request.
	xGoogMetadata metadata.MD
}

// NewPublisherClient creates a new publisher service client.
//
// The service that a publisher client application uses to publish messages to
// topics. Published messages are retained by the service for the duration of
// the retention period configured for the respective topic, and are delivered
// to subscriber clients upon request (via the SubscriberService).
func NewPublisherClient(ctx context.Context, opts ...option.ClientOption) (*PublisherClient, error) {
	clientOpts := defaultPublisherClientOptions()

	if newPublisherClientHook != nil {
		hookOpts, err := newPublisherClientHook(ctx, clientHookParams{})
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
	c := &PublisherClient{
		connPool:         connPool,
		disableDeadlines: disableDeadlines,
		CallOptions:      defaultPublisherCallOptions(),

		publisherClient: pubsublitepb.NewPublisherServiceClient(connPool),
	}
	c.setGoogleClientInfo()

	return c, nil
}

// Connection returns a connection to the API service.
//
// Deprecated.
func (c *PublisherClient) Connection() *grpc.ClientConn {
	return c.connPool.Conn()
}

// Close closes the connection to the API service. The user should invoke this when
// the client is no longer required.
func (c *PublisherClient) Close() error {
	return c.connPool.Close()
}

// setGoogleClientInfo sets the name and version of the application in
// the `x-goog-api-client` header passed on each request. Intended for
// use by Google-written clients.
func (c *PublisherClient) setGoogleClientInfo(keyval ...string) {
	kv := append([]string{"gl-go", versionGo()}, keyval...)
	kv = append(kv, "gapic", versionClient, "gax", gax.Version, "grpc", grpc.Version)
	c.xGoogMetadata = metadata.Pairs("x-goog-api-client", gax.XGoogHeader(kv...))
}

// Publish establishes a stream with the server for publishing messages. Once the
// stream is initialized, the client publishes messages by sending publish
// requests on the stream. The server responds with a PublishResponse for each
// PublishRequest sent by the client, in the same order that the requests
// were sent. Note that multiple PublishRequests can be in flight
// simultaneously, but they will be processed by the server in the order that
// they are sent by the client on a given stream.
func (c *PublisherClient) Publish(ctx context.Context, opts ...gax.CallOption) (pubsublitepb.PublisherService_PublishClient, error) {
	ctx = insertMetadata(ctx, c.xGoogMetadata)
	opts = append(c.CallOptions.Publish[0:len(c.CallOptions.Publish):len(c.CallOptions.Publish)], opts...)
	var resp pubsublitepb.PublisherService_PublishClient
	err := gax.Invoke(ctx, func(ctx context.Context, settings gax.CallSettings) error {
		var err error
		resp, err = c.publisherClient.Publish(ctx, settings.GRPC...)
		return err
	}, opts...)
	if err != nil {
		return nil, err
	}
	return resp, nil
}