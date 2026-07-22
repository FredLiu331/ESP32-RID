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


@dataclass(frozen=True)
class BaselineResult:
    discovered_ids: set[str]
    malformed_payloads: int
    discovery_time_s: float | None
    primary_delivery_ratio: float


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


def evaluate_baseline(records: Iterable[ReceiverRecord], expected_ids: set[str],
                      primary_kind: str = "location") -> BaselineResult:
    """计算验收断言所需的四项指标。"""
    rows = list(records)
    valid = [row for row in rows if row.valid]
    first_seen = {}
    for row in valid:
        first_seen.setdefault(row.test_id, row.timestamp_ms)
    discovered = set(first_seen) & expected_ids
    discovery_time = None
    if len(discovered) == len(expected_ids) and expected_ids:
        discovery_time = (max(first_seen[test_id] for test_id in expected_ids) -
                          min(first_seen[test_id] for test_id in expected_ids)) / 1000
    primary = [row for row in rows if row.message_kind == primary_kind]
    primary_valid = sum(row.valid for row in primary)
    ratio = primary_valid / len(primary) if primary else 0.0
    return BaselineResult(discovered, sum(not row.valid for row in rows), discovery_time, ratio)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="接收机 JSON Lines 文件")
    args = parser.parse_args()
    print(json.dumps(summarize(read_records(args.input)), ensure_ascii=False, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
