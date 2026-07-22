import json

import pytest

from receiver_log import read_records


def test_read_records_requires_fixed_fields(tmp_path):
    path = tmp_path / "receiver.jsonl"
    path.write_text(json.dumps({
        "timestamp_ms": 1000,
        "test_id": "rid-1",
        "message_kind": "location",
        "valid": True,
        "latitude": 31.23,
        "longitude": 121.47,
    }) + "\n", encoding="utf-8")

    records = list(read_records(path))

    assert records[0].test_id == "rid-1"
    assert records[0].valid is True


def test_read_records_rejects_missing_or_invalid_values(tmp_path):
    path = tmp_path / "receiver.jsonl"
    path.write_text(json.dumps({"test_id": "rid-1"}) + "\n", encoding="utf-8")

    with pytest.raises(ValueError, match="required field"):
        list(read_records(path))
