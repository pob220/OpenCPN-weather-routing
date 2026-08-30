/**************************************************************************
 *   Copyright (C) 2024 by David S. Register                               *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   along with this program; if not, see <https://www.gnu.org/licenses/>. *
 **************************************************************************/

/**
 * \file
 *
 * ocpn_plugin.h HostApi122 implementation
 */

#include <wx/app.h>

#include "ocpn_plugin.h"

#include "ocpn-nlohmann/json.hpp"
#include "observable/observable.h"

#include "model/comm_navmsg.h"
#include "model/comm_navmsg_bus.h"

#include "chart_safety_api.h"

namespace {

class HostApi122Impl final : public HostApi122 {
public:
  explicit HostApi122Impl(Api122Impl* api_impl) : m_api_impl(api_impl) {}

  void RegisterApiEventCallback(
      const std::string& plugin_name,
      std::function<void(EventType)> callback) override;
  bool RegisterChartSafetyProvider(
      const std::string& plugin_name,
      const ChartSafetyProviderCallbacks* callbacks) override;
  bool RegisterSegmentSafetyTileCache(
      const std::string& plugin_name,
      const SegmentSafetyTileCacheCallbacks* callbacks) override;
  bool GetSegmentSafetyChartIdentity(std::string* identity) override;
  int GetSegmentSafetyChartInfoCount() override;
  bool GetSegmentSafetyChartInfo(int ordinal,
                                 SegmentSafetyChartInfo* chart_info) override;
  bool GetSegmentSafetyChartCoverageTiles(const int* chart_db_indices,
                                          int chart_count, double tile_degrees,
                                          long* lat_tiles, long* lon_tiles,
                                          int tile_capacity, int* tile_count,
                                          bool* complete) override;
  bool CheckSegmentSafety(double lat1, double lon1, double lat2, double lon2,
                          const SegmentSafetyOptions* options,
                          SegmentSafetyResult* result) override;
  bool PrepareSegmentSafetyTiles(const long* lat_tiles, const long* lon_tiles,
                                 int tile_count, bool require_depth,
                                 SegmentSafetyResult* result) override;
  bool PrepareSegmentSafetySnapshot(double min_lat, double min_lon,
                                    double max_lat, double max_lon,
                                    bool enable_fast_path, bool shadow_compare,
                                    const SegmentSafetyOptions* options,
                                    SegmentSafetyResult* result) override;
  bool PrepareSegmentSafetyCorridor(const double* latitudes,
                                    const double* longitudes,
                                    const int* point_counts, int polyline_count,
                                    double corridor_margin_nm,
                                    int fine_tile_halo,
                                    const SegmentSafetyOptions* options,
                                    SegmentSafetyResult* result) override;
  int GetPendingSegmentSafetyRequestCount() override;
  bool ServicePendingSegmentSafetyRequests(
      int max_requests, int max_milliseconds,
      SegmentSafetyRequestServiceResult* result) override;
  void ReleaseSegmentSafetyPins() override;
  bool SetSegmentSafetyPersistentCacheEnabled(bool enabled) override;
  bool GetSegmentSafetyPersistentCacheEnabled() override;
  bool SaveSegmentSafetyPersistentCache() override;
  bool ClearSegmentSafetyPersistentCache() override;
  const std::set<std::string>& GetActiveMessages() override;
  std::string GetSignalkPayload(ObservedEvt ev) override;

private:
  Api122Impl* m_api_impl;
};

}  // namespace

// FIXME (leamas) find new home.
std::unique_ptr<HostApi> GetHostApi() {
  auto impl = dynamic_cast<Api122Impl*>(wxTheApp);
  assert(impl && "wxTheApp does not implement Api122Impl");
  return std::make_unique<HostApi122Impl>(impl);
}

void HostApi122Impl::RegisterApiEventCallback(
    const std::string& plugin_name, std::function<void(EventType)> callback) {
  m_api_impl->RegisterApiEventCallback(plugin_name, callback);
}

bool HostApi122Impl::RegisterChartSafetyProvider(
    const std::string& plugin_name,
    const ChartSafetyProviderCallbacks* callbacks) {
  return m_api_impl->RegisterChartSafetyProvider(plugin_name, callbacks);
}

bool HostApi122Impl::RegisterSegmentSafetyTileCache(
    const std::string& plugin_name,
    const SegmentSafetyTileCacheCallbacks* callbacks) {
  return m_api_impl->RegisterSegmentSafetyTileCache(plugin_name, callbacks);
}

bool HostApi122Impl::GetSegmentSafetyChartIdentity(std::string* identity) {
  return ocpn::chart_safety::GetChartIdentity(identity);
}

int HostApi122Impl::GetSegmentSafetyChartInfoCount() {
  return ocpn::chart_safety::GetChartInfoCount();
}

bool HostApi122Impl::GetSegmentSafetyChartInfo(
    int ordinal, SegmentSafetyChartInfo* chart_info) {
  return ocpn::chart_safety::GetChartInfo(ordinal, chart_info);
}

bool HostApi122Impl::GetSegmentSafetyChartCoverageTiles(
    const int* chart_db_indices, int chart_count, double tile_degrees,
    long* lat_tiles, long* lon_tiles, int tile_capacity, int* tile_count,
    bool* complete) {
  return ocpn::chart_safety::GetChartCoverageTiles(
      chart_db_indices, chart_count, tile_degrees, lat_tiles, lon_tiles,
      tile_capacity, tile_count, complete);
}

bool HostApi122Impl::CheckSegmentSafety(double lat1, double lon1, double lat2,
                                        double lon2,
                                        const SegmentSafetyOptions* options,
                                        SegmentSafetyResult* result) {
  return ocpn::chart_safety::CheckSegment(lat1, lon1, lat2, lon2, options,
                                          result);
}

bool HostApi122Impl::PrepareSegmentSafetyTiles(const long* lat_tiles,
                                               const long* lon_tiles,
                                               int tile_count,
                                               bool require_depth,
                                               SegmentSafetyResult* result) {
  return ocpn::chart_safety::PrepareRawTiles(lat_tiles, lon_tiles, tile_count,
                                             require_depth, result);
}

bool HostApi122Impl::PrepareSegmentSafetySnapshot(
    double min_lat, double min_lon, double max_lat, double max_lon,
    bool enable_fast_path, bool shadow_compare,
    const SegmentSafetyOptions* options, SegmentSafetyResult* result) {
  return ocpn::chart_safety::PrepareHazardSnapshot(
      min_lat, min_lon, max_lat, max_lon, enable_fast_path, shadow_compare,
      options, result);
}

bool HostApi122Impl::PrepareSegmentSafetyCorridor(
    const double* latitudes, const double* longitudes, const int* point_counts,
    int polyline_count, double corridor_margin_nm, int fine_tile_halo,
    const SegmentSafetyOptions* options, SegmentSafetyResult* result) {
  return ocpn::chart_safety::PrepareCorridor(
      latitudes, longitudes, point_counts, polyline_count, corridor_margin_nm,
      fine_tile_halo, options, result);
}

int HostApi122Impl::GetPendingSegmentSafetyRequestCount() {
  return ocpn::chart_safety::GetPendingRequestCount();
}

bool HostApi122Impl::ServicePendingSegmentSafetyRequests(
    int max_requests, int max_milliseconds,
    SegmentSafetyRequestServiceResult* result) {
  return ocpn::chart_safety::ServicePendingRequests(max_requests,
                                                    max_milliseconds, result);
}

void HostApi122Impl::ReleaseSegmentSafetyPins() {
  ocpn::chart_safety::ReleasePins();
}

bool HostApi122Impl::SetSegmentSafetyPersistentCacheEnabled(bool enabled) {
  return ocpn::chart_safety::SetPersistentCacheEnabled(enabled);
}

bool HostApi122Impl::GetSegmentSafetyPersistentCacheEnabled() {
  return ocpn::chart_safety::GetPersistentCacheEnabled();
}

bool HostApi122Impl::SaveSegmentSafetyPersistentCache() {
  return ocpn::chart_safety::SavePersistentCache();
}

bool HostApi122Impl::ClearSegmentSafetyPersistentCache() {
  return ocpn::chart_safety::ClearPersistentCache();
}

std::string HostApi122Impl::GetSignalkPayload(ObservedEvt ev) {
  auto msg = obs::UnpackEvtPointer<SignalkMsg>(ev);
  nlohmann::json root;
  root["Data"] = msg->raw_message;
  root["Context"] = msg->context;
  root["ContextSelf"] = msg->context_self;
  return root.dump();
}

const std::set<std::string>& HostApi122Impl::GetActiveMessages() {
  return NavMsgBus::GetInstance().GetActiveMessages();
}
