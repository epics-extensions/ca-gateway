#!/usr/bin/env python
import os
import unittest
from epics import caget, caput, PV
import IOCControl
import GatewayControl
import gwtests


MAX_ARRAY_BYTES_KEY = "IOC_EPICS_CA_MAX_ARRAY_BYTES"


class TestWaveformWithCAMaxArrayBytes(unittest.TestCase):
    '''
    Tests for a bug where the gateway will segfault when a waveform is
    requested through the gateway and the value of
    EPICS_CA_MAX_ARRAY_BYTES in the IOC is too small.

    Reference https://github.com/epics-extensions/ca-gateway/issues/20
    '''

    def test_gateway_does_not_crash_after_requesting_waveform_when_max_array_bytes_too_small(self):
        gwtests.setup()
        self.iocControl = IOCControl.IOCControl()
        self.gatewayControl = GatewayControl.GatewayControl()

        # If the bug is present this test is designed to pass the first case
        # and fail the second case
        max_array_bytes_cases = ["6000000", "16384"]
        for max_array_bytes in max_array_bytes_cases:
            print("\n\n\n>>>>>{}={}\n\n\n"
                  .format(MAX_ARRAY_BYTES_KEY, max_array_bytes))

            # The bug crashes the gateway when EPICS_CA_MAX_ARRAY_BYTES
            # on the IOC is too small. Set it here
            os.environ[MAX_ARRAY_BYTES_KEY] = max_array_bytes
            self.iocControl.startIOC()

            # The no_cache argument is required to trigger the bug
            self.gatewayControl.startGateway(extra_args="-no_cache")
            os.environ["EPICS_CA_AUTO_ADDR_LIST"] = "NO"
            os.environ["EPICS_CA_ADDR_LIST"] = (
                "localhost:{0} localhost:{1}".format(
                gwtests.iocPort, gwtests.gwPort)
            )

            # First check that a simple PV can be put and got through gateway
            put_value = 5
            caput("gateway:passive0", put_value, wait=True)
            result = caget("gateway:passive0")

            self.assertIsNotNone(result)
            self.assertEqual(
                result, put_value,
                msg="Initial get: got {} expected {}".format(
                    result, put_value
                )
            )

            # Then try to get waveform through gateway
            try:
                w = PV("gateway:bigpassivewaveform").get(
                    count=3000,
                    # CTRL type is required to trigger the bug
                    with_ctrlvars=True
                )
                self.gatewayControl.poll()
            except TypeError as e:
                raise RuntimeError("Gateway has crashed - "
                                   "exception from pyepics: %s", e)
            except OSError as e:
                raise RuntimeError("Gateway has crashed - "
                                   "exception from subprocess: %s", e)
            else:
                waveform_from_gateway = w
                print(waveform_from_gateway)
                print "waveform_from_gateway"
            finally:
                self.gatewayControl.stop()
                self.iocControl.stop()


if __name__ == '__main__':
    unittest.main(verbosity=2)
