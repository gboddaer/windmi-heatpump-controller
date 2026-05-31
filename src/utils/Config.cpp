/**
 * @file src/utils/Config.cpp
 * @brief Configuration management implementation
 */

#include "utils/Config.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace windmi {

Config::Config() {
    // Default configuration
    set("modbus.host", "192.168.123.10");
    set("modbus.port", "8899");
    set("modbus.slave_id", "1");
    set("web.port", "8080");
    set("dhw.target", "45.0");
    set("heating.target", "40.0");
}

void Config::loadFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw ConfigException("Failed to open config file: " + filename);
    }
    
    std::string line;
    while (std::getline(file, line)) {
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }
        
        // Find delimiter
        size_t pos = line.find('=');
        if (pos == std::string::npos) {
            continue;
        }
        
        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);
        
        // Trim whitespace
        key.erase(0, key.find_first_not_of(" \t"));
        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));
        value.erase(value.find_last_not_of(" \t") + 1);
        
        if (!key.empty()) {
            set(key, value);
        }
    }
}

std::string Config::getString(const std::string& key, const std::string& default_value) const {
    auto it = values_.find(key);
    if (it != values_.end()) {
        return it->second;
    }
    return default_value;
}

int Config::getInt(const std::string& key, int default_value) const {
    auto it = values_.find(key);
    if (it != values_.end()) {
        try {
            return std::stoi(it->second);
        } catch (...) {
            return default_value;
        }
    }
    return default_value;
}

double Config::getDouble(const std::string& key, double default_value) const {
    auto it = values_.find(key);
    if (it != values_.end()) {
        try {
            return std::stod(it->second);
        } catch (...) {
            return default_value;
        }
    }
    return default_value;
}

bool Config::getBool(const std::string& key, bool default_value) const {
    auto it = values_.find(key);
    if (it != values_.end()) {
        std::string value = it->second;
        std::transform(value.begin(), value.end(), value.begin(), ::tolower);
        return value == "true" || value == "1" || value == "yes";
    }
    return default_value;
}

void Config::set(const std::string& key, const std::string& value) {
    values_[key] = value;
}

bool Config::hasKey(const std::string& key) const {
    return values_.find(key) != values_.end();
}

} // namespace windmi
