#!/usr/bin/env python
import logging
import time

import pytest
from epics import ca, dbr

from . import conftest

logger = logging.getLogger(__name__)


UNDEFINED_TIMESTAMP = dbr.EPICS2UNIX_EPOCH


def timestamp_to_string(timestamp: float) -> str:
    if timestamp == UNDEFINED_TIMESTAMP:
        return "<undefined>"
    return time.ctime(timestamp)


@pytest.mark.parametrize(
    "subscription_mask",
    [
        pytest.param(dbr.DBE_VALUE, id="DBE_VALUE"),
        pytest.param(dbr.DBE_LOG, id="DBE_LOG"),
        pytest.param(dbr.DBE_ALARM, id="DBE_ALARM"),
        pytest.param(dbr.DBE_PROPERTY, id="DBE_PROPERTY"),
        pytest.param(dbr.DBE_VALUE | dbr.DBE_PROPERTY, id="DBE_VALUE|DBE_PROPERTY"),
        pytest.param(dbr.DBE_VALUE | dbr.DBE_LOG, id="DBE_VALUE|DBE_LOG"),
        pytest.param(dbr.DBE_VALUE | dbr.DBE_ALARM, id="DBE_VALUE|DBE_ALARM"),
    ],
)
def test_undefined_timestamp_subscription(
    standard_env: conftest.EnvironmentInfo, subscription_mask: int
):
    """
    caget on an mbbi - with subscription configured.

    All timestamps should be defined.
    """
    gateway_events_received = 0
    ioc_events_received = 0

    def on_change_gateway(pvname=None, value=None, timestamp=None, **kwargs):
        nonlocal gateway_events_received
        gateway_events_received += 1
        logger.info(
            f" GW update: {pvname} changed to {value} at %s",
            timestamp_to_string(timestamp),
        )

    def on_change_ioc(pvname=None, value=None, timestamp=None, **kwargs):
        nonlocal ioc_events_received
        ioc_events_received += 1
        logger.info(
            f"IOC update: {pvname} changed to {value} at %s",
            timestamp_to_string(timestamp),
        )

    with conftest.ca_subscription_pair(
        "HUGO:ENUM",
        ioc_callback=on_change_ioc,
        gateway_callback=on_change_gateway,
        mask=subscription_mask,
    ) as (ioc_ch, gateway_ch):
        for iteration in range(1, 5):
            logger.info("Iteration %d", iteration)
            ioc_md = ca.get_with_metadata(ioc_ch, ftype=dbr.TIME_ENUM)
            gateway_md = ca.get_with_metadata(gateway_ch, ftype=dbr.TIME_ENUM)
            assert ioc_md == gateway_md

            if ioc_md["status"] != dbr.AlarmStatus.UDF:
                assert (
                    gateway_md["status"] != dbr.AlarmStatus.UDF
                ), "2nd CA get is undefined!"
            assert gateway_md["timestamp"] != 0, "2nd CA get timestamp is undefined!"
            assert ioc_md["value"] == gateway_md["value"]


def test_undefined_timestamp_get_only(standard_env: conftest.EnvironmentInfo):
    """
    caget on an mbbi - without subscription configured.

    All timestamps should be defined.
    """
    for iteration in range(1, 5):
        logger.info("Iteration %d", iteration)
        ioc_md, gateway_md = conftest.pyepics_caget_pair("HUGO:ENUM", form="time")
        logger.info(
            "IOC timestamp: %s (%s)",
            ioc_md["timestamp"],
            timestamp_to_string(ioc_md["timestamp"]),
        )
        logger.info(
            "Gateway timestamp: %s (%s)",
            gateway_md["timestamp"],
            timestamp_to_string(gateway_md["timestamp"]),
        )

        assert ioc_md["timestamp"] != UNDEFINED_TIMESTAMP, "IOC timestamp undefined"
        assert (
            gateway_md["timestamp"] != UNDEFINED_TIMESTAMP
        ), "Gateway timestamp undefined"
        assert ioc_md["timestamp"] == gateway_md["timestamp"], "Timestamps not equal"

        if ioc_md["status"] != dbr.AlarmStatus.UDF:
            assert (
                gateway_md["status"] != dbr.AlarmStatus.UDF
            ), "2nd CA get is undefined!"
        assert ioc_md["value"] == gateway_md["value"]
