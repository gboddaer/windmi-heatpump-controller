/**
 * @file core/Arbitration.hpp
 * @brief DHW demand arbiter: which device mode the unit should be in
 */

#ifndef WINDMI_CORE_ARBITRATION_HPP
#define WINDMI_CORE_ARBITRATION_HPP

#include "config.h"

namespace windmi {

/**
 * Device mode (register 0x002C) implied by an application working mode.
 *
 * Working modes are the application-level pair of enables the API and the
 * scheduler use: 0 Off, 1 DHW-only, 2 Heating-only, 3 DHW+Heating. The device
 * modes below are measured behaviour, not interpretation: mode 5 is what the
 * display controller writes itself when the user selects DHW-only, and DHW
 * priority is a property of that mode - the priority register 0x02BF is
 * accepted and ignored (spec findings F1, F4 and F7).
 *
 * Working mode 3 resolves to MODE_SET_HEAT_DHW here, which is the heating-priority
 * mode the unit then serves the tank in only when heating is not calling. The
 * arbiter refines it to MODE_SET_DHW_ONLY while the tank is calling; see
 * decideDeviceMode().
 */
int deviceModeForWorkingMode(int working_mode);

} // namespace windmi

#endif // WINDMI_CORE_ARBITRATION_HPP
