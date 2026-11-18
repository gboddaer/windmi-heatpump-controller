#include <gtest/gtest.h>
#include "core/BackupHeater.hpp"
using namespace windmi;
namespace {
DiagnosticSnapshot paired(int before = 310, int after = 336, int flow = 100) {
    DiagnosticSnapshot s;
    s.observed_at_ms = 4000;
    s.readings.at(diagnosticIndex(0x1008)) = {static_cast<uint16_t>(before), 1100};
    s.readings.at(diagnosticIndex(REG_LEAVING_WATER_TEMP)) = {static_cast<uint16_t>(after), 1000};
    s.readings.at(diagnosticIndex(REG_WATER_FLOW)) = {static_cast<uint16_t>(flow), 3000};
    return s;
}
}
TEST(BackupHeaterTest, ReportsOnlyWaterBasedValidationEstimateNeverAnOnOffClaim) {
    auto value = estimateBackupHeater(paired(), true);
    ASSERT_TRUE(value.measurement_valid);
    ASSERT_TRUE(value.estimated_added_heat_w);
    EXPECT_NEAR(*value.estimated_added_heat_w, 3023.22, .1);
    EXPECT_NEAR(*value.delta_c, 2.6, .001);
    EXPECT_EQ(*value.sample_span_ms, 2000u);
    EXPECT_EQ(value.reason, "ok");
}
TEST(BackupHeaterTest, PreservesZeroAndNegativeHeatForOffsetAndTransientAnalysis) {
    auto zero = estimateBackupHeater(paired(310,310), true);
    EXPECT_TRUE(zero.measurement_valid);
    ASSERT_TRUE(zero.estimated_added_heat_w);
    EXPECT_EQ(*zero.estimated_added_heat_w, 0);
    auto negative = estimateBackupHeater(paired(320,310), true);
    EXPECT_TRUE(negative.measurement_valid);
    EXPECT_LT(*negative.estimated_added_heat_w, 0);
}
TEST(BackupHeaterTest, MissingOrSentinelTemperaturesAreNotZeroOrOff) {
    auto s = paired(); s.readings[diagnosticIndex(0x1008)].raw.reset();
    auto missing = estimateBackupHeater(s, true);
    EXPECT_FALSE(missing.measurement_valid);
    EXPECT_FALSE(missing.before_c);
    EXPECT_FALSE(missing.estimated_added_heat_w);
    EXPECT_EQ(missing.reason, "missing_temperature");
    auto invalid = estimateBackupHeater(paired(-400), true);
    EXPECT_FALSE(invalid.measurement_valid);
    EXPECT_FALSE(invalid.estimated_added_heat_w);
    EXPECT_EQ(invalid.reason, "invalid_temperature");
}
TEST(BackupHeaterTest, ZeroMissingAndInvalidFlowCannotProduceAnEnergyEstimate) {
    auto idle = estimateBackupHeater(paired(310,336,0), true);
    EXPECT_FALSE(idle.measurement_valid);
    EXPECT_TRUE(idle.delta_c);
    EXPECT_FALSE(idle.estimated_added_heat_w);
    EXPECT_EQ(idle.reason, "no_flow");
    auto invalid = estimateBackupHeater(paired(310,336,-1), true);
    EXPECT_EQ(invalid.reason, "invalid_flow");
    auto s = paired(); s.readings[diagnosticIndex(REG_WATER_FLOW)].raw.reset();
    EXPECT_EQ(estimateBackupHeater(s, true).reason, "missing_flow");
}
TEST(BackupHeaterTest, RejectsOfflineStaleAndWidelySeparatedSources) {
    auto offline = estimateBackupHeater(paired(), false);
    EXPECT_FALSE(offline.measurement_valid);
    EXPECT_FALSE(offline.estimated_added_heat_w);
    EXPECT_EQ(offline.reason, "offline");
    auto skew = paired(); skew.observed_at_ms = 8000;
    skew.readings[diagnosticIndex(REG_WATER_FLOW)].sampled_at_ms = 7000;
    EXPECT_EQ(estimateBackupHeater(skew, true).reason, "sample_skew");
    auto stale = paired(); stale.observed_at_ms = 20000;
    EXPECT_EQ(estimateBackupHeater(stale, true).reason, "stale_sample");
}
