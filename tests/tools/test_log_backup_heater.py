import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('backup_logger', ROOT / 'tools/log_backup_heater.py')
logger = importlib.util.module_from_spec(spec)
spec.loader.exec_module(logger)


def sample(sequence=1, instance='unit-a'):
    return {'deviceOnline': True, 'statusSequence': sequence, 'scheduleInstanceId': instance,
            'workingMode': 3, 'runningStatus': 'heat',
            'backupHeater': {'status': 'unvalidated', 'measurementValid': True, 'reason': 'ok',
                             'beforeC': 31, 'afterC': 33.6, 'deltaC': 2.6, 'flowM3h': 1.07,
                             'estimatedAddedHeatW': 3234.8, 'sampleSpanMs': 2000},
            'diagnostics': {'registers': {'backupType': {'raw': 0, 'ageSeconds': 20, 'maxAgeSeconds': 480},
                                         'backupWarmup': {'raw': None, 'ageSeconds': None, 'maxAgeSeconds': 480}}}}


class LoggerTest(unittest.TestCase):
    def test_flattens_signed_evidence_and_distinguishes_zero_missing_stale(self):
        value = sample()
        value['backupHeater']['estimatedAddedHeatW'] = -1162.8
        row = logger.flatten_status(value, 'panel36=0')
        self.assertEqual(row['estimatedAddedHeatW'], -1162.8)
        self.assertEqual(row['flowM3h'], 1.07)
        self.assertEqual(row['backupTypeRaw'], 0)
        self.assertIsNone(row['backupWarmupRaw'])
        self.assertTrue(row['backupTypeFresh'])
        self.assertEqual(row['label'], 'panel36=0')
        value['diagnostics']['registers']['backupType']['ageSeconds'] = 481
        self.assertFalse(logger.flatten_status(value)['backupTypeFresh'])

    def test_invalid_offline_and_legacy_are_not_heater_off_samples(self):
        value = sample()
        value['backupHeater']['measurementValid'] = False
        value['backupHeater']['reason'] = 'no_flow'
        self.assertIsNone(logger.flatten_status(value)['estimatedAddedHeatW'])
        value['deviceOnline'] = False
        row = logger.flatten_status(value)
        self.assertEqual(row['reason'], 'offline')
        self.assertIsNone(row['beforeC'])
        legacy = logger.flatten_status({'deviceOnline': True, 'statusSequence': 1})
        self.assertEqual(legacy['reason'], 'backend_unavailable')
        self.assertFalse(legacy['measurementValid'])
        self.assertNotIn('heaterOn', legacy)

    def test_rejects_nonfinite_or_missing_estimate_despite_valid_flag(self):
        value = sample()
        value['backupHeater']['estimatedAddedHeatW'] = float('nan')
        row = logger.flatten_status(value)
        self.assertFalse(row['measurementValid'])
        self.assertIsNone(row['estimatedAddedHeatW'])

    def test_deduplicates_per_instance_and_records_gaps_without_fake_zeros(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'samples.csv'
            with logger.Recorder(path, 'observation') as recorder:
                self.assertTrue(recorder.record_status(sample()))
                self.assertFalse(recorder.record_status(sample()))
                self.assertTrue(recorder.record_status(sample(2)))
                self.assertTrue(recorder.record_status(sample(2, 'unit-b')))
                recorder.record_gap('HTTP unavailable')
            with path.open() as stream:
                rows = list(csv.DictReader(stream))
            self.assertEqual(len(rows), 4)
            self.assertEqual(rows[0]['snapshotProgressObserved'], 'False')
            self.assertEqual(rows[1]['snapshotProgressObserved'], 'True')
            self.assertEqual(rows[2]['snapshotProgressObserved'], 'False')
            self.assertEqual(rows[-1]['kind'], 'gap')
            self.assertEqual(rows[-1]['estimatedAddedHeatW'], '')
            self.assertEqual(rows[-1]['reason'], 'api_error')

    def test_append_does_not_duplicate_header_and_refuses_wrong_schema_or_partial_tail(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'samples.csv'
            with logger.Recorder(path) as recorder:
                recorder.record_status(sample())
            with logger.Recorder(path) as recorder:
                recorder.record_status(sample(2))
            with path.open() as stream:
                self.assertEqual(len(list(csv.DictReader(stream))), 2)
            path.write_text('wrong,columns\n')
            with self.assertRaises(ValueError):
                logger.Recorder(path)
            self.assertEqual(path.read_text(), 'wrong,columns\n')
            path.write_text(','.join(logger.FIELDS) + '\npartial')
            with self.assertRaises(ValueError):
                logger.Recorder(path)

    def test_fetch_is_get_only_and_rejects_other_api_paths(self):
        class Response:
            def __enter__(self): return self
            def __exit__(self, *_): pass
            def read(self): return json.dumps(sample()).encode()
        with patch.object(logger, 'urlopen', return_value=Response()) as mocked:
            logger.fetch_status('http://localhost:8080')
            request = mocked.call_args.args[0]
            self.assertEqual(request.get_method(), 'GET')
            self.assertEqual(request.full_url, 'http://localhost:8080/api/status')
        with self.assertRaises(ValueError):
            logger.status_url('http://localhost:8080/api/set-mode')
        with self.assertRaises(ValueError):
            logger.status_url('tcp://localhost:502')


if __name__ == '__main__':
    unittest.main()
