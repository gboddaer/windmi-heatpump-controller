# Read-only diagnostics

Approved scope: expanded diagnostic dashboard for checking arbitration and device faults, without disturbing the running application.

## Safety and delivery

- Develop/build/test in an isolated worktree. Do not restart, signal, replace the executable of, or attach a Modbus client to the live controller.
- No new command endpoints, automatic corrections or write operations. Existing arbitration behaviour is unchanged.
- Preview on a separate localhost HTTP port with GET-only forwarding to the existing status API; no second controller.
- New backend telemetry is explicitly unavailable against the old API until an operator authorizes deployment/restart.

## Dashboard

Group read-only data into health/alarms, control decisions, compressor/pump/fans/valves, temperatures/targets, electrical/heat estimates, and raw registers/counters. Show requested vs set vs actual modes without declaring ordinary transitions faults. Include snapshot progress, failed tank reads and watchdog suspicion. A failed fetch invalidates live diagnostic readings immediately.

Preserve zero vs missing vs stale. Report hardware alarm coverage, not just a green "no faults" label when banks are unavailable. Decode documented alarm bits and retain unknown/reserved bits as such. Pressure switch bits and valve output commands are not proof of physical pressure or valve position.

Actual compressor 0x0017 has inconsistent manual units: show raw, unverified scale. Fan and expansion-valve scaling is similarly ambiguous: show raw values. Required compressor speed 0x100F is explicitly frequency*10. Runtime counters are reported counters, not independently verified lifetime hours.

## Offline register evidence

Source: original bilingual installation manual downloaded previously to /tmp/windmi-installation-original.pdf, English printed pp. 50–54 (Modbus table). Documented read-only registers: pump command 0x0055; alarm banks 0x1009–0x100C; ODU outputs 0x100E (bit 6 four-way valve, bit 5 SV1, bit 7 SV2); required compressor frequency 0x100F; required fan values 0x101A/B; required EXV opening 0x101C; actual fan values 0x101D/E; ODU pressure-switch inputs 0x101F; IPM protection reason 0x1022. No live probes to confirm implementation.

## Acquisition and API

Record raw unsigned values and read-validity for existing diagnostic registers as part of their existing reads (no duplicates). Capture failures as null, never synthetic zero. Add at most two optional reads per normal status tick: round-robin one of four alarm banks, and one auxiliary register. Cache slow samples with individual ages; stale readings are not evidence of no fault. Optional errors never alter online state, arbitration, targets or power validity. No extra retry loops.

Expose a nested diagnostics.registers object with address, raw (null when unavailable), and ageSeconds. Existing API remains compatible. Read ages use the injected monotonic controller clock. UI adds time since snapshot advancement to per-register age; offline/stale data are not displayed as current.

## Verification

TDD for optional failures, genuine zero, unsigned high bits, read budget, no added writes, cached ages, real production HTTP JSON, DOM rendering, stale/failed API states, fault decoding and read-only operation. Full CTest and existing UI checks; headless browser against an isolated preview. Do not claim hardware mappings calibrated or validated on the live unit.
