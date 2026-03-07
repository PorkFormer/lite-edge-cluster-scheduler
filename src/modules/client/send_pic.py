import os
import argparse
import base64
import json
from typing import List, Tuple

import grpc


def project_root() -> str:
    script_dir = os.path.dirname(os.path.abspath(__file__))
    return os.path.abspath(os.path.join(script_dir, "../../.."))


def list_image_files(directory: str) -> List[str]:
    if not os.path.isdir(directory):
        return []
    files: List[str] = []
    for name in os.listdir(directory):
        path = os.path.join(directory, name)
        if not os.path.isfile(path):
            continue
        files.append(path)

    def natural_key(p: str):
        base = os.path.splitext(os.path.basename(p))[0]
        try:
            return (0, int(base))
        except ValueError:
            return (1, base)

    return sorted(files, key=natural_key)


def upload_batch(stub: grpc.Channel, files: List[str], tasktype: str, req_id: str = "") -> Tuple[int, str, str]:
    total_num = len(files)

    def payload_iter():
        for idx, filename in enumerate(files):
            with open(filename, "rb") as f:
                content_b64 = base64.b64encode(f.read()).decode("ascii")
            payload = {
                "filename": os.path.basename(filename),
                "content_b64": content_b64,
                "tasktype": tasktype,
            }
            if req_id:
                payload["req_id"] = req_id
            if idx == 0:
                payload["total_num"] = total_num
            yield json.dumps(payload).encode("utf-8")

    try:
        resp_bytes = stub.stream_unary("/ImageUpload/UploadImages")(payload_iter())
        resp = json.loads(resp_bytes.decode("utf-8"))
        return resp.get("saved_count", 0), resp.get("req_id", ""), "ok"
    except Exception as exc:
        return -1, "", f"error: {exc}"


def main() -> None:
    parser = argparse.ArgumentParser(description="gRPC stream uploader")
    parser.add_argument("-n", "--max", type=int, default=None, help="limit max files to send")
    parser.add_argument("-H", "--host", default="127.0.0.1", help="gRPC server host")
    parser.add_argument("-P", "--port", type=int, default=9999, help="gRPC server port")
    parser.add_argument(
        "-D",
        "--dir",
        default="",
        help="input directory (override); default uses workspace/client/data/<tasktype>/req",
    )
    parser.add_argument(
        "--root",
        default="workspace/client/data",
        help="client data root directory (default: workspace/client/data)",
    )
    parser.add_argument("--tasktype", default="YoloV5", help="service/task type (e.g. YoloV5, Bert, ...)")
    parser.add_argument("--req-id", default="", help="optional req_id for batch mode")
    args = parser.parse_args()

    root_dir = args.root
    if not os.path.isabs(root_dir):
        root_dir = os.path.join(project_root(), root_dir)

    images_dir = args.dir.strip()
    if not os.path.isabs(images_dir):
        if images_dir:
            images_dir = os.path.join(project_root(), images_dir)
        else:
            images_dir = os.path.join(root_dir, args.tasktype, "req")

    files = list_image_files(images_dir)
    if args.max is not None:
        limit = max(0, args.max)
        files = files[:limit]
    if not files:
        print(f"No images found in directory: {images_dir}")
        return

    target = f"{args.host}:{args.port}"
    server_url = f"grpc://{args.host}:{args.port}/ImageUpload/UploadImages"
    print(f"Sending images from: {images_dir} -> {server_url}")
    from datetime import datetime
    import time
    start_ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
    print(f"start_send_timestamp: {start_ts}")

    with grpc.insecure_channel(
        target,
        options=[
            ("grpc.max_send_message_length", 128 * 1024 * 1024),
            ("grpc.max_receive_message_length", 128 * 1024 * 1024),
        ],
    ) as channel:
        t0 = time.perf_counter()
        saved_count, resp_req_id, msg = upload_batch(channel, files, args.tasktype, req_id=args.req_id)
        elapsed_ms = (time.perf_counter() - t0) * 1000.0
        if resp_req_id:
            print(f"stream sent {len(files)} files -> saved_count={saved_count}, req_id={resp_req_id}, resp={msg}")
        else:
            print(f"stream sent {len(files)} files -> saved_count={saved_count}, resp={msg}")
        print(f"request_elapsed_ms: {elapsed_ms:.2f}")
        avg_ms = elapsed_ms / max(1, len(files))
        print(f"avg_ms_per_image: {avg_ms:.2f}")


if __name__ == "__main__":
    main()
