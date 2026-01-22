#!/usr/bin/env python3
import gc
import os
import unittest
import epics
import IOCControl
import GatewayControl
import gwtests
import time

class TestDBEProp(unittest.TestCase):
    '''Test property updates (client using DBE_PROPERTY flag) direct and through the Gateway'''

    def setUp(self):
        gwtests.setup()
        self.iocControl = IOCControl.IOCControl()
        self.gatewayControl = GatewayControl.GatewayControl()
        self.iocControl.startIOC()
        self.gatewayControl.startGateway()
        os.environ["EPICS_CA_AUTO_ADDR_LIST"] = "NO"
        os.environ["EPICS_CA_ADDR_LIST"] = "localhost:{0} localhost:{1}".format(gwtests.iocPort,gwtests.gwPort)
        gc.collect()
        epics.ca.initialize_libca()
        self.eventsReceivedGW = 0
        self.eventsReceivedIOC = 0

    def tearDown(self):
        epics.ca.finalize_libca()
        self.gatewayControl.stop()
        self.iocControl.stop()

    def onChangeGW(self, pvname=None, **kws):
        self.eventsReceivedGW += 1
        if gwtests.verbose:
            print(" GW update: ", pvname, " changed to ", kws['value'])

    def onChangeIOC(self, pvname=None, **kws):
        self.eventsReceivedIOC += 1
        if gwtests.verbose:
            print("IOC update: ", pvname, " changed to ", kws['value'])

    def testPropAlarmLevels(self):
        '''DBE_PROPERTY monitor on an ai - value changes generate no events; property changes generate events.'''
        # gateway:passive0 is a blank ai record
        ioc = epics.PV("ioc:passive0", auto_monitor=epics.dbr.DBE_PROPERTY)
        ioc.add_callback(self.onChangeIOC)
        gw = epics.PV("gateway:passive0", auto_monitor=epics.dbr.DBE_PROPERTY)
        gw.add_callback(self.onChangeGW)
        pvhihi = epics.PV("ioc:passive0.HIHI", auto_monitor=None)
        pvlolo = epics.PV("ioc:passive0.LOLO", auto_monitor=None)
        pvhigh = epics.PV("ioc:passive0.HIGH", auto_monitor=None)
        pvlow  = epics.PV("ioc:passive0.LOW",  auto_monitor=None)
        ioc.get()
        gw.get()

        for val in range(10):
            ioc.put(val, wait=True)
        gwtests.wait_until(lambda: self.eventsReceivedGW == 2 and self.eventsReceivedIOC == 1, 5.0)
        # We get 1 event: at connection
        self.assertTrue(self.eventsReceivedGW == 2, 'GW events expected: 2; received: ' + str(self.eventsReceivedGW))
        self.assertTrue(self.eventsReceivedIOC == 1, 'IOC events expected: 1; received: ' + str(self.eventsReceivedIOC))

        self.eventsReceived = 0
        pvhihi.put(20.0, wait=True)
        pvhigh.put(18.0, wait=True)
        pvlolo.put(10.0, wait=True)
        pvlow.put(12.0, wait=True)
        time.sleep(.1)

        # Depending on the IOC (supporting PROPERTY changes on limits or not) we get 0 or 4 events.
        # Pass test if updates from IOC act the same as updates from GW
        self.assertTrue(self.eventsReceivedGW == (self.eventsReceivedIOC + 1),
            "Expected GW updates to be one more than IOC; received {0} from GW and {1} from IOC".format(self.eventsReceivedGW, self.eventsReceivedIOC))


if __name__ == '__main__':
    unittest.main(verbosity=2)
