from __future__ import annotations

import os
import pathlib
import shutil
from typing import Optional

MODULE_PATH = pathlib.Path(__file__).parent.resolve()
WORKING_DIRECTORY = pathlib.Path.cwd()


def _boolean_option(value: Optional[str]) -> bool:
    """Environment variable value to a boolean."""
    if not value:
        return False

    try:
        return int(value) > 0
    except TypeError:
        return value.lower() in {"yes", "y", "true"}


libca_so = os.path.join(
    os.environ["EPICS_BASE"], "lib", os.environ["EPICS_HOST_ARCH"], "libca.so"
)
if "PYEPICS_LIBCA" not in os.environ and os.path.exists(libca_so):
    os.environ["PYEPICS_LIBCA"] = libca_so

# Default Channel Access ports to use:
default_ioc_port = 62782
default_gateway_port = 62783
# A port number for the test suite to listen on for caPutLog events:
default_putlog_port = 45635
default_access = os.environ.get("GATEWAY_ACCESS", str(WORKING_DIRECTORY / "access.txt"))

use_pcre = _boolean_option(os.environ.get("USE_PCRE", ""))
default_pvlist = os.environ.get("GATEWAY_PVLIST", str(WORKING_DIRECTORY / "pvlist.txt"))
test_ioc_db = os.environ.get("TEST_DB", str(WORKING_DIRECTORY / "test.db"))

verbose = _boolean_option(os.environ.get("VERBOSE", ""))
# Debug logging from the gateway?
verbose_gateway = _boolean_option(os.environ.get("VERBOSE_GATEWAY", "0"))
gateway_debug_level = int(os.environ.get("VERBOSE_GATEWAY", "") or "10")

host_arch = os.environ.get("EPICS_HOST_ARCH", os.environ.get("T_A"))
if not host_arch:
    raise RuntimeError("Neither EPICS_HOST_ARCH nor T_A is set")

gateway_executable = os.path.join(
    os.environ.get("GATEWAY_ROOT", "."), "bin", host_arch, "gateway"
)


def _get_softioc(host_arch: str) -> str:
    """Find the softIoc binary based on the environment settings."""
    if "IOC_EXECUTABLE" in os.environ:
        ioc_executable = os.environ["IOC_EXECUTABLE"]
    elif "IOC_EPICS_BASE" in os.environ:
        ioc_executable = os.path.join(
            os.environ["IOC_EPICS_BASE"], "bin", host_arch, "softIoc"
        )
    elif "EPICS_BASE" in os.environ:
        ioc_executable = os.path.join(
            os.environ["EPICS_BASE"], "bin", host_arch, "softIoc"
        )
    else:
        ioc_executable = None

    if not ioc_executable or not os.path.exists(ioc_executable):
        ioc_executable = shutil.which("softIoc")
        if not ioc_executable:
            raise RuntimeError(f"softIoc path {ioc_executable} does not exist")
    return ioc_executable


def _check_files_exist():
    """Check that the files specified by environment variables actually exist."""
    files = {
        "GATEWAY_ROOT": gateway_executable,
        "GATEWAY_ACCESS": default_access,
        "GATEWAY_PVLIST": default_pvlist,
        "TEST_DB": test_ioc_db,
        "EPICS_BASE or IOC_EPICS_BASE and EPICS_HOST_ARCH": ioc_executable,
    }
    for env_var, filename in files.items():
        if not filename or not os.path.exists(filename):
            raise RuntimeError(
                f"File derived from environment variable(s) {env_var} does not exist: "
                f"{filename!r}"
            )


ioc_executable = _get_softioc(host_arch)
_check_files_exist()
