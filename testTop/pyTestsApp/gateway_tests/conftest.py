"""
CA Gateway test configuration.


Environment variables used:

    EPICS_BASE
    EPICS_HOST_ARCH
    GATEWAY_PVLIST
    GATEWAY_ROOT
    IOC_EPICS_BASE or EPICS_BASE
    VERBOSE
    VERBOSE_GATEWAY

"""

import contextlib
import dataclasses
import gc
import logging
import math
import os
import subprocess
import tempfile
import textwrap
import threading
import time
from concurrent.futures import ProcessPoolExecutor
from typing import (Any, Dict, Generator, List, Mapping, Optional, Protocol,
                    Tuple)

import epics
import pytest

from . import config

logger = logging.getLogger(__name__)


@contextlib.contextmanager
def run_process(
    cmd: List[str],
    env: Dict[str, str],
    verbose: bool = True,
    interactive: bool = False,
    startup_time: float = 0.5,
    wait_for: Optional[bytes] = None,
):
    """
    Run ``cmd`` and yield a subprocess.Popen instance.

    Parameters
    ----------
    cmd :
    """
    verbose = True
    logger.info("Running: %s (verbose=%s)", " ".join(cmd), verbose)

    proc = subprocess.Popen(
        cmd,
        env=env,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
    )

    stdout = None
    startup_stdout = None
    event = threading.Event()
    assert proc.stdin is not None

    def read_stdout():
        """Read standard output in a background thread."""
        nonlocal startup_stdout
        nonlocal stdout
        lines = []
        startup_lines = []
        t0 = time.monotonic()

        assert proc.stdout is not None
        while True:
            line = proc.stdout.readline()
            if not line:
                break

            if event.is_set():
                startup_lines.append(line)
            else:
                lines.append(line)

            if wait_for is not None and wait_for in line:
                # Set the starting event if we see what we're waiting for
                # to indicate the process is ready.
                event.set()

        stdout = b"".join(lines)
        if verbose:
            logger.warning(
                "Read %d bytes from %s in %.1f sec",
                len(stdout),
                cmd[0],
                time.monotonic() - t0,
            )
            if stdout:
                try:
                    stdout = stdout.decode("latin-1")
                except Exception:
                    stdout = str(stdout)

                logger.warning(
                    "Standard output for %s:\n" "    %s\n\n",
                    cmd[0],
                    textwrap.indent(str(stdout), "    "),
                )

    threading.Thread(daemon=True, target=read_stdout).start()

    # Wait for the "ready" message event, up to ``startup_time`` seconds
    event.wait(startup_time)
    try:
        yield proc
    finally:
        if interactive:
            logger.debug("Exiting interactive process %s", cmd[0])
            proc.stdin.close()
        else:
            logger.debug("Terminating non-interactive process %s", cmd[0])
            proc.terminate()
        proc.wait()
        logger.info("Process %s exited", cmd[0])


@contextlib.contextmanager
def run_ioc(
    *arglist: str,
    startup_time: float = 0.5,
    db_file: Optional[str] = config.test_ioc_db,
    dbd_file: Optional[str] = None,
    ioc_port: int = config.default_ioc_port,
    verbose: bool = config.verbose,
) -> Generator[subprocess.Popen, None, None]:
    """
    Starts a test IOC process with the provided configuration.

    Parameters
    ----------
    *arglist : str
        Extra arguments to pass to the IOC process.

    startup_time : float, optional
        Time to wait for the IOC to be ready.

    db_file : str, optional
        Path to the IOC database.  Defaults to ``test_ioc_db``.

    dbd_file : str, optional
        Path to the IOC database definition.  Defaults to using the database
        definition provided with epics-base/softIoc.

    ioc_port : int, optional
        The IOC port number to listen on - defaults to ``default_ioc_port``.
    """
    env = dict(os.environ)
    env["EPICS_CA_SERVER_PORT"] = str(ioc_port)
    env["EPICS_CA_ADDR_LIST"] = "localhost"
    env["EPICS_CA_AUTO_ADDR_LIST"] = "NO"
    # IOC_ environment overrides
    for v in list(os.environ.keys()):
        if v.startswith("IOC_"):
            env[v.replace("IOC_", "", 1)] = os.environ[v]

    assert config.ioc_executable is not None

    cmd = [config.ioc_executable]
    if dbd_file is not None:
        cmd.extend(["-D", dbd_file])

    if db_file is not None:
        cmd.extend(["-d", db_file])

    cmd.extend(arglist)

    with run_process(
        cmd,
        env,
        verbose=verbose,
        interactive=True,
        wait_for=b"epics>",
        startup_time=startup_time,
    ) as proc:
        yield proc


@contextlib.contextmanager
def run_gateway(
    *extra_args: str,
    access: str = config.default_access,
    pvlist: str = config.default_pvlist,
    ioc_port: int = config.default_ioc_port,
    gateway_port: int = config.default_gateway_port,
    verbose: bool = config.verbose_gateway,
    stats_prefix: str = "gwtest",
) -> Generator[subprocess.Popen, None, None]:
    """
    Starts a gateway process with the provided configuration.

    Parameters
    ----------
    *extra_args : str
        Extra arguments to pass to the gateway process.

    access : str, optional
        The access rights file.  Defaults to ``default_access``.

    pvlist : str, optional
        The pvlist filename.  Defaults to ``default_pvlist``.

    ioc_port : int, optional
        The IOC port number - defaults to ``default_ioc_port``.

    gateway_port : int, optional
        The gateway port number - defaults to ``default_gateway_port``.

    verbose : bool, optional
        Configure the gateway to output verbose information.

    stats_prefix : str, optional
        Gateway statistics PV prefix.
    """
    cmd = [
        config.gateway_executable,
        "-sip",
        "localhost",
        "-sport",
        str(gateway_port),
        "-cip",
        "localhost",
        "-cport",
        str(ioc_port),
        "-access",
        access,
        "-pvlist",
        pvlist,
        "-archive",
        "-prefix",
        stats_prefix,
    ]
    cmd.extend(extra_args)

    if verbose:
        cmd.extend(["-debug", str(config.gateway_debug_level)])

    with run_process(
        cmd,
        dict(os.environ),
        verbose=verbose,
        interactive=False,
        wait_for=b"Running as user",
    ) as proc:
        yield proc


@contextlib.contextmanager
def local_channel_access(
    ioc_port: int = config.default_ioc_port,
    gateway_port: int = config.default_gateway_port,
) -> Generator[None, None, None]:
    """
    Configures environment variables to only talk to the provided ports.

    Parameters
    ----------
    ioc_port : int, optional
        The integer port numbers to configure for EPICS_CA_ADDR_LIST for the
        IOC.
    gateway_port : int, optional
        The integer port numbers to configure for EPICS_CA_ADDR_LIST for the
        gateway.
    """
    address_list = " ".join(
        (
            f"localhost:{ioc_port}",
            f"localhost:{gateway_port}",
        )
    )
    with (
        context_set_env("EPICS_CA_AUTO_ADDR_LIST", "NO"),
        context_set_env("EPICS_CA_ADDR_LIST", address_list),
    ):
        _reset_libca()
        yield


@contextlib.contextmanager
def context_set_env(key: str, value: Any):
    """Context manager to set - and then reset - an environment variable."""
    orig_value = os.environ.get(key, None)
    try:
        os.environ[key] = str(value)
        yield orig_value
    finally:
        if orig_value is not None:
            os.environ[key] = orig_value


def is_pyepics_libca_initialized() -> bool:
    """Has pyepics' LIBCA been initialized?"""
    return epics.ca.libca not in (None, epics.ca._LIBCA_FINALIZED)


def _reset_libca() -> None:
    gc.collect()
    if is_pyepics_libca_initialized():
        epics.ca.finalize_libca()
        epics.ca.clear_cache()
        epics.ca.initialize_libca()


@contextlib.contextmanager
def gateway_channel_access_env(port: int = config.default_gateway_port):
    """Set the environment up for communication solely with the spawned gateway."""
    with (
        context_set_env("EPICS_CA_AUTO_ADDR_LIST", "NO"),
        context_set_env("EPICS_CA_ADDR_LIST", f"localhost:{port}"),
    ):
        _reset_libca()
        yield


@contextlib.contextmanager
def ioc_channel_access_env(port: int = config.default_ioc_port):
    """Set the environment up for communication solely with the spawned IOC."""
    with (
        context_set_env("EPICS_CA_AUTO_ADDR_LIST", "NO"),
        context_set_env("EPICS_CA_ADDR_LIST", f"localhost:{port}"),
    ):
        _reset_libca()
        yield


@dataclasses.dataclass
class EnvironmentInfo:
    """
    Test environment information.

    An instance of this is used as a fixture for ``standard_env``.
    """

    access: str
    pvlist: str
    db_file: str
    dbd_file: Optional[str]


@contextlib.contextmanager
def file_test_environment(
    access: str,
    pvlist: str,
    db_file: str,
    dbd_file: Optional[str] = None,
) -> Generator[EnvironmentInfo, None, None]:
    """
    Test environment using already-existing files on disk.

    Parameters
    ----------
    access : str
        The access rights filename.
    pvlist : str
        The pvlist filename.
    db_file : str
        Path to the IOC database.
    dbd_file : str, optional
        Path to the IOC database definition.  Defaults to using the database
        definition provided with epics-base/softIoc.
    """
    with (
        run_gateway(access=access, pvlist=pvlist),
        run_ioc(db_file=db_file, dbd_file=dbd_file),
        local_channel_access(),
    ):
        yield EnvironmentInfo(
            access=access,
            pvlist=pvlist,
            db_file=db_file,
            dbd_file=dbd_file,
        )


@pytest.fixture(scope="function")
def standard_env() -> Generator[EnvironmentInfo, None, None]:
    with file_test_environment(
        access=config.default_access,
        pvlist=config.default_pvlist,
        db_file=config.test_ioc_db,
    ) as env:
        yield env


@contextlib.contextmanager
def custom_environment(
    access_contents: str,
    pvlist_contents: str,
    db_contents: str = "",
    db_file: Optional[str] = config.test_ioc_db,
    dbd_file: Optional[str] = None,
    encoding: str = "latin-1",
    ioc_args: Optional[List[str]] = None,
    gateway_args: Optional[List[str]] = None,
) -> Generator[EnvironmentInfo, None, None]:
    """
    Run a gateway and an IOC in a custom environment, specifying the raw
    contents of the access control file and the pvlist.

    Parameters
    ----------
    access_contents : str, optional
        The gateway access control configuration contents.

    pvlist_contents : str, optional
        The gateway pvlist configuration contents.

    db_contents : str, optional
        Additional database text to add to ``db_file``, if specified.

    db_file : str, optional
        Path to the IOC database.  Defaults to ``test_ioc_db``.  This is loaded
        in addition to ``db_contents``, if specified.

    dbd_file : str, optional
        Path to the IOC database definition.  Defaults to using the database
        definition provided with epics-base/softIoc.
    """
    gateway_args = gateway_args or []
    ioc_args = ioc_args or []
    if db_file is not None:
        with open(db_file, "rt") as fp:
            existing_db_contents = fp.read()
        db_contents = "\n".join((existing_db_contents, textwrap.dedent(db_contents)))

    access_contents = textwrap.dedent(access_contents)
    pvlist_contents = textwrap.dedent(pvlist_contents)
    db_contents = textwrap.dedent(db_contents)
    with (
        tempfile.NamedTemporaryFile() as access_fp,
        tempfile.NamedTemporaryFile() as pvlist_fp,
        tempfile.NamedTemporaryFile() as dbfile_fp,
    ):

        access_fp.write(access_contents.encode(encoding))
        access_fp.flush()

        pvlist_fp.write(pvlist_contents.encode(encoding))
        pvlist_fp.flush()

        dbfile_fp.write(db_contents.encode(encoding))
        dbfile_fp.flush()

        logger.info(
            "Access rights:\n%s",
            textwrap.indent(access_contents, "    "),
        )
        logger.info(
            "PVList:\n%s",
            textwrap.indent(pvlist_contents, "    "),
        )
        with (
            run_gateway(*gateway_args, access=access_fp.name, pvlist=pvlist_fp.name),
            run_ioc(*ioc_args, db_file=dbfile_fp.name, dbd_file=dbd_file),
            local_channel_access(),
        ):
            yield EnvironmentInfo(
                access=access_fp.name,
                pvlist=pvlist_fp.name,
                db_file=dbfile_fp.name,
                dbd_file=dbd_file,
            )


class PyepicsCallback(Protocol):
    def __call__(self, pvname: str = "", value: Any = None, **kwargs) -> None:
        ...


def get_pv_pair(
    pvname: str,
    *,
    ioc_prefix: str = "ioc:",
    gateway_prefix: str = "gateway:",
    ioc_callback: Optional[PyepicsCallback] = None,
    gateway_callback: Optional[PyepicsCallback] = None,
    **kwargs,
) -> Tuple[epics.PV, epics.PV]:
    """
    Get a PV pair - a direct PV and a gateway PV.

    Parameters
    ----------
    pvname : str
        The PV name suffix, not including "ioc:" or "gateway:".

    ioc_prefix : str, optional
        The prefix to add for direct IOC communication.

    gateway_prefix : str, optional
        The prefix to add for gateway communication.

    ioc_callback : callable, optional
        A callback function to use for value updates of the IOC PV.

    gateway_callback : callable, optional
        A callback function to use for value updates of the gateway PV.

    **kwargs :
        Keyword arguments are passed to both ``epics.PV()`` instances.

    Returns
    -------
    ioc_pv : epics.PV
        The direct IOC PV.

    gateway_pv : epics.PV
        The gateway PV.
    """
    ioc_pv = epics.PV(ioc_prefix + pvname, **kwargs)
    if ioc_callback is not None:
        ioc_pv.add_callback(ioc_callback)
    ioc_pv.wait_for_connection()

    gateway_pv = epics.PV(gateway_prefix + pvname, **kwargs)
    if gateway_callback is not None:
        gateway_pv.add_callback(gateway_callback)
    gateway_pv.wait_for_connection()
    return (ioc_pv, gateway_pv)


class GatewayStats:
    """
    Gateway statistics interface.

    Instantiate and call ``.update()`` to retrieve the number of virtual
    circuits through ``.vctotal``, for example.
    """

    vctotal: Optional[int] = None
    pvtotal: Optional[int] = None
    connected: Optional[int] = None
    active: Optional[int] = None
    inactive: Optional[int] = None

    def __init__(self, prefix="gwtest:"):
        self._vctotal = epics.ca.create_channel(f"{prefix}vctotal")
        self._pvtotal = epics.ca.create_channel(f"{prefix}pvtotal")
        self._connected = epics.ca.create_channel(f"{prefix}connected")
        self._active = epics.ca.create_channel(f"{prefix}active")
        self._inactive = epics.ca.create_channel(f"{prefix}inactive")
        self.update()

    def update(self):
        """Update gateway statistics."""
        self.vctotal = epics.ca.get(self._vctotal)
        self.pvtotal = epics.ca.get(self._pvtotal)
        self.connected = epics.ca.get(self._connected)
        self.active = epics.ca.get(self._active)
        self.inactive = epics.ca.get(self._inactive)


def get_prop_support(*, reset_pyepics: bool = True):
    """Is DBE_PROPERTY supported?"""
    events_received_ioc = 0

    if not is_pyepics_libca_initialized():
        # This is intended to be run in a subprocess, isolated from the test
        # environment.  The CA context may not be set yet, or set correctly.
        # Reset it here.
        epics.ca.initialize_libca()

    def on_change_ioc(**_):
        nonlocal events_received_ioc
        events_received_ioc += 1

    with file_test_environment(
        access=config.default_access,
        pvlist=config.default_pvlist,
        db_file=config.test_ioc_db,
    ):
        passive0 = epics.PV("ioc:passive0", auto_monitor=epics.dbr.DBE_PROPERTY)
        if not passive0.wait_for_connection():
            raise RuntimeError(
                "Unable to check for DBE_PROPERTY support; failed to connect to "
                "test PV."
            )
        passive0.add_callback(on_change_ioc)
        passive0.get()

        passive0_high = epics.PV("ioc:passive0.HIGH", auto_monitor=None)
        passive0_high.put(18.0, wait=True)
        time.sleep(0.2)

    return events_received_ioc == 2


@pytest.fixture(scope="session")
def prop_supported() -> bool:
    """Is DBE_PROPERTY supported?"""
    with ProcessPoolExecutor() as exec:
        future = exec.submit(get_prop_support)

    return future.result()


def find_differences(
    struct1: Mapping[str, Any],
    struct2: Mapping[str, Any],
    desc1: str,
    desc2: str,
    skip_keys: Optional[List[str]] = None,
) -> Generator[Tuple[str, Any, Any], None, None]:
    """
    Compare two "structures" and yield keys and values which differ.

    Parameters
    ----------
    struct1 : dict
        The first structure to compare.  Pairs with the user-friendly ``desc1``
        description. This is a pyepics-provided dictionaries of information
        such as timestamp, value, alarm status, and so on.

    struct2 : dict
        The second structure to compare.  Pairs with the user-friendly
        ``desc2`` description.

    desc1 : str
        A description for the first structure.

    desc2 : str
        A description for the second structure.

    skip_keys : list of str, optional
        List of keys to skip when comparing.  Defaults to ['chid'].

    Yields
    ------
    key : str
        The key that differs.

    value1 :
        The value from struct1.

    value1 :
        The value from struct2.
    """
    if skip_keys is None:
        skip_keys = ["chid"]

    for key in sorted(set(struct1).union(struct2)):
        if key in skip_keys:
            continue

        try:
            value1 = struct1[key]
        except KeyError:
            raise RuntimeError(f"Missing key {key} in the {desc1} struct")

        try:
            value2 = struct2[key]
        except KeyError:
            raise RuntimeError(f"Missing key {key} in the {desc2} struct")

        if hasattr(value2, "tolist"):
            value2 = tuple(value2.tolist())
        if hasattr(value1, "tolist"):
            value1 = tuple(value1.tolist())

        try:
            if math.isnan(value1) and math.isnan(value2):
                # nan != nan, remember?
                continue
        except TypeError:
            ...

        if value2 != value1:
            yield key, value1, value2


def compare_structures(
    struct1: Mapping[str, Any],
    struct2: Mapping[str, Any],
    desc1: str = "Gateway",
    desc2: str = "IOC",
) -> str:
    """
    Compare two "structures" (mappings) and return a human-friendly message
    showing the difference.

    Identical structures will return an empty string.

    Parameters
    ----------
    struct1 : dict
        The first structure to compare.  Pairs with the user-friendly ``desc1``
        description. This is a pyepics-provided dictionaries of information
        such as timestamp, value, alarm status, and so on.

    struct2 : dict
        The second structure to compare.  Pairs with the user-friendly
        ``desc2`` description.

    desc1 : str
        User-friendly description of ``struct1``, by default referring to
        the gateway.

    desc2 : str
        User-friendly description of ``struct2``, by default referring to
        the IOC.
    """
    differences = []
    for key, value1, value2 in find_differences(struct1, struct2, desc1, desc2):
        differences.append(
            f"Element '{key}' : {desc1} has '{value1}', but " f"{desc2} has '{value2}'"
        )
    return "\n\t".join(differences)


@contextlib.contextmanager
def ca_subscription(
    pvname: str,
    callback: PyepicsCallback,
    mask: int = epics.dbr.DBE_VALUE,
    form: str = "time",
    count: int = 0,
    timeout: float = 0.5,
) -> Generator[int, None, None]:
    """
    Create a low-level channel and subscription for a provided pvname.

    Yields channel identifier.

    Parameters
    ----------
    pvname : str
        The PV name suffix, not including "ioc:" or "gateway:".

    callback : callable
        A callback function to use for value updates.

    mask : int, optional
        The DBE mask to use for subscriptions.

    form : {"native", "time", "ctrl"}, optional
        The form to request.

    count : int, optional
        The number of elements to request.

    timeout : float, optional
        The timeout in seconds for connection.

    Yields
    ------
    channel : int
        The Channel Access client channel ID.
    """
    event_id = None
    chid = epics.ca.create_channel(pvname)
    try:
        connected = epics.ca.connect_channel(chid, timeout=timeout)
        assert connected, f"Could not connect to channel: {pvname}"

        (_, _, event_id) = epics.ca.create_subscription(
            chid,
            mask=mask,
            use_time=form == "time",
            use_ctrl=form == "ctrl",
            callback=callback,
            count=count,
        )
        yield chid
    finally:
        if event_id is not None:
            epics.ca.clear_subscription(event_id)
        epics.ca.clear_channel(chid)


@contextlib.contextmanager
def ca_subscription_pair(
    pvname: str,
    ioc_callback: PyepicsCallback,
    gateway_callback: PyepicsCallback,
    mask: int = epics.dbr.DBE_VALUE,
    form: str = "time",
    count: int = 0,
    timeout: float = 0.5,
    ioc_prefix: str = "ioc:",
    gateway_prefix: str = "gateway:",
) -> Generator[Tuple[int, int], None, None]:
    """
    Create low-level channels + subscriptions for IOC and gateway PVs.

    Parameters
    ----------
    pvname : str
        The PV name suffix, not including "ioc:" or "gateway:".

    ioc_callback : callable
        A callback function to use for value updates of the IOC PV.

    gateway_callback : callable
        A callback function to use for value updates of the gateway PV.

    mask : int, optional
        The DBE mask to use for subscriptions.

    form : {"native", "time", "ctrl"}, optional
        The form to request.

    count : int, optional
        The number of elements to request.

    timeout : float, optional
        The timeout in seconds for connection.

    ioc_prefix : str, optional
        The prefix to add for direct IOC communication.

    gateway_prefix : str, optional
        The prefix to add for gateway communication.

    Yields
    ------
    ioc_channel : int
        The IOC Channel Access client channel ID.

    gateway_channel : int
        The gateway Channel Access client channel ID.
    """
    with ca_subscription(
        ioc_prefix + pvname,
        ioc_callback,
        mask=mask,
        form=form,
        count=count,
        timeout=timeout,
    ) as ioc_channel:
        with ca_subscription(
            gateway_prefix + pvname,
            gateway_callback,
            mask=mask,
            form=form,
            count=count,
            timeout=timeout,
        ) as gateway_channel:
            yield ioc_channel, gateway_channel


def pyepics_caget(
    pvname: str,
    form: str = "time",
    count: int = 0,
    timeout: float = 0.5,
) -> Dict[str, Any]:
    """
    Use low-level pyepics.ca to get data from a PV.

    Parameters
    ----------
    pvname : str
        The PV name.

    form : {"native", "time", "ctrl"}
        The form to request.

    count : int
        The number of elements to request.

    Returns
    -------
    data : dict
        The PV data, with keys such as "timestamp" or "value".
    """
    chid = epics.ca.create_channel(pvname)
    try:
        connected = epics.ca.connect_channel(chid, timeout=timeout)
        assert connected, f"Could not connect to channel: {pvname}"

        if form in ("time", "ctrl"):
            ftype = epics.ca.promote_type(
                chid, use_time=form == "time", use_ctrl=form == "ctrl"
            )
        elif form == "native":
            ftype = epics.ca.field_type(chid)
        else:
            raise ValueError(f"Unsupported form={form}")

        return epics.ca.get_with_metadata(
            chid, ftype=ftype, count=count, timeout=timeout
        )
    finally:
        epics.ca.clear_channel(chid)


def pyepics_caget_pair(
    pvname: str,
    form: str = "time",
    count: int = 0,
    timeout: float = 0.5,
    ioc_prefix: str = "ioc:",
    gateway_prefix: str = "gateway:",
) -> Tuple[Dict[str, Any], Dict[str, Any]]:
    """
    Use low-level pyepics.ca to get data from a direct PV and a gateway PV.

    Parameters
    ----------
    pvname : str
        The PV name suffix, not including "ioc:" or "gateway:".

    form : {"native", "time", "ctrl"}
        The form to request.

    count : int
        The number of elements to request.

    ioc_prefix : str, optional
        The prefix to add for direct IOC communication.

    gateway_prefix : str, optional
        The prefix to add for gateway communication.

    Returns
    -------
    ioc_data : dict
        The direct IOC PV data.

    gateway_data : dict
        The gateway PV data.
    """
    return (
        pyepics_caget(
            pvname=ioc_prefix + pvname,
            form=form,
            count=count,
            timeout=timeout,
        ),
        pyepics_caget(
            pvname=gateway_prefix + pvname,
            form=form,
            count=count,
            timeout=timeout,
        ),
    )


def pyepics_caput(
    pvname: str,
    value: Any,
    timeout: float = 0.5,
) -> None:
    """
    Use low-level pyepics.ca to put data to a PV.

    Parameters
    ----------
    pvname : str
        The PV name.

    value :
        The value to put.

    timeout : float, optional
        Timeout in seconds.
    """
    chid = epics.ca.create_channel(pvname)
    try:
        connected = epics.ca.connect_channel(chid, timeout=timeout)
        assert connected, f"Could not connect to channel: {pvname}"
        logger.warning("Connected to %s on %s", pvname, epics.ca.host_name(chid))
        epics.ca.put(chid, value, timeout=timeout)
    finally:
        epics.ca.clear_channel(chid)


@pytest.fixture(autouse=True)
def fix_pyepics_reinitialization():
    logger.debug("Performing garbage collection...")
    gc.collect()
    logger.debug("Initializing libca with pyepics")
    epics.ca.initialize_libca()
    yield
    logger.debug("Finalizing libca with pyepics")
    if is_pyepics_libca_initialized():
        epics.ca.finalize_libca()
