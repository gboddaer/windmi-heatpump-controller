#include <gtest/gtest.h>
#include "core/DiagnosticRegisters.hpp"
#include <map>
#include <vector>

using namespace windmi;
namespace {
class DiagnosticClient : public IModbusClient
{
public:
  std::map<uint16_t, int16_t> values;
  std::vector<uint16_t> reads;
  int writes = 0;
  bool connect() override
  {
    return true;
  }
  void disconnect() override
  {}
  bool isConnected() const override
  {
    return true;
  }
  int16_t readRegister(uint16_t address) override
  {
    reads.push_back(address);
    if (!values.count(address))
      throw ModbusException("unsupported");
    return values.at(address);
  }
  void writeRegister(uint16_t, uint16_t) override
  {
    ++writes;
  }
  void flushBuffer() override
  {}
  std::string getLastError() const override
  {
    return {};
  }
};
const DiagnosticReading& reading(const DiagnosticSnapshot& s, uint16_t address)
{
  return s.readings.at(diagnosticIndex(address));
}
} // namespace

TEST(DiagnosticRegistersTest, BackupParametersStayWithinTwoReadBudgetAndHaveDerivedFreshness)
{
  EXPECT_EQ(diagnosticGroupCount(DiagnosticGroup::Auxiliary), 14u);
  EXPECT_EQ(diagnosticMaxAgeSeconds(DiagnosticGroup::Auxiliary), 480);
  EXPECT_EQ(diagnosticMaxAgeSeconds(DiagnosticGroup::Alarm), 180);
  DiagnosticClient c;
  c.values[0x0259] = 6;
  c.values[0x025A] = 600;
  c.values[0x025B] = 100;
  c.values[0x025C] = -50;
  c.values[0x0202] = -260;
  DiagnosticMonitor m;
  for (uint64_t i = 0; i < 14; ++i)
    m.sampleOptional(c, i * 30000);
  EXPECT_EQ(c.reads.size(), 28u);
  auto snapshot = m.snapshot(14 * 30000);
  for (uint16_t address : {0x0259, 0x025A, 0x025B, 0x025C, 0x0202})
  {
    ASSERT_LT(diagnosticIndex(address), kDiagnosticRegisterCount);
    EXPECT_TRUE(reading(snapshot, address).raw);
  }
  EXPECT_EQ(c.writes, 0);
}

TEST(DiagnosticRegistersTest, ZeroAndUnsignedHighBitsAreNotMissing)
{
  DiagnosticClient c;
  c.values[0x1009] = static_cast<int16_t>(0x8001);
  c.values[0x0055] = 0;
  DiagnosticMonitor m;
  m.sampleOptional(c, 1000);
  const auto s = m.snapshot(1000);
  ASSERT_TRUE(reading(s, 0x1009).raw.has_value());
  EXPECT_EQ(*reading(s, 0x1009).raw, 0x8001);
  ASSERT_TRUE(reading(s, 0x0055).raw.has_value());
  EXPECT_EQ(*reading(s, 0x0055).raw, 0);
  EXPECT_FALSE(reading(s, 0x100A).raw.has_value());
  EXPECT_EQ(c.writes, 0);
}

TEST(DiagnosticRegistersTest, OptionalReadsAreBoundedAndDoNotRetryOrWrite)
{
  DiagnosticClient c; // all optional reads fail
  DiagnosticMonitor m;
  m.sampleOptional(c, 0);
  m.sampleOptional(c, 1);
  m.sampleOptional(c, 29999);
  EXPECT_EQ(c.reads.size(), 2u);
  m.sampleOptional(c, 30000);
  EXPECT_EQ(c.reads.size(), 4u);
  EXPECT_EQ(c.reads[0], 0x1009);
  EXPECT_EQ(c.reads[2], 0x100A);
  m.sampleOptional(c, 900000); // no catch-up burst after a delay
  EXPECT_EQ(c.reads.size(), 6u);
  EXPECT_EQ(c.writes, 0);
}

TEST(DiagnosticRegistersTest, ReadTimeJitterDoesNotSkipNormalPollsOrCauseCatchupBursts)
{
  DiagnosticClient c;
  DiagnosticMonitor m;
  m.sampleOptional(c, 0);
  m.sampleOptional(c, 30010);
  EXPECT_EQ(c.reads.size(), 4u);
  m.sampleOptional(c, 60005); // only 29995 ms since the previous read
  EXPECT_EQ(c.reads.size(), 6u);
  m.sampleOptional(c, 60006); // repeated UI kick must not add traffic
  EXPECT_EQ(c.reads.size(), 6u);
  m.sampleOptional(c, 90009);
  EXPECT_EQ(c.reads.size(), 8u);
  m.sampleOptional(c, 900000); // missed periods are skipped, never caught up
  EXPECT_EQ(c.reads.size(), 10u);
  m.sampleOptional(c, 900001);
  EXPECT_EQ(c.reads.size(), 10u);
  EXPECT_EQ(c.writes, 0);
}

TEST(DiagnosticRegistersTest, CurrentReadsAreCapturedWithoutDuplicateTraffic)
{
  DiagnosticClient c;
  c.values[0x0017] = 280;
  DiagnosticMonitor m;
  m.beginSample();
  EXPECT_EQ(m.readRegister(c, 0x0017, 1000), 280);
  auto s = m.snapshot(31000);
  EXPECT_EQ(reading(s, 0x0017).sampled_at_ms, 1000u);
  EXPECT_EQ(s.observed_at_ms, 31000u);
  ASSERT_TRUE(reading(s, 0x0017).raw);
  EXPECT_EQ(*reading(s, 0x0017).raw, 280);
  EXPECT_EQ(c.reads.size(), 1u);
  m.beginSample();
  EXPECT_FALSE(reading(m.snapshot(31000), 0x0017).raw);
  c.values.clear();
  EXPECT_THROW(m.readRegister(c, 0x0017, 32000), ModbusException);
  EXPECT_FALSE(reading(m.snapshot(32000), 0x0017).raw);
}

TEST(DiagnosticRegistersTest, OptionalSamplesRetainAgeButFailedRereadInvalidates)
{
  DiagnosticClient c;
  c.values[0x1009] = 0;
  c.values[0x0055] = 60;
  DiagnosticMonitor m;
  m.sampleOptional(c, 0);
  m.beginSample();
  EXPECT_EQ(reading(m.snapshot(30000), 0x0055).sampled_at_ms, 0u);
  EXPECT_TRUE(reading(m.snapshot(30000), 0x0055).raw);
  c.values.erase(0x1009);
  for (uint64_t t = 30000; t <= 120000; t += 30000)
    m.sampleOptional(c, t);
  EXPECT_FALSE(reading(m.snapshot(120000), 0x1009).raw);
  EXPECT_TRUE(reading(m.snapshot(120000), 0x0055).raw);
  EXPECT_EQ(c.writes, 0);
}
