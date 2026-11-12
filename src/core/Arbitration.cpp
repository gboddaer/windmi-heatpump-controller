/**
 * @file core/Arbitration.cpp
 * @brief DHW demand arbiter implementation
 */

#include "core/Arbitration.hpp"

namespace windmi {

int deviceModeForWorkingMode(int working_mode) {
    switch (working_mode) {
        case 0: return MODE_SET_OFF;         // Off
        case 1: return MODE_SET_DHW_ONLY;    // DHW only - the unit's native mode 5
        case 2: return MODE_SET_HEAT_ONLY;   // Heating only - the unit's native mode 4
        case 3: return MODE_SET_HEAT_DHW;    // Both; refined to 5 while the tank calls
        default: return MODE_SET_HEAT_DHW;   // Unknown: keep both functions available
    }
}

} // namespace windmi
