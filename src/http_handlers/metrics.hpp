// Copyright 2025 Memgraph Ltd.
//
// Use of this software is governed by the Business Source License
// included in the file licenses/BSL.txt; by using this file, you agree to be bound by the terms of the Business Source
// License, and you may not use this file except in compliance with the Business Source License.
//
// As of the Change Date specified in that file, in accordance with
// the Business Source License, use of this software will be governed
// by the Apache License, Version 2.0, included in the file
// licenses/APL.txt.

#pragma once

#include <atomic>
#include <tuple>
#include <vector>

#include <spdlog/spdlog.h>
#include <boost/beast/http.hpp>
#include <boost/beast/version.hpp>
#include <nlohmann/json_fwd.hpp>

#include "license/license_sender.hpp"
#include "storage/v2/storage.hpp"
#include "utils/event_counter.hpp"
#include "utils/event_gauge.hpp"
#include "utils/event_histogram.hpp"

namespace memgraph::metrics {

// clang-format off
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define RpcCounter(Rpc)            \
  extern const Event Rpc##Success; \
  extern const Event Rpc##Fail;

extern const Event SuccessfulFailovers;
extern const Event RaftFailedFailovers;
extern const Event NoAliveInstanceFailedFailovers;
extern const Event BecomeLeaderSuccess;
extern const Event FailedToBecomeLeader;
extern const Event ShowInstance;
extern const Event ShowInstances;
extern const Event DemoteInstance;
extern const Event UnregisterReplInstance;
extern const Event RemoveCoordInstance;
RpcCounter(StateCheckRpc)
RpcCounter(UnregisterReplicaRpc)
RpcCounter(EnableWritingOnMainRpc)
RpcCounter(PromoteToMainRpc)
RpcCounter(DemoteMainToReplicaRpc)
RpcCounter(RegisterReplicaOnMainRpc)
RpcCounter(SwapMainUUIDRpc)
RpcCounter(GetDatabaseHistoriesRpc)
// clang-format on

}  // namespace memgraph::metrics

namespace memgraph::http {

// NOLINTNEXTLINE
inline const static std::array<metrics::Event, 26> coord_counters_to_reset{metrics::SuccessfulFailovers,
                                                                           metrics::RaftFailedFailovers,
                                                                           metrics::NoAliveInstanceFailedFailovers,
                                                                           metrics::BecomeLeaderSuccess,
                                                                           metrics::FailedToBecomeLeader,
                                                                           metrics::ShowInstance,
                                                                           metrics::ShowInstances,
                                                                           metrics::DemoteInstance,
                                                                           metrics::UnregisterReplInstance,
                                                                           metrics::RemoveCoordInstance,
                                                                           metrics::StateCheckRpcFail,
                                                                           metrics::StateCheckRpcSuccess,
                                                                           metrics::UnregisterReplicaRpcFail,
                                                                           metrics::UnregisterReplicaRpcSuccess,
                                                                           metrics::EnableWritingOnMainRpcFail,
                                                                           metrics::EnableWritingOnMainRpcSuccess,
                                                                           metrics::PromoteToMainRpcFail,
                                                                           metrics::PromoteToMainRpcSuccess,
                                                                           metrics::DemoteMainToReplicaRpcFail,
                                                                           metrics::DemoteMainToReplicaRpcSuccess,
                                                                           metrics::RegisterReplicaOnMainRpcFail,
                                                                           metrics::RegisterReplicaOnMainRpcSuccess,
                                                                           metrics::SwapMainUUIDRpcFail,
                                                                           metrics::SwapMainUUIDRpcSuccess,
                                                                           metrics::GetDatabaseHistoriesRpcFail,
                                                                           metrics::GetDatabaseHistoriesRpcSuccess};

struct MetricsResponse {
  uint64_t vertex_count;
  uint64_t edge_count;
  double average_degree;
  uint64_t memory_usage;
  uint64_t peak_memory_usage;
  uint64_t unreleased_delta_objects;
  uint64_t disk_usage;

  // Storage of all the counter values throughout the system
  // e.g. number of active transactions
  std::vector<std::tuple<std::string, std::string, uint64_t>> event_counters{};

  // Storage of all the current values throughout the system
  std::vector<std::tuple<std::string, std::string, uint64_t>> event_gauges{};

  // Storage of all the percentile values across the histograms in the system
  // e.g. query latency percentiles, snapshot recovery duration percentiles, etc.
  std::vector<std::tuple<std::string, std::string, uint64_t>> event_histograms{};
};

class MetricsService {
 public:
  explicit MetricsService(storage::Storage *storage) : db_(storage) {}

  nlohmann::json GetMetricsJSON() {
    auto response = GetMetrics();
    return AsJson(response);
  }

 private:
  storage::Storage *const db_;

  MetricsResponse GetMetrics() {
    auto info = db_->GetBaseInfo();

    return MetricsResponse{.vertex_count = info.vertex_count,
                           .edge_count = info.edge_count,
                           .average_degree = info.average_degree,
                           .memory_usage = info.memory_res,
                           .peak_memory_usage = info.peak_memory_res,
                           .unreleased_delta_objects = info.unreleased_delta_objects,
                           .disk_usage = info.disk_usage,
                           .event_counters = GetEventCounters(),
                           .event_gauges = GetEventGauges(),
                           .event_histograms = GetEventHistograms()};
  }

  inline static nlohmann::json AsJson(MetricsResponse response) {
    auto metrics_response = nlohmann::json();
    const auto *general_type = "General";

    metrics_response[general_type]["vertex_count"] = response.vertex_count;
    metrics_response[general_type]["edge_count"] = response.edge_count;
    metrics_response[general_type]["average_degree"] = response.average_degree;
    metrics_response[general_type]["memory_usage"] = response.memory_usage;
    metrics_response[general_type]["peak_memory_usage"] = response.peak_memory_usage;
    metrics_response[general_type]["unreleased_delta_objects"] = response.unreleased_delta_objects;
    metrics_response[general_type]["disk_usage"] = response.disk_usage;

    for (const auto &[name, type, value] : response.event_counters) {
      metrics_response[type][name] = value;
    }

    for (const auto &[name, type, value] : response.event_gauges) {
      metrics_response[type][name] = value;
    }

    for (const auto &[name, type, value] : response.event_histograms) {
      metrics_response[type][name] = value;
    }

    return metrics_response;
  }

  inline static std::vector<std::tuple<std::string, std::string, uint64_t>> GetEventCounters() {
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    std::vector<std::tuple<std::string, std::string, uint64_t>> event_counters{};
    event_counters.reserve(memgraph::metrics::CounterEnd());

    // After pulling metrics, we need to reset HA counters on coordinators because on each pull we are only sending
    // deltas
    auto const is_coordinator = flags::CoordinationSetupInstance().IsCoordinator();
    for (auto i = 0; i < memgraph::metrics::CounterEnd(); i++) {
      if (is_coordinator && std::find(coord_counters_to_reset.cbegin(), coord_counters_to_reset.cend(), i)) {
        event_counters.emplace_back(memgraph::metrics::GetCounterName(i), memgraph::metrics::GetCounterType(i),
                                    memgraph::metrics::global_counters[i].exchange(0, std::memory_order_acq_rel));

      } else {
        event_counters.emplace_back(memgraph::metrics::GetCounterName(i), memgraph::metrics::GetCounterType(i),
                                    memgraph::metrics::global_counters[i].load(std::memory_order_acquire));
      }
    }

    return event_counters;
  }

  inline static std::vector<std::tuple<std::string, std::string, uint64_t>> GetEventGauges() {
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    std::vector<std::tuple<std::string, std::string, uint64_t>> event_gauges{};
    event_gauges.reserve(memgraph::metrics::GaugeEnd());

    for (auto i = 0; i < memgraph::metrics::GaugeEnd(); i++) {
      event_gauges.emplace_back(memgraph::metrics::GetGaugeName(i), memgraph::metrics::GetGaugeTypeString(i),
                                memgraph::metrics::global_gauges[i].load(std::memory_order_acquire));
    }

    return event_gauges;
  }

  inline static std::vector<std::tuple<std::string, std::string, uint64_t>> GetEventHistograms() {
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    std::vector<std::tuple<std::string, std::string, uint64_t>> event_histograms{};

    for (auto i = 0; i < memgraph::metrics::HistogramEnd(); i++) {
      const auto *name = memgraph::metrics::GetHistogramName(i);
      auto &histogram = memgraph::metrics::global_histograms[i];

      for (auto &[percentile, value] : histogram.YieldPercentiles()) {
        auto metric_name = std::string(name) + "_" + std::to_string(percentile) + "p";

        event_histograms.emplace_back(metric_name, memgraph::metrics::GetHistogramType(i), value);
      }
    }

    return event_histograms;
  }
};

// TODO: Should this be inside Database?
// Raw pointer could be dangerous
class MetricsRequestHandler final {
 public:
  explicit MetricsRequestHandler(storage::Storage *storage) : service_(storage) {}

  MetricsRequestHandler(const MetricsRequestHandler &) = delete;
  MetricsRequestHandler(MetricsRequestHandler &&) = delete;
  MetricsRequestHandler &operator=(const MetricsRequestHandler &) = delete;
  MetricsRequestHandler &operator=(MetricsRequestHandler &&) = delete;
  ~MetricsRequestHandler() = default;

  template <class Body, class Allocator>
  // NOLINTNEXTLINE(misc-unused-parameters)
  void HandleRequest(boost::beast::http::request<Body, boost::beast::http::basic_fields<Allocator>> &&req,
                     std::function<void(boost::beast::http::response<boost::beast::http::string_body>)> &&send) {
    auto response_json = nlohmann::json();
    // Returns a bad request response
    auto const bad_request = [&req, &response_json](const auto why) {
      response_json["error"] = std::string(why);

      // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
      boost::beast::http::response<boost::beast::http::string_body> res{boost::beast::http::status::bad_request,
                                                                        req.version()};
      res.set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
      res.set(boost::beast::http::field::content_type, "application/json");
      res.keep_alive(req.keep_alive());
      res.body() = response_json.dump();
      res.prepare_payload();
      return res;
    };

#ifdef MG_ENTERPRISE
    if (!memgraph::license::global_license_checker.IsEnterpriseValidFast()) {
      return send(
          bad_request("Memgraph must have an Enterprise License for providing metrics! Check your license status by "
                      "running SHOW LICENSE INFO."));
    }
#else
    return send(bad_request("Memgraph must be built for Enterprise for providing metrics!"));
#endif

    // Make sure we can handle the method
    if (req.method() != boost::beast::http::verb::get) {
      return send(bad_request("Unknown HTTP-method"));
    }

    // Request path must be absolute and not contain "..".
    if (req.target().empty() || req.target()[0] != '/' || req.target().find("..") != boost::beast::string_view::npos) {
      return send(bad_request("Illegal request-target"));
    }

    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    boost::beast::http::string_body::value_type body;

    auto service_response = service_.GetMetricsJSON();
    body.append(service_response.dump());

    // Cache the size since we need it after the move
    const auto size = body.size();

    // Respond to GET request
    // NOLINTNEXTLINE(cppcoreguidelines-init-variables)
    boost::beast::http::response<boost::beast::http::string_body> res{
        std::piecewise_construct, std::make_tuple(std::move(body)),
        std::make_tuple(boost::beast::http::status::ok, req.version())};
    res.set(boost::beast::http::field::server, BOOST_BEAST_VERSION_STRING);
    res.set(boost::beast::http::field::content_type, "application/json");
    res.content_length(size);
    res.keep_alive(req.keep_alive());
    return send(std::move(res));
  }

 private:
  MetricsService service_;
};
}  // namespace memgraph::http
