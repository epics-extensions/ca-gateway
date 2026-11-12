"""
Regression test for issue #71: a client disconnecting while its
put-callback (ca_put_callback) is still pending upstream must not
crash the gateway when the upstream callback arrives later.

The use-after-free is only reliably detected with an AddressSanitizer
build of the gateway; without ASan the test may pass on unfixed code.
"""
import os
import subprocess
import time

import epics

from . import config, conftest

# Processing delay of the asynchronous record (seconds)
DELAY = 2.0

# A calcout record with an output delay (ODLY) completes processing
# asynchronously, so a put-callback to it stays pending for DELAY seconds.
# (Unlike SIMM/SDLY, this works on all supported Base versions, 3.14+.)
DB = f"""
record(calcout, "ioc:asyncput") {{
    field(CALC, "A")
    field(ODLY, "{DELAY}")
}}
"""


def _caput_cmd():
    base = os.environ.get("IOC_EPICS_BASE", os.environ.get("EPICS_BASE", ""))
    return os.path.join(base, "bin", config.host_arch, "caput")


def test_put_callback_client_disconnect():
    with open(config.default_access) as f:
        access = f.read()
    with open(config.default_pvlist) as f:
        pvlist = f.read()

    with conftest.custom_environment(access, pvlist, db_contents=DB):
        env = dict(os.environ)
        env["EPICS_CA_AUTO_ADDR_LIST"] = "NO"
        env["EPICS_CA_ADDR_LIST"] = f"localhost:{config.default_gateway_port}"

        # A second client (monitor) on the same channel as the put keeps the
        # gateway's VC alive after the put client goes away; without it,
        # putCB() returns early. (The gateway has one VC per PV name, so
        # this must also connect to ".A".)
        pv = epics.PV("gateway:asyncput.A", auto_monitor=True)
        assert pv.wait_for_connection(timeout=5.0)

        # Start a put-callback through the gateway, then kill the client
        # while the (simulated) asynchronous record is still processing.
        client = subprocess.Popen(
            [_caput_cmd(), "-c", "-w", "30", "gateway:asyncput.A", "1"],
            env=env,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        time.sleep(DELAY / 2)
        assert client.poll() is None, "caput finished too early"
        client.kill()
        client.wait()

        # Let the upstream put callback arrive at the gateway
        time.sleep(DELAY * 1.5)

        # Gateway must still be alive and serving
        assert pv.get(timeout=5.0, use_monitor=False) == 1
