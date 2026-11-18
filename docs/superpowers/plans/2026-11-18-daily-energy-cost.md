# Daily estimated energy and cost Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show per-day estimated input kWh and its cost (€/day) in Overview → Current Status, with the euro-cents price editable on the Diagnostics tab.

**Architecture:** A small header-only accumulator (`windmi::DailyEnergyAccumulator`, like `CycleCop.hpp`/`BackupHeater.hpp`) integrates estimated AC input power in wall-clock time and rolls over at local midnight. `ControlLoop` feeds it at every snapshot publish through a private `updateDailyEnergy(StatusSnapshot&)`, exposes kWh and price through `StatusSnapshot`, persists kWh hourly through a callback, and accepts the price as `CMD_SET_PRICE`. The browser computes cost = kWh × price/100; the price persists in `settings.ini`. **Deployment is NOT part of this plan** — it needs the documented controlled restart, authorized separately.

**Tech Stack:** C++17, gtest, mongoose HTTP, vanilla JS UI, python integration harness (`test_demo_main`).

**Spec:** `docs/superpowers/specs/2026-11-18-daily-energy-cost-design.md` (revised: price save is 202-then-async, not 500).

## Global Constraints

- TDD everywhere: observe RED before implementing. For a new file, the RED is the new test failing to compile against the missing header — record that output, not a guess.
- Everything user-visible is labelled an estimate. The AC-power hypothesis (raw/2, PF 0.90) is untouched; the UI never rescales power (`static/app.js` says so in a comment).
- No new Modbus reads/writes, no control-behavior changes, no `schedule.json` writes.
- Price: euro cents/kWh, `PRICE_CENTS_MIN 0.0f`, `PRICE_CENTS_MAX 500.0f`, `PRICE_CENTS_DEFAULT 30.0f` in `include/config.h` (project style: `#define`, next to `ESTIMATED_POWER_FACTOR`). Persisted key `ui.price_per_kwh_cents`.
- Energy persistence keys: `state.energy_today_kwh` (double) and `state.energy_today_date` (`YYYY-MM-DD`, local).
- **Counting rule (single place):** integrate only when `status.device_online && status.power_valid`; a sample gap > 120 000 ms integrates nothing (a long outage is not a long run of power).
- **`dailyEnergyValid` semantics:** "the controller holds a counter for today" (seeded or integrated). It is not "device online". Offline snapshots keep showing today's total.
- **Determinism:** the wall clock is injectable (`setWallClockForTesting`) so kWh assertions are exact, never tolerance-fudged against real elapsed time.
- HTTP mirrors the set-endpoint pattern: 503 shutting down / 400 empty body / 400 missing-or-unparseable / 422 out of range / 202 queued. No 500 for deferred failures.
- Conventions verified against the tree: core helpers are header-only; `.cpp` test files join the existing `test_core` target; `add_test` lives in the root `CMakeLists.txt`; cjs tests register under `find_program(NODE_EXECUTABLE)` in `tests/tools/CMakeLists.txt`.

## Review Focus

- **Bad price input** (501, −3, `{}`, empty body) → 400/422 at HTTP, and the loop clamps anything it still receives while keeping a usable price. Pinned: `PriceHttp.*` (Task 4) + `DailyEnergyLoopTest.PriceCommandClampedToConfiguredRange` (Task 3).
- **Day roll and restart** → a saved kWh applies only when the saved date equals today; midnight zeroes the counter and must not resurrect yesterday. Pinned: `DailyEnergyAccumulatorTest.SeedOnlyAcceptsTodaysDate`, `.ObserveResetsAtDayRoll` (Task 1) + `DailyEnergyLoopTest.SeedRestoresTodaysValueOnly` (Task 2).
- **Outage** → no energy accumulates, and reconnection after a long gap integrates nothing. Pinned: `DailyEnergyAccumulatorTest.LongOutageIntegratesNothing` (Task 1) + `DailyEnergyLoopTest.OfflineTicksDoNotAddEnergy` (Task 2).
- **Persistence failure** → a failing save must not throw, must keep counting, must keep the hourly cadence (no per-tick warning storm), and must survive a read-only settings dir scenario that already exists in `test_main_schedule_startup.py`. Pinned: `DailyEnergyLoopTest.PersistenceFailureKeepsCountingAndKeepsHourlyCadence` (Task 2).
- **Stale/older controller** → missing or `undefined` kWh/price renders `Unavailable`, never `NaN`. Pinned: `test_ui_daily_energy.cjs` (Task 6).
- **Diagnostics stays read-only except the price card** → the existing guardrail assertion is turned into an explicit allowlist, not deleted. Pinned: `test_ui_diagnostics.cjs` (Task 6).

---

### Task 1: Daily energy accumulator (header-only) + unit tests

**Files:**
- Create: `include/core/DailyEnergy.hpp`
- Create: `tests/core/test_daily_energy.cpp`
- Modify: `tests/core/CMakeLists.txt` (add `test_daily_energy.cpp` to the **existing** `test_core` source list — no new executable)

**Interfaces:**

```cpp
namespace windmi {

inline constexpr int64_t kDailyEnergyMaxGapMs = 120000;        // 2 min
inline constexpr int64_t kDailyEnergyPersistIntervalMs = 3600000; // 1 h

class DailyEnergyAccumulator
{
public:
  static std::string dayKeyFor(int64_t wallMs);   // "YYYY-MM-DD" local; "" if wallMs <= 0
  void observe(int64_t wallMs, double watts);     // roll, then integrate the gap
  void seed(int64_t wallMs, double kwh, const std::string& savedDayKey);
  void advance(int64_t wallMs);                   // roll only
  bool persistDue(int64_t wallMs) const;
  void markPersisted(int64_t wallMs);
  double kwhToday() const;
  bool hasToday() const;
  std::string storedDayKey() const;
};

} // namespace windmi
```

Semantics:
- `observe`: roll the day first. Integrate `watts * dt / 3.6e9` kWh only when a previous sample exists, `dt > 0` and `dt <= kDailyEnergyMaxGapMs`; then store the sample and set `hasToday()`. The first sample of a day integrates nothing.
- `roll`: when `dayKeyFor(wallMs)` differs from the stored key → key updated, kWh = 0, hasToday = false, previous sample dropped.
- `seed`: roll first, then accept only when `savedDayKey == dayKeyFor(wallMs)` → kWh = value, hasToday = true. Otherwise no-op.
- `persistDue`: true when never persisted, else `wallMs - lastPersistMs >= kDailyEnergyPersistIntervalMs`.
- No OS clock inside the class: every method takes `wallMs`. `dayKeyFor` is the only place using `localtime_r` (guard `_WIN32` with `localtime_s` exactly as `ControlLoop.cpp` already does).

- [ ] **Step 1: Write the failing test** — `tests/core/test_daily_energy.cpp`. Derive the base from **local noon**, never a magic epoch constant (a raw epoch drifts across the day under other timezones):

```cpp
static int64_t localNoonMs(int year, int month, int day)
{
  std::tm t{};
  t.tm_year = year - 1900; t.tm_mon = month - 1; t.tm_mday = day;
  t.tm_hour = 12; t.tm_min = 0; t.tm_isdst = -1;
  return static_cast<int64_t>(std::mktime(&t)) * 1000;
}
const int64_t kBase = localNoonMs(2026, 11, 18);
```
Tests:
- `DayKeyForFormatsLocalDate`: `dayKeyFor(kBase)` matches `^\d{4}-\d{2}-\d{2}$`; `dayKeyFor(kBase + 86400000)` differs; `dayKeyFor(0)` is `""`.
- `ObserveIntegratesBetweenFreshSamples`: `observe(kBase, 1000)`; `kwhToday() == 0.0`; `observe(kBase + 30000, 1000)` → `kwhToday()` is exactly `1000.0 * 30000 / 3.6e9` (0.008333…, `EXPECT_DOUBLE_EQ` is fine here — assert with `EXPECT_NEAR(..., 1e-12)`), `hasToday()` true.
- `ObserveResetsAtDayRoll`: two observes 30 s apart on day 1 → kWh ≈ 0.00833; then `observe(kBase + 86400000 + 20000, 2000)` → `kwhToday() == 0.0` and `storedDayKey() == dayKeyFor(kBase + 86400000)`.
- `LongOutageIntegratesNothing`: `observe(kBase, 500)`; `observe(kBase + 600000, 500)` (10 min gap) → `kwhToday() == 0.0`; `observe(kBase + 630000, 500)` → integrates one 30 s window.
- `SeedOnlyAcceptsTodaysDate`: `seed(kBase, 1.5, dayKeyFor(kBase - 86400000))` → `kwhToday() == 0.0`, `hasToday()` false; `seed(kBase, 1.5, dayKeyFor(kBase))` → `kwhToday() == 1.5`, `hasToday()` true; `advance(kBase + 86400000)` → `kwhToday() == 0.0`, `hasToday()` false.
- `PersistDueAndMarkPersisted`: fresh → `persistDue(kBase)` true; `markPersisted(kBase)` → false at `kBase + 3599999`, true at `kBase + 3600000`.
- [ ] **Step 2: Run to verify RED** — `cmake --build build -j6 --target test_core` fails with `fatal error: core/DailyEnergy.hpp: No such file or directory`. Paste the line in the ledger.
- [ ] **Step 3: Implement** `include/core/DailyEnergy.hpp` (header-only, include guard `WINDMI_CORE_DAILYENERGY_HPP`, matching `CycleCop.hpp`'s style).
- [ ] **Step 4: Run to verify GREEN** — `cmake --build build -j6 --target test_core && ./build/tests/core/test_core --gtest_filter='DailyEnergyAccumulatorTest.*'` → all pass.
- [ ] **Step 5: Commit** — `git add include/core/DailyEnergy.hpp tests/core/test_daily_energy.cpp tests/core/CMakeLists.txt && git commit -m "Add wall-clock daily energy accumulator"`

### Task 2: Snapshot fields, frozen wall clock, publish-site integration

**Files:**
- Modify: `include/config.h`
- Modify: `include/core/ControlLoop.hpp`
- Modify: `src/core/ControlLoop.cpp`
- Modify: `tests/core/test_control_loop.cpp`

**Interfaces:**
- `StatusSnapshot` gains: `double daily_energy_kwh = 0.0; bool daily_energy_valid = false; double price_cents_per_kwh = PRICE_CENTS_DEFAULT;`
- `ControlLoop` public:
  - `using EnergyPersistCallback = std::function<bool(double kwhToday, const std::string& dayKey)>;`
  - `void setEnergyPersistCallback(EnergyPersistCallback callback);`
  - `void seedDailyEnergy(double kwh, const std::string& savedDayKey);` (uses `wallNowMs()`)
  - `void setWallClockForTesting(int64_t wallMs);` — freezes the wall domain at that epoch ms; the steady domain (`mClockOffsetMs`, COPs, arbitration) is untouched.
- `ControlLoop` private:
  - `int64_t wallNowMs() const;` → frozen value when set, else `system_clock::now() + mWallClockOffsetMs`
  - `void updateDailyEnergy(StatusSnapshot& status);` — `advance(wall)`; when `status.device_online && status.power_valid` also `observe(wall, status.ac_power_w)`; fill the three fields; then if `persistDue(wall)`: call the callback, `WINDMI_LOG_WARN` on failure or no callback set, and `markPersisted(wall)` **in both cases** (hourly cadence even on failure — never a per-tick warning).
  - members `windmi::DailyEnergyAccumulator mDailyEnergy; std::atomic<int64_t> mWallClockFrozenMs{-1}; double mPriceCentsPerKwh = PRICE_CENTS_DEFAULT; EnergyPersistCallback mEnergyPersistCallback;`
- **All four publish sites** must call `updateDailyEnergy(status)` immediately before the push:
  1. the initial snapshot in `threadFunc()` (`ControlLoop.cpp:1060`),
  2. the success branch, right after `updateCycleCop(status)` (`ControlLoop.cpp:1137`),
  3. the in-loop read-failure branch (`ControlLoop.cpp:1151`, after `"Failed to read status"`),
  4. `publishOfflineStatus()` (`ControlLoop.cpp:1214`).
- The `TickedLoop` harness gains `void setWall(int64_t wallMs)` → `loop_.setWallClockForTesting(wallMs)` and `void seedEnergy(double kwh, const std::string& dayKey)`.

- [ ] **Step 1: Write the failing tests** — new group `DailyEnergyLoopTest` in `tests/core/test_control_loop.cpp`, using `fullRegisters()` with `registers[REG_AC_CURRENT] = 20; registers[REG_AC_VOLTAGE] = 230;` (raw 20 → 10 A → 2300 VA → 2070 W at `ESTIMATED_POWER_FACTOR`) — the same register setup as `PowerScalingTest.DcVoltageUsesPolishManualMultiplierWithoutChangingCop`, not the pure-arithmetic `AcPowerVaIsVoltageTimesCurrent`:
  - `SnapshotDefaultsCarryNoEnergyAndDefaultPrice`: `StatusSnapshot{}` → `0.0 / false / PRICE_CENTS_DEFAULT`.
  - `IntegratesFreshPowerSamplesBetweenTicks`: `setWall(kBase)` then `tick()` (first sample, integrates nothing) → `daily_energy_kwh == 0.0`, `daily_energy_valid` true; `setWall(kBase + 30000)`, `tick()` → exactly `2070.0 * 30000 / 3.6e9` (≈0.01725) with `EXPECT_NEAR(..., 1e-12)`; a second 30 s window adds the same amount again.
  - `OfflineTicksDoNotAddEnergy`: after one integrating tick, erase `REG_AC_CURRENT` from the register map (`FakeModbusClient::readRegister` throws → `readStatus` fails → the in-loop failure branch publishes `device_online=false`). `setWall(+600000)` then `tick()` → `daily_energy_kwh` unchanged and `daily_energy_valid` still true. (Do **not** use a not-connected client: the reconnect path blocks on `MODBUS_RECONNECT_INTERVAL_S` and would time the test out.)
  - `MidnightRollZeroesAndDoesNotResurrect`: integrate on day 1, `setWall(kBase + 86400000)`, `tick()` → `0.0`, `daily_energy_valid` false; one more 30 s tick → `daily_energy_valid` true and only the new window's energy.
  - `SeedRestoresTodaysValueOnly`: `seedEnergy(1.2345, DailyEnergyAccumulator::dayKeyFor(realNowMs()))` then `tick()` → `daily_energy_kwh == 1.2345`; a fresh loop seeded with `dayKeyFor(realNowMs() - 86400000)` → `0.0` and false.
  - `HourlyPersistCallbackReceivesDayAndKwh`: capture `(kwh, dayKey)`; with `setWall(kBase)` the first `tick()` triggers one call (never persisted before) equal to the snapshot's kWh and `dayKeyFor(kBase)`; `setWall(kBase + 3599999)`, `tick()` → still one call; `setWall(kBase + 3600000)`, `tick()` → two calls.
  - `PersistenceFailureKeepsCountingAndKeepsHourlyCadence`: callback returns `false`; the loop keeps publishing, kWh keeps growing, `WINDMI_LOG_WARN` fires, and the next callback attempt only happens after `+3600000` ms.
- [ ] **Step 2: Run to verify RED** — `ctest --test-dir build -R '^test_core$' --output-on-failure` fails to compile (`updateDailyEnergy`/`setWallClockForTesting`/new fields missing).
- [ ] **Step 3: Implement** the fields, the frozen-wall seam, `updateDailyEnergy`, and the four call sites.
- [ ] **Step 4: Run to verify GREEN** — `ctest --test-dir build -R '^test_core$' --output-on-failure`; also run the whole binary once to prove nothing else regressed.
- [ ] **Step 5: Commit** — `git commit -am "Integrate the daily kWh estimate into every published snapshot"`

### Task 3: Price command, clamping and persistence callback

**Files:**
- Modify: `include/core/ControlLoop.hpp`
- Modify: `src/core/ControlLoop.cpp`
- Modify: `tests/core/test_control_loop.cpp`

**Interfaces:**
- `CommandType::CMD_SET_PRICE = 5` (values 0–4 are taken; next free is 5).
- `using PricePersistCallback = std::function<bool(double cents)>;`, `void setPricePersistCallback(PricePersistCallback)`, `void setPriceCentsPerKwh(double cents)` (startup load, clamped), private `double mPriceCentsPerKwh`.
- In `processCommands()`, the `CMD_SET_PRICE` case: `mPriceCentsPerKwh = std::clamp(static_cast<double>(cmd.float_val), static_cast<double>(PRICE_CENTS_MIN), static_cast<double>(PRICE_CENTS_MAX));` then persist via the callback and `WINDMI_LOG_WARN` on failure. **It must not call `fireSettingsCallback()`** — the price must never rewrite `ui.working_mode`/`ui.dhw_target`.
- `TickedLoop` gains `void sendPrice(double cents)` (push `Command{CMD_SET_PRICE, .float_val = static_cast<float>(cents)}` + `kick()`).

- [ ] **Step 1: Write the failing tests** — add to `DailyEnergyLoopTest`:
  - `PriceCommandUpdatesSnapshotOnly`: `sendPrice(25.0)`, `tick()` → `price_cents_per_kwh == 25.0` and `daily_energy_kwh` unchanged.
  - `PriceCommandClampedToConfiguredRange`: `sendPrice(5000.0)` → `PRICE_CENTS_MAX`; `sendPrice(-10.0)` → `PRICE_CENTS_MIN`.
  - `PricePersistCallbackFiresOnCommand`: callback captures the value; success path stores exactly what was sent (use `EXPECT_NEAR` — `Command::float_val` is a `float`, so 27.3 arrives as 27.2999992).
  - `PricePersistenceFailureWarnsOnly`: callback returns false → no throw, the loop keeps publishing, the in-memory price still updates.
  - `PriceCommandDoesNotRewriteModeOrTargets`: a `SettingsCallback` recorder sees no call after `sendPrice(...)`.
- [ ] **Step 2: Run to verify RED** — `ctest -R '^test_core$'` fails to compile (`CMD_SET_PRICE` undefined).
- [ ] **Step 3: Implement** the enum value, member, case and helpers.
- [ ] **Step 4: Run to verify GREEN** — `ctest --test-dir build -R '^test_core$' --output-on-failure`.
- [ ] **Step 5: Commit** — `git commit -am "Accept a tariff from the control loop without touching ui settings"`

### Task 4: `/api/status` fields and `POST /api/price`

**Files:**
- Modify: `include/web/WebServer.hpp` (declare `apiPriceHandler`)
- Modify: `src/web/WebServer.cpp`
- Create: `tests/web/test_price_http.cpp`
- Modify: `tests/web/CMakeLists.txt` (add the file to the existing `test_web` executable)

**Interfaces:**
- Consumes: the `StatusSnapshot` fields (Task 2), `CMD_SET_PRICE` (Task 3), and the raw-socket fixture from `tests/web/test_schedule_http.cpp` (port **19441**).
- `apiStatusHandler`: three new keys in the **fixed** `char response[4096]` format string, next to `powerValid`:
  `"\"dailyEnergyKwh\":%.3f,"`, `"\"dailyEnergyValid\":%s,"`, `"\"pricePerKwhCents\":%.1f,"`.
  Measured headroom on 2026-11-18 against the live controller: base JSON is 1358 B of 4095; the three fields add ~73 B; `diagnostics` and `backupHeater` are appended to a `std::string` afterwards. Safe, but keep new fields inside the base payload only.
- `apiPriceHandler(struct mg_connection* c, struct mg_str* body)` mirrors `apiSetDhwHandler`: 503 when `mShuttingDown` / 400 empty body / 400 when `mg_json_get_num(body, "$.priceCentsPerKwh", &cents)` fails / 422 outside `[PRICE_CENTS_MIN, PRICE_CENTS_MAX]` / push `Command{CommandType::CMD_SET_PRICE, static_cast<float>(cents)}`, wake the loop, reply 202 `{"success":true,"message":"Price queued","verified":false}`.
- Route: add a branch to the `mg_strcmp` chain in `WebServer::handleRequest` (`src/web/WebServer.cpp:144`, next to the `/api/status` branch at `:146`), POST only, 405 otherwise — same shape as `/api/set-dhw`.

- [ ] **Step 1: Write the failing tests** — `tests/web/test_price_http.cpp`:
  - `StatusCarriesDailyEnergyAndPrice`: push a snapshot with `daily_energy_kwh = 1.5`, `daily_energy_valid = true`, `price_cents_per_kwh = 27.5` → GET `/api/status` contains `"dailyEnergyKwh":1.500`, `"dailyEnergyValid":true`, `"pricePerKwhCents":27.5`; parse it as JSON to prove the 4096 buffer did not truncate.
  - `PriceSaveIsAcceptedAndQueued`: POST `{"priceCentsPerKwh":30}` → 202, queue non-empty, popped command has `type == CMD_SET_PRICE` and `float_val == 30.0f`.
  - `RejectsEmptyBody` → 400; `RejectsMissingField` (`{}`) → 400; `RejectsOutOfRange` (`501`, `-3`) → 422 and nothing queued.
  - `RejectsGetOnPrice` → 405.
- [ ] **Step 2: Run to verify RED** — `ctest --test-dir build -R '^test_web$' --output-on-failure` fails (404 on `/api/price`, missing status keys).
- [ ] **Step 3: Implement** the header declaration, handler, route branch, and the three JSON fields.
- [ ] **Step 4: Run to verify GREEN** — `ctest --test-dir build -R '^test_web$' --output-on-failure`.
- [ ] **Step 5: Commit** — `git commit -am "Expose the daily kWh estimate and a price endpoint over HTTP"`

### Task 5: Startup wiring and restart behaviour

**Files:**
- Modify: `src/main.cpp`
- Modify: `tests/tools/test_main_schedule_startup.py`
- Modify: `tests/tools/CMakeLists.txt` (raise `test_main_schedule_startup` `TIMEOUT` to 90 — the file gains a case with two boots)

**Interfaces:**
- Wiring right after the existing `setSettingsCallback` block (`src/main.cpp:394`):
  - `setPriceCentsPerKwh(config.getDouble("ui.price_per_kwh_cents", PRICE_CENTS_DEFAULT))`
  - `seedDailyEnergy(config.getDouble("state.energy_today_kwh", 0.0), config.getString("state.energy_today_date", ""))`
  - energy callback: `config.set("state.energy_today_kwh", std::to_string(kwh)); config.set("state.energy_today_date", dayKey); return config.saveToFile(config_path);` and `WINDMI_LOG_ERROR` when the save fails.
  - price callback: `config.set("ui.price_per_kwh_cents", std::to_string(cents)); return config.saveToFile(config_path);` with the same error log.
- `Config::saveToFile` writes `key = value` with spaces and doubles via `std::to_string` (6 decimals) — see `ui.dhw_target = 50.000000` in the live `~/.windmi/settings.ini`, and the existing assertion `assertIn("ui.working_mode = 3", …)`.

- [ ] **Step 1: Write the failing test** — new method on `MainScheduleStartupTest`:
  `test_daily_energy_seed_price_default_and_price_persistence`. Pre-write `self.settings` with today's local key in Python (`time.strftime("%Y-%m-%d")`), `state.energy_today_kwh = 1.2`, and **no** `ui.price_per_kwh_cents`. Boot with `self.running()`: `/api/status` → `pricePerKwhCents == 30.0`, `dailyEnergyKwh >= 1.19`, `dailyEnergyValid == True`. POST `/api/price` `{"priceCentsPerKwh": 25}` → 202; `self.wait` until `pricePerKwhCents == 25.0`; assert the settings file matches `ui\.price_per_kwh_cents\s*=\s*25(\.0+)?` (regex over lines — the file uses spaces and 6 decimals). Boot a second time in the same test and assert `pricePerKwhCents == 25.0` is loaded back.
- [ ] **Step 2: Run to verify RED** — `ctest --test-dir build -R '^test_main_schedule_startup$' --output-on-failure` fails: `dailyEnergyKwh` stays 0 (nothing seeds) and no price key appears.
- [ ] **Step 3: Implement** the four wiring lines.
- [ ] **Step 4: Run to verify GREEN** — `ctest --test-dir build -R '^test_main_schedule_startup$' --output-on-failure`.
- [ ] **Step 5: Commit** — `git commit -am "Seed, expose and persist the daily estimate from production startup"`

### Task 6: Overview items and the Diagnostics price card

**Files:**
- Modify: `static/index.html`
- Modify: `static/app.js`
- Modify: `static/style.css`
- Create: `tests/tools/test_ui_daily_energy.cjs`
- Modify: `tests/tools/test_ui_diagnostics.cjs` (turn the read-only assertion into an explicit allowlist)
- Modify: `tests/tools/CMakeLists.txt` (register `test_ui_daily_energy`)

**Interfaces:**
- Overview: two `.status-item`s after `powerOutputValue` in `.status-row-main` (`index.html:36`): `dailyEnergyValue` (label "Energy today (est.)") and `dailyCostValue` (label "Cost today (est.)").
- Diagnostics: a `<section class="status-card" id="priceCard">` before the closing `</section>`/`</div>` of the panel (`index.html`, just before `</div></div></main>`), containing `<h3>Energy price</h3>`, a note that it only affects the estimate, `priceCentsInput` (`type=number min=0 max=500 step=0.1`), `savePriceBtn` (`btn btn-small`), `priceSaveState`.
- `static/app.js`: `const API_PRICE_URL = '/api/price';`, module state `let pricePending = null;`, elements entries for the three diagnostics ids plus the two overview ids, and in `updateUI` (next to the power block at `app.js:354`):
  - energy: `status.dailyEnergyValid === true && Number.isFinite(status.dailyEnergyKwh)` → `status.dailyEnergyKwh.toFixed(2) + ' kWh'`, else `'Unavailable'`.
  - cost: same guard → `'€' + (status.dailyEnergyKwh * price / 100).toFixed(2)` where `price = Number.isFinite(status.pricePerKwhCents) ? status.pricePerKwhCents : 30`; else `'Unavailable'`.
  - input sync only when `pricePending === null && document.activeElement !== elements.priceCentsInput` → `String(price)`.
  - click handler mirrors `setDhwBtn` (`app.js:98`): validate 0–500 (`showNotification` on invalid), disable with "Saving…", `apiPost(API_PRICE_URL, { priceCentsPerKwh: value })`, set `pricePending`, clear it when `/api/status` confirms within 0.05 or after 15 s, then `priceSaveState` = "Saved ✓ (used for today's cost estimate)" or the error text.
- `static/style.css`: one small `.price-note` rule (muted, small). Reuse the existing card/button/`input` classes.

- [ ] **Step 1: Amend the guardrail assertion first** in `tests/tools/test_ui_diagnostics.cjs` — it currently asserts the whole diagnostics slice has no controls. Replace it with an allowlist that strips the price card and asserts everything else is still read-only, plus that the card exists:

```js
const priceCard = section.slice(section.indexOf('id="priceCard"'),
    section.indexOf('</section>', section.indexOf('id="priceCard"')) + '</section>'.length);
assert(priceCard.length > 0, 'diagnostics must declare the energy price card');
assert.match(priceCard, /id="priceCentsInput"/);
assert.match(priceCard, /id="savePriceBtn"/);
assert(!/<(?:button|input|select|form)\b/i.test(section.replace(priceCard, '')),
    'diagnostics must have no write controls other than the energy price setting');
```
- [ ] **Step 2: Write the failing UI test** — `tests/tools/test_ui_daily_energy.cjs`, modelled on `test_ui_power.cjs` (real ids from `index.html`, `init();` stripped, `updateUI(sample)` invoked through the vm):
  - `{dailyEnergyValid: true, dailyEnergyKwh: 4.317, pricePerKwhCents: 30}` → `'4.32 kWh'`, `'€1.30'`.
  - `{dailyEnergyValid: true, dailyEnergyKwh: 2, pricePerKwhCents: 28.5}` → `'€0.57'`.
  - `{dailyEnergyValid: false}` → both `'Unavailable'`.
  - `{dailyEnergyValid: true, dailyEnergyKwh: undefined, pricePerKwhCents: undefined}` → both `'Unavailable'`, and neither text contains `NaN` or `undefined`.
  - `{dailyEnergyValid: true, dailyEnergyKwh: 1, pricePerKwhCents: undefined}` → cost uses the 30-cent fallback (`'€0.30'`).
  - input sync: after `updateUI({pricePerKwhCents: 27.5, …})` → `nodes.priceCentsInput.value === '27.5'`.
- [ ] **Step 3: Run to verify RED** — `node tests/tools/test_ui_daily_energy.cjs` fails (ids missing) and `node tests/tools/test_ui_diagnostics.cjs` fails on the new assertions (no card yet).
- [ ] **Step 4: Implement** the HTML, `app.js` and `style.css` changes; register the test in `tests/tools/CMakeLists.txt`.
- [ ] **Step 5: Run to verify GREEN** — `node tests/tools/test_ui_daily_energy.cjs`, `node tests/tools/test_ui_diagnostics.cjs`, `node tests/tools/test_ui_power.cjs`, `python3 tools/check_ui_refs.py`, `node --check static/app.js`.
- [ ] **Step 6: Offline browser check** — headless Chrome against the real page with a mocked `/api/status` (same offline-fixture approach as the schedule-default browser script), at 1280 px and 390 px: both Overview items render, the price card renders, no console errors, no horizontal overflow, and an out-of-range Save shows the validation path. Save artifacts under `/var/tmp/wpomp-daily-energy-browser-20261118/`.
- [ ] **Step 7: Commit** — `git commit -am "Show daily energy and cost estimates and let the owner set the tariff"`

### Task 7: Docs, full verification, merge

**Files:**
- Modify: `README.md` (one line in the feature blurb: daily kWh + cost estimates, tariff on Diagnostics)
- Modify: `docs/diagnostics.md` (price card section: the only editable control on that tab, affects the cost estimate only, never hardware)

- [ ] **Step 1: Update the two docs**; run `git diff --check`.
- [ ] **Step 2: Full suite** — `ctest --test-dir build --output-on-failure` (expect **22/22**: the 21 existing suites plus `test_ui_daily_energy`; `test_daily_energy.cpp` runs inside `test_core`). Then `ctest --test-dir build --repeat until-fail:2 --output-on-failure` for flakiness, plus `python3 tools/check_ui_refs.py` and `node --check static/app.js`.
- [ ] **Step 3: Sanitizers** — configure the ASan+UBSan tree (`-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=…`, e.g. `/var/tmp/wpomp-daily-asan-20261118`) and run the full ctest with `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`.
- [ ] **Step 4: Merge** — `git merge feature/daily-energy-cost` from the worktree that hosts `main`, then run the full suite once on `main`.
- [ ] **Step 5: Record evidence** in the ledger below. **Stop here** — deployment is a separate authorized controlled restart.

## Execution ledger

_(filled during execution: RED/GREEN evidence per task, deviations, rulings)_
