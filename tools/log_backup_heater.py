#!/usr/bin/env python3
"""GET-only backup-heater validation CSV. No Modbus, commands, state or runtime inference."""
import argparse
import csv
from datetime import datetime, timezone
import json
import math
from pathlib import Path
import time
from urllib.parse import urlsplit, urlunsplit
from urllib.request import Request, urlopen

PARAMETERS = ('backupType', 'backupWarmup', 'backupDelta', 'backupOatThreshold', 'minHeatingOat')
MEASUREMENTS = ('beforeC', 'afterC', 'deltaC', 'flowM3h', 'estimatedAddedHeatW', 'sampleSpanMs')
FIELDS = ('tsUTC', 'kind', 'controllerInstance', 'statusSequence', 'snapshotProgressObserved',
          'deviceOnline', 'workingMode', 'mode', 'runningStatus', 'outdoorC', 'compressorRaw', 'dhwC',
          'validationStatus', 'measurementValid', 'reason') + MEASUREMENTS + tuple(
              name + suffix for name in PARAMETERS for suffix in ('Raw', 'AgeS', 'Fresh')) + ('error', 'label')


def timestamp():
    return datetime.now(timezone.utc).isoformat(timespec='seconds')


def numeric(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def status_url(base):
    parts = urlsplit(base)
    if parts.scheme not in ('http', 'https') or not parts.netloc or parts.path.rstrip('/') not in ('', '/api/status') or parts.query or parts.fragment:
        raise ValueError('Use an HTTP(S) API base URL or its /api/status endpoint, never a Modbus address or command path')
    return urlunsplit((parts.scheme, parts.netloc, '/api/status', '', ''))


def fetch_status(base):
    request = Request(status_url(base), headers={'Accept': 'application/json'}, method='GET')
    with urlopen(request, timeout=5) as response:
        result = json.loads(response.read())
    if not isinstance(result, dict):
        raise ValueError('Status response must be a JSON object')
    return result


def flatten_status(payload, label=''):
    online = payload.get('deviceOnline') is True
    backup = payload.get('backupHeater')
    supported = isinstance(backup, dict) and backup.get('status') == 'unvalidated'
    backup = backup if supported else {}
    valid = online and supported and backup.get('measurementValid') is True and numeric(backup.get('estimatedAddedHeatW'))
    reason = 'offline' if not online else 'backend_unavailable' if not supported else backup.get('reason', 'invalid_payload')
    if supported and online and backup.get('measurementValid') is True and not valid:
        reason = 'invalid_payload'
    row = {'tsUTC': timestamp(), 'kind': 'sample', 'controllerInstance': payload.get('scheduleInstanceId', ''),
           'statusSequence': payload.get('statusSequence'), 'deviceOnline': online,
           'workingMode': payload.get('workingMode'), 'mode': payload.get('mode'), 'runningStatus': payload.get('runningStatus'),
           'outdoorC': payload.get('outdoorTemperature'), 'compressorRaw': payload.get('compressorFrequency'),
           'dhwC': payload.get('dhwTemperature'), 'validationStatus': 'unvalidated' if supported else 'unknown',
           'measurementValid': valid, 'reason': reason, 'error': '', 'label': label}
    for key in MEASUREMENTS:
        value = backup.get(key)
        row[key] = value if online and numeric(value) and (key != 'estimatedAddedHeatW' or valid) else None
    diagnostics = payload.get('diagnostics') or {}
    registers = diagnostics.get('registers') or {}
    for name in PARAMETERS:
        reading = registers.get(name) or {}
        raw, age, maximum = reading.get('raw'), reading.get('ageSeconds'), reading.get('maxAgeSeconds')
        available = isinstance(raw, int) and not isinstance(raw, bool) and 0 <= raw <= 65535
        row[name + 'Raw'] = raw if available else None
        row[name + 'AgeS'] = age if numeric(age) and age >= 0 else None
        row[name + 'Fresh'] = (online and age <= maximum) if available and numeric(age) and age >= 0 and numeric(maximum) else None
    return row


class Recorder:
    def __init__(self, path, label=''):
        self.path = Path(path)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        existing = self.path.exists() and self.path.stat().st_size > 0
        if existing:
            with self.path.open(newline='', encoding='utf-8') as stream:
                if next(csv.reader(stream), None) != list(FIELDS):
                    raise ValueError('Existing CSV has a different schema; choose a new output file')
            with self.path.open('rb') as stream:
                stream.seek(-1, 2)
                if stream.read(1) != b'\n':
                    raise ValueError('Existing CSV has an incomplete tail; preserve it and choose a new file')
        self.stream = self.path.open('a', newline='', encoding='utf-8')
        self.writer = csv.DictWriter(self.stream, fieldnames=FIELDS)
        if not existing:
            self.writer.writeheader()
            self.stream.flush()
        self.label = label
        self.last_token = None
        self.samples = self.gaps = 0

    def record_status(self, payload):
        sequence, instance = payload.get('statusSequence'), payload.get('scheduleInstanceId', '')
        token = (str(instance), sequence) if isinstance(sequence, int) and not isinstance(sequence, bool) and sequence >= 0 else None
        if token is not None and token == self.last_token:
            return False
        row = flatten_status(payload, self.label)
        row['snapshotProgressObserved'] = bool(token and self.last_token and token[0] == self.last_token[0] and token[1] > self.last_token[1])
        self.writer.writerow(row)
        self.stream.flush()
        self.last_token = token
        self.samples += 1
        return True

    def record_gap(self, error):
        self.writer.writerow({'tsUTC': timestamp(), 'kind': 'gap', 'snapshotProgressObserved': False,
                              'measurementValid': False, 'reason': 'api_error', 'error': str(error), 'label': self.label})
        self.stream.flush()
        self.last_token = None
        self.gaps += 1

    def close(self):
        self.stream.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='http://localhost:8080', help='Controller API base URL')
    parser.add_argument('--output', type=Path, default=Path('backup-heater-validation.csv'))
    parser.add_argument('--interval', type=float, default=5, help='HTTP sampling seconds; repeated status sequences are skipped')
    parser.add_argument('--minutes', type=float, default=60, help='Duration; 0 runs until interrupted')
    parser.add_argument('--label', default='', help='Operator observation, e.g. panel36=0; not a device command')
    args = parser.parse_args()
    if not math.isfinite(args.interval) or args.interval <= 0 or not math.isfinite(args.minutes) or args.minutes < 0:
        parser.error('Interval must be positive and duration nonnegative, both finite')
    try:
        url = status_url(args.url)
    except ValueError as error:
        parser.error(str(error))
    deadline = time.monotonic() + args.minutes * 60 if args.minutes else float('inf')
    with Recorder(args.output, args.label) as recorder:
        try:
            while time.monotonic() < deadline:
                try:
                    payload = fetch_status(url)
                except (OSError, ValueError) as error:
                    recorder.record_gap(error)
                else:
                    recorder.record_status(payload)
                time.sleep(min(args.interval, max(0, deadline - time.monotonic())))
        except KeyboardInterrupt:
            pass
        print(f'{recorder.samples} status samples, {recorder.gaps} API gaps appended to {args.output}; no heater state/runtime inferred')


if __name__ == '__main__':
    main()
