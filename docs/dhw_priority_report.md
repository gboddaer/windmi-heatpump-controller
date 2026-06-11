# DHW Priority Analysis

## Register: 0x02BF (DHW Priority)

### What it is
Register `0x02BF` is documented in the Rotenso Windmi Modbus register list as a read/write register controlling DHW vs Heating priority. Expected values:
- `1` = DHW priority (domestic hot water takes precedence)
- `0` = Heating priority (space heating takes precedence)

### Observed Behavior
**The register consistently returns Modbus Exception `0x02` (Illegal Data Address)** on the target device firmware. This was verified using the `tests/tools/probe_registers.c` probe tool which sends raw Modbus RTU FC 0x03 frames over TCP to the Waveshare gateway.

This means:
- **Reading** the register fails — the device does not support this address in its current firmware
- **Writing** to the register also fails — we cannot change priority through this register

### Impact
The priority setting is still functional in the application through a different mechanism:
1. **Working Mode** (`0x002C`) controls whether the heat pump is in DHW, Heating, or Combined mode
2. **Application-level priority** is managed in `ControlLoop::current_priority_` which determines the control logic behavior
3. When mode changes are applied (e.g., switching to Heating-only), the `applyControlLogic()` method sets appropriate targets and priorities internally

### Working Mode vs Priority
The relationship between working mode and priority:

| Working Mode | Value | Description | Priority Behavior |
|---|---|---|---|
| Off | 0 | Heat pump OFF | N/A |
| DHW Only | 1 | DHW heating only | DHW priority enforced |
| Heating Only | 2 | Space heating only | Heating priority enforced |
| DHW + Heating | 3 | Both active | Priority depends on `current_priority_` |

### Key Finding: DHW Only vs DHW+Heating Ambiguity
At the device register level, both "DHW Only" and "DHW+Heating" modes map to the same running mode value (`MODE_SET_HEAT_DHW = 2`). The distinction is maintained entirely in application memory:
- "DHW Only" is achieved by setting heating target to minimum (25°C)
- "DHW+Heating" uses both targets at normal values

This is why **persistent settings** are critical — without them, the application always restarts in DHW+Heating mode (mode 3) regardless of what the user had selected.

### Recommendation
1. **Keep `0x02BF` in the code** for status reporting (the read is attempted but failure is caught gracefully)
2. **Do NOT attempt to write** to `0x02BF` as it will always fail
3. **Use application-level priority** (`current_priority_`) and **working mode** (`desired_working_mode_`) for all control decisions
4. **Persist settings** to `~/.windmi/settings.ini` to maintain user selection across restarts
