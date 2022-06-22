#!/usr/bin/env python3
'''CA Gateway test configuration'''

import os
import sys
import time

# Do we want verbose logging
verbose = False
# Do we want debug logging from the gateway
verboseGateway = False

# CA ports to use
iocPort = 12782
gwPort = 12783

# Duration of standalong runs
gwRunDuration = 300
iocRunDuration = 300

# Gateway attributes
gwStatsPrefix = "gwtest"

# Defaults for EPICS properties:
gwLocation = "../.."
hostArch = "linux-x86_64"

iocExecutable = "softIoc"
gwExecutable = ""
gwDebug = 10

def setup():
    '''Sets up test parameters for CA Gateway tests
    '''
    global verbose, verboseGateway, gwLocation, hostArch
    global iocExecutable, gwExecutable, gwDebug

    if 'VERBOSE' in os.environ and os.environ['VERBOSE'].lower().startswith('y'):
        verbose = True

    if 'VERBOSE_GATEWAY' in os.environ:
        verboseGateway = True
        if os.environ['VERBOSE_GATEWAY'].isdigit():
            gwDebug = os.environ['VERBOSE_GATEWAY']

    if 'EPICS_HOST_ARCH' in os.environ:
        hostArch = os.environ['EPICS_HOST_ARCH']
    elif 'T_A' in os.environ:
        hostArch = os.environ['T_A']
    else:
        print("Warning: EPICS_HOST_ARCH not set. Using default value of '{0}'".format(hostArch))

    if 'TOP' in os.environ:
        gwLocation = os.path.join(os.environ['TOP'], '..')
    else:
        print("Warning: TOP not set. Using default value of '../..'")

    gwExecutable = os.path.join(gwLocation, 'bin', hostArch, 'gateway')
    if not os.path.exists(gwExecutable):
        print("Cannot find the gateway executable at", gwExecutable)
        sys.exit(1)

    if 'IOC_EPICS_BASE' in os.environ:
        iocExecutable = os.path.join(os.environ['IOC_EPICS_BASE'], 'bin', hostArch, 'softIoc')
    elif 'EPICS_BASE' in os.environ:
        iocExecutable = os.path.join(os.environ['EPICS_BASE'], 'bin', hostArch, 'softIoc')
    else:
        print("Warning: IOC_EPICS_BASE or EPICS_BASE not set. Running 'softIoc' executable in PATH")

    if 'EPICS_BASE' in os.environ:
        os.environ['PATH'] += os.pathsep + os.path.join(os.environ['EPICS_BASE'], 'bin', hostArch)
    else:
        print("Warning: EPICS_BASE not set. Will look for 'caRepeater' executable in PATH")

def wait_until(predicate, timeout, period=0.1, *args, **kwargs):
    mustend = time.time() + timeout
    while time.time() < mustend:
      if predicate(*args, **kwargs): return True
      time.sleep(period)
    return False
