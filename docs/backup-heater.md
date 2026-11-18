# Backup water heater: validation and minimization review

This concerns the main water-loop electric backup heater, not the DHW tank immersion
heater, crankcase/base-pan heater or the outdoor board's ambiguous Power PTC output.
No heater settings are changed by this feature. There is no verified direct Modbus
ON/OFF address for the main heater in the sources checked.

## Manufacturer and independent sources

- [Original installation manual](https://thermosilesia.pl/assets/default/system_object_files/43e99485889db21827eed29418c8fbec.pdf),
  §10.7, printed p42/PDF105: standard 3 kW internal EH1, optional external EH2/EH3.
  Verify the installed unit's actual heater rating before using that number.
- [ORIS controller manual published for Windmi](https://clima-air.com/cdn/shop/files/Instalation_configuration_MANUAL_WINDMI_1.pdf?v=6564792783602280408),
  printed p51/PDF25: query item **36** is main-loop EH status, 0=OFF/1=ON; 37 is DHW
  EH status; 42–44 are smaller protection/chassis/crankcase heaters. These menu item
  numbers are NOT Modbus addresses. If the installed panel is ORIS, its Query menu
  can be used for read-only comparison; do not press the heater activation button.
- [Independent Home Assistant configuration](https://gist.github.com/hvdb/a6a6fdc889573084ac2bdd53e71303c7)
  exposes backup type at decimal 601 (0x0259), not confirmed main-heater activation.
  Other brands' heater registers and our own repository are not independent Windmi
  register evidence.

## Can normal resistance assistance be disabled except for anti-freeze?

**No verified anti-freeze-only configuration has been established. Do not select
Backup Type=7 merely because it says "no auxiliary heater".**

The original manual §2.1 says water-circuit freeze protection may heat the water using
the heat pump, heating tape or backup heater. The separate small protection heaters
being available does not prove that disabling main-loop EH preserves all required
water-loop protection, especially if the compressor is unavailable or outside its
operating envelope. Defrost and water freeze protection are different functions.

The documented review parameters are:

| Register | Meaning | Review policy |
| --- | --- | --- |
| 0x0259 | Backup type: 0 main-loop+DHW+boiler; 1 main-loop+DHW; 2 DHW+boiler; 3 main-loop+boiler; 4 DHW only; 5 boiler only; 6 main-loop EH only; 7 no backup | Read the installed configuration; no automatic disable or rewiring assumptions |
| 0x025A | Heat-pump warmup delay | Keep raw until scale/range is resolved: controller manual says 0–120 min/default 60, Modbus table says 0–60 with inconsistent temperature units/scaling |
| 0x025B | Booster temperature deficit | Documented raw/10 °C, range 1–20°C; a larger permitted deficit may reduce assistance but can reduce comfort/performance |
| 0x025C | Booster outdoor-temperature threshold | Documented raw/10 °C; controller manual says −10..0°C/default −5, Modbus table says −20..15°C. A lower threshold may reduce normal eligibility, not prove frost-only operation |
| 0x0202 | Minimum outdoor temperature for compressor heating | Read only. Do not lower this to force the compressor outside its operating envelope to avoid resistance heat |

Keeping the fitted backup configured while considering a lower assistance threshold,
a longer delay or a larger allowed deficit is preferable to an unverified global
disable. Exact recommendations require actual readback, firmware-specific limits,
and confirmation of which frost/defrost/failure exceptions remain effective. This
may still allow normal backup use in extreme cold; it is not an anti-freeze-only
promise. Preserve any DHW hygiene/anti-legionella requirements as a separate concern.
The feature supplies evidence, not a configuration policy or actuator.

## Paired-temperature evidence

- 0x1008: Tw-out, water leaving the heat-pump exchanger, before the backup heater.
- 0x0004: T1, leaving water after the electric heater according to the controller's
  query item 22. The location/order needs validation on the installed unit.
- 0x102A: flow in m³/h, raw/100.

Compute signed ΔT = T1 − Tw-out and added-water-heat estimate:

```
added W = flow(m³/h) × 1000/3600 × 4186 × ΔT(°C)
```

At 1 m³/h, a 2.6°C rise is about 3.0 kW. This is a **water-based estimate**, not an
ON/OFF detector or electricity meter. Sensor offsets, thermal inertia, plumbing,
sensor placement, glycol heat capacity/density and other heat sources can affect it.
Zero and negative differences are retained for validation rather than clipped away.

Before/after reads are adjacent within each normal status read. The additional
before-heater read is non-critical and never affects device-online, normal COP,
arbitration or target writes if unsupported. Valid estimates require current usable
temperatures (including rejection of the −40°C sentinel), positive plausible flow,
a source span <=5 seconds and source age <=10 seconds within the snapshot. No flow,
missing data or stale/skewed sources yield a null heat estimate, never synthetic OFF.
"measurementValid" means usable inputs for the calculation, not verified accuracy.

`/api/status` adds `backupHeater` with status always **unvalidated**, nullable paired
measurements, estimatedAddedHeatW, measurementValid, reason and sampleSpanMs. This
phase deliberately does not infer activation, count runtime, estimate kWh or change
COP. Current COP may be misleading if backup heat contributes to measured water heat
but its electricity is not covered by the current register; circuit coverage is not
verified, so no speculative correction is made.

Configuration values stay in `diagnostics.registers`, with raw values and ages. The
optional read limit stays at two per interval: one alarm bank and one auxiliary.
Fourteen auxiliaries rotate every 420 seconds at the normal 30-second tick; their
freshness limit is 480 seconds (full rotation plus 60-second allowance). Alarm banks
remain on a 120-second rotation/180-second freshness limit. No retry loop is added;
the underlying transport's existing timeout/retry policy still applies.

## Recovered historical evidence (16 November 2026)

A subsequent offline search recovered 21 raw configuration snapshots from 11–13
November. All agree: backup type raw 7 ("no auxiliary heater"), warmup raw 60,
booster deficit raw 100 (documented 10°C), booster OAT raw −140 (−14°C), compressor
minimum OAT raw −260 (−26°C). The latest is 13 November 16:32:37 UTC / 17:32:37 CET.
These are historical settings, NOT current readback or proof of frost exceptions.

Twenty snapshots also contain both temperature registers; nineteen have positive
flow. Their raw-derived differences range −1.5..5.0°C, median 1.5°C. Individual read
timestamps are missing, and known full sweeps took about 54 seconds. These pairs
cannot qualify as synchronized validation or establish backup use, runtime or kWh.
Positive differences despite recorded type 7 particularly require checking timing,
sensor offsets/locations and firmware semantics before any activation classifier.

The requested recent 24-hour window yielded 2,082 continuous-monitor rows covering
only 16 November 15:32:32–21:19:22 UTC (5.78 hours); earlier coverage is missing.
Those rows lack 0x1008 and the backup parameters, so the missing signals cannot be
backfilled. Source-preserving exports, coverage metadata and raw copies are in
`/var/tmp/wpomp-backup-history-20261116T211930Z/`; see its README and summary.json.
No live probes, device writes, restart or telemetry activation were performed.

## Collecting validation data after telemetry deployment

```sh
python3 tools/log_backup_heater.py \
  --url http://localhost:8080 \
  --output /var/tmp/wpomp-backup-heater-validation.csv \
  --minutes 720 \
  --label 'panel item 36 checked OFF at start; not continuous feedback'
```

The logger uses only GET /api/status, skips repeated snapshot sequences within a
logger session, distinguishes controller restarts using instance ID, and appends UTC
samples. API failures are explicit gap rows with blank measurements, not zero heat
or OFF events. Raw configuration values retain age/freshness, including genuine zero.
The first sample and the first after a restart/gap cannot establish snapshot progress;
check snapshotProgressObserved. A schema mismatch or partial CSV tail is rejected
without overwriting existing data. Deduplicate instance/sequence keys across separate
logger runs if combining them in later analysis.

The label is an operator note about the run, not an asserted panel state for every
sample. Record exact times of later panel observations separately. Compare naturally
occurring, stable-flow periods against query 36 or an independent meter; do not force
the heater on merely to validate this feature. If no ON example occurs, the OFF
baseline alone does not validate an activation classifier. A dedicated meter/current
sensor on the heater feed, installed by a qualified electrician, is the more reliable
fallback if no status mapping is available.
