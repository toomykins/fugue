"""Timed raw observations, independent of a world's actions and expectations."""
import datetime
import json
import math
import time
from pathlib import Path


def write_record(directory, record):
    Path(directory, 'capture.json').write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')


def capture_sequence(directory, subjects, grab, duration, interval, record, trigger=None):
    """Grab subjects sequentially on a shared schedule; each image has its own capture bounds.

    grab(subject) returns (image, geometry). These timestamps bound the capture call,
    not the frame's presentation or the world's event time. Delayed samples are skipped,
    never represented as frames captured at their requested time.
    """
    if not all(math.isfinite(v) and v > 0 for v in (duration, interval)):
        raise ValueError('duration and interval must be finite positive seconds')
    period = int(interval * 1_000_000_000)
    span = int(duration * 1_000_000_000)
    if not period or not span:
        raise ValueError('duration and interval must be at least one nanosecond')
    directory = Path(directory)
    for subject in subjects:
        (directory / subject).mkdir(parents=True, exist_ok=True)
    origin = time.monotonic_ns()
    record.update(complete=False, captured_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
                  monotonic_origin_ns=origin, duration_ns=span, interval_ns=period, frames=[], missed=[])
    record['timing'] = 'Capture-call bounds only; sequential subjects are not simultaneous; presentation time is unknown'
    try:
        if trigger is not None:
            record['trigger'] = {'started_ns': time.monotonic_ns() - origin}
            record['trigger'].update(trigger())
            record['trigger']['finished_ns'] = time.monotonic_ns() - origin
        index = 0
        count = (span + period - 1) // period
        while index < count:
            elapsed = time.monotonic_ns() - origin
            missed = min(count - index, max(0, (elapsed - index * period) // period))
            if missed:
                record['missed'].append({'first_index': index, 'count': missed})
                index += missed
            if index >= count:
                break
            delay = origin + index * period - time.monotonic_ns()
            if delay > 0:
                time.sleep(delay / 1_000_000_000)
            for subject in subjects:
                frame = {'subject': subject, 'index': index, 'requested_ns': index * period,
                         'started_ns': time.monotonic_ns() - origin}
                record['frames'].append(frame)
                try:
                    image, geometry = grab(subject)
                finally:
                    frame['finished_ns'] = time.monotonic_ns() - origin
                frame['geometry'] = geometry
                image_path = f'{subject}/{index:06d}.png'
                image.save(directory / image_path)
                frame['image'] = image_path
            index += 1
        record['complete'] = True
    except BaseException as error:
        record['error'] = f'{type(error).__name__}: {error}'
        raise
    finally:
        record['finished_ns'] = time.monotonic_ns() - origin
        write_record(directory, record)
    return record
