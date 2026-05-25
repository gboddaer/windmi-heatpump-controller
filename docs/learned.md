Ja. We hebben ondertussen experimenteel bevestigd hoe de Rotenso effectief reageert, en dat wijkt licht af van de aannames uit de manual/header.

# Wat we geleerd hebben

## 1. Waveshare werkt als pure transparante TCP↔RS485 bridge

De gateway doet:

```text
TCP socket <-> raw RS485 bytes
```

Dus:

* geen Modbus TCP protocol
* geen MBAP header
* geen TLS
* geen framing
* gewoon volledige Modbus RTU frames inclusief CRC sturen

Dus correct model:

```text
PC
  -> TCP socket
    -> RTU frame + CRC
      -> Waveshare
        -> RS485
          -> warmtepomp
```

---

# 2. Functiecode 0x03 werkt

De warmtepomp gebruikt blijkbaar:

```text
03 = holding registers
```

ook voor waarden die in de manual als “input” lijken.

Functie `0x04` geeft:

```text
Exception 04 = Slave Device Failure
```

Dus:

```text
Gebruik alleen function 0x03
```

voor praktisch alle reads.

---

# 3. Registers zijn correct 0-based

Bevestigd:

```text
0x0001 = outdoor temperature
0x002D = running mode
```

Dus geen +1 offset nodig.

---

# 4. Temperatuur scaling klopt

Voorbeeld:

```text
antwoord:
00 D7 = 215
```

→

```text
21.5 °C
```

Dus:

```text
temperature = raw / 10.0
```

---

# 5. Running mode bevestigd

Request:

```text
0B 03 00 2D 00 01 14 A9
```

Response:

```text
0B 03 02 00 04 21 86
```

Decoded:

```text
4 = DHW
```

Dus enum klopt.

---

# 6. Register 0x0209 is waarschijnlijk fout/onbruikbaar

`0x0209` geeft exception 04.

Dus mogelijk:

* fout in manual
* firmware-afhankelijk
* niet geïmplementeerd
* andere functiecode nodig
* alleen schrijfbaar
* andere offset/documentatie

Dus voorlopig:

```cpp
UserInterfaceType = 0x0209
```

markeren als:

```cpp
// unverified
```

of verwijderen tot bevestigd.

---

# Aanpassingen aan de header

Ja, ik zou enkele dingen aanpassen.

---

# 1. Verwijder function 0x04 aannames

Nu staat:

```cpp
constexpr std::uint16_t functionReadInputRegister = 0x04;
```

Dat kan blijven bestaan, maar documenteer:

```cpp
// Rotenso firmware appears to use 0x03 for almost all readable registers.
// Function 0x04 returns Modbus exception 04 on tested firmware.
```

---

# 2. Voeg voorbeeldframes toe

Handig voor debugging:

```cpp
// Read outdoor temperature:
// 0B 03 00 01 00 01 D5 60
//
// Read running mode:
// 0B 03 00 2D 00 01 14 A9
```

---

# 3. Device type register markeren als onbevestigd

Dus:

```cpp
UserInterfaceType = 0x0209, // unverified on tested firmware
```

---

# 4. Voeg helper toe voor CRC generation

Heel nuttig:

```cpp
uint16_t modbus_crc16(const uint8_t* data, size_t len);
```

---

# 5. Voeg helper toe om RTU frame te bouwen

Bijvoorbeeld:

```cpp
buildReadHoldingRegisterFrame(
    slave,
    reg,
    count
);
```

Dat voorkomt CRC fouten.

---

# 6. Mogelijk naming aanpassen

Nu:

```cpp
functionReadInputRegister
```

maar praktisch:

```cpp
functionReadHoldingRegister = 0x03
```

is de echte.

---

# Belangrijkste conclusie

De warmtepomp is eigenlijk extreem eenvoudig aan te sturen:

```text
raw TCP socket
+
Modbus RTU frame
+
CRC
```

Geen virtual COM nodig.
Geen Modbus TCP stack nodig.
Geen HTTPS/API nodig.

Gewoon:

```c
send(sock, rtu_frame, len, 0);
recv(sock, ...)
```

werkt.
