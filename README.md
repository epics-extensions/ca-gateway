<a target="_blank" href="http://semver.org">![Version][badge.version]</a>
<a target="_blank" href="https://github.com/epics-extensions/ca-gateway/actions/workflows/ci-scripts-build.yml">![GitHub Actions status][badge.gha]</a>
<a target="_blank" href="https://app.codacy.com/gh/epics-extensions/ca-gateway/dashboard">![Codacy grade][badge.codacy]</a>


# Channel Access PV Gateway

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

The CA Gateway is using the PCAS server library and needs the
[PCAS module](https://github.com/epics-modules/pcas) when compiled against
EPICS 7 (>= 3.16).

If you compile the CA Gateway with
[caPutLog](https://github.com/epics-modules/caPutLog) support,
a caPutLog version >= 3.5 is required.
The `-caputlogjson` option (JSON format) is available if the caPutLog
module provides JSON support (`caPutJsonLogTask.h`).

## Continuous Integration and Static Code Analysis

The CI jobs for CA Gateway are provided by
[GitHub Actions](https://github.com/epics-extensions/ca-gateway/actions/workflows/ci-scripts-build.yml).

Static Code Analysis is provided by
[Codacy](https://www.codacy.com).

## Links

More details are available on the
[CA Gateway main web page](http://www.aps.anl.gov/epics/extensions/gateway/).

<!-- Links -->
<!-- Links -->
[badge.version]: https://badge.fury.io/gh/epics-extensions%2Fca-gateway.svg
[badge.gha]: https://github.com/epics-extensions/ca-gateway/actions/workflows/ci-scripts-build.yml/badge.svg
[badge.codacy]: https://app.codacy.com/project/badge/Grade/380b43d8acf743eab6b6236a5aa104a4
