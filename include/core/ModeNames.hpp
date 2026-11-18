/**
 * @file core/ModeNames.hpp
 * @brief Human-readable names for the device modes of register 0x002C
 */

#ifndef WINDMI_CORE_MODE_NAMES_HPP
#define WINDMI_CORE_MODE_NAMES_HPP

#include "config.h"

namespace windmi {

/**
 * Name of a device setting mode as written to 0x002C.
 *
 * Modes 3, 4 and 5 are the GCHV single-function modes: marked "only in GCHV" in
 * the vendor manual, but written by the display controller on this unit and
 * executed by it (see docs/superpowers/specs/2026-11-11-native-dhw-only-mode-and-dhw-priority.md,
 * findings F1 and F2).
 */
inline const char* mode_to_string(int mode)
{
  switch (mode)
  {
  case MODE_SET_OFF:
    return "off";
  case MODE_SET_COOL_DHW:
    return "cool+dhw";
  case MODE_SET_HEAT_DHW:
    return "heat+dhw";
  case MODE_SET_COOL_ONLY:
    return "cool";
  case MODE_SET_HEAT_ONLY:
    return "heat";
  case MODE_SET_DHW_ONLY:
    return "dhw_only";
  default:
    return "unknown";
  }
}

} // namespace windmi

#endif // WINDMI_CORE_MODE_NAMES_HPP
