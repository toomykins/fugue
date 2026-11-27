import json
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from capture import capture_sequence


class Clock:
    now = 0

    def monotonic_ns(self): return self.now
    def sleep(self, seconds): self.now += round(seconds * 1_000_000_000)


class Image:
    def save(self, path): Path(path).write_bytes(b'raw observation')


class CaptureTest(unittest.TestCase):
    def record(self, directory, clock, grab, duration=3, interval=1, trigger=None):
        with patch('capture.time.monotonic_ns', clock.monotonic_ns), patch('capture.time.sleep', clock.sleep):
            return capture_sequence(directory, ('stock', 'client'), grab, duration, interval, {}, trigger)

    def test_each_subject_has_actual_sequential_capture_bounds(self):
        clock = Clock()
        def grab(subject):
            clock.sleep(.2)
            return Image(), {'subject': subject}
        with tempfile.TemporaryDirectory() as directory:
            record = self.record(directory, clock, grab)
            self.assertTrue(record['complete'])
            self.assertEqual([f['started_ns'] for f in record['frames']],
                             [0, 200_000_000, 1_000_000_000, 1_200_000_000, 2_000_000_000, 2_200_000_000])
            self.assertTrue(all(f['finished_ns'] - f['started_ns'] == 200_000_000 for f in record['frames']))
            self.assertTrue(all((Path(directory) / f['image']).exists() for f in record['frames']))

    def test_slow_grabs_report_missed_samples_without_fabricating_timestamps(self):
        clock = Clock()
        def grab(subject):
            clock.sleep(1.4)
            return Image(), {}
        with tempfile.TemporaryDirectory() as directory:
            record = self.record(directory, clock, grab, duration=5)
            self.assertEqual([f['index'] for f in record['frames']], [0, 0, 2, 2])
            self.assertEqual(record['missed'], [{'first_index': 1, 'count': 1}, {'first_index': 3, 'count': 2}])
            self.assertEqual(record['frames'][2]['requested_ns'], 2_000_000_000)
            self.assertEqual(record['frames'][2]['started_ns'], 2_800_000_000)

    def test_failed_capture_retains_earlier_pixels_and_failure_bounds(self):
        clock = Clock()
        def grab(subject):
            clock.sleep(.2)
            if subject == 'client': raise RuntimeError('capture failed')
            return Image(), {}
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(RuntimeError, 'capture failed'):
                self.record(directory, clock, grab)
            record = json.loads(Path(directory, 'capture.json').read_text())
            self.assertFalse(record['complete'])
            self.assertEqual(record['error'], 'RuntimeError: capture failed')
            self.assertTrue(Path(directory, 'stock/000000.png').exists())
            self.assertEqual(record['frames'][1]['finished_ns'], 400_000_000)
            self.assertNotIn('image', record['frames'][1])

    def test_trigger_has_its_own_bounds_before_sampling(self):
        clock = Clock()
        def trigger():
            clock.sleep(.1)
            return {'token': 'fresh'}
        with tempfile.TemporaryDirectory() as directory:
            record = self.record(directory, clock, lambda subject: (Image(), {}), trigger=trigger)
            self.assertEqual(record['trigger'], {'started_ns': 0, 'finished_ns': 100_000_000, 'token': 'fresh'})
            self.assertEqual(record['frames'][0]['started_ns'], 100_000_000)

    def test_failed_image_write_is_not_published_as_an_observation(self):
        class BrokenImage:
            def save(self, path):
                Path(path).write_bytes(b'partial')
                raise OSError('write failed')
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(OSError, 'write failed'):
                self.record(directory, Clock(), lambda subject: (BrokenImage(), {}))
            record = json.loads(Path(directory, 'capture.json').read_text())
            self.assertFalse(record['complete'])
            self.assertNotIn('image', record['frames'][0])
            self.assertTrue(Path(directory, 'stock/000000.png').exists())


if __name__ == '__main__': unittest.main()
