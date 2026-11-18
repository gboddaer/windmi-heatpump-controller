#ifndef WINDMI_TESTCLOCK_HPP
#define WINDMI_TESTCLOCK_HPP

#include <cstdint>
#include <ctime>

namespace windmi_test {

/**
 * @brief Epoch milliseconds at local noon on a calendar date.
 *
 * Tests that reason about calendar days must not hard-code an epoch constant:
 * the same instant lands on a different local date under another timezone, and
 * "today" assertions would silently start covering two days. Local noon keeps a
 * whole test window inside one day for any offset between UTC-11 and UTC+14.
 */
inline int64_t localNoonMs(int year, int month, int day)
{
  std::tm t{};
  t.tm_year = year - 1900;
  t.tm_mon = month - 1;
  t.tm_mday = day;
  t.tm_hour = 12;
  t.tm_min = 0;
  t.tm_isdst = -1; // let the C library resolve DST for that date
  return static_cast<int64_t>(std::mktime(&t)) * 1000;
}

} // namespace windmi_test

#endif // WINDMI_TESTCLOCK_HPP
