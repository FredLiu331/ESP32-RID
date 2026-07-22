import pytest

from receiver_log import ReceiverRecord, evaluate_baseline


def test_baseline_evaluation_checks_ids_and_validity():
    rows = [
        ReceiverRecord(1000, "rid-1", "location", True, 31.2, 121.4),
        ReceiverRecord(2000, "rid-2", "location", True, 31.2, 121.4),
        ReceiverRecord(3000, "rid-1", "location", False, 31.2, 121.4),
    ]

    result = evaluate_baseline(rows, {"rid-1", "rid-2"}, expected_primary_messages=3)

    assert result.discovered_ids == {"rid-1", "rid-2"}
    assert result.malformed_payloads == 1
    assert result.discovery_time_s == 1.0
    assert result.primary_delivery_ratio == pytest.approx(2 / 3)
