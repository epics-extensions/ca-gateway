#!/usr/bin/env python
import dataclasses
import logging
import textwrap
from typing import List, Optional

import pytest

from . import conftest

try:
    from . import util
except ImportError as ex:
    have_requirements = pytest.mark.skip(reason=f"Missing dependencies: {ex}")
else:

    def have_requirements(func):
        return func


logger = logging.getLogger(__name__)


# Keep the header with regex rules separate; \\ is a pain to deal with in
# strings.
#
# NOTE: this pvlist is done in BRE regex format and not PCRE and assumes
# the gateway was built with it.
pvlist_header = r"""
EVALUATION ORDER ALLOW, DENY
gateway:\(.*\)  ALIAS ioc:\1
ioc:.*          DENY
gwtest:.*       ALLOW
"""

pvlist_footer = r"""
"""


def with_pvlist_header(pvlist_rules: str) -> str:
    """Add on the 'standard' pvlist header to the provided rules."""
    return "\n".join((pvlist_header, textwrap.dedent(pvlist_rules), pvlist_footer))


@dataclasses.dataclass
class AccessCheck:
    hostname: str
    pvname: str
    access: str
    username: Optional[str] = None


def check_permissions(
    access_contents: str, pvlist_contents: str, access_checks: List[AccessCheck]
):
    pvlist_contents = with_pvlist_header(pvlist_contents)
    with conftest.custom_environment(access_contents, pvlist_contents):
        for access_check in access_checks:
            logger.info("Testing %s", access_check)
            result = util.caget_from_host(
                access_check.hostname,
                access_check.pvname,
                username=access_check.username,
            )
            assert access_check.access == result.access, str(access_check)


@have_requirements
@pytest.mark.parametrize(
    "access_contents",
    [
        pytest.param(
            """\
            HAG(mfxhosts) {mfx-control,mfx-console}
            ASG(DEFAULT) {
                RULE(1,READ)
            }

            ASG(RWMFX) {
                RULE(1,READ)
                RULE(1,WRITE,TRAPWRITE){
                  HAG(mfxhosts)
                }
            }
            """,
            id="minimal",
        ),
    ],
)
@pytest.mark.parametrize(
    "pvlist_contents, access_checks",
    [
        pytest.param(
            """
            gateway:HUGO:ENUM  ALIAS ioc:HUGO:ENUM RWMFX
            gateway:HUGO:AI    ALIAS ioc:HUGO:AI DEFAULT
            """,
            [
                AccessCheck("mfx-control", "gateway:HUGO:ENUM", "WRITE|READ"),
                AccessCheck("mfx-console", "gateway:HUGO:ENUM", "WRITE|READ"),
                AccessCheck("anyhost", "gateway:HUGO:ENUM", "READ"),
                AccessCheck("mfx-control", "gateway:HUGO:AI", "READ"),
                AccessCheck("mfx-console", "gateway:HUGO:AI", "READ"),
                AccessCheck("anyhost", "gateway:HUGO:AI", "READ"),
            ],
            id="test",
        ),
    ],
)
def test_permissions_by_host_aliased(
    access_contents: str, pvlist_contents: str, access_checks: List[AccessCheck]
):
    check_permissions(access_contents, pvlist_contents, access_checks)


@have_requirements
@pytest.mark.parametrize(
    "access_contents",
    [
        pytest.param(
            """\
            UAG(testusers) {usera,userb}
            ASG(DEFAULT) {
                RULE(1,READ)
            }

            ASG(RWTESTUSERS) {
                RULE(1,READ)
                RULE(1,WRITE,TRAPWRITE){
                  UAG(testusers)
                }
            }
            """,
            id="minimal",
        ),
    ],
)
@pytest.mark.parametrize(
    "pvlist_contents, access_checks",
    [
        pytest.param(
            """
            gateway:HUGO:ENUM  ALIAS ioc:HUGO:ENUM RWTESTUSERS
            gateway:HUGO:AI    ALIAS ioc:HUGO:AI DEFAULT
            """,
            [
                AccessCheck(
                    "mfx-control", "gateway:HUGO:ENUM", "WRITE|READ", username="usera"
                ),
                AccessCheck(
                    "mfx-console", "gateway:HUGO:ENUM", "READ", username="userc"
                ),
                AccessCheck(
                    "anyhost", "gateway:HUGO:ENUM", "WRITE|READ", username="userb"
                ),
                AccessCheck("mfx-control", "gateway:HUGO:AI", "READ", username="userc"),
                AccessCheck("mfx-console", "gateway:HUGO:AI", "READ", username="usera"),
                AccessCheck("anyhost", "gateway:HUGO:AI", "READ", username="usera"),
            ],
            id="test",
        ),
    ],
)
def test_permissions_by_user_aliased(
    access_contents: str, pvlist_contents: str, access_checks: List[AccessCheck]
):
    check_permissions(access_contents, pvlist_contents, access_checks)


@have_requirements
@pytest.mark.parametrize(
    "access_contents",
    [
        pytest.param(
            """\
            HAG(mfxhosts) {mfx-control,mfx-console}
            ASG(DEFAULT) {
                RULE(1,READ)
            }

            ASG(RWMFX) {
                RULE(1,READ)
                RULE(1,WRITE,TRAPWRITE){
                  HAG(mfxhosts)
                }
            }
            """,
            id="minimal",
        ),
    ],
)
@pytest.mark.parametrize(
    "pvlist_contents, access_checks",
    [
        pytest.param(
            """
            EVALUATION ORDER ALLOW, DENY
            ioc:HUGO:ENUM  ALLOW RWMFX
            ioc:HUGO:AI    ALLOW
            """,
            [
                AccessCheck("mfx-control", "ioc:HUGO:ENUM", "WRITE|READ"),
                AccessCheck("mfx-console", "ioc:HUGO:ENUM", "WRITE|READ"),
                AccessCheck("anyhost", "ioc:HUGO:ENUM", "READ"),
                AccessCheck("mfx-control", "ioc:HUGO:AI", "READ"),
                AccessCheck("mfx-console", "ioc:HUGO:AI", "READ"),
                AccessCheck("anyhost", "ioc:HUGO:AI", "READ"),
            ],
            id="test",
        ),
    ],
)
def test_permissions_by_user_direct(
    access_contents: str, pvlist_contents: str, access_checks: List[AccessCheck]
):
    # pvlist_contents = with_pvlist_header(pvlist_contents)
    with conftest.custom_environment(access_contents, pvlist_contents):
        with conftest.gateway_channel_access_env():
            for access_check in access_checks:
                logger.info("Testing %s", access_check)
                result = util.caget_from_host(
                    access_check.hostname,
                    access_check.pvname,
                    username=access_check.username,
                )
                assert access_check.access == result.access, str(access_check)


@pytest.mark.parametrize(
    "access_contents",
    [
        pytest.param(
            """\
            HAG(mfxhosts) {mfx-control,mfx-console}
            HAG(testhosts) {localhost}
            ASG(DEFAULT) {
                RULE(1,READ)
            }
            """,
            id="minimal",
        ),
    ],
)
@pytest.mark.parametrize(
    "pvlist_contents, localhost_allow",
    [
        pytest.param(
            """\
            EVALUATION ORDER ALLOW, DENY
            ioc:HUGO:ENUM  DENY
            ioc:HUGO:AI    ALLOW
            """,
            False,
            id="blanket_deny",
        ),
        pytest.param(
            """\
            EVALUATION ORDER ALLOW, DENY
            ioc:HUGO:ENUM  DENY FROM testhosts
            ioc:HUGO:AI    ALLOW
            """,
            False,
            id="deny_localhost",
        ),
        pytest.param(
            """\
            EVALUATION ORDER ALLOW, DENY
            ioc:.*            ALLOW
            ioc:HUGO:ENUM      DENY FROM psbuild-rhel7-01
            """,
            True,
            id="deny_others",
        ),
    ],
)
def test_permissions_with_deny(
    access_contents: str, pvlist_contents: str, localhost_allow: bool
):
    allow_pv = "ioc:HUGO:AI"
    deny_pv = "ioc:HUGO:ENUM"
    host_to_check = "localhost"

    # pvlist_contents = with_pvlist_header(pvlist_contents)
    with conftest.custom_environment(access_contents, pvlist_contents):
        for pvname, should_exist in [(allow_pv, True), (deny_pv, localhost_allow)]:
            # Baseline using direct IOC communication
            logger.info("Testing %s (should exist: %s)", pvname, should_exist)
            with conftest.ioc_channel_access_env():
                baseline = util.caget_from_host(host_to_check, pvname)
                assert baseline.access == "WRITE|READ"

            with conftest.gateway_channel_access_env():
                result = util.caget_from_host(host_to_check, pvname)
                if not should_exist:
                    assert result.error == "timeout", "Should not exist on the gateway"
                else:
                    assert not result.error, "Should exist on the gateway"
                    assert result.access == "READ"
