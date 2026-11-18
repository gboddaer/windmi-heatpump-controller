# Backup-heater read-only validation and parameter review

User approved the paired-temperature validation approach, and asked to check how normal resistance assistance can be minimized while retaining anti-freeze. This is NOT authorization for speculative heater-setting writes.

## Evidence

Original installation manual: https://thermosilesia.pl/assets/default/system_object_files/43e99485889db21827eed29418c8fbec.pdf
- §10.7, printed p42/PDF105: standard internal 3kW EH1, optional external EH2/EH3; booster operation is conditional on configured outdoor thresholds.
- §2.1: water-circuit freeze protection may use the heat pump, heating tape, or backup heater. Leaving the small craft heaters available does not prove the main water circuit remains protected without backup.
- Printed p46/PDF109 distinguishes main-loop EHs, DHW EHs and small anti-freeze/base-pan/compressor heaters. Do not equate outdoor Power PTC bit with EH1.
- Registers 0x0259 backup type (7 = no auxiliary heater), 0x025A warmup, 0x025B booster delta, 0x025C booster OAT threshold, 0x0202 compressor minimum heating OAT.
- Published warmup units/ranges and booster OAT ranges conflict with the separate controller manual: retain raw values and document ambiguity, not a guessed recommended value.

ORIS controller manual: https://clima-air.com/cdn/shop/files/Instalation_configuration_MANUAL_WINDMI_1.pdf?v=6564792783602280408
- Printed p51/PDF25 query item 36 is main-loop EH state (0/1); 37 is DHW EH; 42–44 are small protection/chassis/crankcase heaters. Query 36 is NOT Modbus register 36.
- Query item22 calls T1 after EHs. Installation map 0x0004 is T1; 0x1008 is Tw-out after BPHE, before EHs. No verified direct Modbus EH1 ON/OFF bit found.

## Phase implemented now

- Remain isolated; do not restart/change the live app or connect another Modbus client.
- Add one non-critical 0x1008 read immediately after existing 0x0004 on each normal status read. Missing/unsupported reads do not change online state, COP, arbitration or targets.
- Record raw source readings with timestamps; compute signed delta T = after minus before and estimated added water heat = flow(m³/h)*1000/3600*4186*delta T.
- Require current valid temperatures, valid positive flow, no sentinel/implausible values, and source sample span <=5s. Zero/negative delta is preserved. Offline/missing/zero-flow/skewed samples are explicitly unavailable for the heat estimate.
- Always label this validation-only/unvalidated: no asserted heater ON/OFF, no inferred runtime/kWh or changes to COP yet. Assumes water, not verified glycol properties. Temperature offsets, lag and other heat paths require comparison with panel item36 or an independent meter.
- Add the five configuration registers above to the existing auxiliary round robin, with no writes. Keep at most two optional reads per scheduled interval (one alarm + one auxiliary); derive auxiliary freshness from actual register count (14 auxiliaries: 420s rotation, 480s maximum age).
- API exposes validation fields with nullable measurements, validity/reason/source timing; raw configuration stays in diagnostics with individual ages.
- Diagnostics shows before/after/delta/estimated added heat, validity/unvalidated state, and read-only configuration. Warmup stays raw because documentation is ambiguous. No heater-setting controls.
- An API-only CSV logger records new status sequences, paired readings, operating context, configuration values/ages and optional operator label. It never writes device state; transport errors create explicit gap records, not invented OFF samples.

## Parameter policy

Do not set Backup Type=7, lower compressor operating limits, or alter frost/defrost/anti-legionella behavior on this evidence. Lowering booster OAT, increasing delay and increasing allowed deficit are candidates to reduce normal assistance, not guaranteed anti-freeze-only settings. Read actual parameters first, resolve firmware-specific scales/limits, and seek confirmation of safety exceptions before proposing exact values or writing anything. Preserve native protection and avoid silently removing heating fallback below the compressor's operating envelope.

## Delivery

TDD for estimator/validity, acquisition failure isolation, round-robin budget/ages, production HTTP, DOM and logger. Build and test in an isolated worktree. Do not merge/push/restart/activate without authorization. Live parameter values cannot be obtained until new read-only telemetry is deployed; no probing around that constraint.
