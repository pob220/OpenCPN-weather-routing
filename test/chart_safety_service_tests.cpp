#include <gtest/gtest.h>

#include "gui/chart_safety_service.h"

namespace {

using ocpn::chart_safety::MayPrefetchNeighbour;
using ocpn::chart_safety::PlanTileBatchBlocks;

TEST(ChartSafetyService, PermitsPrefetchBeforePositiveBudgetExpires) {
  EXPECT_TRUE(MayPrefetchNeighbour(49, 50));
}

TEST(ChartSafetyService, StopsPrefetchAtAndAfterBudget) {
  EXPECT_FALSE(MayPrefetchNeighbour(50, 50));
  EXPECT_FALSE(MayPrefetchNeighbour(5000, 50));
}

TEST(ChartSafetyService, NonPositiveBudgetIsUnlimited) {
  EXPECT_TRUE(MayPrefetchNeighbour(5000, 0));
  EXPECT_TRUE(MayPrefetchNeighbour(5000, -1));
}

TEST(ChartSafetyService, PlansProviderSizedGeographicBlocks) {
  std::set<std::pair<long, long>> tiles;
  for (long lat = 0; lat < 7; ++lat)
    for (long lon = 0; lon < 7; ++lon) tiles.insert({lat, lon});

  const auto blocks = PlanTileBatchBlocks(tiles, 6);
  ASSERT_EQ(blocks.size(), 4u);
  for (const auto& block : blocks) {
    EXPECT_LE(block.max_lat_tile - block.min_lat_tile + 1, 6);
    EXPECT_LE(block.max_lon_tile - block.min_lon_tile + 1, 6);
    const long rows =
        (block.max_lat_tile - block.min_lat_tile + 1) * 40 + 1;
    const long cols =
        (block.max_lon_tile - block.min_lon_tile + 1) * 40 + 1;
    EXPECT_LE(rows * cols, 65536);
  }
}

TEST(ChartSafetyService, NegativeTileIndexesStayInBoundedBlocks) {
  const std::set<std::pair<long, long>> tiles = {
      {-7, -7}, {-6, -6}, {-1, -1}, {0, 0}, {5, 5}, {6, 6}};
  const auto blocks = PlanTileBatchBlocks(tiles, 6);
  ASSERT_FALSE(blocks.empty());
  for (const auto& block : blocks) {
    EXPECT_LE(block.max_lat_tile - block.min_lat_tile + 1, 6);
    EXPECT_LE(block.max_lon_tile - block.min_lon_tile + 1, 6);
  }
}

}  // namespace
