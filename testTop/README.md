README for CA Gateway Tests
===========================

This is an embedded TOP structure.
By default, it is being used as part of the CA Gateway sources.
You can also copy it out of the CA Gateway source tree and use it
separately. In that case, you will have to set the pointers in
`.../configure/RELEASE` appropriately.

## Additional Prerequisites

Python and the packages:

nose-tap      - Creating TAP output (for 'make tapfiles')<br/>
pyepics (v3)  - Python CA client library

The PyEpics module needs the EPICS Base libraries Com and ca as _shared
libraries_ (`.so` on Linux, `.dll` on Windows) in a system or environment
variable path. See the [PyEpics documentation](http://cars9.uchicago.edu/software/python/pyepics3/installation.html#prerequisites)
for details.

## pyTestsApp

Python unit tests.
Default settings and configuration in the `gwtests.py` file.

Most of the tests start an EPICS (soft) IOC and a CA Gateway,
working on different non-standard ports on the 'localhost' loopback interface,
The Gateway uses an ALIAS directive to export the channels on the IOC using
a different prefix.
The test routines, using the pyepics binding, have access to both the IOC and
the Gateway channels to set up subscriptions, trigger events on the IOC and
check the resulting event streams through the Gateway.

### Running the Tests

There are two main ways to run the tests:

1. As part of the EPICS Build system `make runtests` and `make tapfiles`
   targets:</br>
   Test files and scripts are copied to the `O.<arch>` directory,
   then `nosetests` is used to run the tests there.
   The build system sets all necessary environment variables.
   For `make tapfiles` the nosetests tap plugin from `nose-tap` is used
   to create TAP output.
2. Manually:</br>
   Test can be run in the `pyTestApp` directory using the `unittest` or
   `nosetests` wrapper. (E.g. run `nosetests --exe`.)
   These wrappers also allow selecting specific tests to run.
   You might have to set `EPICS_BASE`, `EPICS_HOST_ARCH` and `TOP` (to find the
   gateway executable under test and the CA libraries).

### Tests Needing Root

The tests for the `-uid` and `-gid` options in `test_uid_gid.py` that
actually drop privileges start the gateway as root, using passwordless
`sudo -n`. They are skipped if that is not available, when the tests run
as root, or on systems other than Linux.

### Environment Variables
`VERBOSE`          - `YES` to make tests more verbose</br>
`VERBOSE_GATEWAY`  - `YES` to run the gateway with default debuglevel 10,
                   `<debuglevel>` for setting a specific debuglevel

### IOC Environment Overrides
To allow control over the IOC that is being used independently from any
settings for the Gateway under test, you can specify environment variables
with an `IOC_` prefix that will be set for the IOC (with the prefix removed).
E.g. `IOC_EPICS_BASE=<location>` will set `EPICS_BASE=<location>` for the IOC.
As this setting is also used to determine the location of the 'softIoc'
executable, it allows testing against IOCs from different versions of base.
