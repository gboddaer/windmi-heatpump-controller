/**
 * @file utils/JsonHelpers.hpp
 * @brief JSON helper utilities
 */

#ifndef WINDMI_UTILS_JSON_HELPERS_HPP
#define WINDMI_UTILS_JSON_HELPERS_HPP

#include <string>
#include <mongoose.h>

namespace windmi {

/**
 * @brief JSON helper utilities
 * 
 * Provides C++ convenience functions for JSON parsing and generation.
 */
class JsonHelpers {
public:
    /**
     * @brief Parse temperature from JSON
     * @param json JSON string
     * @param key Key name
     * @param temperature Output parameter
     * @return true if successful, false otherwise
     */
    static bool parseTemperature(const std::string& json, 
                                  const std::string& key, 
                                  double& temperature);

    /**
     * @brief Parse integer from JSON
     * @param json JSON string
     * @param key Key name
     * @param value Output parameter
     * @return true if successful, false otherwise
     */
    static bool parseInt(const std::string& json, 
                         const std::string& key, 
                         long& value);

    /**
     * @brief Parse string from JSON
     * @param json JSON string
     * @param key Key name
     * @param value Output parameter
     * @return true if successful, false otherwise
     */
    static bool parseString(const std::string& json, 
                            const std::string& key, 
                            std::string& value);

    /**
     * @brief Generate status JSON
     * @param dhw_temp DHW temperature
     * @param dhw_target DHW target
     * @param heating_temp Heating temperature
     * @param heating_target Heating target
     * @param outdoor_temp Outdoor temperature
     * @param leaving_water_temp Leaving water temperature
     * @param mode Mode string
     * @param running_status Running status string
     * @param priority Priority string
     * @param status Status string
     * @param device_online Device online flag
     * @param ac_current AC current
     * @param dc_current DC current
     * @param ac_voltage AC voltage
     * @param dc_voltage DC voltage
     * @param ac_power AC power
     * @param working_mode Working mode
     * @return JSON string
     */
    static std::string generateStatusJson(
        double dhw_temp, double dhw_target,
        double heating_temp, double heating_target,
        double outdoor_temp, double leaving_water_temp,
        const std::string& mode, const std::string& running_status,
        const std::string& priority, const std::string& status,
        bool device_online,
        double ac_current, double dc_current,
        double ac_voltage, double dc_voltage,
        double ac_power, int working_mode);

    /**
     * @brief Generate success response
     * @param success Success flag
     * @param verified Verified flag
     * @param message Optional message
     * @return JSON string
     */
    static std::string generateSuccessResponse(
        bool success, bool verified = false,
        const std::string& message = "");

    /**
     * @brief Generate error response
     * @param error Error message
     * @param code HTTP status code
     * @return JSON string
     */
    static std::string generateErrorResponse(
        const std::string& error, int code = 400);

private:
    JsonHelpers() = delete;
};

} // namespace windmi

#endif // WINDMI_UTILS_JSON_HELPERS_HPP
