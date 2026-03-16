import argparse
import os
import threading
import time

from background_loads import BackgroundManager
from config import load_config
from event_scheduler import DAY_SECONDS, build_event_schedule, run_schedule
from logger import CsvLogger
from monitor import Monitor
from task_processor import TaskProcessor
from task_queue import TaskQueue


class MetricsStore:
    def __init__(self):
        self._lock = threading.Lock()
        self._latest = {}

    def update(self, metrics):
        with self._lock:
            self._latest = dict(metrics)

    def get(self):
        with self._lock:
            return dict(self._latest)


def apply_override(profile, override):
    if not override:
        return profile
    merged = dict(profile)
    if "duration_sec" in override:
        merged["duration_sec"] = override["duration_sec"]
    if "loads" in override:
        merged["loads"] = merge_items(
            base_items=profile.get("loads", []),
            override_items=override.get("loads", []),
            key_field="type",
        )
    if "enqueue_tasks" in override:
        merged["enqueue_tasks"] = merge_items(
            base_items=profile.get("enqueue_tasks", []),
            override_items=override.get("enqueue_tasks", []),
            key_field="task_type",
        )
    return merged


def merge_items(base_items, override_items, key_field):
    merged = [dict(item) for item in base_items]
    for override in override_items:
        key = override.get(key_field)
        if key is None:
            merged.append(dict(override))
            continue
        matched = False
        for item in merged:
            if item.get(key_field) == key:
                item.update(override)
                matched = True
                break
        if not matched:
            merged.append(dict(override))
    return merged


def main():
    parser = argparse.ArgumentParser(description="Event-driven load simulator")
    parser.add_argument("--config", required=True, help="Path to device config JSON")
    parser.add_argument(
        "--sim-seconds",
        type=int,
        default=DAY_SECONDS,
        help="Total simulated seconds",
    )
    parser.add_argument(
        "--output-dir",
        default="output",
        help="CSV output directory",
    )
    parser.add_argument(
        "--monitor-interval-sec",
        type=float,
        default=None,
        help="Override monitor interval seconds",
    )
    args = parser.parse_args()

    config = load_config(args.config)
    device_name = config["device_name"]

    monitor_interval = config.get("monitor_interval_sec", 3)
    if args.monitor_interval_sec is not None:
        monitor_interval = args.monitor_interval_sec

    background_manager = BackgroundManager()
    metrics_store = MetricsStore()

    monitor_dir = os.path.join(args.output_dir, "monitor")
    task_output_dir = os.path.join(args.output_dir, "tasks")
    logger = CsvLogger(monitor_dir, device_name)
    monitor = Monitor(monitor_interval, metrics_store, background_manager, logger)
    monitor.start()

    task_queue = TaskQueue()
    task_processor = TaskProcessor(
        task_queue=task_queue,
        output_dir=task_output_dir,
        logger=logger,
        metrics_provider=metrics_store.get,
    )
    task_processor.start()

    event_profiles = config.get("event_profiles", {})
    event_schedule = build_event_schedule(config.get("event_schedule", []))

    active_lock = threading.Lock()
    active_instances = []

    def handle_event(event):
        event_type = event.get("type")
        base_profile = event_profiles.get(event_type, {})
        profile = apply_override(base_profile, event.get("override"))
        loads = profile.get("loads", [])
        enqueue_tasks = profile.get("enqueue_tasks", [])

        def _resolve_duration(load_cfg):
            if "duration_sec" in load_cfg:
                return float(load_cfg.get("duration_sec", 0))
            return float(profile.get("duration_sec", 0))

        instances = []
        for load_cfg in loads:
            inst = background_manager.start_load(load_cfg)
            if inst is not None:
                instances.append((inst, _resolve_duration(load_cfg)))
        if instances:
            with active_lock:
                active_instances.extend([inst for inst, _ in instances])

        for task_cfg in enqueue_tasks:
            task_type = task_cfg.get("task_type", "unknown")
            input_dir = task_cfg.get("input_dir")
            task_num = int(task_cfg.get("task_num", 1))
            task_queue.put(
                {"task_type": task_type, "input_dir": input_dir, "task_num": task_num}
            )

        for inst, duration in instances:
            def _stop_one(instance=inst, dur=duration):
                if dur > 0:
                    time.sleep(dur)
                background_manager.stop_loads([instance])
                with active_lock:
                    if instance in active_instances:
                        active_instances.remove(instance)

            t = threading.Thread(target=_stop_one, name=f"event_{event_type}_stop")
            t.daemon = True
            t.start()

    try:
        run_schedule(
            events=event_schedule,
            sim_seconds=args.sim_seconds,
            on_event=handle_event,
        )
        time.sleep(0.5)
    finally:
        with active_lock:
            background_manager.stop_loads(list(active_instances))
            active_instances.clear()
        monitor.stop()
        task_processor.stop()
        logger.close()


if __name__ == "__main__":
    main()
