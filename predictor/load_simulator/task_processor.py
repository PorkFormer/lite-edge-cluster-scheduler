import threading
import time

from task_handlers import process_yolo_task


class TaskProcessor:
    def __init__(self, task_queue, output_dir, logger, metrics_provider):
        self.task_queue = task_queue
        self.output_dir = output_dir
        self.logger = logger
        self.metrics_provider = metrics_provider
        self._stop = threading.Event()
        self._thread = threading.Thread(target=self._run, name="task_processor")

    def start(self):
        self._thread.daemon = True
        self._thread.start()

    def stop(self):
        self._stop.set()
        self._thread.join(timeout=2.0)

    def _run(self):
        while not self._stop.is_set():
            task = self.task_queue.get(self._stop)
            if task is None:
                continue
            queue_snapshot = self.task_queue.snapshot_counts()
            queue_repr = _format_queue_snapshot(queue_snapshot)

            start_ms = int(time.time() * 1000)
            metrics = self.metrics_provider()
            self.logger.log_task_start(
                task_type=task.get("task_type", "unknown"),
                task_num=task.get("task_num", 1),
                start_time_ms=start_ms,
                queue_len_at_start=queue_repr,
                metrics=metrics,
            )
            print(
                "[Task_Start] 取任务开始执行 "
                f"type={task.get('task_type', 'unknown')} "
                f"num={task.get('task_num', 1)} "
                f"queue={queue_repr}",
                flush=True,
            )

            if task.get("task_type") == "YoloV5":
                process_yolo_task(
                    input_dir=task.get("input_dir"),
                    output_dir=self.output_dir,
                    task_num=task.get("task_num", 1),
                )
            else:
                time.sleep(0.5)

            end_ms = int(time.time() * 1000)
            metrics = self.metrics_provider()
            self.logger.log_task_end(
                task_type=task.get("task_type", "unknown"),
                task_num=task.get("task_num", 1),
                start_time_ms=start_ms,
                end_time_ms=end_ms,
                metrics=metrics,
            )
            duration_ms = end_ms - start_ms
            print(
                "[Task_End] 任务完成 "
                f"type={task.get('task_type', 'unknown')} "
                f"num={task.get('task_num', 1)} "
                f"duration_ms={duration_ms}",
                flush=True,
            )


def _format_queue_snapshot(counts):
    items = [[k, v] for k, v in sorted(counts.items(), key=lambda x: x[0])]
    return str(items)
