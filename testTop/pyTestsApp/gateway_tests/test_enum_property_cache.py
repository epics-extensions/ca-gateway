#!/usr/bin/env python
"""
Testing the Gateway PV property cache for ENUM type data
(list of state strings)

Set up a connection through the Gateway - change a property externally -
check if Gateway cache was updated Detects EPICS bug lp:1510955
(https://bugs.launchpad.net/epics-base/+bug/1510955)
"""

import logging
import time

import pytest
from epics import ca, dbr

from . import conftest

logger = logging.getLogger(__name__)


@pytest.mark.skip(reason="detects CAS bug lp:1510955 which is not fixed yet")
def test_enum_prop_cache_value_monitor_ctrl_get(
    standard_env: conftest.EnvironmentInfo, prop_supported: bool
):
    """
    Monitor PV (value events) through GW - change ENUM string directly -
    get the DBR_CTRL of the PV through GW
    """

    gateway_stats = conftest.GatewayStats()
    # gateway should show no VC (client side connection) and no PV (IOC side connection)
    assert gateway_stats.vctotal == 0
    assert gateway_stats.pvtotal == 0
    assert gateway_stats.connected == 0
    assert gateway_stats.active == 0
    assert gateway_stats.inactive == 0

    def on_change(pvname=None, **kws):
        ...

    # enumtest is an mbbi record with three strings defined: zero one two
    gw = ca.create_channel("gateway:enumtest")
    connected = ca.connect_channel(gw, timeout=0.5)
    assert connected, "Could not connect to gateway channel " + ca.name(gw)
    (gw_cbref, gw_uaref, gw_eventid) = ca.create_subscription(
        gw, mask=dbr.DBE_VALUE, callback=on_change
    )
    ioc = ca.create_channel("ioc:enumtest")
    connected = ca.connect_channel(ioc, timeout=0.5)
    assert connected, "Could not connect to ioc channel " + ca.name(gw)
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

    # enum string should not have been updated
    ioc_ctrl = ca.get_ctrlvars(ioc)
    oneStr = ioc_ctrl["enum_strs"][1]
    assert oneStr == "one", "Expected IOC enum[1]: one; actual enum[1]: " + oneStr
    gw_ctrl = ca.get_ctrlvars(gw)
    oneStr = gw_ctrl["enum_strs"][1]
    assert oneStr == "one", "Expected GW enum[1]: one; actual enum[1]: " + oneStr

    # set enum string on IOC
    ioc_enum1 = ca.create_channel("ioc:enumtest.ONST")
    ca.put(ioc_enum1, "uno", wait=True)
    time.sleep(0.1)

    # Now the enum string should have been updated (if IOC supports DBE_PROPERTY)
    ioc_ctrl = ca.get_ctrlvars(ioc)
    oneStr = ioc_ctrl["enum_strs"][1]
    assert oneStr == "uno", "Expected IOC enum[1]: uno; actual enum[1]: " + oneStr
    if prop_supported:
        gw_expected = "uno"
    else:
        gw_expected = "one"
    gw_ctrl = ca.get_ctrlvars(gw)
    oneStr = gw_ctrl["enum_strs"][1]
    assert (
        oneStr == gw_expected
    ), f"Expected GW enum[1]: {gw_expected}; actual enum[1]: {oneStr}"


@pytest.mark.skip(reason="detects CAS bug lp:1510955 which is not fixed yet")
def test_enum_prop_cache_value_get_ctrl_get(
    standard_env: conftest.EnvironmentInfo, prop_supported: bool
):
    """
    Get PV (value) through GW - change ENUM string directly - get the
    DBR_CTRL of the PV through GW
    """
    # gateway should show no VC (client side connection) and no PV (IOC side connection)
    gateway_stats = conftest.GatewayStats()
    assert gateway_stats.vctotal == 0
    assert gateway_stats.pvtotal == 0
    assert gateway_stats.connected == 0
    assert gateway_stats.active == 0
    assert gateway_stats.inactive == 0

    # enumtest is an mbbi record with three strings defined: zero one two
    gw = ca.create_channel("gateway:enumtest")
    connected = ca.connect_channel(gw, timeout=0.5)
    assert connected, "Could not connect to gateway channel " + ca.name(gw)
    ioc = ca.create_channel("ioc:enumtest")
    connected = ca.connect_channel(ioc, timeout=0.5)
    assert connected, "Could not connect to ioc channel " + ca.name(gw)

    # gateway should show one VC and one connected active PV
    gateway_stats.update()
    assert gateway_stats.vctotal == 1
    assert gateway_stats.pvtotal == 1
    assert gateway_stats.connected == 1
    assert gateway_stats.active == 1
    assert gateway_stats.inactive == 0

    # enum string should not have been updated
    ioc_ctrl = ca.get_ctrlvars(ioc)
    oneStr = ioc_ctrl["enum_strs"][1]
    assert oneStr == "one", "Expected IOC enum[1]: one; actual enum[1]: " + oneStr
    gw_ctrl = ca.get_ctrlvars(gw)
    oneStr = gw_ctrl["enum_strs"][1]
    assert oneStr == "one", "Expected GW enum[1]: one; actual enum[1]: " + oneStr

    # set enum string on IOC
    ioc_enum1 = ca.create_channel("ioc:enumtest.ONST")
    ca.put(ioc_enum1, "uno", wait=True)
    time.sleep(0.1)

    # Now the enum string should have been updated (if IOC supports DBE_PROPERTY)
    ioc_ctrl = ca.get_ctrlvars(ioc)
    oneStr = ioc_ctrl["enum_strs"][1]
    assert oneStr == "uno", "Expected IOC enum[1]: uno; actual enum[1]: " + oneStr
    if prop_supported:
        gw_expected = "uno"
    else:
        gw_expected = "one"
    gw_ctrl = ca.get_ctrlvars(gw)
    oneStr = gw_ctrl["enum_strs"][1]
    assert (
        oneStr == gw_expected
    ), f"Expected GW enum[1]: {gw_expected}; actual enum[1]: {oneStr}"


def test_enum_prop_cache_value_get_disconnect_ctrl_get(
    standard_env: conftest.EnvironmentInfo,
):
    """
    Get PV (value) through GW - disconnect client - change ENUM string
    directly - get the DBR_CTRL of the PV through GW
    """
    gateway_stats = conftest.GatewayStats()
    # gateway should show no VC (client side connection) and no PV (IOC side connection)
    assert gateway_stats.vctotal == 0
    assert gateway_stats.pvtotal == 0
    assert gateway_stats.connected == 0
    assert gateway_stats.active == 0
    assert gateway_stats.inactive == 0

    # enumtest is an mbbi record with three strings defined: zero one two
    gw = ca.create_channel("gateway:enumtest")
    connected = ca.connect_channel(gw, timeout=0.5)
    assert connected, "Could not connect to gateway channel " + ca.name(gw)
    ioc = ca.create_channel("ioc:enumtest")
    connected = ca.connect_channel(ioc, timeout=0.5)
    assert connected, "Could not connect to ioc channel " + ca.name(gw)

    # gateway should show one VC and one connected active PV
    gateway_stats.update()
    assert gateway_stats.vctotal == 1
    assert gateway_stats.pvtotal == 1
    assert gateway_stats.connected == 1
    assert gateway_stats.active == 1
    assert gateway_stats.inactive == 0

    # enum string should not have been updated
    ioc_ctrl = ca.get_ctrlvars(ioc)
    oneStr = ioc_ctrl["enum_strs"][1]
    assert oneStr == "one", "Expected IOC enum[1]: one; actual enum[1]: " + oneStr
    gw_ctrl = ca.get_ctrlvars(gw)
    oneStr = gw_ctrl["enum_strs"][1]
    assert oneStr == "one", "Expected GW enum[1]: one; actual enum[1]: " + oneStr

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

    # set enum string on IOC
    ioc_enum1 = ca.create_channel("ioc:enumtest.ONST")
    ca.put(ioc_enum1, "uno", wait=True)
    time.sleep(0.1)

    # reconnect Gateway and IOC
    gw = ca.create_channel("gateway:enumtest")
    connected = ca.connect_channel(gw, timeout=0.5)
    assert connected, "Could not connect to gateway channel " + ca.name(gw)
    ioc = ca.create_channel("ioc:enumtest")
    connected = ca.connect_channel(ioc, timeout=0.5)
    assert connected, "Could not connect to ioc channel " + ca.name(gw)

    # gateway should show one VC and one connected active PV
    gateway_stats.update()
    assert gateway_stats.vctotal == 1
    assert gateway_stats.pvtotal == 1
    assert gateway_stats.connected == 1
    assert gateway_stats.active == 1
    assert gateway_stats.inactive == 0

    # Now the enum string should have been updated
    ioc_ctrl = ca.get_ctrlvars(ioc)
    oneStr = ioc_ctrl["enum_strs"][1]
    assert oneStr == "uno", "Expected IOC enum[1]: uno; actual enum[1]: " + oneStr
    gw_ctrl = ca.get_ctrlvars(gw)
    oneStr = gw_ctrl["enum_strs"][1]
    assert oneStr == "uno", "Expected GW enum[1]: uno; actual enum[1]: " + oneStr
