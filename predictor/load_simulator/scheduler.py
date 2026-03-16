import time


DAY_SECONDS = 24 * 60 * 60


def parse_time_to_sec(value):
    if value == "24:00:00":
        return DAY_SECONDS
    parts = value.split(":")
    if len(parts) != 3:
        raise ValueError(f"invalid time: {value}")
    h, m, s = [int(x) for x in parts]
    if h < 0 or h > 24 or m < 0 or m > 59 or s < 0 or s > 59:
        raise ValueError(f"invalid time: {value}")
    if h == 24 and (m != 0 or s != 0):
        raise ValueError(f"invalid time: {value}")
    return h * 3600 + m * 60 + s


def normalize_intervals(schedule):
    intervals = []
    for item in schedule:
        start = parse_time_to_sec(item["start"])
        end = parse_time_to_sec(item["end"])
        level = int(item["level"])
        if level < 0 or level > 100:
            raise ValueError("level must be 0-100")
        if start == end:
            continue
        if end < start:
            intervals.append((start, DAY_SECONDS, level))
            intervals.append((0, end, level))
        else:
            intervals.append((start, end, level))
    intervals.sort(key=lambda x: x[0])
    _ensure_no_overlap(intervals)
    return intervals


def _ensure_no_overlap(intervals):
    last_end = None
    for start, end, _ in intervals:
        if last_end is not None and start < last_end:
            raise ValueError("overlapping intervals are not allowed")
        last_end = end


def build_events(resources):
    events = []
    for resource, cfg in resources.items():
        schedule = cfg.get("schedule", [])
        intervals = normalize_intervals(schedule)
        for start, end, level in intervals:
            events.append((start, resource, "start", level))
            events.append((end, resource, "stop", 0))
    events.sort(key=lambda x: x[0])
    return events


def shift_events(events, start_offset_sec):
    shifted = []
    for t, resource, action, level in events:
        if t == DAY_SECONDS and start_offset_sec == 0:
            rel = DAY_SECONDS
        else:
            rel = (t - start_offset_sec) % DAY_SECONDS
        shifted.append((rel, resource, action, level))
    shifted.sort(key=lambda x: x[0])
    return shifted


def run_schedule(
    events,
    sim_seconds,
    start_offset_sec,
    on_event,
    logger,
    device_name,
):
    shifted = shift_events(events, start_offset_sec)
    start_real = time.perf_counter()
    sim_time = 0.0
    idx = 0

    while sim_time < sim_seconds:
        if idx >= len(shifted):
            next_time = sim_seconds
        else:
            next_time = shifted[idx][0]
            if next_time < sim_time:
                idx += 1
                continue

        target_real = start_real + next_time
        now = time.perf_counter()
        sleep_time = target_real - now
        if sleep_time > 0:
            time.sleep(sleep_time)

        sim_time = next_time

        while idx < len(shifted) and shifted[idx][0] == next_time:
            _, resource, action, level = shifted[idx]
            on_event(resource, action, level)
            logger.log(sim_time, device_name, resource, action, level)
            idx += 1

        if idx >= len(shifted):
            break

    return True
