#include "utils/ScheduleConfig.hpp"
#include "config.h"
#include "utils/Platform.hpp"
#include "utils/Logger.hpp"
#include "utils/LogTags.hpp"
#include <mongoose.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <locale>
#include <map>
#include <regex>
#include <sstream>
namespace windmi {
namespace {
std::string text(mg_str value) { return std::string(value.buf, value.len); }
// Mongoose is deliberately permissive about number grammar/trailing commas.
// Reject those before tokenizing, and require the root token to consume all input.
bool lexicalJson(const std::string& s) {
    static const std::regex number(R"(-?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?)");
    bool quoted = false, escaped = false;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
            continue;
        }
        if (c == '"') { quoted = true; continue; }
        if (c == ',') {
            const auto next = s.find_first_not_of(" \r\n\t", i + 1);
            if (next == std::string::npos || s[next] == '}' || s[next] == ']') return false;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const auto end = s.find_first_of(" \r\n\t,]}:", i);
            const auto n = s.substr(i, end == std::string::npos ? end : end - i);
            if (n.size() > 64 || !std::regex_match(n, number)) return false;
            i += n.size() - 1;
        }
    }
    return !quoted;
}
using Fields = std::map<std::string, mg_str>;
bool fields(mg_str obj, const std::vector<std::string>& allowed, Fields& result) {
    if (obj.len < 2 || obj.buf[0] != '{') return false;
    size_t offset = 0; mg_str key{}, value{};
    while ((offset = mg_json_next(obj, offset, &key, &value)) != 0) {
        const auto name = text(key);
        bool known = false;
        for (const auto& k : allowed) if (name == '"' + k + '"') known = true;
        if (!known || !result.emplace(name, value).second) return false;
    }
    return true;
}
bool numeric(mg_str token, double& value) {
    return mg_json_get_num(token, "$", &value) && std::isfinite(value);
}
bool integer(const Fields& f, const char* key, int& out) {
    auto it = f.find(key); double value;
    if (it == f.end() || !numeric(it->second, value) || std::floor(value) != value ||
        value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max()) return false;
    out = static_cast<int>(value); return true;
}
bool temperature(const Fields& f, const char* key, float& out, double minimum, double maximum) {
    auto it = f.find(key);
    if (it == f.end()) return true; // Documented legacy safe defaults, never malformed values.
    double value;
    if (!numeric(it->second, value) || value < minimum || value > maximum) return false;
    out = static_cast<float>(value); return true;
}
}
bool ScheduleConfig::parseJson(const std::string& json, ScheduleState& out, std::string& error, bool* defaultsPresent) {
    auto fail = [&](const char* reason) { error = reason; return false; };
    const auto first = json.find_first_not_of(" \r\n\t"), last = json.find_last_not_of(" \r\n\t");
    if (first == std::string::npos) return fail("Empty schedule JSON");
    const auto trimmed = json.substr(first, last - first + 1);
    if (!lexicalJson(trimmed)) return fail("Malformed schedule JSON");
    const auto root = mg_str_n(trimmed.data(), trimmed.size());
    int length = 0;
    if (mg_json_get(root, "$", &length) != 0 || static_cast<size_t>(length) != trimmed.size())
        return fail("Malformed schedule JSON");
    Fields f;
    if (!fields(root, {"enabled", "paused", "ranges", "defaults"}, f)) return fail("Invalid or duplicate schedule fields");
    const auto enabled = f.find("\"enabled\""), ranges = f.find("\"ranges\"");
    if (enabled == f.end() || ranges == f.end()) return fail("Enabled and ranges are required");
    ScheduleState candidate;
    if (!mg_json_get_bool(enabled->second, "$", &candidate.enabled)) {
        double value;
        if (!numeric(enabled->second, value) || (value != 0 && value != 1)) return fail("Enabled must be boolean or 0/1");
        candidate.enabled = value == 1;
    }
    const auto paused = f.find("\"paused\"");
    if (paused != f.end() && !mg_json_get_bool(paused->second, "$", &candidate.paused)) return fail("Paused must be boolean");
    const auto defaults = f.find("\"defaults\"");
    if (defaults != f.end() && !parseDefaultsJson(std::string(defaults->second.buf,defaults->second.len),candidate.defaults,error)) return false;
    const auto array = ranges->second;
    if (array.len < 2 || array.buf[0] != '[') return fail("Ranges must be an array");
    size_t offset = 0; mg_str object{};
    while ((offset = mg_json_next(array, offset, nullptr, &object)) != 0) {
        Fields r;
        if (!fields(object, {"start", "end", "mode", "heatingTemp", "dhwTemp"}, r)) return fail("Invalid or duplicate range fields");
        ScheduleRange range;
        if (!integer(r, "\"start\"", range.startMinutes) || !integer(r, "\"end\"", range.endMinutes) ||
            !integer(r, "\"mode\"", range.mode) ||
            !temperature(r, "\"heatingTemp\"", range.heatingTemp, HEATING_TEMP_MIN, HEATING_TEMP_MAX) ||
            !temperature(r, "\"dhwTemp\"", range.dhwTemp, DHW_TEMP_MIN, DHW_TEMP_MAX))
            return fail("Invalid time, mode or temperature fields");
        candidate.ranges.push_back(range);
        if (candidate.ranges.size() > 1440) return fail("Too many schedule ranges");
    }
    if (!Scheduler::validateRanges(candidate.ranges)) return fail("Invalid bounds or overlapping schedule ranges");
    if (defaultsPresent) *defaultsPresent = defaults != f.end();
    out = std::move(candidate);
    error.clear(); return true;
}
bool ScheduleConfig::parseDefaultsJson(const std::string& json, ScheduleDefaults& out, std::string& error) {
    const auto first = json.find_first_not_of(" \r\n\t"), last = json.find_last_not_of(" \r\n\t");
    const auto trimmed = first == std::string::npos ? std::string{} : json.substr(first,last-first+1);
    const auto root = mg_str_n(trimmed.data(),trimmed.size());
    Fields values; ScheduleDefaults candidate; int length = 0;
    if (!lexicalJson(trimmed) || mg_json_get(root,"$",&length) != 0 || static_cast<size_t>(length) != trimmed.size() ||
        !fields(root,{"mode","heatingTemp","dhwTemp"},values) || values.size() != 3 ||
        !integer(values,"\"mode\"",candidate.mode) ||
        !temperature(values,"\"heatingTemp\"",candidate.heatingTemp,HEATING_TEMP_MIN,HEATING_TEMP_MAX) ||
        !temperature(values,"\"dhwTemp\"",candidate.dhwTemp,DHW_TEMP_MIN,DHW_TEMP_MAX) || !Scheduler::validateDefaults(candidate)) {
        error = "Invalid or incomplete schedule defaults"; return false;
    }
    out = candidate; error.clear(); return true;
}
bool ScheduleConfig::parseEnabledJson(const std::string& json, bool& enabled) {
    const auto first = json.find_first_not_of(" \r\n\t"), last = json.find_last_not_of(" \r\n\t");
    if (first == std::string::npos) return false;
    const auto trimmed = json.substr(first, last - first + 1);
    if (!lexicalJson(trimmed)) return false;
    const auto root = mg_str_n(trimmed.data(), trimmed.size());
    int length = 0;
    if (mg_json_get(root, "$", &length) != 0 || static_cast<size_t>(length) != trimmed.size()) return false;
    Fields f;
    if (!fields(root, {"enabled"}, f) || f.size() != 1) return false;
    const auto token = f.begin()->second;
    if (mg_json_get_bool(token, "$", &enabled)) return true;
    double value;
    if (!numeric(token, value) || (value != 0 && value != 1)) return false;
    enabled = value == 1; return true;
}
std::string ScheduleConfig::toJson(const ScheduleState& state, bool runtime) {
    std::ostringstream out; out.imbue(std::locale::classic());
    out << "{\"enabled\":" << (state.enabled ? "true" : "false")
        << ",\"paused\":" << (state.paused ? "true" : "false")
        << ",\"defaults\":{\"mode\":" << state.defaults.mode << ",\"heatingTemp\":" << state.defaults.heatingTemp
        << ",\"dhwTemp\":" << state.defaults.dhwTemp << "},\"ranges\":[";
    for (size_t i = 0; i < state.ranges.size(); ++i) {
        if (i) out << ',';
        const auto& r = state.ranges[i];
        out << "{\"start\":" << r.startMinutes << ",\"end\":" << r.endMinutes << ",\"mode\":" << r.mode
            << ",\"heatingTemp\":" << r.heatingTemp << ",\"dhwTemp\":" << r.dhwTemp << '}';
    }
    out << ']';
    if (runtime) out << ",\"revision\":" << state.revision << ",\"instanceId\":\"" << state.instanceId
                     << "\",\"persistenceError\":" << (state.persistenceError ? "true" : "false");
    out << '}'; return out.str();
}
bool ScheduleConfig::setRanges(const std::vector<ScheduleRange>& ranges) {
    if (!Scheduler::validateRanges(ranges)) return false;
    state_.ranges = ranges; return true;
}
bool ScheduleConfig::setState(const ScheduleState& state) {
    if (!Scheduler::validateRanges(state.ranges) || !Scheduler::validateDefaults(state.defaults)) return false;
    state_ = state; has_defaults_ = true; return true;
}
bool ScheduleConfig::loadFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) return false;
    std::stringstream contents; contents << file.rdbuf();
    ScheduleState candidate; std::string error; bool defaultsPresent = false;
    if (file.bad() || !parseJson(contents.str(), candidate, error, &defaultsPresent)) {
        WINDMI_LOG_WARN(LOG_TAG_SCHEDULER, "Invalid schedule file: %s", error.c_str()); return false;
    }
    state_ = std::move(candidate); has_defaults_ = defaultsPresent; return true;
}
bool ScheduleConfig::saveToFile(const std::string& filename) const {
    if (!Scheduler::validateRanges(state_.ranges) || !Scheduler::validateDefaults(state_.defaults)) return false;
    const std::filesystem::path destination(filename);
    std::error_code error;
    if (!destination.parent_path().empty()) std::filesystem::create_directories(destination.parent_path(), error);
    if (error) return false;
    const std::string temporary = filename + ".tmp";
    {
        std::ofstream file(temporary, std::ios::trunc);
        if (!file) return false;
        file << toJson(state_) << '\n';
        file.flush(); file.close();
        if (file.fail()) { std::filesystem::remove(temporary, error); return false; }
    }
    if (!platform::atomic_replace_file(temporary, filename)) {
        std::filesystem::remove(temporary, error); return false;
    }
    return true;
}
}
