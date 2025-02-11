# SPDX-FileCopyrightText: 2024 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Unlicense OR CC0-1.0

import pytest
from pytest_embedded import Dut

def test_catch2_example(dut: Dut) -> None:
    dut.expect_exact("All tests passed")
