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

---

# Corrections, 2026-11-11/12 (re-measured on current firmware)

The notes above are a record of measurements from mid-2026, and three of the
"probably unusable" conclusions do not hold on the firmware now on the unit. Re-derive
any of it with the read-only tools first, and only then the writing ones:

```text
tools/windmi_regdump.py    read registers, snapshot to JSON, --diff two snapshots
tools/watch_changes.py     poll and print only what moves (this is how the panel was walked)
tools/test_set_mode.py     write 0x002C = 0..5, verified by echo and read-back
tools/test_set_*.py        the other hardware write tests, same shape
```

Every write test was run against a simulated gateway before it was run against the unit,
and that is how two bugs in our own client were found: an exception reply is 5 bytes
(address, `function | 0x80`, code, CRC) and we were waiting for 8, and a `/proc` scan that
did not actually match the controller process.

| Claim above | Measured on current firmware |
|---|---|
| `0x0209` gives exception 04, "probably broken" | Reads fine with FC 03 (`regdumps/probe-20261111-164334.json`: raw 2) |
| `0x02BF` (DHW priority) cannot be used | Reads and writes fine, both directions — and is **inert**. Writing `1` changed nothing: `0x002D` stayed `2`, the valve stayed open, the tank was flat for 153 s. See `docs/dhw_priority_report.md` |
| No native DHW-only mode; emulate it by pinning the heating target to 25 °C | `0x002C = 5` is DHW only and `4` is heat only. Both were written by us and executed by the unit in under 2 s: `0x002D` went to `4` (dhw), `0x00D2` closed the valve, the space loop stopped. Setpoint pinning is gone |

The reason the old readings looked broken is not known; firmware changed, and our
exception parsing was itself broken until 2026-11-11, so an "exception code N" reported by
this project before that date should be treated as unverified.

Mode 5 was confirmed by writing it, watching the unit react, writing 2 back, and watching
it revert in about 7 s. The 8-minute panel walk that followed moved exactly three
registers: `0x002C`, `0x002D`, `0x00D2`.

# Corrections, 2026-11-13 (which DHW setpoint the unit obeys)

Two more claims in this file and in `docs/rotenso_windmi_manual_summary.md` did not survive
contact with the unit. Both were found by watching a tank that would not finish charging.

| Claim | Measured |
|---|---|
| `0x0194` is *the* DHW setpoint; `0x0033` is the control point the unit regulates to | `0x0194` is the **normal** sub-mode setpoint and `0x0196` the **economic** one (manual's own table). With `0x00C9 = 2` the unit charged to `0x0196 = 45.0` and stopped, while `0x0194` said 50.0 and the tank sat there for 80 minutes. Raising `0x0196` to 52.0 started the compressor within 2 minutes (31 → 59 Hz, valve closed, flow 1.07 m³/h). `0x0033` read 45.0 in every snapshot ever taken and followed neither DHW setpoint nor the heating setpoint (raising `0x0191` from 45.0 to 55.0 left it at 45.0 and started nothing), so whatever it is, it is not the DHW target |
| A DHW-only grant means the unit is charging | Only if the unit agrees. A grant with `0x0017` (compressor) at 0 and `0x102A` (flow) at 0 can last an hour and more: the restart temperature belongs to the unit's own differential below `0x0196`, not to anything we write. Priority has to be measured as progress, which is why the arbiter releases a declined charge (`chargeIsParked`, `docs/superpowers/specs/...`) instead of holding the compressor for a register |

`0x000B` (manual: T5, "DHW tank temperature") reads 8.5 °C while `0x00CE` (T4) reads 44.4 °C
in a tank that has been running. One of those two sensors is not in the tank. The controller
uses `0x00CE` only; if a future change touches `0x000B`, find out where that probe is first.

**One Modbus client at a time.** On 2026-11-13 a logging script polled the same gateway the
application was using. Frames collided: `Unexpected byte count: 242 (expected 2)`, values
appearing and vanishing between reads, and — found in the log, not on the tank — a garbled
DHW read of 52.0 °C became a real write to `0x0194` because drift correction trusted the
first reading. Observe through `/api/status`; the writing tools are for when the application
is stopped. Drift correction now needs a plausible value confirmed on two consecutive ticks.
