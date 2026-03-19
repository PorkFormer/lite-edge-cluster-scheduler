import json


def load_config(path):
    with open(path, "r", encoding="utf-8") as f:
        data = json.load(f)
    _validate_root(data)
    if "monitor_interval_sec" not in data:
        data["monitor_interval_sec"] = 3
    if "event_profiles" not in data:
        data["event_profiles"] = {}
    if "event_schedule" not in data:
        data["event_schedule"] = []
    return data


def _validate_root(data):
    if not isinstance(data, dict):
        raise ValueError("config must be a JSON object")
    if "device_type" not in data:
        raise ValueError("device_type is required")
    if "device_name" not in data:
        raise ValueError("device_name is required")
    if "net_latency_target" not in data:
        raise ValueError("net_latency_target is required")
