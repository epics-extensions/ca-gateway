#!/usr/bin/env python
import logging
import time

import epics

from . import conftest

logger = logging.getLogger(__name__)


def test_value_no_deadband(standard_env: conftest.EnvironmentInfo):
    """DBE_VALUE monitor on an ai - value changes generate events."""
    events_received = 0

    def on_change(pvname=None, **kws):
        nonlocal events_received
        events_received += 1
        logger.info(f' GW update: {pvname} changed to {kws["value"]}')

    # gateway:passive0 is a blank ai record
    ioc, gw = conftest.get_pv_pair(
        "passive0", auto_monitor=epics.dbr.DBE_VALUE, gateway_callback=on_change
    )
    ioc.get()
    gw.get()

    for val in range(10):
        ioc.put(val, wait=True)
    time.sleep(0.1)

    # We get 11 events: at connection, then at 10 value changes (puts)
    assert events_received == 11
