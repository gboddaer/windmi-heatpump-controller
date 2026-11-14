#ifndef WINDMI_WEB_DIAGNOSTICS_JSON_HPP
#define WINDMI_WEB_DIAGNOSTICS_JSON_HPP

#include "core/DiagnosticRegisters.hpp"
#include <string>

namespace windmi {
inline std::string diagnosticsJson(const DiagnosticSnapshot& snapshot) {
    std::string json = "{\"registers\":{";
    for (size_t i = 0; i < kDiagnosticRegisterCount; ++i) {
        const auto& spec = kDiagnosticRegisters[i];
        const auto& reading = snapshot.readings[i];
        if (i) json += ',';
        json += '"';
        json += spec.key; // compile-time keys only; never user input
        json += "\":{\"address\":" + std::to_string(spec.address) + ",\"raw\":";
        json += reading.raw ? std::to_string(*reading.raw) : "null";
        json += ",\"ageSeconds\":";
        const uint64_t age_ms = snapshot.observed_at_ms >= reading.sampled_at_ms
            ? snapshot.observed_at_ms - reading.sampled_at_ms : 0;
        json += reading.raw ? std::to_string(age_ms / 1000) : "null";
        const int max_age = spec.group == DiagnosticGroup::Existing ? 90 :
                            spec.group == DiagnosticGroup::Alarm ? 180 : 330;
        json += ",\"maxAgeSeconds\":" + std::to_string(max_age) + '}';
    }
    return json + "}}";
}
}
#endif
