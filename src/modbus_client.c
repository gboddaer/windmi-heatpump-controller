/**
 * @file src/modbus_client.c
 * @brief Modbus TCP client implementation with cross-platform socket support
 */

#include "modbus_client.h"
#include "modbus/modbus_rtu_frame.h"
#include "crc16.h"
#include "utils/LogTags.hpp"
#include "utils/LoggerC.h"
#include "utils/PlatformC.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET windmi_socket_t;
#define WINDMI_INVALID_SOCKET INVALID_SOCKET
#define windmi_close_socket closesocket
#define windmi_recv(fd, buf, len, flags) recv((fd), (char*)(buf), (int)(len), (flags))
#define windmi_send(fd, buf, len, flags) send((fd), (const char*)(buf), (int)(len), (flags))
#define windmi_select(nfds, rfds, wfds, efds, tv) select(0, (rfds), (wfds), (efds), (tv))
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0
#endif
#else
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <time.h>
typedef int windmi_socket_t;
#define WINDMI_INVALID_SOCKET (-1)
#define windmi_close_socket close
#define windmi_recv(fd, buf, len, flags) recv((fd), (buf), (len), (flags))
#define windmi_send(fd, buf, len, flags) send((fd), (buf), (len), (flags))
#define windmi_select select
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define MODBUS_MAX_FRAME 256
#define MODBUS_RECV_TIMEOUT_MS 2000
#define MODBUS_WRITE_MAX_RETRIES 3
#define MODBUS_RETRY_DELAY_MS 100
#define MODBUS_READ_HEADER_LEN 3

struct modbus_client
{
  char ip[16];
  int port;
  uint8_t slave_id;
  windmi_socket_t socket_fd;
  bool connected;
#ifdef _WIN32
  bool ws_started;
#endif
};

#ifdef TEST_BUILD
#define STATIC_FOR_TEST
#else
#define STATIC_FOR_TEST static
#endif

#ifdef _WIN32
static void cleanup_winsock_if_started(modbus_client_t* client)
{
  if (client && client->ws_started)
  {
    WSACleanup();
    client->ws_started = false;
  }
}
#endif

static uint16_t bytes_to_uint16(const uint8_t* bytes)
{
  return (uint16_t)((bytes[0] << 8) | bytes[1]);
}

modbus_client_t* modbus_client_create(const char* ip, int port, uint8_t slave_id)
{
  modbus_client_t* client = (modbus_client_t*)malloc(sizeof(modbus_client_t));
  if (!client)
    return NULL;
  strncpy(client->ip, ip, sizeof(client->ip) - 1);
  client->ip[sizeof(client->ip) - 1] = '\0';
  client->port = port;
  client->slave_id = slave_id;
  client->socket_fd = WINDMI_INVALID_SOCKET;
  client->connected = false;
#ifdef _WIN32
  client->ws_started = false;
#endif
  return client;
}

void modbus_client_destroy(modbus_client_t* client)
{
  if (client)
  {
    if (client->connected)
      modbus_client_disconnect(client);
#ifdef _WIN32
    cleanup_winsock_if_started(client);
#endif
    free(client);
  }
}

bool modbus_client_connect(modbus_client_t* client)
{
  if (!client || client->connected)
    return false;

#ifdef _WIN32
  if (!client->ws_started)
  {
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
    {
      WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "WSAStartup failed");
      return false;
    }
    client->ws_started = true;
  }
#endif

  windmi_socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock == WINDMI_INVALID_SOCKET)
  {
#ifndef _WIN32
    WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "socket failed: %s", strerror(errno));
#else
    WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "socket failed: %d", WSAGetLastError());
    cleanup_winsock_if_started(client);
#endif
    return false;
  }

  struct sockaddr_in server_addr;
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(client->port);

  if (inet_pton(AF_INET, client->ip, &server_addr.sin_addr) <= 0)
  {
    WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Invalid address");
    windmi_close_socket(sock);
#ifdef _WIN32
    cleanup_winsock_if_started(client);
#endif
    return false;
  }

  if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
  {
#ifndef _WIN32
    WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "connect failed: %s", strerror(errno));
#else
    WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "connect failed: %d", WSAGetLastError());
#endif
    windmi_close_socket(sock);
#ifdef _WIN32
    cleanup_winsock_if_started(client);
#endif
    return false;
  }

  client->socket_fd = sock;
  client->connected = true;
  return true;
}

void modbus_client_disconnect(modbus_client_t* client)
{
  if (client && client->connected && client->socket_fd != WINDMI_INVALID_SOCKET)
  {
    windmi_close_socket(client->socket_fd);
    client->socket_fd = WINDMI_INVALID_SOCKET;
    client->connected = false;
#ifdef _WIN32
    cleanup_winsock_if_started(client);
#endif
  }
}

bool modbus_client_is_connected(modbus_client_t* client)
{
  return client && client->connected;
}

STATIC_FOR_TEST int flush_read_buffer(modbus_client_t* client)
{
  if (!client || client->socket_fd == WINDMI_INVALID_SOCKET)
    return 0;
  uint8_t dummy[128];
  fd_set fds;
  struct timeval tv = {0, 0};
  int flushed = 0;
  while (1)
  {
    FD_ZERO(&fds);
    FD_SET(client->socket_fd, &fds);
    int ready = windmi_select(client->socket_fd + 1, &fds, NULL, NULL, &tv);
    if (ready <= 0)
      break;
    int n = (int)windmi_recv(client->socket_fd, dummy, sizeof(dummy), MSG_DONTWAIT);
    if (n <= 0)
      break;
    flushed += n;
  }
  if (flushed > 0)
  {
    WINDMI_C_LOG(WINDMI_LOG_DEBUG, LOG_TAG_MODBUS, "Flushed %d bytes of stale data from socket",
                 flushed);
  }
  return flushed;
}

void modbus_client_flush_buffer(modbus_client_t* client)
{
  flush_read_buffer(client);
}

STATIC_FOR_TEST int send_frame(modbus_client_t* client, const uint8_t* frame, size_t len)
{
  flush_read_buffer(client);
  int sent = (int)windmi_send(client->socket_fd, frame, len, 0);
  return (sent == (int)len) ? 0 : -1;
}

STATIC_FOR_TEST int receive_exact(modbus_client_t* client, uint8_t* buffer, size_t expected_len)
{
  size_t total_received = 0;
  while (total_received < expected_len)
  {
    fd_set fds;
    struct timeval tv;
    FD_ZERO(&fds);
    FD_SET(client->socket_fd, &fds);
    tv.tv_sec = MODBUS_RECV_TIMEOUT_MS / 1000;
    tv.tv_usec = (MODBUS_RECV_TIMEOUT_MS % 1000) * 1000;
    int ready = windmi_select(client->socket_fd + 1, &fds, NULL, NULL, &tv);
    if (ready <= 0)
    {
      if (ready == 0)
      {
        WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Receive timeout (got %zu/%zu bytes)",
                     total_received, expected_len);
      }
      return -1;
    }
    int received = (int)windmi_recv(client->socket_fd, buffer + total_received,
                                    expected_len - total_received, 0);
    if (received <= 0)
      return -1;
    total_received += (size_t)received;
  }
  return 0;
}

STATIC_FOR_TEST int modbus_read_write_registers_internal(modbus_client_t* client, uint16_t address,
                                                         int16_t* values, uint16_t count,
                                                         bool single)
{
  if (!client || !client->connected || !values || count == 0)
    return -1;

  uint8_t frame[MODBUS_MAX_FRAME];
  uint16_t read_count = single ? 1 : count;
  modbus_rtu_build_read_frame(frame, client->slave_id, address, read_count);

  if (send_frame(client, frame, 8) != 0)
  {
    client->connected = false;
    return -1;
  }

  uint8_t header[MODBUS_READ_HEADER_LEN];
  if (receive_exact(client, header, MODBUS_READ_HEADER_LEN) != 0)
  {
    client->connected = false;
    return -1;
  }

  if (header[0] != client->slave_id)
    return -1;

  if (header[1] & 0x80)
  {
    uint8_t exc_crc[2];
    receive_exact(client, exc_crc, 2);
    WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Modbus exception: slave=%d func=0x%02X code=%d",
                 header[0], header[1], header[2]);
    return -1;
  }

  uint8_t byte_count = header[2];
  if (byte_count != read_count * 2)
  {
    WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Unexpected byte count: %d (expected %d)",
                 byte_count, read_count * 2);
    return -1;
  }

  uint8_t data_and_crc[MODBUS_MAX_FRAME];
  size_t remaining = byte_count + 2;
  if (receive_exact(client, data_and_crc, remaining) != 0)
  {
    client->connected = false;
    return -1;
  }

  uint8_t full_frame[MODBUS_MAX_FRAME];
  size_t total_len = MODBUS_READ_HEADER_LEN + remaining;
  full_frame[0] = header[0];
  full_frame[1] = header[1];
  full_frame[2] = header[2];
  memcpy(full_frame + 3, data_and_crc, remaining);

  uint16_t crc_received =
      (uint16_t)full_frame[total_len - 2] | ((uint16_t)full_frame[total_len - 1] << 8);
  uint16_t crc_calculated = crc16(full_frame, total_len - 2);
  if (crc_received != crc_calculated)
  {
    WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "CRC error in read response");
    return -1;
  }

  for (uint16_t i = 0; i < read_count; i++)
  {
    values[i] = (int16_t)bytes_to_uint16(&data_and_crc[i * 2]);
  }
  return 0;
}

int modbus_read_register(modbus_client_t* client, uint16_t address, int16_t* value)
{
  if (!value)
    return -1;
  return modbus_read_write_registers_internal(client, address, value, 1, true);
}

int modbus_write_register(modbus_client_t* client, uint16_t address, uint16_t value)
{
  if (!client || !client->connected)
    return -1;
  for (int retry = 0; retry < MODBUS_WRITE_MAX_RETRIES; retry++)
  {
    if (retry > 0)
    {
      WINDMI_C_LOG(WINDMI_LOG_DEBUG, LOG_TAG_MODBUS, "Write retry %d/%d for address 0x%04X", retry,
                   MODBUS_WRITE_MAX_RETRIES, address);
      windmi_sleep_ms(MODBUS_RETRY_DELAY_MS);
    }

    uint8_t frame[MODBUS_MAX_FRAME];
    modbus_rtu_build_write_frame(frame, client->slave_id, address, value);
    if (send_frame(client, frame, 8) != 0)
      continue;

    uint8_t response[8];
    if (receive_exact(client, response, 8) != 0)
      continue;

    if (response[1] & 0x80)
    {
      WINDMI_C_LOG(
          WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
          "Modbus exception on write: slave=%d func=0x%02X exception_code=%d (attempt %d/%d)",
          response[0], response[1] & 0x7F, response[2], retry + 1, MODBUS_WRITE_MAX_RETRIES);
      continue;
    }
    if (response[0] != client->slave_id || response[1] != 0x06)
    {
      WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
                   "Write response mismatch: slave=%d func=0x%02X (attempt %d/%d)", response[0],
                   response[1], retry + 1, MODBUS_WRITE_MAX_RETRIES);
      continue;
    }
    uint16_t crc_received = response[6] | (response[7] << 8);
    uint16_t crc_calculated = crc16(response, 6);
    if (crc_received != crc_calculated)
    {
      WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Modbus CRC error on write (attempt %d/%d)",
                   retry + 1, MODBUS_WRITE_MAX_RETRIES);
      continue;
    }
    int16_t read_value;
    if (modbus_read_register(client, address, &read_value) != 0)
      continue;
    if ((uint16_t)read_value != value)
    {
      WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS,
                   "Modbus write verify failed: wrote %u, read %d (attempt %d/%d)", value,
                   read_value, retry + 1, MODBUS_WRITE_MAX_RETRIES);
      continue;
    }
    if (retry > 0)
    {
      WINDMI_C_LOG(WINDMI_LOG_DEBUG, LOG_TAG_MODBUS, "Write succeeded after %d retry(ies)", retry);
    }
    return 0;
  }
  WINDMI_C_LOG(WINDMI_LOG_ERROR, LOG_TAG_MODBUS, "Write failed after %d attempts",
               MODBUS_WRITE_MAX_RETRIES);
  return -1;
}

int modbus_read_registers(modbus_client_t* client, uint16_t address, int16_t* values,
                          uint16_t count)
{
  if (!values || count == 0)
    return -1;
  return modbus_read_write_registers_internal(client, address, values, count, false);
}
