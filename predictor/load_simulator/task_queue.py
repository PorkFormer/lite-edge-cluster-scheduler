import threading
from collections import deque


class TaskQueue:
    def __init__(self):
        self._queue = deque()
        self._cond = threading.Condition()

    def put(self, task):
        with self._cond:
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
