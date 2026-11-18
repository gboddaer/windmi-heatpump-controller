// Read-only view: consumes the existing status refresh; no fetches or commands.
const DiagnosticsUI = (() => {
    let lastSequence;
    let sequenceChangedAt = 0;
    let observedAdvance = false;
    let lastStatus = {};
    const missing = 'Unavailable';
    const hex = n => '0x' + n.toString(16).toUpperCase().padStart(4, '0');
    const number = Number.isFinite;
    const signed = raw => raw > 32767 ? raw - 65536 : raw;
    const alarmNames = [
        ['Water flow switch fault', 'ODU / hydraulic PCB communication fault',
            'Leaving-water sensor after electric heater fault', 'Reserved refrigerant outlet sensor bit',
            'Reserved refrigerant inlet sensor bit', 'Outdoor-unit fault', 'DHW tank sensor fault',
            'Entering-water sensor fault', 'Leaving-water sensor fault', 'Reserved controller communication bit',
            'Bi-zone sensor fault', 'Auxiliary-heat leaving-water sensor fault'],
        [null, 'Excessive entering/leaving water temperature difference', 'Insufficient water flow',
            'Abnormal entering/leaving water temperature difference', null, null,
            'Feedback protection (manual wording unclear)'],
        ['Condenser sensor fault', 'Discharge-temperature sensor fault', null,
            'Refrigerant outlet high-temperature protection', 'Repeated IPM protection',
            'Abnormal AC voltage', 'Outdoor-air temperature sensor fault', 'Overcurrent protection',
            'IPM protection', 'Repeated discharge-temperature protection', 'Repeated IPM overtemperature',
            'EEPROM alarm', 'High-pressure protection', 'Repeated low-pressure protection',
            'Repeated DC fan motor alarm', 'Condenser high-temperature protection'],
        ['Reserved IDU / ODU communication bit', 'Outdoor fan motor error', 'IPM overtemperature protection',
            'Reserved IDU-count bit', 'Repeated overcurrent protection', 'Discharge sensor fault',
            'Refrigerant pipe sensor fault', 'Low-pressure protection']
    ];
    const ipmReasons = {0: 'No reason reported', 1: 'DC voltage low protection',
        2: 'DC voltage high protection', 4: 'MCE / synchronization error',
        5: 'Compressor speed fault', 7: 'Phase error', 8: 'Compressor speed-change fault',
        9: 'Incorrect compressor speed', 10: 'IPM error'};
    function set(id, text, warning = false) {
        const node = document.getElementById(id);
        node.textContent = text;
        node.dataset.state = warning ? 'warning' : 'normal';
    }
    function update(status, apiError = false) {
        lastStatus = status;
        const now = Date.now();
        const hasSequence = Number.isInteger(status.statusSequence);
        if (hasSequence && status.statusSequence !== lastSequence) {
            observedAdvance = lastSequence !== undefined;
            lastSequence = status.statusSequence;
            sequenceChangedAt = now;
        }
        const sequenceAge = hasSequence ? Math.max(0, (now - sequenceChangedAt) / 1000) : 0;
        const stale = hasSequence && sequenceAge > 90;
        const live = status.deviceOnline === true && !apiError && !stale;
        const registers = status.diagnostics && status.diagnostics.registers;
        const connection = apiError ? 'API request failed' : status.deviceOnline ? 'Online' : 'Offline';
        set('diagConnection', connection, !live);
        set('diagFreshness', apiError ? 'Unavailable (API error)' : !hasSequence ? 'Freshness not reported' :
            stale ? 'Stale — no snapshot advance for ' + Math.floor(sequenceAge) + ' s' :
                observedAdvance ? 'Snapshots progressing · last advance ' + Math.floor(sequenceAge) + ' s ago' :
                    'Snapshot observed · awaiting next advance (' + Math.floor(sequenceAge) + ' s)', stale || apiError);
        set('diagSequence', hasSequence ? String(status.statusSequence) : missing);
        set('diagTelemetryStatus', registers ? 'Read-only controller samples; register validity and ages available.' :
            'Existing telemetry only — extended registers and hardware alarms need the backend update.');

        // Legacy values are explicitly qualified; failed new-backend reads must
        // never silently fall back to legacy zero-filled fields.
        function get(key, legacy) {
            if (!live) return {state: stale && !apiError && status.deviceOnline ? 'stale' : 'missing'};
            if (!registers) return number(legacy) ? {state: 'valid', raw: legacy, legacy: true} : {state: 'missing'};
            const r = registers[key];
            if (!r || !Number.isInteger(r.raw) || r.raw < 0 || r.raw > 65535 ||
                !number(r.ageSeconds) || r.ageSeconds < 0) return {state: 'missing'};
            const age = r.ageSeconds + sequenceAge;
            const maxAge = number(r.maxAgeSeconds) ? r.maxAgeSeconds : 90;
            return {...r, age, state: age > maxAge ? 'stale' : 'valid'};
        }
        const valid = r => r.state === 'valid';
        function fmt(r, fn = String) {
            if (!valid(r)) return r.state === 'stale' ? 'Stale' : missing;
            return fn(r.raw) + (r.legacy ? ' (validity not reported)' : '');
        }
        const plain = value => live && value !== undefined && value !== null ? String(value) : missing;
        const yesNo = value => live && typeof value === 'boolean' ? value ? 'Yes' : 'No' : missing;
        const temp = r => fmt(r, n => (signed(n) / 10).toFixed(1) + ' °C');
        const fallbackTemp = value => number(value) ? Math.round(value * 10) : undefined;
        const modes = ['Off', 'DHW only', 'Heating only', 'DHW + Heating'];
        set('diagWorkingMode', live ? modes[status.workingMode] || 'Unknown' : missing);
        set('diagRequested', plain(status.requestedDeviceMode));
        set('diagSetMode', plain(status.mode));
        set('diagOperation', plain(status.runningStatus));
        set('diagDhwDemand', live ? (status.dhwStateKnown ? yesNo(status.dhwCalling) : 'Unknown') + ' · ' +
            (status.dhwState || 'unknown') : missing);
        set('diagActuating', yesNo(status.unitActuating));
        set('diagWriteAge', live && number(status.secondsSinceModeWrite) ? status.secondsSinceModeWrite + ' s' : missing);
        const schedule = typeof status.scheduleActive === 'boolean' ? status.scheduleActive ?
            status.schedulePaused ? 'Paused' : 'Active' : 'Disabled' : 'Unknown';
        set('diagSchedule', live ? schedule + ' / ' + (['Away', 'Sleep', 'Home'][status.occupancyMode] || 'Unknown') : missing);
        set('diagTankReads', plain(status.tankReadFailures), live && status.tankReadFailures > 0);
        set('diagWatchdog', !live || typeof status.dhwSensorFaultSuspected !== 'boolean' ? missing :
            status.dhwSensorFaultSuspected ? 'Prolonged DHW demand with low net tank rise — check hot-water use and charging' :
                'No progress warning reported', live && status.dhwSensorFaultSuspected);

        set('diagCompressor', fmt(get('compressorSpeed', status.compressorFrequency > 0 ? status.compressorFrequency : undefined), n => n + ' raw (scale unverified)'));
        set('diagCompressorTarget', fmt(get('compressorTarget'), n => (n / 10).toFixed(1) + ' Hz'));
        set('diagPump', fmt(get('pumpCommand'), n => n + ' / 100 (command)'));
        set('diagFanUpper', fmt(get('fanUpper')) + ' / ' + fmt(get('fanUpperTarget')));
        set('diagFanLower', fmt(get('fanLower')) + ' / ' + fmt(get('fanLowerTarget')));
        set('diagCompressorHours', fmt(get('compressorRuntime', status.compressorRuntimeHours), n => n + ' h (reported counter)'));
        set('diagPumpHours', fmt(get('pumpRuntime', status.pumpRuntimeHours), n => n + ' h (reported counter)'));
        set('diagValve', fmt(get('dhwValve', [0, 1].includes(status.dhwValveStatus) ? status.dhwValveStatus : undefined), n =>
            n === 0 ? '0 · Heating route (reported)' : n === 1 ? '1 · DHW route (reported)' : n + ' · Unknown routing'));
        const outputs = get('oduOutputs');
        const outputBit = bit => fmt(outputs, n => (n & (1 << bit) ? 'On' : 'Off') + ' (output command)');
        set('diagFourWay', outputBit(6));
        set('diagSv1', outputBit(5));
        set('diagSv2', outputBit(7));
        set('diagExv', fmt(get('exvCommand'), n => n + ' raw (scale unverified)'));
        set('diagOduOutputs', fmt(outputs, hex));
        set('diagInputs', fmt(get('oduInputs', status.oduInputStatus), n => 'LP=' + (n & 1) + ', HP=' + ((n >> 1) & 1) + ' · ' + hex(n)));
        const ipm = get('ipmReason');
        set('diagIpmReason', fmt(ipm, n => hex(n) + ' · ' + (ipmReasons[n] || 'Undocumented reason') + ' (not an active alarm by itself)'));

        const thermalFallback = status.heatOutputValid === true;
        const lwt = get('leavingWater', thermalFallback ? fallbackTemp(status.leavingWaterTemperature) : undefined);
        const ewt = get('enteringWater', thermalFallback ? fallbackTemp(status.enteringWaterTemperature) : undefined);
        set('diagLwt', temp(lwt));
        set('diagEwt', temp(ewt));
        set('diagDeltaT', valid(lwt) && valid(ewt) ? ((signed(lwt.raw) - signed(ewt.raw)) / 10).toFixed(1) + ' °C' :
            lwt.state === 'stale' || ewt.state === 'stale' ? 'Stale' : missing);
        set('diagFlow', fmt(get('waterFlow', thermalFallback && number(status.waterFlow) ? status.waterFlow * 100 : undefined), n => (n / 100).toFixed(2) + ' m³/h'));
        set('diagTank', temp(get('tankTemperature', status.dhwStateKnown && status.tankReadFailures === 0 ? fallbackTemp(status.dhwTemperature) : undefined)));
        set('diagDhwTargets', temp(get('dhwTarget', fallbackTemp(status.dhwTarget))) + ' / ' +
            temp(get('ecoDhwTarget', status.dhwEcoTarget > 0 ? fallbackTemp(status.dhwEcoTarget) : undefined)));
        set('diagEffectiveTarget', live && status.effectiveDhwTarget > 0 ? status.effectiveDhwTarget.toFixed(1) + ' °C' : missing);
        set('diagHeatingTarget', temp(get('heatingTarget', fallbackTemp(status.heatingTarget))));
        set('diagWaterPoint', temp(get('waterControlPoint', status.waterControlPoint > 0 ? fallbackTemp(status.waterControlPoint) : undefined)));
        set('diagDhwProfile', fmt(get('dhwProfile', status.dhwModeStatus >= 0 ? status.dhwModeStatus : undefined), n =>
            n + ' · ' + (['Economic', 'Anti-legionella', 'Regular'][n] || 'Unknown')));
        // Raw registers establish availability; the backend owns electrical
        // decoding so future calibration changes cannot leave this tab behind.
        const electrical = (key, value, unit, decimals, legacyAllowed) => number(value) ?
            fmt(get(key, legacyAllowed ? value : undefined), () => value.toFixed(decimals) + ' ' + unit) : missing;
        set('diagAc', electrical('acVoltage', status.acVoltage, 'V', 0, status.powerValid) + ' / ' +
            electrical('acCurrent', status.acCurrent, 'A', 1, status.powerValid));
        set('diagDc', electrical('dcVoltage', status.dcVoltage, 'V', 0, status.dcVoltage > 0) + ' / ' +
            electrical('dcCurrent', status.dcCurrent, 'A', 1, true));
        set('diagElectric', live && status.powerValid && number(status.acPowerW) ? Math.round(status.acPowerW) + ' W' : missing);
        set('diagThermal', live && status.heatOutputValid && number(status.heatOutputW) ? Math.round(status.heatOutputW) + ' W' : missing);
        set('diagCycleCop', [status.heatingCycleCop, status.dhwCycleCop].map(n => number(n) ? n.toFixed(2) : 'Awaiting cycle').join(' / '));
        set('diagUnitRating', fmt(get('unitCapacity', status.unitCapacityKw > 0 ? status.unitCapacityKw : undefined), n => n + ' kW'));
        set('diagCapacityRaw', fmt(get('capacityOutput', status.actualCapacityOutput)));

        const backup = status.backupHeater;
        const evidence = (value, format) => !live ? (stale && !apiError ? 'Stale' : missing) :
            number(value) ? format(value) : missing;
        set('diagBackupState', backup ? 'Unknown — validation not complete' : 'Unknown — backend update required');
        set('diagBackupBefore', evidence(backup && backup.beforeC, n => n.toFixed(1) + ' °C'));
        set('diagBackupAfter', evidence(backup && backup.afterC, n => n.toFixed(1) + ' °C'));
        set('diagBackupDelta', evidence(backup && backup.deltaC, n => n.toFixed(1) + ' °C'));
        set('diagBackupHeat', evidence(backup && backup.measurementValid === true ? backup.estimatedAddedHeatW : null,
            n => Math.round(n) + ' W'));
        set('diagBackupSpan', evidence(backup && backup.sampleSpanMs, n => n + ' ms'));
        const reasons = {ok: 'Paired readings usable; heater state unvalidated', no_flow: 'No flow — heat estimate unavailable',
            offline: 'Device offline', missing_temperature: 'A paired temperature is unavailable',
            invalid_temperature: 'Invalid temperature reading', missing_flow: 'Flow reading unavailable',
            invalid_flow: 'Invalid flow reading', sample_skew: 'Source reads more than 5 seconds apart',
            stale_sample: 'Source readings too old', sample_time: 'Invalid source timing'};
        set('diagBackupValidity', !live ? (stale && !apiError ? 'Stale snapshot' : missing) :
            backup ? reasons[backup.reason] || 'Unknown measurement condition' : 'Backend update required');
        const backupTypes = ['Main-loop + DHW + boiler', 'Main-loop + DHW', 'DHW + boiler', 'Main-loop + boiler',
            'DHW only', 'Boiler only', 'Main-loop EH only', 'No auxiliary heater configured'];
        set('diagBackupType', fmt(get('backupType'), n => n + ' · ' + (backupTypes[n] || 'Unknown configuration')));
        set('diagBackupWarmup', fmt(get('backupWarmup'), n => n + ' raw (scale/range unverified)'));
        set('diagBackupDeltaSetting', temp(get('backupDelta')));
        set('diagBackupOatSetting', temp(get('backupOatThreshold')));
        set('diagCompressorMinOat', temp(get('minHeatingOat')));

        let coverage = 0, activeBits = 0;
        const alarms = [];
        for (let bank = 0; bank < 4; bank++) {
            const r = get('alarm' + (bank + 1));
            if (!valid(r)) continue;
            coverage++;
            for (let bit = 0; bit < 16; bit++) {
                if (!(r.raw & (1 << bit))) continue;
                activeBits++;
                alarms.push('Bank ' + (bank + 1) + ' bit ' + bit + ': ' +
                    (alarmNames[bank][bit] || 'Reserved / undocumented bit'));
            }
        }
        const coverageText = coverage + '/4 fresh banks';
        set('diagHardwareAlarmStatus', activeBits ? activeBits + ' alarm bits set; ' + coverageText +
            (coverage < 4 ? ' — status incomplete' : '') : coverage === 4 ?
                'No alarm bits set (4/4 banks read)' : coverageText + ' — status incomplete', activeBits > 0 || coverage < 4);
        set('diagHardwareAlarms', alarms.length ? alarms.join('\n') : coverage === 4 ?
            'No set bits in the sampled hardware alarm banks.' : 'Hardware alarm telemetry unavailable or stale; absence of faults is not established.', activeBits > 0);

        const rows = [];
        if (registers) {
            for (const [key, spec] of Object.entries(registers)) {
                const r = get(key);
                const row = document.createElement('tr');
                const cells = [key, Number.isInteger(spec.address) ? hex(spec.address) : missing,
                    valid(r) ? r.raw + ' / ' + hex(r.raw) : fmt(r),
                    live && number(r.age) ? Math.floor(r.age) + ' s' + (r.state === 'stale' ? ' (stale)' : '') : missing];
                for (const text of cells) {
                    const cell = document.createElement('td');
                    cell.textContent = text;
                    row.appendChild(cell);
                }
                rows.push(row);
            }
        }
        document.getElementById('diagRawRows').replaceChildren(...rows);
    }
    return {update, connectionError() { update(lastStatus, true); }};
})();
