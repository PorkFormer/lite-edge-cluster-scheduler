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

# parse time string to seconds
def build_event_schedule(event_schedule):
    events = []
    for item in event_schedule:
        t = parse_time_to_sec(item["time"])
        events.append((t, item))
    events.sort(key=lambda x: x[0])
    return events


def run_schedule(events, sim_seconds, on_event):
    start_real = time.perf_counter()
    last_event_time = 0
    for event_time, event in events:
        if event_time > sim_seconds:
            break
        target_real = start_real + event_time
        now = time.perf_counter()
        sleep_time = target_real - now
        if sleep_time > 0:
            time.sleep(sleep_time)
        on_event(event)
        last_event_time = event_time
    if sim_seconds > last_event_time:
        target_real = start_real + sim_seconds
        now = time.perf_counter()
        sleep_time = target_real - now
        if sleep_time > 0:
            time.sleep(sleep_time)
