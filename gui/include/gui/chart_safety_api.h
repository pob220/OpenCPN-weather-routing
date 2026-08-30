/***************************************************************************
 * Internal chart-safety service declarations.                             *
 ***************************************************************************/

#ifndef GUI_CHART_SAFETY_API_H_
#define GUI_CHART_SAFETY_API_H_

#include <string>

#include "ocpn_plugin.h"

namespace ocpn::chart_safety {

using SegmentSafetyStatus = HostApi122::SegmentSafetyStatus;
using SegmentSafetySource = HostApi122::SegmentSafetySource;
using SegmentSafetyDiagnosticReason = HostApi122::SegmentSafetyDiagnosticReason;
using SegmentSafetyHitCause = HostApi122::SegmentSafetyHitCause;
using SegmentSafetyOptions = HostApi122::SegmentSafetyOptions;
using SegmentSafetyResult = HostApi122::SegmentSafetyResult;
using SegmentSafetyRequestServiceResult =
    HostApi122::SegmentSafetyRequestServiceResult;
using SegmentSafetyTile = HostApi122::SegmentSafetyTile;
using SegmentSafetyTileCacheLookup = HostApi122::SegmentSafetyTileCacheLookup;
using SegmentSafetyTileCacheStore = HostApi122::SegmentSafetyTileCacheStore;
using SegmentSafetyTileCacheIdentity =
    HostApi122::SegmentSafetyTileCacheIdentity;
using SegmentSafetyTileCacheDependenciesChanged =
    HostApi122::SegmentSafetyTileCacheDependenciesChanged;
using SegmentSafetyTileCacheCallbacks =
    HostApi122::SegmentSafetyTileCacheCallbacks;
using SegmentSafetyChartInfo = HostApi122::SegmentSafetyChartInfo;
using ChartSafetyFeature = HostApi122::ChartSafetyFeature;

constexpr auto kSafe = HostApi122::kSegmentSafetySafe;
constexpr auto kCrossesLand = HostApi122::kSegmentSafetyCrossesLand;
constexpr auto kWithinLandMargin = HostApi122::kSegmentSafetyWithinLandMargin;
constexpr auto kUnsafeArea = HostApi122::kSegmentSafetyUnsafeArea;
constexpr auto kNoData = HostApi122::kSegmentSafetyNoData;
constexpr auto kError = HostApi122::kSegmentSafetyError;
constexpr auto kDryingArea = HostApi122::kSegmentSafetyDryingArea;
constexpr auto kTooShallow = HostApi122::kSegmentSafetyTooShallow;
constexpr auto kUnknownDepth = HostApi122::kSegmentSafetyUnknownDepth;
constexpr auto kPendingData = HostApi122::kSegmentSafetyPendingData;

constexpr auto kSourceNone = HostApi122::kSegmentSafetySourceNone;
constexpr auto kSourceVectorChart = HostApi122::kSegmentSafetySourceVectorChart;
constexpr auto kSourceCm93 = HostApi122::kSegmentSafetySourceCm93;
constexpr auto kSourceGshhsFallback =
    HostApi122::kSegmentSafetySourceGshhsFallback;
constexpr auto kSourcePluginVector =
    HostApi122::kSegmentSafetySourcePluginVector;

constexpr auto kDiagnosticNone = HostApi122::kSegmentSafetyDiagnosticNone;
constexpr auto kDiagnosticNoChartDatabase =
    HostApi122::kSegmentSafetyDiagnosticNoChartDatabase;
constexpr auto kDiagnosticNoCandidateChart =
    HostApi122::kSegmentSafetyDiagnosticNoCandidateChart;
constexpr auto kDiagnosticRasterOnly =
    HostApi122::kSegmentSafetyDiagnosticRasterOnly;
constexpr auto kDiagnosticUnsupportedChartType =
    HostApi122::kSegmentSafetyDiagnosticUnsupportedChartType;
constexpr auto kDiagnosticChartLoadFailed =
    HostApi122::kSegmentSafetyDiagnosticChartLoadFailed;
constexpr auto kDiagnosticNoLandAreaGeometry =
    HostApi122::kSegmentSafetyDiagnosticNoLandAreaGeometry;
constexpr auto kDiagnosticChartGeometryClear =
    HostApi122::kSegmentSafetyDiagnosticChartGeometryClear;
constexpr auto kDiagnosticChartGeometryHit =
    HostApi122::kSegmentSafetyDiagnosticChartGeometryHit;
constexpr auto kDiagnosticGshhsFallback =
    HostApi122::kSegmentSafetyDiagnosticGshhsFallback;
constexpr auto kDiagnosticPendingData =
    HostApi122::kSegmentSafetyDiagnosticPendingData;

constexpr auto kHitNone = HostApi122::kSegmentSafetyHitNone;
constexpr auto kHitEndpointInLandArea =
    HostApi122::kSegmentSafetyHitEndpointInLandArea;
constexpr auto kHitSegmentIntersectsLandAreaEdge =
    HostApi122::kSegmentSafetyHitSegmentIntersectsLandAreaEdge;
constexpr auto kHitMarginToLandAreaEdge =
    HostApi122::kSegmentSafetyHitMarginToLandAreaEdge;

wxString PointDiagnostic(double lat, double lon);
int GetPendingRequestCount();
bool ServicePendingRequests(int max_requests, int max_milliseconds,
                            SegmentSafetyRequestServiceResult *result);
bool PrepareRawTiles(const long *lat_tiles, const long *lon_tiles,
                     int tile_count, bool require_depth,
                     SegmentSafetyResult *result);
bool PrepareHazardSnapshot(double min_lat, double min_lon, double max_lat,
                           double max_lon, bool enable_fast_path,
                           bool shadow_compare,
                           const SegmentSafetyOptions *options,
                           SegmentSafetyResult *result);
bool CheckSegment(double lat1, double lon1, double lat2, double lon2,
                  const SegmentSafetyOptions *options,
                  SegmentSafetyResult *result);
bool PrepareCorridor(const double *latitudes, const double *longitudes,
                     const int *point_counts, int polyline_count,
                     double corridor_margin_nm, int fine_tile_halo,
                     const SegmentSafetyOptions *options,
                     SegmentSafetyResult *result);
bool PrepareGridForSegment(double lat1, double lon1, double lat2, double lon2,
                           double safety_margin_nm,
                           SegmentSafetyResult *result);
bool PrepareRouteMaskForPolylines(const double *latitudes,
                                  const double *longitudes,
                                  const int *point_counts, int polyline_count,
                                  double corridor_margin_nm,
                                  const SegmentSafetyOptions *options,
                                  SegmentSafetyResult *result);
void ReleasePins();
bool RegisterTileCache(const SegmentSafetyTileCacheCallbacks *callbacks);
bool GetChartIdentity(std::string *identity);
int GetChartInfoCount();
bool GetChartInfo(int ordinal, SegmentSafetyChartInfo *chart_info);
bool GetChartCoverageTiles(const int *chart_db_indices, int chart_count,
                           double tile_degrees, long *lat_tiles,
                           long *lon_tiles, int tile_capacity, int *tile_count,
                           bool *complete);
bool SetPersistentCacheEnabled(bool enabled);
bool GetPersistentCacheEnabled();
bool SavePersistentCache();
bool ClearPersistentCache();

}  // namespace ocpn::chart_safety

#endif  // GUI_CHART_SAFETY_API_H_
