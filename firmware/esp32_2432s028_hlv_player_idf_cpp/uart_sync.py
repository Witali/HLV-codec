#!/usr/bin/env python3
"""Run the primary C99 firmware's block synchronization host client."""

import pathlib
import runpy
import sys


PRIMARY = (
    pathlib.Path(__file__).parents[1]
    / "esp32_2432s028_hlv_player_idf_c"
)
sys.path.insert(0, str(PRIMARY))
runpy.run_path(str(PRIMARY / "uart_sync.py"), run_name="__main__")
