# Working Modes Strategy

This controller exposes 4 UI working modes:

- `0 = OFF`
- `1 = DHW only`
- `2 = Heating only`
- `3 = DHW + Heating`

## Device register behavior

Two different mode-related registers are used:

- `0x002C` (`REG_RUNNING_MODE`, RW): mode setting register
  - `0 = Off`
  - `1 = Cool + DHW`
  - `2 = Heat + DHW`
- `0x002D` (`REG_RUNNING_STATUS`, R): actual running state register
  - `0 = Off`, `1 = Cool`, `2 = Heat`, `4 = DHW`, `7 = Defrost`, `20 = Anti-freeze`

The web API reports both:

- `mode`: derived from `0x002C` (what is set)
- `runningStatus`: derived from `0x002D` (what unit is actually doing)

## Why target overrides are needed

The device does not provide native "DHW only" or "Heating only" settings in the writable mode register.

So the controller uses `Heat + DHW` mode (`0x002C = 2`) and applies target overrides:

- **DHW only (1)**:
  - set heating target to minimum (`25°C`) to suppress space heating demand
  - set DHW priority (`REG_DHW_PRIORITY = 1`)
- **Heating only (2)**:
  - set DHW target to minimum (`40°C`) to suppress DHW demand
  - clear DHW priority (`REG_DHW_PRIORITY = 0`)
- **DHW + Heating (3)**:
  - restore user-saved DHW and heating targets
  - set DHW priority (`REG_DHW_PRIORITY = 1`)

## Saved target restore behavior

User setpoints are saved when explicit temperature commands are received (`/api/set-dhw`, `/api/set-heating`).

When returning to mode `3`, those saved setpoints are restored.
