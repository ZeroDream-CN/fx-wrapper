#!/usr/bin/env python3
from pathlib import Path

root = Path(__file__).resolve().parents[1] / "server-linux/alpine/opt/cfx-server"
for path in sorted(root.glob("*.so")):
    count = path.read_bytes().count(b"execute\x00")
    if count:
        print(count, path.name)
