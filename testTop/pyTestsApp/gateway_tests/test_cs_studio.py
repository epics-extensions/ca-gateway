#!/usr/bin/env python
import logging
import time

from epics import ca, dbr

from . import conftest

logger = logging.getLogger(__name__)


def test_cs_studio_value_and_prop_monitor(standard_env: conftest.EnvironmentInfo):
    """
    Test CS-Studio workflow through the Gateway.

    Set up a TIME_DOUBLE (DBE_VALUE | DBE_ALARM) and a CTRL_DOUBLE
    (DBE_PROPERTY) connection directly and through the Gateway - change value
    and property - check consistency of data

    Monitor PV (imitating CS-Studio) through GW - change value and
    properties directly - check CTRL structure consistency
    """
    events_received_ioc = 0
    events_received_gw = 0
    ioc_struct = dict()
    gw_struct = dict()

    def on_change_ioc(pvname=None, **kws):
        nonlocal events_received_ioc
        events_received_ioc += 1
        ioc_struct.update(kws)
        logger.info(
            "New IOC Value for %s value=%s, kw=%s\n",
            pvname,
            str(kws["value"]),
            repr(kws),
        )

    def on_change_gw(pvname=None, **kws):
        nonlocal events_received_gw
        events_received_gw += 1
        gw_struct.update(kws)
        logger.info(
            "New GW Value for %s value=%s, kw=%s\n",
            pvname,
            str(kws["value"]),
            repr(kws),
        )

    # gwcachetest is an ai record with full set of alarm limits: -100 -10 10 100
    gw = ca.create_channel("gateway:gwcachetest")
    connected = ca.connect_channel(gw, timeout=0.5)
    assert connected, "Could not connect to gateway channel " + ca.name(gw)
    (gw_cbref, gw_uaref, gw_eventid) = ca.create_subscription(
        gw,
        mask=dbr.DBE_VALUE | dbr.DBE_ALARM,
        use_time=True,
        callback=on_change_gw,
    )
    (gw_cbref2, gw_uaref2, gw_eventid2) = ca.create_subscription(
        gw, mask=dbr.DBE_PROPERTY, use_ctrl=True, callback=on_change_gw
    )
    ioc = ca.create_channel("ioc:gwcachetest")
    connected = ca.connect_channel(ioc, timeout=0.5)
    assert connected, "Could not connect to ioc channel " + ca.name(ioc)
    (ioc_cbref, ioc_uaref, ioc_eventid) = ca.create_subscription(
        ioc,
        mask=dbr.DBE_VALUE | dbr.DBE_ALARM,
        use_time=True,
        callback=on_change_ioc,
    )
    (ioc_cbref2, ioc_uaref2, ioc_eventid2) = ca.create_subscription(
        ioc, mask=dbr.DBE_PROPERTY, use_ctrl=True, callback=on_change_ioc
    )

    time.sleep(0.1)

    # set value on IOC
    ioc_value = ca.create_channel("ioc:gwcachetest")
    ca.put(ioc_value, 10.0, wait=True)
    time.sleep(0.1)

    assert events_received_ioc == events_received_gw, (
        f"After setting value, no. of received updates differ: "
        f"GW {events_received_gw}, IOC {events_received_ioc}"
    )

    differences = conftest.compare_structures(gw_struct, ioc_struct)
    assert not differences, (
        f"At update {events_received_ioc} (change value), received "
        f"structure updates differ:\n\t{differences}"
    )

    # set property on IOC
    ioc_hihi = ca.create_channel("ioc:gwcachetest.HIHI")
    ca.put(ioc_hihi, 123.0, wait=True)
    time.sleep(0.1)

    ca.put(ioc_value, 11.0, wait=True)
    time.sleep(0.1)

    assert events_received_ioc == events_received_gw, (
        f"After setting property, no. of received updates differ: "
        f"GW {events_received_gw}, IOC {events_received_ioc}"
    )

    differences = conftest.compare_structures(gw_struct, ioc_struct)
    assert not differences, (
        f"At update {events_received_ioc} (change property), received "
        f"structure updates differ:\n\t{differences}"
    )
