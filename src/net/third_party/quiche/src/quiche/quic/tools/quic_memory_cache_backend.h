// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_QUIC_TOOLS_QUIC_MEMORY_CACHE_BACKEND_H_
#define QUICHE_QUIC_TOOLS_QUIC_MEMORY_CACHE_BACKEND_H_

#include <list>
#include <map>
#include <memory>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "quiche/http2/core/spdy_framer.h"
#include "quiche/quic/core/http/spdy_utils.h"
#include "quiche/quic/tools/quic_backend_response.h"
#include "quiche/quic/tools/quic_simple_server_backend.h"
#include "quiche/common/http/http_header_block.h"
#include "quiche/common/platform/api/quiche_mutex.h"

namespace quic {

// In-memory cache for HTTP responses.
// Reads from disk cache generated by:
// `wget -p --save_headers <url>`
class QuicMemoryCacheBackend : public QuicSimpleServerBackend {
 public:
  // Class to manage loading a resource file into memory.  There are
  // two uses: called by InitializeBackend to load resources
  // from files, and recursively called when said resources specify
  // server push associations.
  class ResourceFile {
   public:
    explicit ResourceFile(const std::string& file_name);
    ResourceFile(const ResourceFile&) = delete;
    ResourceFile& operator=(const ResourceFile&) = delete;
    virtual ~ResourceFile();

    void Read();

    // |base| is |file_name_| with |cache_directory| prefix stripped.
    void SetHostPathFromBase(absl::string_view base);

    const std::string& file_name() { return file_name_; }

    absl::string_view host() { return host_; }

    absl::string_view path() { return path_; }

    const quiche::HttpHeaderBlock& spdy_headers() { return spdy_headers_; }

    absl::string_view body() { return body_; }

    const std::vector<absl::string_view>& push_urls() { return push_urls_; }

   private:
    void HandleXOriginalUrl();
    absl::string_view RemoveScheme(absl::string_view url);

    std::string file_name_;
    std::string file_contents_;
    absl::string_view body_;
    quiche::HttpHeaderBlock spdy_headers_;
    absl::string_view x_original_url_;
    std::vector<absl::string_view> push_urls_;
    std::string host_;
    std::string path_;
  };

  QuicMemoryCacheBackend();
  QuicMemoryCacheBackend(const QuicMemoryCacheBackend&) = delete;
  QuicMemoryCacheBackend& operator=(const QuicMemoryCacheBackend&) = delete;
  ~QuicMemoryCacheBackend() override;

  // Retrieve a response from this cache for a given host and path..
  // If no appropriate response exists, nullptr is returned.
  const QuicBackendResponse* GetResponse(absl::string_view host,
                                         absl::string_view path) const;

  // Adds a simple response to the cache.  The response headers will
  // only contain the "content-length" header with the length of |body|.
  void AddSimpleResponse(absl::string_view host, absl::string_view path,
                         int response_code, absl::string_view body);

  // Add a response to the cache.
  void AddResponse(absl::string_view host, absl::string_view path,
                   quiche::HttpHeaderBlock response_headers,
                   absl::string_view response_body);

  // Add a response, with trailers, to the cache.
  void AddResponse(absl::string_view host, absl::string_view path,
                   quiche::HttpHeaderBlock response_headers,
                   absl::string_view response_body,
                   quiche::HttpHeaderBlock response_trailers);

  // Add a response, with 103 Early Hints, to the cache.
  void AddResponseWithEarlyHints(
      absl::string_view host, absl::string_view path,
      quiche::HttpHeaderBlock response_headers, absl::string_view response_body,
      const std::vector<quiche::HttpHeaderBlock>& early_hints);

  // Simulate a special behavior at a particular path.
  void AddSpecialResponse(
      absl::string_view host, absl::string_view path,
      QuicBackendResponse::SpecialResponseType response_type);

  void AddSpecialResponse(
      absl::string_view host, absl::string_view path,
      quiche::HttpHeaderBlock response_headers, absl::string_view response_body,
      QuicBackendResponse::SpecialResponseType response_type);

  // Finds a response with the given host and path, and assign it a simulated
  // delay. Returns true if the requisite response was found and the delay was
  // set.
  bool SetResponseDelay(absl::string_view host, absl::string_view path,
                        QuicTime::Delta delay);

  // Sets a default response in case of cache misses.  Takes ownership of
  // 'response'.
  void AddDefaultResponse(QuicBackendResponse* response);

  // Once called, URLs which have a numeric path will send a dynamically
  // generated response of that many bytes.
  void GenerateDynamicResponses();

  void EnableWebTransport();

  // Implements the functions for interface QuicSimpleServerBackend
  // |cache_cirectory| can be generated using `wget -p --save-headers <url>`.
  bool InitializeBackend(const std::string& cache_directory) override;
  bool IsBackendInitialized() const override;
  void FetchResponseFromBackend(
      const quiche::HttpHeaderBlock& request_headers,
      const std::string& request_body,
      QuicSimpleServerBackend::RequestHandler* quic_stream) override;
  void CloseBackendResponseStream(
      QuicSimpleServerBackend::RequestHandler* quic_stream) override;
  WebTransportResponse ProcessWebTransportRequest(
      const quiche::HttpHeaderBlock& request_headers,
      WebTransportSession* session) override;
  bool SupportsWebTransport() override { return enable_webtransport_; }

 private:
  void AddResponseImpl(absl::string_view host, absl::string_view path,
                       QuicBackendResponse::SpecialResponseType response_type,
                       quiche::HttpHeaderBlock response_headers,
                       absl::string_view response_body,
                       quiche::HttpHeaderBlock response_trailers,
                       const std::vector<quiche::HttpHeaderBlock>& early_hints);

  std::string GetKey(absl::string_view host, absl::string_view path) const;

  // Cached responses.
  absl::flat_hash_map<std::string, std::unique_ptr<QuicBackendResponse>>
      responses_ QUICHE_GUARDED_BY(response_mutex_);

  // The default response for cache misses, if set.
  std::unique_ptr<QuicBackendResponse> default_response_
      QUICHE_GUARDED_BY(response_mutex_);

  // The generate bytes response, if set.
  std::unique_ptr<QuicBackendResponse> generate_bytes_response_
      QUICHE_GUARDED_BY(response_mutex_);

  // Protects against concurrent access from test threads setting responses, and
  // server threads accessing those responses.
  mutable quiche::QuicheMutex response_mutex_;
  bool cache_initialized_;

  bool enable_webtransport_ = false;
};

}  // namespace quic

#endif  // QUICHE_QUIC_TOOLS_QUIC_MEMORY_CACHE_BACKEND_H_