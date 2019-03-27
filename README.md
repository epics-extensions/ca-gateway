# Channel Access PV Gateway
[![Build Status](https://travis-ci.org/epics-extensions/ca-gateway.svg?branch=master)](https://travis-ci.org/epics-extensions/ca-gateway)

The [EPICS](https://epics-controls.org) Channel Access PV Gateway is both a 
Channel Access server and Channel Access client.
It provides a means for many clients to access a process variable,
while making only one connection to the server that owns the process variable.

It also provides additional access security beyond that on the server.
It thus protects critical servers while providing possibly restricted access
to needed process variables.

The Gateway typically runs on a machine with multiple network cards,
and the clients and the server may be on different subnets.

## Dependencies

The CA Gateway is using the PCAS server library and needs the PCAS module (https://github.com/epics-modules/pcas) that was unbundled from Base back in 3.16.

If you use caPutLog (https://github.com/epics-modules/caPutLog), CA Gateway now requires it to be greater or equal R3.5.

## Continuous Integration

The CI jobs for CA Gateway are provided by
[Travis](https://travis-ci.org/epics-extensions/ca-gateway).

## Links

More details are available on the
[CA Gateway main web page](http://www.aps.anl.gov/epics/extensions/gateway/).
