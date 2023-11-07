#!/usr/bin/env python
import logging
import os
import time

import pytest
from epics import PV, caget, caput

from . import conftest

logger = logging.getLogger(__name__)


MAX_ARRAY_BYTES_KEY = "IOC_EPICS_CA_MAX_ARRAY_BYTES"


# @pytest.mark.skip(reason="FIXME: test fails with unmanaged segfault, breaking the build")
@pytest.mark.parametrize(
    "max_array_bytes",
    [
        "6000000",
        "16384",
    ],
)
def test_gateway_does_not_crash_after_requesting_waveform_when_max_array_bytes_too_small(
    max_array_bytes,
):
    """
    Tests for a bug where the gateway will segfault when a waveform is
    requested through the gateway and the value of
    EPICS_CA_MAX_ARRAY_BYTES in the IOC is too small.

    Reference https://github.com/epics-extensions/ca-gateway/issues/20
    """

    # If the bug is present this test is designed to pass the first case
    # and fail the second case

    # The bug crashes the gateway when EPICS_CA_MAX_ARRAY_BYTES
    # on the IOC is too small. Set it here
    os.environ[MAX_ARRAY_BYTES_KEY] = max_array_bytes
    # The no_cache argument is required to trigger the bug
    with conftest.run_gateway("-no_cache"):
        with conftest.run_ioc():
            with conftest.local_channel_access():
                # First check that a simple PV can be put and got through gateway
                put_value = 5
                caput("gateway:passive0", put_value, wait=True)
                time.sleep(0.2)
                result = caget("gateway:passive0")

                assert result == put_value

                # Then try to get waveform through gateway
                try:
                    w = PV("gateway:bigpassivewaveform").get(
                        count=3000,
                        # CTRL type is required to trigger the bug
                        with_ctrlvars=True,
                    )
                    time.sleep(0.1)
                except TypeError as e:
                    raise RuntimeError(
                        "Gateway has crashed - " "exception from pyepics: %s", e
                    )
                except OSError as e:
                    raise RuntimeError(
                        "Gateway has crashed - " "exception from subprocess: %s", e
                    )
                else:
                    waveform_from_gateway = w
                    print(waveform_from_gateway)
                    print("waveform_from_gateway")
