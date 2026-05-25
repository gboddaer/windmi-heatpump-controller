#pragma once
/*
 * Rotenso WindmiLL 8 kW heat pump - Modbus RTU register map
 *
 * Source: user-provided manual photos, section 16.5 "Modbus table".
 *
 * Defaults shown in the manual:
 *   Baud rate      : 9600 baud, configurable
 *   Modbus address : 11, configurable
 *   Modbus type    : RTU
 *   Frame type     : configurable, default appears to be N,8,1
 *
 * Register notation:
 *   The manual lists "GCHV Address" in hexadecimal, e.g. 002CH.
 *   These constants use the numeric register address exactly as listed.
 *
 * Function codes:
 *   Read:  0x03 / 0x04
 *   Write: 0x06 / 0x10 for writable registers
 *
 * Many values use scaling:
 *   temperature: value = degC * 10
 *   frequency  : value = Hz * 10
 *   RPM        : value = rpm / 10 in the register, so rpm = value * 10
 *   flow       : value = m3/h * 100
 */

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace rotenso::windmill8kw::modbus {

constexpr std::uint8_t  kDefaultSlaveId = 11;
constexpr std::uint32_t kDefaultBaudRate = 9600;

enum class Access : std::uint8_t {
    ReadOnly,
    ReadWrite,
};

enum class Scale : std::uint8_t {
    Raw,
    TemperatureTimes10,   // °C = raw / 10
    FrequencyTimes10,     // Hz = raw / 10
    TimeHHMM,             // raw = hour * 256 + minute
    Bitfield,
    RpmDiv10,             // rpm = raw * 10
    OpeningDegreeDiv4,    // displayed = raw / 4
    FlowTimes100,         // m3/h = raw / 100
    ValueDiv100,
};

struct RegisterInfo {
    std::uint16_t address;
    std::string_view name;
    Access access;
    Scale scale;
    std::optional<std::int32_t> min;
    std::optional<std::int32_t> max;
    std::optional<std::int32_t> defaultValue;
};

enum class Register : std::uint16_t {
    SettingMode                         = 0x002C,
    RunningMode                         = 0x002D,
    UserInterfaceType                   = 0x0209,
    OccupancyMode                       = 0x0029,
    NormalEcoSwitchStatus               = 0x0067,

    OutdoorAirTemperature               = 0x0001,
    IndoorAirTemperature                = 0x0002,
    EnteringWaterTemperatureTwIn        = 0x0003,
    LeavingWaterTemperatureT1           = 0x0004,
    RefrigerantTemperatureT2B           = 0x0005,
    DischargeTemperature                = 0x000A,
    AirExchangerTemperatureT3           = 0x000B,

    ActualCompressorFrequency           = 0x0017,
    FrequencyReductionNightMode         = 0x0044,

    CompressorRuntimeHours              = 0x0174,
    PumpRuntimeHours                    = 0x0176,

    OccupiedHeatingAirSetpoint          = 0x01A5,
    UnoccupiedHeatingAirSetpointOffset  = 0x01A6,
    EconomicHeatingAirSetpointOffset    = 0x01A7,
    OccupiedCoolingAirSetpoint          = 0x01A8,
    UnoccupiedCoolingAirSetpointOffset  = 0x01A9,
    EconomicCoolingAirSetpointOffset    = 0x01AA,

    HeatingClimaticCurveSelection       = 0x0245,
    HeatingCurveMinOat                  = 0x0246,
    HeatingCurveMaxOat                  = 0x0247,
    HeatingCurveMinLwt                  = 0x0248,
    HeatingCurveMaxLwt                  = 0x0249,

    CoolingClimaticCurveSelection       = 0x024A,
    CoolingCurveMinOat                  = 0x024B,
    CoolingCurveMaxOat                  = 0x024C,
    CoolingCurveMinLwt                  = 0x024D,
    CoolingCurveMaxLwt                  = 0x024E,

    HeatingClimaticCurveMaxSetpointOffset = 0x019C,
    CoolingClimaticCurveMinSetpointOffset = 0x019D,

    OccupiedHeatingWaterSetpoint        = 0x0191,
    UnoccupiedHeatingWaterSetpointOffset= 0x0192,
    EconomicHeatingWaterSetpointOffset  = 0x0193,
    OccupiedCoolingWaterSetpoint        = 0x0197,
    UnoccupiedCoolingWaterSetpointOffset= 0x0198,
    EconomicCoolingWaterSetpointOffset  = 0x0199,

    PumpSpeed                           = 0x0055,
    WaterControlPoint                   = 0x0033,
    NightModeStartTime                  = 0x0206,
    NightModeEndTime                    = 0x0207,
    BackupType                          = 0x0259,

    WarmupTimeMinutes                   = 0x025A,
    BoosterDeltaTemperature             = 0x025B,
    BoosterOatThreshold                 = 0x025C,
    MinimumOatForHeatingWithComp        = 0x0202,
    DhwTempNormalSetpoint               = 0x0194,
    DhwTempEconSetpoint                 = 0x0196,
    DhwPriority                         = 0x028F,
    DhwScheduledDays                    = 0x02C7,
    DhwScheduledStartingTime            = 0x02C8,
    DhwScheduledStoppingTime            = 0x02C9,
    DhwMode                             = 0x02C9, // manual photo appears to show 02C9H; verify in full manual
    DhwTankTemperature                  = 0x1C5B,
    DhwAntiLegionellaSetpoint           = 0x0195,
    AntiLegionellaScheduledDays         = 0x02CA,
    AntiLegionellaScheduledStartingTime = 0x02CB,

    DiscreteInput5Type                  = 0x01F6,
    DiscreteInput6Type                  = 0x01F7,
    DiscreteInput7Type                  = 0x01F8,
    DiscreteInput8Type                  = 0x01F9,
    DiscreteOutput5Type                 = 0x01FA,
    DiscreteOutput8Type                 = 0x01FB,
    DiscreteOutput9Type                 = 0x01FC,

    FlowSwitchStatus                    = 0x0050,
    DiscreteInput5Status                = 0x006A,
    DiscreteInput6Status                = 0x006B,
    DiscreteInput7Status                = 0x006C,
    DiscreteInput8Status                = 0x006D,

    DiscreteOutput5Force                = 0x0140,
    DiscreteOutput8Force                = 0x0151,
    DiscreteOutput9Force                = 0x0152,

    DhwValveStatus                      = 0x00D2,
    IduSideCapacityDemand               = 0x1001,
    CapacityDemandAfterOduReceive       = 0x1002,
    ActualCapacityOutput                = 0x1004,
    FanSpeed                            = 0x1005,
    LwtAfterBpheInsideUnitTwOut         = 0x1008,
    ExvOpeningDegree                    = 0x1012,
    IpmRefrigerantCoolPipeTemperature   = 0x1013,
    AcCurrent                           = 0x1014,
    DcCurrent                           = 0x1015,
    AcVoltage                           = 0x1016,
    DcVoltage                           = 0x1017,

    CompressorFrequencyLimitationReason1 = 0x1019,
    CompressorFrequencyLimitationReason2 = 0x1025,
    ProgramVersion                       = 0x1020,
    EepromVersion                        = 0x1021,
    P6ErrorReasonIpmProtection           = 0x1022,
    T9IpmTemperature                     = 0x1023,
    T30DefrostLogicTemperature           = 0x1024,
    TargetDischargeTemperature           = 0x1026,
    OduPcbCrch                           = 0x1027,
    OduPcbCrcl                           = 0x1028,
    IduPcbCrch                           = 0x1029,
    IduPcbCrcl                           = 0x1030,
    ModbusBaudrate                       = 0x1031,
    ModbusParityCheck                    = 0x1032,
    ModbusId                             = 0x1033,

    AlarmBitmap1                         = 0x1009,
    AlarmBitmap2                         = 0x100A,
    AlarmBitmap3                         = 0x100B,
    AlarmBitmap4                         = 0x100C,

    CapacityOfUnit                       = 0x1006,
    AmbientOrWaterTempControl            = 0x100D,
    OduOutputStatus                      = 0x100E,
    RequiredCompressorFrequency          = 0x100F,
    RequiredFanSpeedUpperMotor           = 0x101A,
    RequiredFanSpeedDownMotor            = 0x101B,
    RequiredOpeningDegreeOfExv           = 0x101C,
    ActualFanSpeedUpperMotor             = 0x101D,
    ActualFanSpeedDownMotor              = 0x101E,
    OutdoorUnitInputStatus               = 0x101F,
    WaterFlowFeedbackFromWaterPump       = 0x102A,
    WaterDeltaTSetpoint                  = 0x0239,
};

enum class SettingMode : std::uint16_t {
    Off          = 0,
    CoolDhw      = 1,
    HeatDhw      = 2,
    CoolOnly     = 3,
    HeatOnly     = 4,
    DhwOnly      = 5,
};

enum class RunningMode : std::uint16_t {
    Off                    = 0,
    Cool                   = 1,
    Heat                   = 2,
    Dhw                    = 4,
    Defrost                = 7,
    HomeAntiFreezeLogic    = 20,
};

enum class OccupancyMode : std::uint16_t {
    Away  = 0,
    Sleep = 1,
    Home  = 2,
};

enum class BackupType : std::uint16_t {
    MainWaterLoopEhs_DhwEhs_Boiler = 0,
    MainWaterLoopEhs_DhwEhs        = 1,
    DhwEhs_Boiler                  = 2,
    MainWaterLoopEhs_Boiler        = 3,
    DhwEhsOnly                     = 4,
    BoilerOnly                     = 5,
    MainWaterLoopEhsOnly           = 6,
    NoBackup                       = 7,
};

enum AlarmBitmap1 : std::uint16_t {
    Alarm1_WaterFlowSwitchFail              = 1u << 0,
    Alarm1_CommFailODUHydraulicPCB          = 1u << 1,
    Alarm1_LwtSensorAfterEhFail             = 1u << 2,
    Alarm1_RefrigerantSensorBpheOutletFail  = 1u << 3,
    Alarm1_RefrigerantSensorBpheInletFail   = 1u << 4,
    Alarm1_OduFail                          = 1u << 5,
    Alarm1_DhwTankSensorFail                = 1u << 6,
    Alarm1_EwtOfBpheFail                    = 1u << 7,
    Alarm1_LwtOfBpheFail                    = 1u << 8,
    Alarm1_CommFailWiredControllerPCB       = 1u << 9,
    Alarm1_BiZoneSensorFail                 = 1u << 10,
    Alarm1_AuxiliaryHeaterLwtSensorFail     = 1u << 11,
};

enum AlarmBitmap2 : std::uint16_t {
    Alarm2_Reserved1                         = 1u << 0,
    Alarm2_TempDifferenceEwtLwtTooHuge       = 1u << 1,
    Alarm2_WaterFlowRateShortage             = 1u << 2,
    Alarm2_TempDifferenceEwtLwtAbnormal      = 1u << 3,
    Alarm2_Reserved5                         = 1u << 4,
    Alarm2_Reserved6                         = 1u << 5,
    Alarm2_EHFeedbackProtect                 = 1u << 6,
};

enum AlarmBitmap3 : std::uint16_t {
    Alarm3_CondenserSensorFail               = 1u << 0,
    Alarm3_DischargeTempSensorFail           = 1u << 1,
    Alarm3_Reserved3                         = 1u << 2,
    Alarm3_BpheOutletHighTempProtection      = 1u << 3,
    Alarm3_P6Error3TimesIn30min              = 1u << 4,
    Alarm3_AcVoltageAbnormal                 = 1u << 5,
    Alarm3_OatSensorFail                     = 1u << 6,
    Alarm3_OverCurrentProtection             = 1u << 7,
    Alarm3_IpmProtectionP6                   = 1u << 8,
    Alarm3_HighDischargeTemp3TimesH6         = 1u << 9,
    Alarm3_IpmHighTemp3TimesH12              = 1u << 10,
    Alarm3_EepromAlarmE10                    = 1u << 11,
    Alarm3_HighPressureProtectionP1          = 1u << 12,
    Alarm3_LowPressureProtectionH5           = 1u << 13,
    Alarm3_DcFanMotorAlarmH9                 = 1u << 14,
    Alarm3_CondenserTempTooHighP5            = 1u << 15,
};

enum AlarmBitmap4 : std::uint16_t {
    Alarm4_CommFailIDUODU_E2                 = 1u << 0,
    Alarm4_OduFanMotorErrorP9                = 1u << 1,
    Alarm4_IpmTempTooHighPb                  = 1u << 2,
    Alarm4_IdQtyDecreaseH7                   = 1u << 3,
    Alarm4_OverCurrent3TimesH10              = 1u << 4,
    Alarm4_DischargeSensorFailP4             = 1u << 5,
    Alarm4_RefrigerantCoolPipeSensorFailEc   = 1u << 6,
    Alarm4_LowPressureProtectionP2           = 1u << 7,
};

constexpr std::array<RegisterInfo, 99> kRegisterMap{{
    {0x002C, "Setting mode", Access::ReadWrite, Scale::Raw, 0, 2, std::nullopt},
    {0x002D, "Running mode", Access::ReadOnly, Scale::Raw, 0, 20, std::nullopt},
    {0x0209, "User interface type", Access::ReadWrite, Scale::Raw, 0, 0, std::nullopt},
    {0x0029, "Occupancy mode", Access::ReadWrite, Scale::Raw, 0, 2, std::nullopt},
    {0x0067, "Normal/Eco switch status", Access::ReadOnly, Scale::Raw, 0, 1, std::nullopt},

    {0x0001, "Outdoor air temperature", Access::ReadOnly, Scale::TemperatureTimes10, std::nullopt, std::nullopt, std::nullopt},
    {0x0002, "Indoor air temperature", Access::ReadOnly, Scale::TemperatureTimes10, std::nullopt, std::nullopt, std::nullopt},
    {0x0003, "Entering water temperature Tw-in", Access::ReadOnly, Scale::TemperatureTimes10, std::nullopt, std::nullopt, std::nullopt},
    {0x0004, "Leaving water temperature T1", Access::ReadOnly, Scale::TemperatureTimes10, std::nullopt, std::nullopt, std::nullopt},
    {0x0005, "Refrigerant temperature T2B", Access::ReadOnly, Scale::TemperatureTimes10, std::nullopt, std::nullopt, std::nullopt},
    {0x000A, "Discharge temperature", Access::ReadOnly, Scale::TemperatureTimes10, std::nullopt, std::nullopt, std::nullopt},
    {0x000B, "Air exchanger temperature T3", Access::ReadOnly, Scale::TemperatureTimes10, std::nullopt, std::nullopt, std::nullopt},

    {0x0017, "Actual compressor frequency", Access::ReadOnly, Scale::FrequencyTimes10, std::nullopt, std::nullopt, std::nullopt},
    {0x0044, "Frequency reduction mode - night mode", Access::ReadOnly, Scale::Raw, 0, 1, 0},

    {0x0174, "Compressor runtime", Access::ReadOnly, Scale::Raw, 0, 65535, std::nullopt},
    {0x0176, "Pump runtime", Access::ReadOnly, Scale::Raw, 0, 65535, std::nullopt},

    {0x01A5, "Occupied heating air setpoint", Access::ReadWrite, Scale::TemperatureTimes10, 160, 320, 250},
    {0x01A6, "Unoccupied heating air setpoint offset", Access::ReadWrite, Scale::TemperatureTimes10, -200, 0, -40},
    {0x01A7, "Economic heating air setpoint offset", Access::ReadWrite, Scale::TemperatureTimes10, -200, 0, -20},
    {0x01A8, "Occupied cooling air setpoint", Access::ReadWrite, Scale::TemperatureTimes10, 160, 320, 250},
    {0x01A9, "Unoccupied cooling air setpoint offset", Access::ReadWrite, Scale::TemperatureTimes10, 0, 100, 40},
    {0x01AA, "Economic cooling air setpoint offset", Access::ReadWrite, Scale::TemperatureTimes10, 0, 100, 20},

    {0x0245, "Heating climatic curve selection", Access::ReadWrite, Scale::Raw, -1, 12, -1},
    {0x0246, "Customized heating curve min OAT", Access::ReadWrite, Scale::TemperatureTimes10, -300, 100, std::nullopt},
    {0x0247, "Customized heating curve max OAT", Access::ReadWrite, Scale::TemperatureTimes10, 100, 300, std::nullopt},
    {0x0248, "Customized heating curve min LWT", Access::ReadWrite, Scale::TemperatureTimes10, 250, 400, std::nullopt},
    {0x0249, "Customized heating curve max LWT", Access::ReadWrite, Scale::TemperatureTimes10, 300, 600, std::nullopt},

    {0x024A, "Cooling climatic curve selection", Access::ReadWrite, Scale::Raw, -1, 2, -1},
    {0x024B, "Customized cooling curve min OAT", Access::ReadWrite, Scale::TemperatureTimes10, 0, 300, std::nullopt},
    {0x024C, "Customized cooling curve max OAT", Access::ReadWrite, Scale::TemperatureTimes10, 240, 500, std::nullopt},
    {0x024D, "Customized cooling curve min LWT", Access::ReadWrite, Scale::TemperatureTimes10, 50, 200, std::nullopt},
    {0x024E, "Customized cooling curve max LWT", Access::ReadWrite, Scale::TemperatureTimes10, 50, 200, std::nullopt},

    {0x019C, "Heating climatic curve max setpoint offset", Access::ReadWrite, Scale::TemperatureTimes10, -50, 50, 0},
    {0x019D, "Cooling climatic curve min setpoint offset", Access::ReadWrite, Scale::TemperatureTimes10, -50, 50, 0},

    {0x0191, "Occupied heating water setpoint", Access::ReadWrite, Scale::TemperatureTimes10, 250, 630, std::nullopt},
    {0x0192, "Unoccupied heating water setpoint offset", Access::ReadWrite, Scale::TemperatureTimes10, -200, 0, -40},
    {0x0193, "Economic heating water setpoint offset", Access::ReadWrite, Scale::TemperatureTimes10, -200, 0, -20},
    {0x0197, "Occupied cooling water setpoint", Access::ReadWrite, Scale::TemperatureTimes10, 50, 250, std::nullopt},
    {0x0198, "Unoccupied cooling water setpoint offset", Access::ReadWrite, Scale::TemperatureTimes10, 0, 100, 40},
    {0x0199, "Economic cooling water setpoint offset", Access::ReadWrite, Scale::TemperatureTimes10, 0, 100, 20},

    {0x0055, "Pump speed", Access::ReadOnly, Scale::Raw, 0, 100, std::nullopt},
    {0x0033, "Water control point", Access::ReadWrite, Scale::TemperatureTimes10, 50, 630, std::nullopt},
    {0x0206, "Night mode start time", Access::ReadWrite, Scale::TimeHHMM, 0, 2359, 0},
    {0x0207, "Night mode end time", Access::ReadWrite, Scale::TimeHHMM, 0, 2359, 0},
    {0x0259, "Backup type", Access::ReadWrite, Scale::Raw, 0, 7, std::nullopt},

    {0x025A, "Warmup time", Access::ReadWrite, Scale::Raw, 0, 60, std::nullopt},
    {0x025B, "Booster delta temperature", Access::ReadWrite, Scale::TemperatureTimes10, 10, 200, std::nullopt},
    {0x025C, "Booster OAT threshold", Access::ReadWrite, Scale::TemperatureTimes10, -200, 150, std::nullopt},
    {0x0202, "Minimum OAT for heating with compressor", Access::ReadWrite, Scale::TemperatureTimes10, -250, 100, std::nullopt},
    {0x0194, "DHW temp normal setpoint", Access::ReadWrite, Scale::TemperatureTimes10, 400, 630, std::nullopt},
    {0x0196, "DHW temp econ setpoint", Access::ReadWrite, Scale::TemperatureTimes10, 400, 630, std::nullopt},
    {0x028F, "DHW priority", Access::ReadWrite, Scale::Raw, 0, 1, std::nullopt},
    {0x02C7, "DHW scheduled days", Access::ReadWrite, Scale::Bitfield, 0, 0b11111110, std::nullopt},
    {0x02C8, "DHW scheduled starting time", Access::ReadWrite, Scale::TimeHHMM, 0, 2359, 0},
    {0x02C9, "DHW scheduled stopping time", Access::ReadWrite, Scale::TimeHHMM, 0, 2359, 0},
    {0x02C9, "DHW mode - verify address", Access::ReadOnly, Scale::Raw, std::nullopt, std::nullopt, std::nullopt},
    {0x1C5B, "DHW tank temperature", Access::ReadOnly, Scale::TemperatureTimes10, std::nullopt, std::nullopt, std::nullopt},
    {0x0195, "DHW anti-legionella setpoint", Access::ReadWrite, Scale::TemperatureTimes10, 600, 700, 600},
    {0x02CA, "Anti-legionella scheduled days", Access::ReadWrite, Scale::Bitfield, 0, 0b11111110, std::nullopt},
    {0x02CB, "Anti-legionella scheduled starting time", Access::ReadWrite, Scale::TimeHHMM, 0, 2359, 0},

    {0x0050, "Flow switch status", Access::ReadOnly, Scale::Raw, 0, 1, std::nullopt},
    {0x006A, "Discrete input #5 status", Access::ReadOnly, Scale::Raw, 0, 1, std::nullopt},
    {0x006B, "Discrete input #6 status", Access::ReadOnly, Scale::Raw, 0, 1, std::nullopt},
    {0x006C, "Discrete input #7 status", Access::ReadOnly, Scale::Raw, 0, 1, std::nullopt},
    {0x006D, "Discrete input #8 status", Access::ReadOnly, Scale::Raw, 0, 1, std::nullopt},
    {0x00D2, "DHW valve status", Access::ReadOnly, Scale::Raw, 0, 1, std::nullopt},

    {0x1001, "IDU side capacity demand", Access::ReadOnly, Scale::Raw, std::nullopt, std::nullopt, std::nullopt},
    {0x1002, "Capacity demand after ODU receive", Access::ReadOnly, Scale::Raw, std::nullopt, std::nullopt, std::nullopt},
    {0x1004, "Actual capacity output", Access::ReadOnly, Scale::Raw, std::nullopt, std::nullopt, std::nullopt},
    {0x1005, "Fan speed", Access::ReadOnly, Scale::Raw, 0, 8, std::nullopt},
    {0x1008, "LWT after BPHE inside unit Tw-out", Access::ReadOnly, Scale::TemperatureTimes10, std::nullopt, std::nullopt, std::nullopt},
    {0x1012, "EXV opening degree", Access::ReadOnly, Scale::OpeningDegreeDiv4, std::nullopt, std::nullopt, std::nullopt},
    {0x1013, "IPM refrigerant cool pipe temperature", Access::ReadOnly, Scale::TemperatureTimes10, std::nullopt, std::nullopt, std::nullopt},
    {0x1014, "AC current", Access::ReadOnly, Scale::Raw, std::nullopt, std::nullopt, std::nullopt},
    {0x1015, "DC current", Access::ReadOnly, Scale::Raw, std::nullopt, std::nullopt, std::nullopt},
    {0x1016, "AC voltage", Access::ReadOnly, Scale::Raw, std::nullopt, std::nullopt, std::nullopt},
    {0x1017, "DC voltage", Access::ReadOnly, Scale::Raw, std::nullopt, std::nullopt, std::nullopt},

    {0x1019, "Compressor frequency limitation reason 1", Access::ReadOnly, Scale::Raw, std::nullopt, std::nullopt, std::nullopt},
    {0x1025, "Compressor frequency limitation reason 2", Access::ReadOnly, Scale::Raw, 0, 1, std::nullopt},
    {0x1020, "Program version", Access::ReadOnly, Scale::Raw, std::nullopt, std::nullopt, std::nullopt},
    {0x1021, "EEPROM version", Access::ReadOnly, Scale::Raw, std::nullopt, std::nullopt, std::nullopt},
    {0x1022, "P6 error reason / IPM protection reason", Access::ReadOnly, Scale::Raw, std::nullopt, std::nullopt, std::nullopt},
    {0x1023, "T9 IPM temperature", Access::ReadOnly, Scale::TemperatureTimes10, std::nullopt, std::nullopt, std::nullopt},
    {0x1024, "T30 defrost logic temperature", Access::ReadOnly, Scale::TemperatureTimes10, std::nullopt, std::nullopt, std::nullopt},
    {0x1026, "Target discharge temperature", Access::ReadOnly, Scale::TemperatureTimes10, std::nullopt, std::nullopt, std::nullopt},

    {0x1031, "Modbus baudrate", Access::ReadOnly, Scale::ValueDiv100, 9600, 38400, 9600},
    {0x1032, "Modbus parity check", Access::ReadOnly, Scale::Raw, 0, 2, std::nullopt},
    {0x1033, "Modbus ID", Access::ReadOnly, Scale::Raw, 1, 255, 11},

    {0x1009, "Alarm bitmap #1", Access::ReadOnly, Scale::Bitfield, std::nullopt, std::nullopt, std::nullopt},
    {0x100A, "Alarm bitmap #2", Access::ReadOnly, Scale::Bitfield, std::nullopt, std::nullopt, std::nullopt},
    {0x100B, "Alarm bitmap #3", Access::ReadOnly, Scale::Bitfield, std::nullopt, std::nullopt, std::nullopt},
    {0x100C, "Alarm bitmap #4", Access::ReadOnly, Scale::Bitfield, std::nullopt, std::nullopt, std::nullopt},

    {0x1006, "Capacity of unit", Access::ReadOnly, Scale::Raw, std::nullopt, std::nullopt, std::nullopt},
    {0x100D, "Ambient/water temperature control", Access::ReadWrite, Scale::Raw, std::nullopt, std::nullopt, std::nullopt},
    {0x100E, "ODU output status", Access::ReadOnly, Scale::Bitfield, std::nullopt, std::nullopt, std::nullopt},
    {0x100F, "Required compressor frequency", Access::ReadOnly, Scale::FrequencyTimes10, std::nullopt, std::nullopt, std::nullopt},
    {0x101A, "Required fan speed upper motor", Access::ReadOnly, Scale::RpmDiv10, std::nullopt, std::nullopt, std::nullopt},
    {0x101B, "Required fan speed down motor", Access::ReadOnly, Scale::RpmDiv10, std::nullopt, std::nullopt, std::nullopt},
    {0x101C, "Required opening degree of EXV", Access::ReadOnly, Scale::OpeningDegreeDiv4, std::nullopt, std::nullopt, std::nullopt},
    {0x101D, "Actual fan speed upper motor", Access::ReadOnly, Scale::RpmDiv10, std::nullopt, std::nullopt, std::nullopt},
    {0x101E, "Actual fan speed down motor", Access::ReadOnly, Scale::RpmDiv10, std::nullopt, std::nullopt, std::nullopt},
    {0x101F, "Outdoor unit input status", Access::ReadOnly, Scale::Bitfield, std::nullopt, std::nullopt, std::nullopt},
    {0x102A, "Water flow feedback from water pump", Access::ReadOnly, Scale::FlowTimes100, std::nullopt, std::nullopt, std::nullopt},
    {0x0239, "Water Delta T setpoint", Access::ReadWrite, Scale::Raw, 35, std::nullopt, 50},
}};

constexpr std::uint16_t address(Register reg) noexcept {
    return static_cast<std::uint16_t>(reg);
}

constexpr std::int16_t encodeTemperature(float degC) noexcept {
    return static_cast<std::int16_t>(degC * 10.0f);
}

constexpr float decodeTemperature(std::int16_t raw) noexcept {
    return static_cast<float>(raw) / 10.0f;
}

constexpr std::uint16_t encodeFrequency(float hz) noexcept {
    return static_cast<std::uint16_t>(hz * 10.0f);
}

constexpr float decodeFrequency(std::uint16_t raw) noexcept {
    return static_cast<float>(raw) / 10.0f;
}

constexpr std::uint16_t encodeTimeHHMM(std::uint8_t hour, std::uint8_t minute) noexcept {
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(hour) << 8) | minute);
}

constexpr std::uint8_t decodeHour(std::uint16_t raw) noexcept {
    return static_cast<std::uint8_t>((raw >> 8) & 0xFFu);
}

constexpr std::uint8_t decodeMinute(std::uint16_t raw) noexcept {
    return static_cast<std::uint8_t>(raw & 0xFFu);
}

constexpr float decodeFlowM3h(std::uint16_t raw) noexcept {
    return static_cast<float>(raw) / 100.0f;
}

constexpr std::uint16_t encodeFlowM3h(float m3h) noexcept {
    return static_cast<std::uint16_t>(m3h * 100.0f);
}

constexpr std::uint16_t functionReadHoldingRegister = 0x03;
constexpr std::uint16_t functionReadInputRegister   = 0x04;
constexpr std::uint16_t functionWriteSingleRegister = 0x06;
constexpr std::uint16_t functionWriteMultipleRegs   = 0x10;

inline constexpr bool hasFlag(std::uint16_t bitmap, std::uint16_t flag) noexcept {
    return (bitmap & flag) != 0;
}

} // namespace rotenso::windmill8kw::modbus
