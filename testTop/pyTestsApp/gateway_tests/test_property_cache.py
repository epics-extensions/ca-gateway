#!/usr/bin/env python
"""
Testing the Gateway PV property cache.

Set up a connection through the Gateway - change a property externally - check
if Gateway cache was updated
"""
import logging
import time

from epics import ca, dbr

from . import conftest

logger = logging.getLogger(__name__)


def test_prop_cache_value_monitor_ctrl_get(
    prop_supported: bool, standard_env: conftest.EnvironmentInfo
):
    """
    Monitor PV (value events) through GW - change properties (HIGH, EGU)
    directly - get the DBR_CTRL of the PV through GW
    """

    def on_change(pvname=None, **kws):
        ...

    # gateway should show no VC (client side connection) and no PV (IOC side connection)
    gateway_stats = conftest.GatewayStats()
    assert gateway_stats.vctotal == 0
    assert gateway_stats.pvtotal == 0
    assert gateway_stats.connected == 0
    assert gateway_stats.active == 0
    assert gateway_stats.inactive == 0

    # gwcachetest is an ai record with full set of alarm limits: -100 -10 10 100
    gw = ca.create_channel("gateway:gwcachetest")
    connected = ca.connect_channel(gw, timeout=0.5)
    assert connected, "Could not connect to gateway channel " + ca.name(gw)
    (gw_cbref, gw_uaref, gw_eventid) = ca.create_subscription(
        gw, mask=dbr.DBE_VALUE, callback=on_change
    )
    ioc = ca.create_channel("ioc:gwcachetest")
    connected = ca.connect_channel(ioc, timeout=0.5)
    assert connected, "Could not connect to ioc channel " + ca.name(ioc)
    (ioc_cbref, ioc_uaref, ioc_eventid) = ca.create_subscription(
        ioc, mask=dbr.DBE_VALUE, callback=on_change
    )

    # gateway should show one VC and one connected active PV
    gateway_stats.update()
    assert gateway_stats.vctotal == 1
    assert gateway_stats.pvtotal == 1
    assert gateway_stats.connected == 1
    assert gateway_stats.active == 1
    assert gateway_stats.inactive == 0

    # limit should not have been updated
    ioc_ctrl = ca.get_ctrlvars(ioc)
    high_val = ioc_ctrl["upper_warning_limit"]
    assert high_val == 10.0, "Expected IOC warning_limit: 10; actual limit: " + str(
        high_val
    )
    gw_ctrl = ca.get_ctrlvars(gw)
    high_val = gw_ctrl["upper_warning_limit"]
    assert high_val == 10.0, "Expected GW warning_limit: 10; actual limit: " + str(
        high_val
    )

    # set warning limit on IOC
    ioc_high = ca.create_channel("ioc:gwcachetest.HIGH")
    ca.put(ioc_high, 20.0, wait=True)
    time.sleep(0.1)

    # Now the limit should have been updated (if IOC supports DBE_PROPERTY)
    ioc_ctrl = ca.get_ctrlvars(ioc)
    high_val = ioc_ctrl["upper_warning_limit"]
    assert high_val == 20.0, "Expected IOC warning_limit: 20; actual limit: " + str(
        high_val
    )
    if prop_supported:
        gw_expected = 20.0
    else:
        gw_expected = 10.0
    gw_ctrl = ca.get_ctrlvars(gw)
    high_val = gw_ctrl["upper_warning_limit"]
    assert (
        high_val == gw_expected
    ), f"Expected GW warning_limit: {gw_expected}; actual limit: {high_val}"

    # set unit string on IOC
    ioc_egu = ca.create_channel("ioc:gwcachetest.EGU")
    old_egu = ca.get(ioc_egu)
    ca.put(ioc_egu, "foo", wait=True)
    time.sleep(0.1)

    # Now the unit string should have been updated (if IOC supports DBE_PROPERTY)
    ioc_ctrl = ca.get_ctrlvars(ioc)
    egu_val = ioc_ctrl["units"]
    assert egu_val == "foo", (
        "Expected IOC units string: foo; actual units string: " + egu_val
    )
    if prop_supported:
        gw_expected = "foo"
    else:
        gw_expected = old_egu
    gw_ctrl = ca.get_ctrlvars(gw)
    egu_val = gw_ctrl["units"]
    assert (
        egu_val == gw_expected
    ), f"Expected GW units string: {gw_expected}; actual units string: {egu_val}"


def test_prop_cache_value_get_ctrl_get(
    prop_supported: bool, standard_env: conftest.EnvironmentInfo
):
    """
    Get PV (value) through GW - change properties (HIGH, EGU) directly -
    get the DBR_CTRL of the PV through GW
    """
    # gateway should show no VC (client side connection) and no PV (IOC side connection)
    gateway_stats = conftest.GatewayStats()
    assert gateway_stats.vctotal == 0
    assert gateway_stats.pvtotal == 0
    assert gateway_stats.connected == 0
    assert gateway_stats.active == 0
    assert gateway_stats.inactive == 0

    # gwcachetest is an ai record with full set of alarm limits: -100 -10 10 100
    gw = ca.create_channel("gateway:gwcachetest")
    connected = ca.connect_channel(gw, timeout=0.5)
    assert connected, "Could not connect to gateway channel " + ca.name(gw)
    ioc = ca.create_channel("ioc:gwcachetest")
    connected = ca.connect_channel(ioc, timeout=0.5)
    assert connected, "Could not connect to ioc channel " + ca.name(gw)

    # gateway should show one VC and one connected active PV
    gateway_stats.update()
    assert gateway_stats.vctotal == 1
    assert gateway_stats.pvtotal == 1
    assert gateway_stats.connected == 1
    assert gateway_stats.active == 1
    assert gateway_stats.inactive == 0

    # limit should not have been updated
    ioc_ctrl = ca.get_ctrlvars(ioc)
    high_val = ioc_ctrl["upper_warning_limit"]
    assert high_val == 10.0, "Expected IOC warning_limit: 10; actual limit: " + str(
        high_val
    )
    gw_ctrl = ca.get_ctrlvars(gw)
    high_val = gw_ctrl["upper_warning_limit"]
    assert high_val == 10.0, "Expected GW warning_limit: 10; actual limit: " + str(
        high_val
    )

    # set warning limit on IOC
    ioc_high = ca.create_channel("ioc:gwcachetest.HIGH")
    ca.put(ioc_high, 20.0, wait=True)
    time.sleep(0.1)

    # Now the limit should have been updated (if IOC supports DBE_PROPERTY)
    ioc_ctrl = ca.get_ctrlvars(ioc)
    high_val = ioc_ctrl["upper_warning_limit"]
    assert high_val == 20.0, "Expected IOC warning_limit: 20; actual limit: " + str(
        high_val
    )
    if prop_supported:
        gw_expected = 20.0
    else:
        gw_expected = 10.0
    gw_ctrl = ca.get_ctrlvars(gw)
    high_val = gw_ctrl["upper_warning_limit"]
    assert (
        high_val == gw_expected
    ), f"Expected GW warning_limit: {gw_expected}; actual limit: {high_val}"

    # set unit string on IOC
    ioc_egu = ca.create_channel("ioc:gwcachetest.EGU")
    old_egu = ca.get(ioc_egu)
    ca.put(ioc_egu, "foo", wait=True)
    time.sleep(0.1)

    # Now the unit string should have been updated (if IOC supports DBE_PROPERTY)
    ioc_ctrl = ca.get_ctrlvars(ioc)
    egu_val = ioc_ctrl["units"]
    assert egu_val == "foo", (
        "Expected IOC units string: foo; actual units string: " + egu_val
    )
    if prop_supported:
        gw_expected = "foo"
    else:
        gw_expected = old_egu
    gw_ctrl = ca.get_ctrlvars(gw)
    egu_val = gw_ctrl["units"]
    assert (
        egu_val == gw_expected
    ), f"Expected GW units string: {gw_expected}; actual units string: {egu_val}"


def test_prop_cache_value_get_disconnect_ctrl_get(
    standard_env: conftest.EnvironmentInfo,
):
    """
    Get PV (value) through GW - disconnect client - change properties
    (HIGH, EGU) directly - get the DBR_CTRL of the PV through GW
    """
    # gateway should show no VC (client side connection) and no PV (IOC side connection)
    gateway_stats = conftest.GatewayStats()
    assert gateway_stats.vctotal == 0
    assert gateway_stats.pvtotal == 0
    assert gateway_stats.connected == 0
    assert gateway_stats.active == 0
    assert gateway_stats.inactive == 0

    # gwcachetest is an ai record with full set of alarm limits: -100 -10 10 100
    gw = ca.create_channel("gateway:gwcachetest")
    connected = ca.connect_channel(gw, timeout=0.5)
    assert connected, "Could not connect to gateway channel " + ca.name(gw)
    ioc = ca.create_channel("ioc:gwcachetest")
    connected = ca.connect_channel(ioc, timeout=0.5)
    assert connected, "Could not connect to ioc channel " + ca.name(gw)

    # gateway should show one VC and one connected active PV
    gateway_stats.update()
    assert gateway_stats.vctotal == 1
    assert gateway_stats.pvtotal == 1
    assert gateway_stats.connected == 1
    assert gateway_stats.active == 1
    assert gateway_stats.inactive == 0

    # limit should not have been updated
    ioc_ctrl = ca.get_ctrlvars(ioc)
    high_val = ioc_ctrl["upper_warning_limit"]
    assert high_val == 10.0, "Expected IOC warning_limit: 10; actual limit: " + str(
        high_val
    )
    egu_val = ioc_ctrl["units"]
    assert egu_val == "wobbles", (
        "Expected IOC units string: wobbles; actual units string: " + egu_val
    )
    gw_ctrl = ca.get_ctrlvars(gw)
    high_val = gw_ctrl["upper_warning_limit"]
    assert high_val == 10.0, "Expected GW warning_limit: 10; actual limit: " + str(
        high_val
    )
    egu_val = gw_ctrl["units"]
    assert egu_val == "wobbles", (
        "Expected GW units string: wobbles; actual units string: " + egu_val
    )

    # disconnect Channel Access, reconnect Gateway stats
    ca.finalize_libca()
    ca.initialize_libca()

    # gateway should show no VC and 1 connected inactive PV
    gateway_stats = conftest.GatewayStats()
    assert gateway_stats.vctotal == 0
    assert gateway_stats.pvtotal == 1
    assert gateway_stats.connected == 1
    assert gateway_stats.active == 0
    assert gateway_stats.inactive == 1

    # set warning limit on IOC
    ioc_high = ca.create_channel("ioc:gwcachetest.HIGH")
    ca.put(ioc_high, 20.0, wait=True)
    # set unit string on IOC
    ioc_egu = ca.create_channel("ioc:gwcachetest.EGU")
    ca.put(ioc_egu, "foo", wait=True)
    time.sleep(0.1)

    # reconnect Gateway and IOC
    gw = ca.create_channel("gateway:gwcachetest")
    connected = ca.connect_channel(gw, timeout=0.5)
    assert connected, "Could not connect to gateway channel " + ca.name(gw)
    ioc = ca.create_channel("ioc:gwcachetest")
    connected = ca.connect_channel(ioc, timeout=0.5)
    assert connected, "Could not connect to ioc channel " + ca.name(gw)

    # gateway should show one VC and one connected active PV
    gateway_stats.update()
    assert gateway_stats.vctotal == 1
    assert gateway_stats.pvtotal == 1
    assert gateway_stats.connected == 1
    assert gateway_stats.active == 1
    assert gateway_stats.inactive == 0

    # now the limit should have been updated
    ioc_ctrl = ca.get_ctrlvars(ioc)
    high_val = ioc_ctrl["upper_warning_limit"]
    assert high_val == 20.0, "Expected IOC warning_limit: 20; actual limit: " + str(
        high_val
    )
    egu_val = ioc_ctrl["units"]
    assert egu_val == "foo", (
        "Expected IOC units string: foo; actual units string: " + egu_val
    )
    gw_ctrl = ca.get_ctrlvars(gw)
    high_val = gw_ctrl["upper_warning_limit"]
    assert high_val == 20.0, "Expected GW warning_limit: 20; actual limit: " + str(
        high_val
    )
    egu_val = gw_ctrl["units"]
    assert egu_val == "foo", (
        "Expected GW units string: wobbles; actual units string: " + egu_val
    )
