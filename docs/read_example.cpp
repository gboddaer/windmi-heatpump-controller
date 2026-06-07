// gcc read_rotenso_mode.c -o read_rotenso_mode
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

static uint16_t modbus_crc16(const uint8_t* buf, int len)
{
  uint16_t crc = 0xFFFF;

  for (int pos = 0; pos < len; pos++)
  {
    crc ^= buf[pos];

    for (int i = 0; i < 8; i++)
    {
      if (crc & 1)
        crc = (crc >> 1) ^ 0xA001;
      else
        crc >>= 1;
    }
  }

  return crc;
}

static const char* mode_name(uint16_t mode)
{
  switch (mode)
  {
  case 0:
    return "Off";
  case 1:
    return "Cool";
  case 2:
    return "Heat";
  case 4:
    return "DHW";
  case 7:
    return "Defrost";
  case 20:
    return "Home Anti-Freeze";
  default:
    return "Unknown";
  }
}

int main(void)
{
  const char* ip = "192.168.1.50"; // IP van je Waveshare
  const int port = 4196;           // poort van je Waveshare
  const uint8_t slave = 11;

  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0)
  {
    perror("socket");
    return 1;
  }

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1)
  {
    perror("inet_pton");
    close(sock);
    return 1;
  }

  if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0)
  {
    perror("connect");
    close(sock);
    return 1;
  }

  uint8_t req[8];

  req[0] = slave; // slave id
  req[1] = 0x03;  // read holding registers
  req[2] = 0x00;  // register high byte
  req[3] = 0x2D;  // register low byte
  req[4] = 0x00;  // count high byte
  req[5] = 0x01;  // count low byte

  uint16_t crc = modbus_crc16(req, 6);
  req[6] = crc & 0xFF;        // CRC low
  req[7] = (crc >> 8) & 0xFF; // CRC high

  if (write(sock, req, sizeof(req)) != sizeof(req))
  {
    perror("write");
    close(sock);
    return 1;
  }

  uint8_t resp[256];
  int n = read(sock, resp, sizeof(resp));

  if (n < 7)
  {
    printf("Ongeldig of te kort antwoord, bytes=%d\n", n);
    close(sock);
    return 1;
  }

  uint16_t resp_crc = resp[n - 2] | (resp[n - 1] << 8);
  uint16_t calc_crc = modbus_crc16(resp, n - 2);

  if (resp_crc != calc_crc)
  {
    printf("CRC fout\n");
    close(sock);
    return 1;
  }

  if (resp[0] != slave || resp[1] != 0x03)
  {
    printf("Modbus fout of verkeerde slave/function\n");
    close(sock);
    return 1;
  }

  uint16_t mode = ((uint16_t)resp[3] << 8) | resp[4];

  printf("Running mode raw: %u\n", mode);
  printf("Running mode: %s\n", mode_name(mode));

  close(sock);
  return 0;
}
