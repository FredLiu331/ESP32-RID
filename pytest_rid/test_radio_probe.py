import pytest


@pytest.mark.esp32c5
def test_all_public_radio_gates(dut):
    dut.expect_exact("PROBE wifi_beacon_ch6 PASS")
    dut.expect_exact("PROBE wifi_beacon_ch149 PASS")
    dut.expect_exact("PROBE ble4 PASS")
    dut.expect_exact("PROBE ble5 PASS")
    dut.expect_exact("PROBE coexist_hop PASS")
