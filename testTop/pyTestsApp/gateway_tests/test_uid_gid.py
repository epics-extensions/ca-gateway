"""
Tests for the -uid and -gid options (dropping privileges).

The tests that actually drop privileges need to start the gateway as root.
They use passwordless sudo and are skipped if that is not available.
They read /proc/<pid>/status and are therefore Linux only.
"""
import os
import subprocess
import sys
import tempfile
import time
from typing import List

import epics
import pytest

from . import config, conftest

needs_root = pytest.mark.skipif(
    not sys.platform.startswith("linux")
    or os.getuid() == 0
    or not conftest.sudo_available(),
    reason="needs Linux, a non-root user, and passwordless sudo",
)

# Port of the second IOC for the issue #63 scenario
SECOND_IOC_PORT = config.default_ioc_port + 2

SECOND_IOC_DB = """
record(calc, "ioc:second:cnt") {
    field(SCAN, ".1 second")
    field(CALC, "A+1")
    field(INPA, "ioc:second:cnt")
}
"""


def _run_gateway_cmd(*args: str, timeout: float = 10.0) -> subprocess.CompletedProcess:
    """Run a gateway that is expected to exit on its own."""
    cmd = conftest.gateway_command(*args)
    try:
        return subprocess.run(
            cmd,
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=timeout,
        )
    except subprocess.TimeoutExpired:
        pytest.fail(f"Gateway did not exit: {' '.join(cmd)}")


def _proc_status_ids(pid: int, key: str) -> List[int]:
    """Get the ids of a "Uid:", "Gid:" or "Groups:" line from /proc/<pid>/status."""
    with open(f"/proc/{pid}/status") as f:
        for line in f:
            if line.startswith(key + ":"):
                return [int(v) for v in line.split()[1:]]
    raise RuntimeError(f"No {key} in /proc/{pid}/status")


@pytest.mark.parametrize("option", ["-uid", "-gid"])
@pytest.mark.parametrize("value", ["abc", "-5"])
def test_bad_id_value(option, value):
    result = _run_gateway_cmd(option, value)
    assert result.returncode != 0
    assert b"Bad command line" in result.stdout


@pytest.mark.skipif(
    sys.platform.startswith("win") or os.getuid() == 0,
    reason="needs a non-root user on a POSIX system",
)
def test_failed_uid_change_aborts():
    """A non-root user cannot switch to root: the gateway must not start."""
    result = _run_gateway_cmd("-uid", "0")
    assert result.returncode != 0
    assert b"Cannot set user id" in result.stdout


@pytest.mark.skipif(
    sys.platform.startswith("win") or os.getuid() == 0 or os.getgid() == 0,
    reason="needs a non-root user and group on a POSIX system",
)
def test_failed_gid_change_aborts():
    """A non-root user cannot switch to group root: the gateway must not start."""
    result = _run_gateway_cmd("-gid", "0")
    assert result.returncode != 0
    assert b"Cannot set group id" in result.stdout


@pytest.mark.skipif(sys.platform.startswith("win"), reason="needs a POSIX system")
def test_own_ids():
    """Specifying the current user and group works without privileges."""
    with (
        conftest.run_gateway("-uid", str(os.getuid()), "-gid", str(os.getgid())),
        conftest.run_ioc(),
        conftest.local_channel_access(),
    ):
        pv = epics.PV("gateway:passive0")
        assert pv.wait_for_connection(timeout=5.0)
        assert pv.get(timeout=5.0) is not None


@needs_root
@pytest.mark.parametrize("order", ["uid-first", "gid-first"])
def test_drop_privileges(order):
    """Started as root, the gateway must end up with exactly the given ids."""
    uid, gid = os.getuid(), os.getgid()
    uid_args = ["-uid", str(uid)]
    gid_args = ["-gid", str(gid)]
    args = uid_args + gid_args if order == "uid-first" else gid_args + uid_args

    with (
        conftest.run_gateway(*args, as_root=True) as gateway,
        conftest.run_ioc(),
        conftest.local_channel_access(),
    ):
        # Once the gateway serves PVs, it has dropped its privileges
        pv = epics.PV("gateway:passive0")
        assert pv.wait_for_connection(timeout=5.0)

        pid = gateway.gateway_pid
        assert _proc_status_ids(pid, "Uid") == [uid] * 4
        assert _proc_status_ids(pid, "Gid") == [gid] * 4
        assert _proc_status_ids(pid, "Groups") == [gid]


@needs_root
def test_ioc_disappears_issue_63():
    """
    Scenario from issue #63: gateway started as root with -uid/-gid,
    PVs from two IOCs monitored, one IOC is stopped.
    The gateway must survive and keep serving the other IOC's PV.
    """
    cip = f"localhost localhost:{SECOND_IOC_PORT}"
    with (
        tempfile.NamedTemporaryFile(mode="w", suffix=".db") as db,
        conftest.run_gateway(
            "-uid", str(os.getuid()), "-gid", str(os.getgid()),
            cip=cip, as_root=True,
        ),
        conftest.run_ioc(),
    ):
        db.write(SECOND_IOC_DB)
        db.flush()
        with (
            conftest.run_ioc(db_file=db.name, ioc_port=SECOND_IOC_PORT) as second_ioc,
            conftest.local_channel_access(),
        ):
            updates = {"auto": 0, "second": 0}

            def counter(name):
                def callback(**_):
                    updates[name] += 1
                return callback

            pv_auto = epics.PV("gateway:auto", callback=counter("auto"))
            pv_second = epics.PV("gateway:second:cnt", callback=counter("second"))
            assert pv_auto.wait_for_connection(timeout=5.0)
            assert pv_second.wait_for_connection(timeout=5.0)
            time.sleep(2.0)
            assert updates["second"] > 0

            # Stop the second IOC while its PV is being monitored
            second_ioc.kill()
            second_ioc.wait()
            time.sleep(1.0)
            assert not pv_second.connected

            # The other IOC's PV must keep updating through the gateway
            before = updates["auto"]
            time.sleep(5.0)
            assert updates["auto"] > before
            assert pv_auto.get(timeout=5.0, use_monitor=False) is not None
