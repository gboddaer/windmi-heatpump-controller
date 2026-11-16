#include <gtest/gtest.h>
#include "web/WebServer.hpp"
#include "utils/ScheduleConfig.hpp"
#if !defined(_WIN32)
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <memory>
using namespace windmi;
namespace {
const std::string dayJson = R"({"enabled":true,"ranges":[{"start":0,"end":1440,"mode":3,"heatingTemp":40,"dhwTemp":50}]})";
class ScheduleHttpTest : public testing::Test {
protected:
    Scheduler scheduler;
    CmdQueue commands;
    StatusQueue snapshots;
    int wakes = 0, saves = 0;
    bool diskWorks = true;
    std::unique_ptr<WebServer> server;
    void SetUp() override {
        ASSERT_TRUE(scheduler.replace(false, {{0,1440,3,40,50}}));
        scheduler.setPersistenceHandler([this](const ScheduleState&) { ++saves; return diskWorks; });
        server = std::make_unique<WebServer>(19440, "static", &commands, &snapshots, &scheduler, [this] { ++wakes; });
    }
    std::string request(const std::string& path, const std::string& body = "", const char* method = "POST") {
        struct Socket { int fd; ~Socket() { if (fd >= 0) close(fd); } } sock{socket(AF_INET, SOCK_STREAM, 0)};
        sockaddr_in address{}; address.sin_family = AF_INET; address.sin_port = htons(19440); address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (sock.fd < 0 || connect(sock.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address))) return {};
        const std::string message = std::string(method) + " " + path + " HTTP/1.0\r\nHost: localhost\r\nContent-Length: " +
            std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        if (send(sock.fd, message.data(), message.size(), 0) != static_cast<ssize_t>(message.size())) return {};
        std::string response;
        for (int i = 0; i < 500; ++i) {
            server->pollOnce(2); char buffer[8192]; auto n = recv(sock.fd, buffer, sizeof(buffer), MSG_DONTWAIT);
            if (n > 0) response.append(buffer, n); else if (n == 0) break;
            else if (errno != EAGAIN && errno != EWOULDBLOCK) break;
        }
        return response;
    }
    mg_str json(const std::string& response) {
        auto split = response.find("\r\n\r\n");
        return split == std::string::npos ? mg_str_n(nullptr,0) : mg_str_n(response.data()+split+4,response.size()-split-4);
    }
};
}
TEST_F(ScheduleHttpTest, ProductionStatusIncludesScheduleConfigurationAndPendingState) {
    StatusSnapshot status;
    status.schedule_enabled = true; status.schedule_paused = true;
    status.schedule_pending = true; status.schedule_persistence_error = true; status.schedule_revision = 123;
    status.schedule_instance_id = "test-instance";
    snapshots.push(status);
    const auto response = request("/api/status", "", "GET");
    bool enabled = false, pending = false, error = false;
    EXPECT_TRUE(mg_json_get_bool(json(response), "$.scheduleEnabled", &enabled)); EXPECT_TRUE(enabled);
    EXPECT_TRUE(mg_json_get_bool(json(response), "$.schedulePending", &pending)); EXPECT_TRUE(pending);
    EXPECT_TRUE(mg_json_get_bool(json(response), "$.schedulePersistenceError", &error)); EXPECT_TRUE(error);
    EXPECT_EQ(mg_json_get_long(json(response), "$.scheduleRevision", -1), 123);
    EXPECT_NE(response.find("\"scheduleInstanceId\":\"test-instance\""), std::string::npos);
}
TEST_F(ScheduleHttpTest, InvalidSaveCannotEnableOrPersistAnything) {
    auto before = scheduler.snapshot();
    auto response = request("/api/schedule", R"({"enabled":true,"ranges":[{"start":0,"end":0,"mode":3}]})");
    EXPECT_NE(response.find("422"), std::string::npos);
    EXPECT_FALSE(scheduler.isEnabled());
    EXPECT_EQ(scheduler.snapshot().revision, before.revision);
    EXPECT_EQ(saves, 0); EXPECT_EQ(wakes, 0);
}
TEST_F(ScheduleHttpTest, RejectsUnsafeTempsOverlapAndPartiallyMalformedArrays) {
    for (const auto& body : {
        R"({"enabled":true,"ranges":[{"start":0,"end":1440,"mode":3,"heatingTemp":999,"dhwTemp":-200}]})",
        R"({"enabled":true,"ranges":[{"start":0,"end":420,"mode":3},{"start":1380,"end":360,"mode":1}]})",
        R"({"enabled":true,"ranges":[{"start":0,"end":1440,"mode":3},{}]})"}) {
        auto response = request("/api/schedule", body);
        EXPECT_NE(response.find("422"), std::string::npos) << response;
    }
    EXPECT_FALSE(scheduler.isEnabled()); EXPECT_EQ(saves, 0); EXPECT_EQ(wakes, 0);
}
TEST_F(ScheduleHttpTest, ConfirmedSaveAndClearReturnAuthoritativeStateWithoutCommands) {
    auto response = request("/api/schedule", dayJson);
    ASSERT_NE(response.find("200 OK"), std::string::npos);
    bool enabled = false;
    EXPECT_TRUE(mg_json_get_bool(json(response), "$.enabled", &enabled)); EXPECT_TRUE(enabled);
    EXPECT_EQ(saves, 1); EXPECT_EQ(wakes, 1);
    EXPECT_NE(response.find("\"instanceId\":\"" + scheduler.snapshot().instanceId + "\""), std::string::npos);
    response = request("/api/schedule", R"({"enabled":false,"ranges":[]})");
    ASSERT_NE(response.find("200 OK"), std::string::npos);
    EXPECT_TRUE(scheduler.getRanges().empty()); EXPECT_FALSE(scheduler.isEnabled());
    EXPECT_TRUE(mg_json_get_bool(json(response), "$.enabled", &enabled)); EXPECT_FALSE(enabled);
    EXPECT_EQ(saves, 2); EXPECT_EQ(wakes, 2);
    Command command; EXPECT_FALSE(commands.pop(command));
}
TEST_F(ScheduleHttpTest, StorageFailureReturns500AndLeavesOldConfiguration) {
    auto before = scheduler.snapshot(); diskWorks = false;
    auto response = request("/api/schedule", dayJson);
    EXPECT_NE(response.find("500"), std::string::npos) << response;
    EXPECT_EQ(scheduler.snapshot().revision, before.revision);
    EXPECT_FALSE(scheduler.isEnabled()); EXPECT_EQ(saves, 1); EXPECT_EQ(wakes, 0);
}
TEST_F(ScheduleHttpTest, EditsAndEnablePreservePauseUntilExplicitResume) {
    scheduler.setEnabled(true); scheduler.pause(); saves = 0;
    auto response = request("/api/schedule", dayJson);
    ASSERT_TRUE(scheduler.isPaused());
    bool paused = false;
    EXPECT_TRUE(mg_json_get_bool(json(response), "$.paused", &paused)); EXPECT_TRUE(paused);
    request("/api/schedule/toggle", R"({"enabled":false})");
    request("/api/schedule/toggle", R"({"enabled":true})");
    EXPECT_TRUE(scheduler.isPaused());
    response = request("/api/schedule/resume", "{}");
    EXPECT_NE(response.find("200 OK"), std::string::npos);
    EXPECT_FALSE(scheduler.isPaused());
    EXPECT_EQ(saves, 4); EXPECT_EQ(wakes, 4);
}
TEST_F(ScheduleHttpTest, CannotEnableEmptyScheduleAndFailedResumeStaysPaused) {
    scheduler.replace(false, {});
    auto response = request("/api/schedule/toggle", R"({"enabled":true})");
    EXPECT_NE(response.find("422"), std::string::npos);
    scheduler.replace(true, {{0,1440,3,40,50}}); scheduler.pause(); diskWorks = false;
    response = request("/api/schedule/resume", "{}");
    EXPECT_NE(response.find("500"), std::string::npos);
    EXPECT_TRUE(scheduler.isPaused());
}
#endif
