/***************************************************************************
 * Optional, versioned batch interface for licensed plugin-vector charts.
 * The provider retains chart geometry; no plaintext chart is exported.
 ***************************************************************************/
#ifndef OCPN_PLUGIN_CHART_SAFETY_H
#define OCPN_PLUGIN_CHART_SAFETY_H
#include <stdint.h>
#define OCPN_PLUGIN_CHART_SAFETY_GRID_ABI_V1 1u
#define OCPN_PLUGIN_CHART_SAFETY_RESULT_DERIVED_CACHE_ALLOWED 0x00000001u
#define OCPN_PLUGIN_CHART_SAFETY_GRID_SYMBOL_V1 \
  "OCPN_PluginChartSafetyGridV1"
#define OCPN_PLUGIN_CHART_SAFETY_IDENTITY_SYMBOL_V1 \
  "OCPN_PluginChartSafetyIdentityV1"
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*OCPN_PluginChartSafetyObjectVisitorV1)(
    void *context, const void *pi_s57_object, const uint64_t *hit_cells,
    uint32_t hit_word_count);
typedef struct OCPN_PluginChartSafetyGridRequestV1 {
  uint32_t struct_size;
  uint32_t abi_version;
  double min_lat;
  double min_lon;
  double lat_step;
  double lon_step;
  uint32_t rows;
  uint32_t cols;
  float select_radius_degrees;
  const uint8_t *active_cells;
  const void *plugin_viewport;
  void *visitor_context;
  OCPN_PluginChartSafetyObjectVisitorV1 visit_object;
} OCPN_PluginChartSafetyGridRequestV1;
typedef struct OCPN_PluginChartSafetyGridResultV1 {
  uint32_t struct_size;
  uint32_t abi_version;
  uint32_t processed_cells;
  uint32_t candidate_objects;
  uint32_t hit_objects;
  uint32_t result_flags;
} OCPN_PluginChartSafetyGridResultV1;
/* Returns 1 for complete, 0 for unsupported, and -1 for error. */
typedef int (*OCPN_PluginChartSafetyGridFnV1)(
    void *plugin_chart, const OCPN_PluginChartSafetyGridRequestV1 *request,
    OCPN_PluginChartSafetyGridResultV1 *result);
/*
 * Returns a stable identifier for the provider's chart-safety semantics.
 * Change the returned value only when identical requests can produce
 * materially different classifications. It must not contain build paths,
 * timestamps or other installation-specific data.
 */
typedef const char *(*OCPN_PluginChartSafetyIdentityFnV1)(void);
#ifdef __cplusplus
}
#endif
#endif
