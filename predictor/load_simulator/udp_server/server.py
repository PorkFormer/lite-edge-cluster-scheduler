import argparse
import socket


def run_server(host, port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((host, port))
    sock.settimeout(1.0)
    print(f"[UDP] listening on {host}:{port}")
    try:
        while True:
            try:
                data, addr = sock.recvfrom(65535)
            except socket.timeout:
                continue
            if not data:
                continue
            sock.sendto(b"hello world", addr)
    except KeyboardInterrupt:
        print("[UDP] shutting down")
    finally:
        sock.close()


def main():
    parser = argparse.ArgumentParser(description="Simple UDP echo server")
    parser.add_argument("--host", default="0.0.0.0", help="Bind host")
    parser.add_argument("--port", type=int, default=9999, help="Bind port")
    args = parser.parse_args()
    run_server(args.host, args.port)


if __name__ == "__main__":
    main()
