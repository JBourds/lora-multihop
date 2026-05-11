"""
macro_defines.py

Script shared across all platformio.ini files that inject macro definitions into
compiled programs based on environment variables.

Author: Jordan Bourdeau
"""

import os
from pprint import pprint

Import("env")

if env.IsIntegrationDump():
    Return()

FLAGS = [
    "PRODUCTION",
    "NO_SATELLITE",
    "POWER_DOWN_ON_HALT",
    "HAS_ALL",
    "HAS_TEMPERATURE",
    "HAS_HUMIDITY",
    "HAS_LASER",
    "HAS_SONAR",
    "HAS_WIND_SPEED",
    "HAS_SOLAR_RADIATION",
    "HAS_PHASE_PREDICTION",
]
PARAMETERS = [
    "TDMA_NODE_COUNT",
    "DEVICE_ID",
    "GUARD_DURATION_S",
    "SLOT_DURATION_S",
    "SEND_DELAY_S",
    "TDMA_INTERVAL_S",
    "POLL_INTERVAL_S",
    "PIPELINE_INTERVAL_S",
    "SAT_INTERVAL_S",
    "RECORDING_LENGTH_MS",
    "RECORDING_LENGTH_S",
    "SAMPLING_FREQ_HZ",
    "LOW_FREQUENCY_CUTOFF_HZ",
    "SAMPLE_COUNT",
    "MAX_FRAMES",
    "MFCC_COEFFICIENT_COUNT",
]


def is_set(field: str) -> bool:
    return bool(os.getenv(field))


def val(field: str) -> any:
    return os.getenv(field)


defines = []
defines.extend([(field, val(field)) for field in filter(is_set, PARAMETERS)])
defines.extend([(field, 1) for field in filter(is_set, FLAGS)])

print("Defined the following macros:")
pprint(defines)

env.Append(CPPDEFINES=defines)
