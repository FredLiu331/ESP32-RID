"""RID 接收机 JSON Lines 日志校验与基准统计工具。"""

from __future__ import annotations

import argparse
import json
import math
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Iterator


@dataclass(frozen=True)
class ReceiverRecord:
    timestamp_ms: int
    test_id: str
    message_kind: str
    valid: bool
    latitude: float
    longitude: float


_REQUIRED = {
    "timestamp_ms",
    "test_id",
    "message_kind",
    "valid",
    "latitude",
    "longitude",
}


def _record(value: object, line_number: int) -> ReceiverRecord:
    if not isinstance(value, dict) or not _REQUIRED.issubset(value):
        raise ValueError(f"line {line_number}: required field missing")
    timestamp = value["timestamp_ms"]
    latitude = value["latitude"]
    longitude = value["longitude"]
    if isinstance(timestamp, bool) or not isinstance(timestamp, int) or timestamp < 0:
        raise ValueError(f"line {line_number}: invalid timestamp_ms")
    if not isinstance(value["test_id"], str) or not value["test_id"]:
        raise ValueError(f"line {line_number}: invalid test_id")
    if not isinstance(value["message_kind"], str) or not value["message_kind"]:
        raise ValueError(f"line {line_number}: invalid message_kind")
    if not isinstance(value["valid"], bool):
        raise ValueError(f"line {line_number}: invalid valid flag")
    if (isinstance(latitude, bool) or not isinstance(latitude, (int, float)) or
            not math.isfinite(latitude) or not -90 <= latitude <= 90):
        raise ValueError(f"line {line_number}: invalid latitude")
    if (isinstance(longitude, bool) or not isinstance(longitude, (int, float)) or
            not math.isfinite(longitude) or not -180 <= longitude <= 180):
        raise ValueError(f"line {line_number}: invalid longitude")
    return ReceiverRecord(timestamp, value["test_id"], value["message_kind"],
                          value["valid"], float(latitude), float(longitude))


def read_records(path: str | Path) -> Iterator[ReceiverRecord]:
    """逐行读取并严格校验接收机 JSON Lines 记录。"""
    with Path(path).open(encoding="utf-8") as stream:
        for line_number, line in enumerate(stream, 1):
            if not line.strip():
                continue
            try:
                value = json.loads(line)
            except json.JSONDecodeError as exc:
                raise ValueError(f"line {line_number}: invalid JSON") from exc
            yield _record(value, line_number)


def summarize(records: Iterable[ReceiverRecord]) -> dict[str, object]:
    rows = list(records)
    valid = [row for row in rows if row.valid]
    ids = {row.test_id for row in valid}
    first = min((row.timestamp_ms for row in valid), default=None)
    last = max((row.timestamp_ms for row in valid), default=None)
    return {
        "records": len(rows),
        "valid_records": len(valid),
        "discovered_ids": sorted(ids),
        "discovery_time_s": None if first is None or last is None else (last - first) / 1000,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="接收机 JSON Lines 文件")
    args = parser.parse_args()
    print(json.dumps(summarize(read_records(args.input)), ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
