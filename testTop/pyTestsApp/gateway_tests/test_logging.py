from __future__ import annotations

import contextlib
import dataclasses
import datetime
import json
import logging
import os
import socket
import subprocess
import tempfile
import threading
import time
from typing import Any, Generator, List, Optional, Union, Tuple

import epics
import pytest

from . import config, conftest

logger = logging.getLogger(__name__)


@contextlib.contextmanager
def listen_on_sock(sock: socket.socket, encoding="latin-1") -> Generator[List[str], None, None]:
    """Listen on TCP socket for caPutLog data."""
    data = []

    def listen():
        sock.listen(1)
        client, addr = sock.accept()
        try:
            logger.warning("Accepted client on localhost:%d - %s",
                           sock.getsockname()[1],
                           addr)
            while True:
                read = client.recv(4096)
                logger.info("caPutLog TCP server received %s", read)
                if not read:
                    break
                data.append(read.decode(encoding))
        finally:
            client.close()

    threading.Thread(target=listen, daemon=True).start()
    try:
        yield data
    finally:
        sock.close()


def create_socket(addr: str) -> Tuple[socket.socket, int]:
    """Create a TCP socket on the specified address using a system-chosen port."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    # Try to avoid "address already in use" between successive caputlog tests
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((addr, 0))
    return sock, sock.getsockname()[1]


@dataclasses.dataclass
class Caput:
    """A single caputlog line, parsed into its parts."""

    date: datetime.datetime
    user: str
    host: str
    pvname: str
    value: Union[str, List[str]]
    old: Optional[str] = None

    @classmethod
    def from_line(cls, line: str) -> Caput:
        parts = line.split(" ")
        logger.info("parts %s", parts)
        user, host = parts[3].split("@")
        # We'll make a point of not using such strings in our values, OK?
        if " old=" in line:
            value, old = " ".join(parts[5:]).split(" old=")
        else:
            value, old = parts[5:], None

        return cls(
            date=datetime.datetime.strptime(
                " ".join(parts[:3]), "%b %d %H:%M:%S"
            ).replace(year=datetime.datetime.now().year),
            user=user,
            host=host,
            pvname=parts[4],
            value=value,
            old=old,
        )


@dataclasses.dataclass
class CaputLog:
    """An entire caputlog."""

    header: List[str]
    puts: List[Caput]

    @classmethod
    def from_string(cls, contents: str) -> CaputLog:
        """Parse a caputlog file contents into a CaputLog instance."""
        lines = contents.splitlines()
        try:
            attempted_writes_idx = lines.index("Attempted Writes:")
        except ValueError:
            raise RuntimeError(f"Invalid caputlog? Lines: {lines}")

        header, puts = lines[:attempted_writes_idx], lines[attempted_writes_idx + 1 :]
        logger.info("caputlog header:\n%s", "\n".join(header))
        logger.info("Puts:\n%s", "\n".join(puts))
        return cls(
            header=header,
            puts=[Caput.from_line(line) for line in puts],
        )

    @classmethod
    def from_bytes(cls, raw: bytes) -> CaputLog:
        """Parse a raw caputlog file contents into a CaputLog instance."""
        return cls.from_string(raw.decode("latin-1"))


@pytest.mark.parametrize(
    "access_contents, pvlist_contents",
    [
        pytest.param(
            """\
            ASG(DEFAULT) {
                RULE(1,READ)
                RULE(1,WRITE,TRAPWRITE)
            }
            """,
            """\
            EVALUATION ORDER ALLOW, DENY
            .* ALLOW
            """,
            id="minimal",
        ),
    ],
)
@pytest.mark.parametrize(
    "pvname, values",
    [
        pytest.param("ioc:HUGO:AI", [0.2, 1.2]),
        pytest.param("ioc:HUGO:ENUM", [1, 2]),
        pytest.param("ioc:enumtest", [1, 2]),
        pytest.param("ioc:gwcachetest", [-20, 0, 20]),
        pytest.param("ioc:passive0", [1, 21]),
        pytest.param("ioc:passiveADEL", [1, 20]),
        pytest.param("ioc:passiveADELALRM", [1, 20]),
        pytest.param("ioc:passiveALRM", [1, 5, 10]),
        pytest.param("ioc:passiveMBBI", [1, 2]),
        pytest.param("ioc:passivelongin", [1, 2]),
        pytest.param("ioc:bigpassivewaveform", [[1, 2, 3], [4, 5, 6]]),
    ],
)
@pytest.mark.skipif(os.getenv("SKIP_CAPUTLOG_TESTS") == "true", reason="No caputlog support")
def test_caputlog(
    access_contents: str, pvlist_contents: str, pvname: str, values: List[Any]
):
    """
    Test that caPutLog works by putting to a PV and checking the output.
    """
    sock, putlog_port = create_socket("127.0.0.1")
    with (
        tempfile.NamedTemporaryFile() as caputlog_fp,
        listen_on_sock(sock) as tcp_data,
    ):
        with (
            conftest.custom_environment(
                access_contents=access_contents,
                pvlist_contents=pvlist_contents,
                gateway_args=[
                    "-putlog",
                    caputlog_fp.name,
                    "-caputlog",
                    f"127.0.0.1:{putlog_port}",
                ],
            ) as env,
            conftest.gateway_channel_access_env(),
        ):
            logger.info("Environment: %s", env)
            logger.info("Initial value: %s", conftest.pyepics_caget(pvname))
            for value in values:
                conftest.pyepics_caput(pvname, value)

        caputlog_fp.seek(0)
        caputlog_raw = caputlog_fp.read()

    caputlog = CaputLog.from_bytes(caputlog_raw)

    # TCP caputlog doesn't appear functional; leave in for future usage?
    logger.info("TCP data was:\n%s", tcp_data)
    logger.info("CaputLog:\n%s", caputlog)
    for put, value in zip(caputlog.puts, values):
        assert put.pvname == pvname
        assert put.value == str(value)

    assert len(caputlog.puts) == len(values)


def gateway_has_caputlog_json() -> bool:
    """Was the gateway built with caPutLog JSON support?"""
    result = subprocess.run(
        [config.gateway_executable, "-help"],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        timeout=10,
    )
    return b"-caputlogjson ip_addr:port" in result.stdout


JSON_DB = """
record(stringout, "ioc:json:string") {
    field(VAL, "initial")
}
"""


@pytest.mark.skipif(os.getenv("SKIP_CAPUTLOG_TESTS") == "true", reason="No caputlog support")
def test_caputlog_json():
    """
    Test caPutLog in JSON format (-caputlogjson) for scalars and arrays.

    caPutLog merges consecutive puts to the same PV (burst filter), so each
    put goes to a different PV. The last message is sent after the burst
    timeout (5 seconds).
    """
    if not gateway_has_caputlog_json():
        pytest.skip("Gateway built without caPutLog JSON support")

    access_contents = """\
        ASG(DEFAULT) {
            RULE(1,READ)
            RULE(1,WRITE,TRAPWRITE)
        }
        """
    pvlist_contents = """\
        EVALUATION ORDER ALLOW, DENY
        .* ALLOW
        """
    long_array = list(range(100))
    puts = [
        ("ioc:passive0", 21),
        ("ioc:passivelongin", 2),
        ("ioc:json:string", "hello"),
        ("ioc:passivewaveform", [1, 2, 3]),
        ("ioc:bigpassivewaveform", long_array),
        ("ioc:passiveMBBI", 1),
    ]

    sock, putlog_port = create_socket("127.0.0.1")
    with listen_on_sock(sock) as tcp_data:
        with (
            conftest.custom_environment(
                access_contents=access_contents,
                pvlist_contents=pvlist_contents,
                db_contents=JSON_DB,
                gateway_args=["-caputlogjson", f"127.0.0.1:{putlog_port}"],
            ),
            conftest.gateway_channel_access_env(),
        ):
            # Monitor the PVs, so that the gateway caches their (old) values
            monitors = [epics.PV(pvname, auto_monitor=True) for pvname, _ in puts]
            for pv in monitors:
                assert pv.wait_for_connection(timeout=5.0)
                pv.get(timeout=5.0)

            for pvname, value in puts:
                conftest.pyepics_caput(pvname, value)

            # Wait for all messages (the last one after the burst timeout)
            deadline = time.monotonic() + 15.0
            while time.monotonic() < deadline:
                if "".join(tcp_data).count("\n") >= len(puts):
                    break
                time.sleep(0.2)

    lines = "".join(tcp_data).splitlines()
    logger.info("JSON caPutLog messages:\n%s", "\n".join(lines))
    messages = [json.loads(line) for line in lines]
    assert [msg["pv"] for msg in messages] == [pvname for pvname, _ in puts]

    for msg in messages:
        for key in ("date", "time", "host", "user", "old"):
            assert key in msg, f"{key} missing in {msg}"

    by_pv = {msg["pv"]: msg for msg in messages}

    # Scalars: plain values, no sizes
    assert by_pv["ioc:passive0"]["new"] == 21.0
    assert by_pv["ioc:passive0"]["old"] == 0.0
    assert "new-size" not in by_pv["ioc:passive0"]
    assert by_pv["ioc:passivelongin"]["new"] == 2
    assert by_pv["ioc:passiveMBBI"]["new"] == 1
    assert by_pv["ioc:json:string"]["new"] == "hello"
    assert by_pv["ioc:json:string"]["old"] == "initial"

    # Arrays: lists with sizes
    wf = by_pv["ioc:passivewaveform"]
    assert wf["new"] == [1.0, 2.0, 3.0]
    assert wf["new-size"] == 3
    assert isinstance(wf["old"], list)
    assert isinstance(wf["old-size"], int)

    # Long arrays are truncated to the size of caPutLog's buffer (400 bytes)
    big = by_pv["ioc:bigpassivewaveform"]
    assert big["new-size"] == len(long_array)
    assert big["new"] == [float(v) for v in long_array[: 400 // 8]]
