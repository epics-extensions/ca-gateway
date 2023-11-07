#!/usr/bin/env python
import logging
import time

import epics

from . import conftest

logger = logging.getLogger(__name__)


def test_log_deadband(standard_env: conftest.EnvironmentInfo):
    """
    Test log/archive updates (client using DBE_LOG flag) through the Gateway.

    DBE_LOG monitor on an ai with an ADEL - leaving the deadband generates
    events.
    """

    events_received = 0
    diff_inside_deadband = 0
    last_value = -99.9

    def on_change(pvname=None, **kws):
        nonlocal events_received
        nonlocal last_value
        nonlocal diff_inside_deadband

        events_received += 1
        logger.debug("%s changed to %s (%s)", pvname, kws["value"], kws["severity"])
        if (kws["value"] != 0.0) and (abs(last_value - kws["value"]) <= 10.0):
            diff_inside_deadband += 1
        last_value = kws["value"]

    # gateway:passiveADEL has ADEL=10
    ioc, gw = conftest.get_pv_pair(
        "passiveADEL", auto_monitor=epics.dbr.DBE_LOG, gateway_callback=on_change
    )
    ioc.get()
    gw.get()
    for val in range(35):
        ioc.put(val, wait=True)
    time.sleep(0.1)

    # We get 5 events: at connection, first put, then at 11 22 33
    assert (
        events_received == 5
    ), f"events expected: 5; events received: {events_received}"

    # Any updates inside deadband are an error
    assert (
        diff_inside_deadband == 0
    ), f"{diff_inside_deadband} events with change <= deadband received"
