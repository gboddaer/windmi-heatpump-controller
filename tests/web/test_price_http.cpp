#include <gtest/gtest.h>

#include "web/WebServer.hpp"

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <memory>
#include <string>

using namespace windmi;

namespace {

/**
 * @brief HTTP surface for the daily estimate and the tariff setting.
 *
 * Uses the same raw-socket harness as the schedule tests: a real WebServer, a
 * real queue, and no control loop, so the response under test is the one the
 * browser would receive.
 */
class PriceHttpTest : public testing::Test
{
protected:
  CmdQueue commands;
  StatusQueue snapshots;
  int wakes = 0;
  std::unique_ptr<WebServer> server;

  void SetUp() override
  {
    server = std::make_unique<WebServer>(19441, "static", &commands, &snapshots, nullptr,
                                         [this] { ++wakes; });
  }

  std::string request(const std::string& path, const std::string& body = "",
                      const char* method = "POST")
  {
    struct Socket
    {
      int fd;
      ~Socket()
      {
        if (fd >= 0)
          close(fd);
      }
    } sock{socket(AF_INET, SOCK_STREAM, 0)};
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(19441);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (sock.fd < 0 || connect(sock.fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)))
      return {};
    const std::string message = std::string(method) + " " + path +
                                " HTTP/1.0\r\nHost: localhost\r\nContent-Length: " +
                                std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" +
                                body;
    if (send(sock.fd, message.data(), message.size(), 0) != static_cast<ssize_t>(message.size()))
      return {};
    std::string response;
    for (int i = 0; i < 500; ++i)
    {
      server->pollOnce(2);
      char buffer[8192];
      auto n = recv(sock.fd, buffer, sizeof(buffer), MSG_DONTWAIT);
      if (n > 0)
      {
        response.append(buffer, n);
      } else if (n == 0)
      {
        break;
      } else if (errno != EAGAIN && errno != EWOULDBLOCK)
      {
        break;
      }
    }
    return response;
  }

  static mg_str bodyOf(const std::string& response)
  {
    const auto split = response.find("\r\n\r\n");
    return split == std::string::npos
               ? mg_str_n(nullptr, 0)
               : mg_str_n(response.data() + split + 4, response.size() - split - 4);
  }
};

TEST_F(PriceHttpTest, StatusCarriesDailyEnergyAndPrice)
{
  StatusSnapshot status;
  status.device_online = true;
  status.daily_energy_kwh = 1.5;
  status.daily_energy_valid = true;
  status.price_cents_per_kwh = 27.5;
  snapshots.push(status);

  const auto response = request("/api/status", "", "GET");
  ASSERT_NE(std::string::npos, response.find("200 OK")) << response;

  const mg_str json = bodyOf(response);
  double energy = 0, price = 0;
  bool valid = false;
  EXPECT_TRUE(mg_json_get_num(json, "$.dailyEnergyKwh", &energy));
  EXPECT_TRUE(mg_json_get_bool(json, "$.dailyEnergyValid", &valid));
  EXPECT_TRUE(mg_json_get_num(json, "$.pricePerKwhCents", &price));
  EXPECT_DOUBLE_EQ(1.5, energy);
  EXPECT_TRUE(valid);
  EXPECT_DOUBLE_EQ(27.5, price);
}

TEST_F(PriceHttpTest, StatusReturnsTheTariffTheUserSaved)
{
  // A tariff can be typed with decimals; the snapshot must carry it back unchanged,
  // or the field visibly rewrites what the user just saved.
  StatusSnapshot status;
  status.device_online = true;
  status.daily_energy_kwh = 2.0;
  status.daily_energy_valid = true;
  status.price_cents_per_kwh = 30.25;
  snapshots.push(status);

  const auto response = request("/api/status", "", "GET");
  const mg_str json = bodyOf(response);
  double price = 0;
  ASSERT_TRUE(mg_json_get_num(json, "$.pricePerKwhCents", &price)) << response;
  EXPECT_DOUBLE_EQ(30.25, price);
}

TEST_F(PriceHttpTest, PriceSaveIsAcceptedAndQueued)
{
  const auto response = request("/api/price", R"({"priceCentsPerKwh":30})");
  EXPECT_NE(std::string::npos, response.find("202")) << response;

  Command queued{};
  ASSERT_TRUE(commands.pop(queued));
  EXPECT_EQ(CommandType::CMD_SET_PRICE, queued.type);
  EXPECT_FLOAT_EQ(30.0f, queued.float_val);
  EXPECT_EQ(1, wakes) << "the loop must be woken to apply it";
}

TEST_F(PriceHttpTest, RejectsEmptyBody)
{
  const auto response = request("/api/price", "");
  EXPECT_NE(std::string::npos, response.find("400")) << response;
  Command ignored{};
  EXPECT_FALSE(commands.pop(ignored));
}

TEST_F(PriceHttpTest, RejectsMissingField)
{
  const auto response = request("/api/price", "{}");
  EXPECT_NE(std::string::npos, response.find("400")) << response;
  Command ignored{};
  EXPECT_FALSE(commands.pop(ignored));
}

TEST_F(PriceHttpTest, RejectsOutOfRange)
{
  for (const char* body : {R"({"priceCentsPerKwh":501})", R"({"priceCentsPerKwh":-3})"})
  {
    const auto response = request("/api/price", body);
    EXPECT_NE(std::string::npos, response.find("422")) << body << " -> " << response;
  }
  Command ignored{};
  EXPECT_FALSE(commands.pop(ignored));
}

TEST_F(PriceHttpTest, RejectsGetOnPrice)
{
  const auto response = request("/api/price", "", "GET");
  EXPECT_NE(std::string::npos, response.find("405")) << response;
}

} // namespace
#endif
