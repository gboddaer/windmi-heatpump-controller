/**
 * @file utils/Config.hpp
 * @brief Configuration management
 */

#ifndef WINDMI_UTILS_CONFIG_HPP
#define WINDMI_UTILS_CONFIG_HPP

#include <string>
#include <map>
#include <stdexcept>

namespace windmi {

/**
 * @brief Configuration exception class
 */
class ConfigException : public std::runtime_error {
public:
    explicit ConfigException(const std::string& msg)
        : std::runtime_error(msg) {}
};

/**
 * @brief Configuration class
 * 
 * Manages application configuration settings.
 */
class Config {
public:
    /**
     * @brief Default constructor
     */
    Config();

    /**
     * @brief Load configuration from file
     * @param filename Path to config file
     */
    void loadFromFile(const std::string& filename);

    /**
     * @brief Get string value
     * @param key Configuration key
     * @param default_value Default value if not found
     * @return Configuration value
     */
    std::string getString(const std::string& key, 
                          const std::string& default_value = "") const;

    /**
     * @brief Get integer value
     * @param key Configuration key
     * @param default_value Default value if not found
     * @return Configuration value
     */
    int getInt(const std::string& key, int default_value = 0) const;

    /**
     * @brief Get floating point value
     * @param key Configuration key
     * @param default_value Default value if not found
     * @return Configuration value
     */
    double getDouble(const std::string& key, double default_value = 0.0) const;

    /**
     * @brief Get boolean value
     * @param key Configuration key
     * @param default_value Default value if not found
     * @return Configuration value
     */
    bool getBool(const std::string& key, bool default_value = false) const;

    /**
     * @brief Set configuration value
     * @param key Configuration key
     * @param value Configuration value (as string)
     */
    void set(const std::string& key, const std::string& value);

    /**
     * @brief Check if key exists
     * @param key Configuration key
     * @return true if key exists, false otherwise
     */
    bool hasKey(const std::string& key) const;

private:
    std::map<std::string, std::string> values_;
};

} // namespace windmi

#endif // WINDMI_UTILS_CONFIG_HPP
