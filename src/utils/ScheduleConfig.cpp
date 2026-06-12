/**
 * @file src/utils/ScheduleConfig.cpp
 * @brief Schedule JSON persistence implementation
 *
 * Simple hand-parsed JSON — no external dependency needed.
 * Schema: {"enabled":true,"ranges":[{"start":0,"end":360,"mode":0},...]}
 */

#include "utils/ScheduleConfig.hpp"
#include "utils/Logger.hpp"
#include "utils/LogTags.hpp"

#include <fstream>
#include <sstream>
#include <algorithm>

namespace windmi {

// Minimal JSON helpers for schedule-only parsing (no external deps)

static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

static bool parseJsonBool(const std::string& key, const std::string& json) {
    std::string search = "\"" + key + "\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return false;
    
    // Find the colon after the key
    pos = json.find(':', pos + search.size());
    if (pos == std::string::npos) return false;
    
    // Skip whitespace
    std::string rest = json.substr(pos + 1);
    rest = trim(rest);
    
    return rest.rfind("true", 0) == 0;
}

static std::vector<ScheduleRange> parseJsonRanges(const std::string& json) {
    std::vector<ScheduleRange> ranges;
    
    std::string search = "\"ranges\"";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return ranges;
    
    // Find the array start
    pos = json.find('[', pos);
    if (pos == std::string::npos) return ranges;
    
    // Find matching ]
    int depth = 0;
    size_t endPos = pos;
    for (size_t i = pos; i < json.size(); ++i) {
        if (json[i] == '[') depth++;
        else if (json[i] == ']') {
            depth--;
            if (depth == 0) {
                endPos = i;
                break;
            }
        }
    }
    
    std::string arrayContent = json.substr(pos + 1, endPos - pos - 1);
    
    // Parse each object in the array
    size_t objStart = 0;
    while ((objStart = arrayContent.find('{', objStart)) != std::string::npos) {
        size_t objEnd = arrayContent.find('}', objStart);
        if (objEnd == std::string::npos) break;
        
        std::string obj = arrayContent.substr(objStart, objEnd - objStart + 1);
        
        ScheduleRange range;
        
        // Parse start
        {
            std::string key = "\"start\"";
            size_t kp = obj.find(key);
            if (kp != std::string::npos) {
                size_t cp = obj.find(':', kp + key.size());
                if (cp != std::string::npos) {
                    std::string val = trim(obj.substr(cp + 1));
                    // Remove trailing comma
                    if (!val.empty() && val.back() == ',') val.pop_back();
                    try { range.startMinutes = std::stoi(val); } catch (...) {}
                }
            }
        }
        
        // Parse end
        {
            std::string key = "\"end\"";
            size_t kp = obj.find(key);
            if (kp != std::string::npos) {
                size_t cp = obj.find(':', kp + key.size());
                if (cp != std::string::npos) {
                    std::string val = trim(obj.substr(cp + 1));
                    if (!val.empty() && val.back() == ',') val.pop_back();
                    try { range.endMinutes = std::stoi(val); } catch (...) {}
                }
            }
        }
        
        // Parse mode
        {
            std::string key = "\"mode\"";
            size_t kp = obj.find(key);
            if (kp != std::string::npos) {
                size_t cp = obj.find(':', kp + key.size());
                if (cp != std::string::npos) {
                    std::string val = trim(obj.substr(cp + 1));
                    if (!val.empty() && val.back() == ',') val.pop_back();
                    try { range.mode = std::stoi(val); } catch (...) {}
                }
            }
        }
        
        // Parse heatingTemp
        {
            std::string key = "\"heatingTemp\"";
            size_t kp = obj.find(key);
            if (kp != std::string::npos) {
                size_t cp = obj.find(':', kp + key.size());
                if (cp != std::string::npos) {
                    std::string val = trim(obj.substr(cp + 1));
                    if (!val.empty() && val.back() == ',') val.pop_back();
                    try { range.heatingTemp = std::stof(val); } catch (...) {}
                }
            }
        }
        
        // Parse dhwTemp
        {
            std::string key = "\"dhwTemp\"";
            size_t kp = obj.find(key);
            if (kp != std::string::npos) {
                size_t cp = obj.find(':', kp + key.size());
                if (cp != std::string::npos) {
                    std::string val = trim(obj.substr(cp + 1));
                    if (!val.empty() && val.back() == ',') val.pop_back();
                    try { range.dhwTemp = std::stof(val); } catch (...) {}
                }
            }
        }
        
        ranges.push_back(range);
        objStart = objEnd + 1;
    }
    
    return ranges;
}

static std::string rangeToJson(const ScheduleRange& r) {
    std::ostringstream oss;
    oss << "{\"start\":" << r.startMinutes
        << ",\"end\":" << r.endMinutes
        << ",\"mode\":" << r.mode
        << ",\"heatingTemp\":" << static_cast<int>(r.heatingTemp * 10) / 10.0f
        << ",\"dhwTemp\":" << static_cast<int>(r.dhwTemp * 10) / 10.0f
        << "}";
    return oss.str();
}

ScheduleConfig::ScheduleConfig() = default;

bool ScheduleConfig::loadFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        WINDMI_LOG_INFO(LOG_TAG_SCHEDULER, "Schedule file not found: %s (using defaults)", filename.c_str());
        return false;
    }
    
    std::stringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();
    
    enabled_ = parseJsonBool("enabled", content);
    ranges_ = parseJsonRanges(content);
    
    if (!Scheduler::validateRanges(ranges_)) {
        WINDMI_LOG_WARN(LOG_TAG_SCHEDULER, "Schedule ranges invalid, clearing");
        ranges_.clear();
        enabled_ = false;
        return false;
    }
    
    WINDMI_LOG_INFO(LOG_TAG_SCHEDULER, "Loaded schedule: %zu ranges, enabled=%s",
                    ranges_.size(), enabled_ ? "true" : "false");
    return true;
}

bool ScheduleConfig::saveToFile(const std::string& filename) const {
    std::ofstream file(filename);
    if (!file.is_open()) {
        WINDMI_LOG_ERROR(LOG_TAG_SCHEDULER, "Failed to open schedule file for writing: %s", filename.c_str());
        return false;
    }
    
    file << "{\"enabled\":" << (enabled_ ? "true" : "false") << ",\"ranges\":[";
    
    for (size_t i = 0; i < ranges_.size(); ++i) {
        if (i > 0) file << ",";
        file << rangeToJson(ranges_[i]);
    }
    
    file << "]}" << std::endl;
    
    if (file.fail()) {
        WINDMI_LOG_ERROR(LOG_TAG_SCHEDULER, "Failed to write schedule file: %s", filename.c_str());
        return false;
    }
    
    WINDMI_LOG_INFO(LOG_TAG_SCHEDULER, "Saved schedule: %zu ranges", ranges_.size());
    return true;
}

bool ScheduleConfig::isEnabled() const {
    return enabled_;
}

void ScheduleConfig::setEnabled(bool enabled) {
    enabled_ = enabled;
}

const std::vector<ScheduleRange>& ScheduleConfig::getRanges() const {
    return ranges_;
}

bool ScheduleConfig::setRanges(const std::vector<ScheduleRange>& ranges) {
    if (!Scheduler::validateRanges(ranges)) {
        return false;
    }
    ranges_ = ranges;
    return true;
}

} // namespace windmi
