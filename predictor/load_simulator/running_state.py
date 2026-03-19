import threading


class RunningState:
    def __init__(self):
        self._lock = threading.Lock()
        self._task_type = ""
        self._total_files = 0
        self._remaining_files = 0

    def set_running(self, task_type, total_files, remaining_files):
        with self._lock:
            self._task_type = task_type or ""
            self._total_files = int(total_files or 0)
            self._remaining_files = int(remaining_files or 0)

    def update_remaining(self, remaining_files):
        with self._lock:
            self._remaining_files = int(remaining_files or 0)

    def clear(self):
        with self._lock:
            self._task_type = ""
            self._total_files = 0
            self._remaining_files = 0

    def snapshot(self):
        with self._lock:
            return {
                "running_task_type": self._task_type,
                "running_task_total_files": self._total_files,
                "running_task_remaining_files": self._remaining_files,
            }
