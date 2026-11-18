#ifndef WINDMI_WEB_BACKUP_HEATER_JSON_HPP
#define WINDMI_WEB_BACKUP_HEATER_JSON_HPP
#include "core/BackupHeater.hpp"
#include <iomanip>
#include <locale>
#include <sstream>
namespace windmi {
inline std::string backupHeaterJson(const DiagnosticSnapshot& snapshot, bool online) {
    const auto value = estimateBackupHeater(snapshot, online);
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::fixed << std::setprecision(1);
    out << "{\"status\":\"unvalidated\",\"measurementValid\":" << (value.measurement_valid ? "true" : "false")
        << ",\"reason\":\"" << value.reason << "\",\"waterAssumption\":true,\"sourceBeforeRegister\":"
        << REG_BEFORE_BACKUP_HEATER_TEMP << ",\"sourceAfterRegister\":" << REG_LEAVING_WATER_TEMP
        << ",\"maxSampleSpanMs\":5000";
    auto field = [&](const char* name, const auto& number) {
        out << ",\"" << name << "\":";
        if (number) out << *number; else out << "null";
    };
    field("beforeC", value.before_c);
    field("afterC", value.after_c);
    field("deltaC", value.delta_c);
    out << std::setprecision(2); // Preserve the flow register's 0.01 m3/h resolution.
    field("flowM3h", value.flow_m3h);
    out << std::setprecision(1);
    field("estimatedAddedHeatW", value.estimated_added_heat_w);
    field("sampleSpanMs", value.sample_span_ms);
    out << '}';
    return out.str();
}
}
#endif
