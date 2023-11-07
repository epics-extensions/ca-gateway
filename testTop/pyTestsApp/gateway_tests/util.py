import array
import contextlib
import dataclasses
import getpass
import logging
import socket
from typing import Any, Dict, List, Optional, Tuple

import caproto
import caproto.sync.client as ca_client

try:
    import numpy as np
except ImportError:
    np = None

logger = logging.getLogger(__name__)


@dataclasses.dataclass
class PVInfo:
    name: str
    access: Optional[str] = None
    data_type: Optional[str] = None
    data_count: Optional[int] = None
    value: Optional[List[Any]] = None
    error: Optional[str] = None
    time_md: Optional[Dict[str, Any]] = None
    control_md: Optional[Dict[str, Any]] = None
    address: Optional[Tuple[str, int]] = None


@contextlib.contextmanager
def bound_udp_socket(
    reusable_socket: Optional[socket.socket] = None,
    timeout: float = ca_client.common.GLOBAL_DEFAULT_TIMEOUT,
):
    """Create a bound UDP socket, optionally reusing the passed-in one."""
    if reusable_socket is not None:
        reusable_socket.settimeout(timeout)
        yield reusable_socket
        return

    udp_sock = caproto.bcast_socket()
    udp_sock.bind(("", 0))

    udp_sock.settimeout(timeout)
    yield udp_sock
    udp_sock.close()


@contextlib.contextmanager
def override_hostname_and_username(
    hostname: Optional[str] = None, username: Optional[str] = None
):
    """Optionally monkeypatch/override socket.gethostname and getpass.getuser."""
    orig_gethostname = socket.gethostname
    orig_getuser = getpass.getuser

    def get_host_name() -> str:
        host = hostname or orig_gethostname()
        logger.debug("Hostname will be: %s", host)
        return host

    def get_user() -> str:
        user = username or orig_getuser()
        logger.debug("Username will be: %s", user)
        return user

    try:
        getpass.getuser = get_user
        socket.gethostname = get_host_name
        yield
    finally:
        getpass.getuser = orig_getuser
        socket.gethostname = orig_gethostname


def _channel_cleanup(chan: caproto.ClientChannel):
    """Clean up the sync client channel."""
    try:
        if chan.states[caproto.CLIENT] is caproto.CONNECTED:
            ca_client.send(chan.circuit, chan.clear(), chan.name)
    finally:
        ca_client.sockets[chan.circuit].close()
        del ca_client.sockets[chan.circuit]
        del ca_client.global_circuits[(chan.circuit.address, chan.circuit.priority)]


def _basic_enum_name(value) -> str:
    """AccessRights.X -> X"""
    return str(value).split(".", 1)[1]


def caget_from_host(
    hostname: str,
    pvname: str,
    timeout: float = ca_client.common.GLOBAL_DEFAULT_TIMEOUT,
    priority: int = 0,
    udp_sock: Optional[socket.socket] = None,
    username: Optional[str] = None,
) -> PVInfo:
    """
    Read a Channel's access security settings for the given hostname.

    Not thread-safe.

    Parameters
    ----------
    hostname : str
        The host name to report when performing the caget.
    pvname : str
        The PV name to check.
    timeout : float, optional
        Default is 1 second.
    priority : 0, optional
        Virtual Circuit priority. Default is 0, lowest. Highest is 99.
    udp_sock : socket.socket, optional
        Optional re-usable UDP socket.
    username : str, optional
        The username to provide when performing the caget.

    Returns
    -------
    pv_info : PVInfo
    """

    chan = None
    pv_info = PVInfo(name=pvname)
    try:
        with bound_udp_socket(
            udp_sock, timeout=timeout
        ) as udp_sock, override_hostname_and_username(hostname, username):
            chan = ca_client.make_channel(pvname, udp_sock, priority, timeout)
            pv_info.access = _basic_enum_name(chan.access_rights)
            pv_info.data_type = _basic_enum_name(chan.native_data_type)
            pv_info.data_count = chan.native_data_count
            pv_info.address = chan.circuit.address
            control_value = ca_client._read(
                chan,
                timeout,
                data_type=ca_client.field_types["control"][chan.native_data_type],
                data_count=min((chan.native_data_count, 1)),
                force_int_enums=True,
                notify=True,
            )
            pv_info.control_md = control_value.metadata.to_dict()

            time_value = ca_client._read(
                chan,
                timeout,
                data_type=ca_client.field_types["time"][chan.native_data_type],
                data_count=min((chan.native_data_count, 1000)),
                force_int_enums=True,
                notify=True,
            )
            pv_info.time_md = time_value.metadata.to_dict()
            pv_info.value = time_value.data
    except TimeoutError:
        pv_info.error = "timeout"
    finally:
        if chan is not None:
            _channel_cleanup(chan)

    return pv_info


def _filter_data(data):
    """Filter data for byte strings and other non-JSON serializable items."""
    if isinstance(data, dict):
        return {key: _filter_data(value) for key, value in data.items()}

    if isinstance(data, array.ArrayType):
        return data.tolist()

    if np is not None and isinstance(data, np.ndarray):
        return data.tolist()

    if isinstance(data, (list, tuple)):
        return [_filter_data(item) for item in data]

    if isinstance(data, bytes):
        return str(data, "latin-1", "ignore")  # _EPICS_
    return data


def caget_many_from_host(hostname: str, *pvnames: str):
    with bound_udp_socket() as udp_sock:
        results = {}
        for pvname in pvnames:
            try:
                info = caget_from_host(hostname, pvname, udp_sock=udp_sock)
            except TimeoutError:
                info = PVInfo(
                    name=pvname,
                    error="timeout",
                )
            results[pvname] = _filter_data(dataclasses.asdict(info))

    return {
        "hostname": hostname,
        "pvs": results,
    }
