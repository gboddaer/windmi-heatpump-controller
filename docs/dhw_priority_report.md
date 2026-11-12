# DHW Priority: register 0x02BF

**Status: superseded.** This page used to say the register was unreadable and unwritable.
That was wrong. Corrected by direct measurement on real firmware on 2026-11-11; the
control rule that replaced everything described here lives in
`docs/working-modes.md` and `docs/superpowers/specs/2026-11-11-native-dhw-only-mode-and-dhw-priority.md`.

## What the register is

`0x02BF` (`REG_DHW_PRIORITY`) is listed in the Rotenso Windmi register map as read/write,
`1` = domestic hot water takes precedence, `0` = space heating takes precedence.

## What was measured (2026-11-11, Windmi 8 kW, firmware of that date)

| Claim | Reality |
|---|---|
| Reading `0x02BF` returns exception `0x02` | Reads fine with FC 03 |
| Writing `0x02BF` returns exception `0x02` | Writes fine both directions with FC 06: correct echo, and `0x03` reads the new value back |
| Writing `1` gives hot water priority | **Nothing happens.** `0x002D` stayed `2` (heat), the diverter valve stayed open, the tank temperature was flat for 153 s, and the panel kept indicating heating priority |
| The panel's priority indication comes from this register | An 8-minute read-only walk of the panel changing its own settings moved exactly three registers: `0x002C`, `0x002D`, `0x00D2`. `0x02BF` never moved |

The last two rows are the ones that matter. A register the firmware accepts, stores and
shows on the register map, but which changes nothing observable in the unit's behaviour,
is a register that cannot be relied on to arbitrate anything. Writing `1` immediately
after re-writing the running mode (in case the priority flag is latched on a mode change)
changed nothing either.

## Why the old report was wrong

Our own Modbus client mis-parsed exception replies until 2026-11-11: an exception frame is
5 bytes (address, `function | 0x80`, code, CRC) and the client was waiting for the 8 bytes
of a normal response. That bug was found with a simulated gateway, not on the unit, and it
is fixed — the client now reads the 3-byte header, checks `function | 0x80`, and reads the
2-byte CRC. Any "exception code" reported by this project before that fix should be read
as unverified.

## What the code does now

- **No code writes `0x02BF`.** Five write sites were deleted on 2026-11-11; the register is
  not even read, because a value with no effect is not a status.
- `dhw_priority` in the status payload is derived, not read: `running_mode == 5`. It says
  what the unit is doing, which is the only honest version of that word.
- `POST /api/set-priority` still accepted and still ignored, so an old client does not
  break; the web UI's heating-priority button was removed, because the unit has no
  heating-priority state to switch to.
- Priority itself is a rule, not a setting: while both demands are enabled, the tank gets
  the compressor when it is cold and the house gets it back when the tank is confirmed at
  target. See `docs/working-modes.md`.

## How to reproduce

Read-only tools, all of which were exercised against a simulated gateway before touching
hardware:

- `tools/windmi_regdump.py` — read a block of registers, snapshot to JSON, `--diff` two snapshots
- `tools/watch_changes.py` — poll continuously and print only what moves (this is what
  proved the panel does not touch `0x02BF`)
- `tools/windmi_mode.py` — write `0x002C` with read-back verification
- `tools/test_priority_latch.py` — the write `0x02BF`, re-write the mode, watch for a
  reaction experiment
