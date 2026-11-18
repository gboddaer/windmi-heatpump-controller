#include <gtest/gtest.h>
#include "web/WebServer.hpp"
#if !defined(_WIN32)
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>

using namespace windmi;
namespace {
// Exercise the production Mongoose handler; do not test the unused JsonHelpers path.
std::string getStatus(WebServer& server, int port) {
    struct Socket { int fd; ~Socket() { if (fd >= 0) close(fd); } } socket{::socket(AF_INET, SOCK_STREAM, 0)};
    if (socket.fd < 0) return {};
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(socket.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) return {};
    const std::string request = "GET /api/status HTTP/1.0\r\nHost: localhost\r\nConnection: close\r\n\r\n";
    if (send(socket.fd, request.data(), request.size(), 0) != static_cast<ssize_t>(request.size())) return {};
    std::string response;
    for (int i = 0; i < 500; ++i) {
        server.pollOnce(2);
        char buffer[8192];
        const auto count = recv(socket.fd, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (count > 0) response.append(buffer, static_cast<size_t>(count));
        else if (count == 0) break;
        else if (errno != EAGAIN && errno != EWOULDBLOCK) break;
    }
    const auto split = response.find("\r\n\r\n");
    return split == std::string::npos ? std::string{} : response.substr(split + 4);
}
}
TEST(DiagnosticsHttpTest, ProductionHandlerPreservesNullZeroHighBitsAndAge) {
    CmdQueue commands;
    StatusQueue snapshots;
    StatusSnapshot status;
    status.device_online = true;
    status.diagnostics.observed_at_ms = 31000;
    status.diagnostics.readings[diagnosticIndex(0x1009)] = {0x8001, 1000};
    status.diagnostics.readings[diagnosticIndex(0x0055)] = {0, 31000};
    snapshots.push(status);
    WebServer server(19417, "static", &commands, &snapshots, nullptr);
    const auto json = getStatus(server, 19417);
    ASSERT_FALSE(json.empty());
    EXPECT_EQ(json.back(), '}'); // detect truncated responses
    SCOPED_TRACE(json);
    const auto body = mg_str_n(json.data(), json.size());
    EXPECT_EQ(mg_json_get_long(body, "$.diagnostics.registers.alarm1.raw", -1), 0x8001);
    EXPECT_EQ(mg_json_get_long(body, "$.diagnostics.registers.alarm1.ageSeconds", -1), 30);
    EXPECT_EQ(mg_json_get_long(body, "$.diagnostics.registers.pumpCommand.raw", -1), 0);
    EXPECT_EQ(mg_json_get_long(body, "$.diagnostics.registers.pumpCommand.ageSeconds", -1), 0);
    EXPECT_NE(json.find("\"raw\":null"), std::string::npos);
    EXPECT_NE(json.find("\"ageSeconds\":null"), std::string::npos);
    EXPECT_EQ(mg_json_get_long(body, "$.diagnostics.registers.alarm4.address", -1), 0x100C);
    EXPECT_EQ(mg_json_get_long(body, "$.diagnostics.registers.alarm1.maxAgeSeconds", -1), 180);
    Command command;
    EXPECT_FALSE(commands.pop(command));
}

TEST(DiagnosticsHttpTest, FullRegisterPayloadIsNotTruncatedAtLegacyBufferSize) {
    CmdQueue commands;
    StatusQueue snapshots;
    StatusSnapshot status;
    status.device_online = true;
    status.diagnostics.observed_at_ms = 86400000;
    for (auto& reading : status.diagnostics.readings) reading = {65535, 0};
    snapshots.push(status);
    WebServer server(19418, "static", &commands, &snapshots, nullptr);
    const auto json = getStatus(server, 19418);
    ASSERT_FALSE(json.empty());
    const auto body = mg_str_n(json.data(), json.size());
    EXPECT_EQ(mg_json_get_long(body, "$.diagnostics.registers.ipmReason.raw", -1), 65535);
    EXPECT_EQ(mg_json_get_long(body, "$.diagnostics.registers.ipmReason.ageSeconds", -1), 86400);
    EXPECT_EQ(json.back(), '}');
}
TEST(BackupHeaterHttpTest, ProductionHandlerLabelsValidationAndPreservesUnavailableData) {
    CmdQueue commands; StatusQueue snapshots; StatusSnapshot status;
    status.device_online = true;
    status.diagnostics.observed_at_ms = 4000;
    status.diagnostics.readings.at(diagnosticIndex(REG_BEFORE_BACKUP_HEATER_TEMP)) = {310,1100};
    status.diagnostics.readings.at(diagnosticIndex(REG_LEAVING_WATER_TEMP)) = {336,1000};
    status.diagnostics.readings.at(diagnosticIndex(REG_WATER_FLOW)) = {107,3000};
    snapshots.push(status);
    WebServer server(19419, "static", &commands, &snapshots, nullptr);
    auto json = getStatus(server, 19419);
    auto body = mg_str_n(json.data(),json.size());
    bool valid = false;
    EXPECT_TRUE(mg_json_get_bool(body,"$.backupHeater.measurementValid",&valid)); EXPECT_TRUE(valid);
    double watts = 0;
    EXPECT_TRUE(mg_json_get_num(body,"$.backupHeater.estimatedAddedHeatW",&watts));
    EXPECT_NEAR(watts,3234.8,.1);
    double flow = 0;
    EXPECT_TRUE(mg_json_get_num(body,"$.backupHeater.flowM3h",&flow));
    EXPECT_NEAR(flow,1.07,.0001);
    EXPECT_NE(json.find("\"status\":\"unvalidated\""),std::string::npos);
    EXPECT_EQ(mg_json_get_long(body,"$.diagnostics.registers.backupWarmup.maxAgeSeconds",-1),480);
    status.diagnostics.readings.at(diagnosticIndex(REG_BEFORE_BACKUP_HEATER_TEMP)).raw.reset();
    snapshots.push(status); json = getStatus(server,19419);
    EXPECT_NE(json.find("\"estimatedAddedHeatW\":null"),std::string::npos);
    EXPECT_NE(json.find("\"reason\":\"missing_temperature\""),std::string::npos);
    Command command; EXPECT_FALSE(commands.pop(command));
}
#endif
