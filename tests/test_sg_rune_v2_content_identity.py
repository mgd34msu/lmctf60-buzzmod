#!/usr/bin/env python3
import hashlib
import subprocess
import sys


def main() -> int:
    probe = sys.argv[1]
    cases = [
        b"",
        b"a",
        b"a" * 55,
        b"a" * 56,
        bytes(range(64)),
        bytes(range(65)),
        bytes((index * 37 + 11) & 0xFF for index in range(4096)),
    ]
    for payload in cases:
        actual = subprocess.run(
            [probe], input=payload, check=True, capture_output=True
        ).stdout.decode("ascii").strip()
        expected = hashlib.sha256(payload).hexdigest()
        if actual != expected:
            raise AssertionError((len(payload), expected, actual))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
