import copy
import functools
import logging
import math
import time
from typing import Any, List

import pytest
from epics import ca, dbr

from . import conftest

logger = logging.getLogger(__name__)

masks = pytest.mark.parametrize(
    "mask",
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

value_masks = pytest.mark.parametrize(
    "mask",
    [
        pytest.param(dbr.DBE_VALUE, id="DBE_VALUE"),
        pytest.param(dbr.DBE_VALUE | dbr.DBE_PROPERTY, id="DBE_VALUE|DBE_PROPERTY"),
        pytest.param(dbr.DBE_VALUE | dbr.DBE_LOG, id="DBE_VALUE|DBE_LOG"),
        pytest.param(dbr.DBE_VALUE | dbr.DBE_ALARM, id="DBE_VALUE|DBE_ALARM"),
    ],
)


forms = pytest.mark.parametrize(
    "form",
    [
        "ctrl",
        "time",
    ],
)


@pytest.mark.parametrize(
    "pvname",
    [
        "HUGO:AI",
        "HUGO:ENUM",
        # "auto",      # <-- updates based on auto:cnt
        # "auto:cnt",  # <-- updates periodically
        "enumtest",
        "gwcachetest",
        "passive0",
        "passiveADEL",
        "passiveADELALRM",
        "passiveALRM",
        "passiveMBBI",
        "passivelongin",
        "bigpassivewaveform",
        "fillingaai",
        "fillingaao",
        "fillingcompress",
        "fillingwaveform",
        "passivewaveform",
    ],
)
@masks
@forms
def test_subscription_on_connection(
    standard_env: conftest.EnvironmentInfo, pvname: str, mask: int, form: str
):
    """
    Basic subscription test.

    For the provided pv name, mask, and form (ctrl/time), do we receive the same
    subscription updates on connection to the gateway/IOC?
    """
    gateway_events = []
    ioc_events = []

    def on_change(event_list, pvname=None, chid=None, **kwargs):
        event_list.append(copy.deepcopy(kwargs))

    with conftest.ca_subscription_pair(
        pvname,
        ioc_callback=functools.partial(on_change, ioc_events),
        gateway_callback=functools.partial(on_change, gateway_events),
        form=form,
        mask=mask,
    ):
        time.sleep(0.1)

    compare_subscription_events(
        gateway_events,
        form,
        ioc_events,
        strict=False,
        nan_strict=False,
    )


def is_acceptable_nan_difference(value1, value2) -> bool:
    """Are (value1, value2) either NaN or 0.0?"""
    if not isinstance(value1, (int, float)) or not isinstance(value2, (int, float)):
        return False

    # Value1 = nan, value2 = {0, nan}
    if math.isnan(value1):
        return value2 == 0.0 or math.isnan(value2)

    # Value2 = nan, value1 = {0, nan}
    if math.isnan(value2):
        return value1 == 0.0 or math.isnan(value1)

    # Something else is different (value1 != value2)
    return False


def deduplicate_events(events: List[dict]) -> List[dict]:
    """De-duplicate identical subsequent events in the list."""
    if not events:
        return []

    result = [events[0]]
    for event in events[1:]:
        if conftest.compare_structures(result[-1], event) != "":
            logger.warning("Removing duplicate event: %s", event)
            result.append(event)

    return result


def compare_subscription_events(
    gateway_events: List[dict],
    form: str,
    ioc_events: List[dict],
    strict: bool = False,
    nan_strict: bool = False,
    deduplicate: bool = True,
):
    """
    Compare subscription events.
    """
    for event_idx, ioc_event in enumerate(ioc_events, 1):
        logger.info("IOC %d/%d: %s", event_idx, len(ioc_events), ioc_event)

    for event_idx, gateway_event in enumerate(gateway_events, 1):
        logger.info("Gateway %d/%d: %s", event_idx, len(gateway_events), gateway_event)

    if deduplicate:
        gateway_events = deduplicate_events(gateway_events)

    for event_idx, (gateway_event, ioc_event) in enumerate(
        zip(gateway_events, ioc_events), 1
    ):
        # assert gateway_event == ioc_event
        if form == "ctrl":
            # Ignore timestamp for control events, if it made its way into the
            # dictionary somehow.
            gateway_event = dict(gateway_event)
            ioc_event = dict(ioc_event)
            gateway_event.pop("timestamp", None)
            ioc_event.pop("timestamp", None)

        differences = conftest.compare_structures(gateway_event, ioc_event)
        if not differences:
            logger.info(
                "Event %d is identical, with value=%s timestamp=%s",
                event_idx,
                gateway_event.get("value"),
                gateway_event.get("timestamp"),
            )
            continue

        if all(
            is_acceptable_nan_difference(value1, value2)
            for _, value1, value2 in conftest.find_differences(
                gateway_event,
                ioc_event,
                "gateway",
                "ioc",
            )
        ):
            if nan_strict:
                raise RuntimeError(
                    f"NaN-handling differences in event {event_idx} of "
                    f"IOC={len(ioc_events)} GW={len(gateway_events)}:\n {differences}"
                )
            else:
                logger.warning("Partial passed comparison - NaN handling issue.")
                continue

        raise RuntimeError(
            f"Differences in event {event_idx} of IOC={len(ioc_events)} "
            f"GW={len(gateway_events)}:\n{differences}"
        )

    if len(gateway_events) == 2 and len(ioc_events) == 1:
        differences = conftest.compare_structures(
            gateway_events[0],
            gateway_events[1],
            desc1="event 0",
            desc2="event 1",
        )
        if not differences:
            if strict:
                raise RuntimeError("Duplicate initial event received")

            logger.warning(
                "Partial passed test - gateway behaves slightly differently.  "
                "Gateway duplicates initial subscription callback for this mask."
            )
            return

        if all(
            is_acceptable_nan_difference(value1, value2)
            for _, value1, value2 in conftest.find_differences(
                gateway_events[0],
                gateway_events[1],
                desc1="event 0",
                desc2="event 1",
            )
        ):
            if nan_strict:
                raise RuntimeError(f"NaN handling difference:\n{differences}")

            logger.warning(
                "Partial passed test - gateway behaves slightly differently.  "
                "NaN and 0.0 values are mixed."
            )
            return
        else:
            raise RuntimeError(f"Differences in events:\n{differences}")

    assert len(gateway_events) == len(ioc_events), (
        f"Gateway events = {len(gateway_events)}, "
        f"but IOC events = {len(ioc_events)}."
    )


@pytest.mark.parametrize(
    "pvname, values",
    [
        pytest.param("HUGO:AI", [0.2, 1.2]),
        pytest.param("HUGO:ENUM", [1, 2]),
        pytest.param("enumtest", [1, 2]),
        pytest.param("gwcachetest", [-20, 0, 20]),
        pytest.param("passive0", [1, 21]),
        pytest.param("passiveADEL", [1, 20]),
        pytest.param("passiveADELALRM", [1, 20]),
        pytest.param("passiveALRM", [1, 5, 10]),
        pytest.param("passiveMBBI", [1, 2]),
        pytest.param("passivelongin", [1, 2]),
        pytest.param("bigpassivewaveform", [[1, 2, 3], [4, 5, 6]]),
        # pytest.param("fillingaai", []),
        # pytest.param("fillingaao", []),
        # pytest.param("fillingcompress", []),
        # pytest.param("fillingwaveform", []),
        # pytest.param("passivewaveform", []),
    ],
)
@forms
@value_masks
def test_subscription_with_put(
    standard_env: conftest.EnvironmentInfo,
    pvname: str,
    mask: int,
    form: str,
    values: List[Any],
):
    """
    Putting a value to the IOC and compare subscription events.

    For the provided pv name, mask, and form (ctrl/time), do we receive the same
    subscription updates after putting values to the IOC?
    """
    gateway_events = []
    ioc_events = []

    def on_change(event_list, pvname=None, chid=None, **kwargs):
        event_list.append(kwargs)

    with conftest.ca_subscription_pair(
        pvname,
        ioc_callback=functools.partial(on_change, ioc_events),
        gateway_callback=functools.partial(on_change, gateway_events),
        form=form,
        mask=mask,
    ) as (ioc_ch, gateway_ch):
        # Time for initial monitor event
        time.sleep(0.2)
        # Throw away initial events; we care what happens from now on
        del gateway_events[:]
        del ioc_events[:]
        for value in values:
            ca.put(ioc_ch, value)
            time.sleep(0.3)
        time.sleep(0.3)

    compare_subscription_events(
        gateway_events,
        form,
        ioc_events,
        strict=True,
        nan_strict=False,
    )
