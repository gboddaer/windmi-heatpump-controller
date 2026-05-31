/**
 * @file src/utils/JsonHelpers.cpp
 * @brief JSON helper utilities implementation
 */

#include "utils/JsonHelpers.hpp"
#include <sstream>
#include <iomanip>
#include <cstring>

namespace windmi {

bool JsonHelpers::parseTemperature(const std::string& json, const std::string& key, double& temperature) {
    double temp;
    if (mg_json_get_num(mg_str(json.c_str()), (key + ".temperature").c_str(), &temp) < 1) {
        return false;
    }
    temperature = temp;
    return true;
}

bool JsonHelpers::parseInt(const std::string& json, const std::string& key, long& value) {
    long val = mg_json_get_long(mg_str(json.c_str()), key.c_str(), -1);
    if (val == -1) {
        return false;
    }
    value = val;
    return true;
}

bool JsonHelpers::parseString(const std::string& json, const std::string& key, std::string& value) {
    const char* str = mg_json_get_str(mg_str(json.c_str()), key.c_str());
    if (str == nullptr) {
        return false;
    }
    value = str;
    return true;
}

std::string JsonHelpers::generateStatusJson(
    double dhw_temp, double dhw_target,
    double heating_temp, double heating_target,
    double outdoor_temp, double leaving_water_temp,
    const std::string& mode, const std::string& running_status,
    const std::string& priority, const std::string& status,
    bool device_online,
    double ac_current, double dc_current,
    double ac_voltage, double dc_voltage,
    double ac_power, int working_mode) {

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1);
    oss << "{"
        << "\"dhwTemperature\":" << dhw_temp << ","
        << "\"dhwTarget\":" << dhw_target << ","
        << "\"heatingTemperature\":" << heating_temp << ","
        << "\"heatingTarget\":" << heating_target << ","
        << "\"outdoorTemperature\":" << outdoor_temp << ","
        << "\"leavingWaterTemperature\":" << leaving_water_temp << ","
        << "\"mode\":\"" << mode << "\","
        << "\"runningStatus\":\"" << running_status << "\","
        << "\"priority\":\"" << priority << "\","
        << "\"status\":\"" << status << "\","
        << "\"deviceOnline\":" << (device_online ? "true" : "false") << ","
        << "\"acCurrent\":" << ac_current << ","
        << "\"dcCurrent\":" << dc_current << ","
        << "\"acVoltage\":" << ac_voltage << ","
        << "\"dcVoltage\":" << dc_voltage << ","
        << "\"acPower\":" << ac_power << ","
        << "\"workingMode\":" << working_mode
        << "}";
    return oss.str();
}

std::string JsonHelpers::generateSuccessResponse(bool success, bool verified, const std::string& message) {
    std::ostringstream oss;
    oss << "{"
        << "\"success\":" << (success ? "true" : "false") << ","
        << "\"verified\":" << (verified ? "true" : "false");

    if (!message.empty()) {
        oss << ",\"message\":\"" << message << "\"";
    }

    oss << "}";
    return oss.str();
}

std::string JsonHelpers::generateErrorResponse(const std::string& error, int /*code*/) {
    std::ostringstream oss;
    oss << "{" 
        << "\"error\":\"" << error << "\""
        << "}";
    return oss.str();
}

} // namespace windmi
