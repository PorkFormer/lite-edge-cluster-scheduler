import os
import threading
import time

from task_handlers import process_yolo_task


class TaskProcessor:
    def __init__(self, task_queue, output_dir, logger, metrics_provider, running_state):
        self.task_queue = task_queue
        self.output_dir = output_dir
        self.logger = logger
        self.metrics_provider = metrics_provider
        self.running_state = running_state
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
            task_type = task.get("task_type", "unknown")
            if "enqueue_task_total_files" not in task:
                if task_type == "YoloV5":
                    task["enqueue_task_total_files"] = _count_files(task.get("input_dir"))
                else:
                    task["enqueue_task_total_files"] = 0
            queue_snapshot = self.task_queue.snapshot_counts()
            queue_repr = _format_queue_snapshot(queue_snapshot)
            queue_total_tasks, queue_total_files = self.task_queue.snapshot_totals()
            total_files = task.get("enqueue_task_total_files", 0)
            self.running_state.set_running(task_type, total_files, total_files)

            start_time = int(time.time() * 1000)
            metrics = self.metrics_provider()
            self.logger.log_task_start(
                enqueue_task_type=task_type,
                enqueue_task_total_files=total_files,
                enqueue_time=task.get("enqueue_time", 0),
                start_time=start_time,
                queue_total_tasks=queue_total_tasks,
                queue_total_files=queue_total_files,
                running_task_type=task_type,
                running_task_total_files=total_files,
                running_task_remaining_files=total_files,
                metrics=metrics,
            )
            print(
                "[Task_Start] "
                f"enqueue_task_type={task_type} "
                f"enqueue_task_total_files={total_files} "
                f"enqueue_time={task.get('enqueue_time', 0)} "
                f"start_time={start_time} "
                f"queue_total_tasks={queue_total_tasks} "
                f"queue_total_files={queue_total_files} "
                f"running_task_type={task_type} "
                f"running_task_total_files={total_files} "
                f"running_task_remaining_files={total_files}",
                flush=True,
            )

            if task_type == "YoloV5":
                def _progress_cb(processed, total):
                    remaining = max(int(total) - int(processed), 0)
                    self.running_state.update_remaining(remaining)

                process_yolo_task(
                    input_dir=task.get("input_dir"),
                    output_dir=self.output_dir,
                    task_num=task.get("task_num", 1),
                    task_cfg=task,
                    progress_cb=_progress_cb,
                )
            else:
                time.sleep(0.5)

            end_time = int(time.time() * 1000)
            self.running_state.update_remaining(0)
            metrics = self.metrics_provider()
            self.logger.log_task_end(
                enqueue_task_type=task_type,
                enqueue_task_total_files=total_files,
                enqueue_time=task.get("enqueue_time", 0),
                start_time=start_time,
                end_time=end_time,
                queue_total_tasks=queue_total_tasks,
                queue_total_files=queue_total_files,
                running_task_type=task_type,
                running_task_total_files=total_files,
                running_task_remaining_files=0,
                metrics=metrics,
            )
            duration_ms = end_time - start_time
            self.running_state.clear()
            print(
                "[Task_End] "
                f"enqueue_task_type={task_type} "
                f"enqueue_task_total_files={total_files} "
                f"enqueue_time={task.get('enqueue_time', 0)} "
                f"start_time={start_time} "
                f"end_time={end_time} "
                f"queue_total_tasks={queue_total_tasks} "
                f"queue_total_files={queue_total_files} "
                f"running_task_type={task_type} "
                f"running_task_total_files={total_files} "
                f"running_task_remaining_files=0 "
                f"duration_ms={duration_ms}",
                flush=True,
            )


def _format_queue_snapshot(counts):
    items = [[k, v] for k, v in sorted(counts.items(), key=lambda x: x[0])]
    return str(items)


def _count_files(input_dir):
    if not input_dir or not os.path.isdir(input_dir):
        return 0
    total = 0
    for _, _, files in os.walk(input_dir):
        total += len(files)
    return total
