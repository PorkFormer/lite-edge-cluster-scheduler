import os
import threading
import time
from collections import deque


class TaskQueue:
    def __init__(self):
        self._queue = deque()
        self._cond = threading.Condition()

    def put(self, task):
        with self._cond:
            if "enqueue_time" not in task:
                task["enqueue_time"] = int(time.time() * 1000)
            if "enqueue_task_total_files" not in task:
                if task.get("task_type") == "YoloV5":
                    task["enqueue_task_total_files"] = _count_files(task.get("input_dir"))
                else:
                    task["enqueue_task_total_files"] = 0
            self._queue.append(task)
            self._cond.notify()

    def get(self, stop_event):
        with self._cond:
            while not self._queue:
                if stop_event.is_set():
                    return None
                self._cond.wait(timeout=0.5)
            return self._queue.popleft()

    def snapshot_counts(self):
        counts = {}
        with self._cond:
            for item in self._queue:
                ttype = item.get("task_type", "unknown")
                counts[ttype] = counts.get(ttype, 0) + 1
        return counts

    def snapshot_totals(self):
        total_tasks = 0
        total_files = 0
        with self._cond:
            for item in self._queue:
                total_tasks += 1
                total_files += int(item.get("enqueue_task_total_files", 0) or 0)
        return total_tasks, total_files


def _count_files(input_dir):
    if not input_dir or not os.path.isdir(input_dir):
        return 0
    total = 0
    for _, _, files in os.walk(input_dir):
        total += len(files)
    return total
