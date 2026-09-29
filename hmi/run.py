"""CLI runner for HMI Bridge Server."""

import argparse
import os
import sys
import uvicorn


def main() -> None:
    parser = argparse.ArgumentParser(description="ROS 2 Modbus Industrial Gateway HMI Server")
    parser.add_argument("--host", default="0.0.0.0", help="Listen host (default: 0.0.0.0)")
    parser.add_argument("--port", type=int, default=8000, help="Listen port (default: 8000)")
    parser.add_argument("--adapter", choices=["auto", "ros2", "mock"], default="auto",
                        help="Data adapter mode (default: auto)")
    parser.add_argument("--reload", action="store_true", help="Enable auto-reload for development")

    args = parser.parse_args()
    os.environ["HMI_ADAPTER_MODE"] = args.adapter

    print(f"================================================================")
    print(f"🚀 Launching ROS 2 Industrial HMI Server")
    print(f"   Address: http://{args.host}:{args.port}")
    print(f"   Adapter: {args.adapter}")
    print(f"================================================================")

    uvicorn.run("hmi.bridge_server:app", host=args.host, port=args.port, reload=args.reload)


if __name__ == "__main__":
    main()
