CA Gateway Release Notes
========================

## 2.1.4 (not released yet)
[View diff](https://github.com/epics-extensions/ca-gateway/compare/R2-1-3-0...master)

* Fix some compiler warnings
* Ensure that ASL defaults to 1
* Fix `-uid`/`-gid`: the group id was not changed when `-uid` came first
  on the command line, leaving the gateway running with group root.
  Supplementary groups are now dropped, and failures abort the start-up.

## 2.1.3 (15 Dec 2021)
[View diff](https://github.com/epics-extensions/ca-gateway/compare/R2-1-2-0...R2-1-3-0)

* Small fixes in caPutLog support
* Fix a bug (#33) where array values were lost (except the first element)
  on subscriptions after a read request for a container type
* Fix update of enum labels on active subscriptions using a container type
* Move CI from Travis-CI to GitHub Actions
* A bug that interprets doubles (written as strings) as hex numbers (#37)
  is fixed when the Gateway is linked against PCAS >= 4.13.3 or Base 3.15 >= 3.15.10

## 2.1.2 (25 Oct 2019)
[View diff](https://github.com/epics-extensions/ca-gateway/compare/R2-1-1-0...R2-1-2-0)

* Crash reported and fixed by Diamond (on MAX_ARRAY_BYTES mismatch).
* Improve handling of DBR_CTRL requests.
* Remove support for EPICS Base 3.13, Solaris
* Improve tests, make them compatible with python3
* Properly depend on PCAS for EPICS 7 builds
* Update to support caPutLog >= 3.5 (older versions not supported)
* Add support for 64bit integers
* Raise PV name length limit to 256 characters

## 2.1.1 (17 Oct 2018)
[View diff](https://github.com/epics-extensions/ca-gateway/compare/R2-1-0-0...R2-1-1-0)

* Clean up release notes (that you are reading right now)
* Improve test readme
* Improve test cleanup on failure (shut down gateway processes)
* Lots of changes and fixing for CI setup
* Build changes necessary for EPICS 7 (unbundled PCAS module)

## 2.1.0 (11 May 2016)
[View diff](https://github.com/epics-extensions/ca-gateway/compare/R2-0-6-0...R2-1-0-0)

* Full support for DBE_PROPERTY event flag.
* Add unit test framework (as embedded TOP).
* Add tests for event flag support, including DBE_PROPERTY.
* Fix many small bugs and compiler warnings.

## 2.0.6.0 (30 Jan 2015)
[View diff](https://github.com/epics-extensions/ca-gateway/compare/R2-0-5-1...R2-0-6-0)

* Changes source structure to be a standard EPICS module.
* Add Jenkins job for CloudBees CI.
* Fix crashes when forwarding empty arrays (bug lp:1415938).
* Use variable length arrays for CAC (IOC side) subscriptions.

## 2.0.5.1 (09 Dec 2014)
[View diff](https://github.com/epics-extensions/ca-gateway/compare/R2-0-5-0...R2-0-5-1)

* Reenabled Windows builds MSVC 32 and 64, MinGW 32 (EPICS 3.15 only).
* Support for PCRE version 3 (API changes).
    
## 2.0.5.0 (03 Dec 2014)
[View diff](https://github.com/epics-extensions/ca-gateway/compare/R2-0-4-0...R2-0-5-0)

* Merged FRIB branch that adds proper CAPutLog logging.
* Supports building against EPICS Base 3.15.1.
* Windows build has been broken for a long time - without complaints.

## 2.0.4.0 (17 Sep 2009)
[View diff](https://github.com/epics-extensions/ca-gateway/compare/R2-0-3-0...R2-0-4-0)

* Added new writeNotify interface.
  I updated the PCAS/casdef.h to add a new backwards compatible virtual 
  writeNotify interface in casChannel and casPV. This is important
  for the GW so that it can execute the proper form of ca_put or
  ca_put_callback in its implementation of write, or writeNotify,
  and so I did also install the necessary changes in the GW so that 
  it will benefit from the new writeNotify interface. _- Jeff Hill_
* Added a build optional heartbeat pv, <suffix>:heartbeat, with val 0.

## 2.0.3.0 (14 Jan 2008)
[View diff](https://github.com/epics-extensions/ca-gateway/compare/R2-0-2-1...R2-0-3-0)

* Restored gateway default behavior. Now if "-archive" is not used, Gateway 
  will post log events along with value change events. _- Gasper Jansa_

## 2.0.2.1 (02 Aug 2007)
[View diff](https://github.com/epics-extensions/ca-gateway/compare/R2-0-2-0...R2-0-2-1)

* Bug fix to the conditional compilation for the "negated regexp" feature.
  _- Dirk Zimoch_

## 2.0.2.0 (01 Aug 2007)
[View diff](https://github.com/epics-extensions/ca-gateway/compare/R2-0-1-0...R2-0-2-0)

* New option to use Perl regexp instead of GNU regexp, controlled with a
  compiler switch defined in the Makefile.  _- Dirk Zimoch_
* New options to use DENY FROM and negated regular expressions to prevent
  loops in reverse gateways while allowing access to the internal PVs.
  These require USE_NEG_REGEXP=YES and USE_DENY_FROM=YES in Makefile. _- Dirk Zimoch_
* Added docs subdirectory containing all Gateway docs.

## 2.0.1.0 = 2.0.0.3 (10 Jul 2007)
[View diff](https://github.com/epics-extensions/ca-gateway/compare/R2-0-0-2...R2-0-1-0)

Changes from Dirk Zimoch

* The two main changes in behavior ("don't use cached values in caget"
  and "create separate archive monitor for archivers") are controlled by
  command line switches: -no_cache and -archive.
* The option to use "DENY FROM" in the configuration is chosen by a 
  compiler switch in the Makefile.
* Other changes fix bugs (enable behavior that matches the documentation 
  or the expectation) without any switch:
  * events are now forwared to alarm handler clients only when STAT or 
    SEVR changes
  * bugfix: beacons were not sent to clients when -cip option was used.
  * bugfix: enums appeared frozen and analog values were rounded when 
    alarm handler client is connected
  * bugfix: gateway hung (using 100% CPU time) then arrays > 
    EPICS_CA_MAX_ARRAY_BYTES were read.

## 2.0.0.2 (22 May 2007)
[View diff](https://github.com/epics-extensions/ca-gateway/compare/R2-0-0-1...R2-0-0-2)

 * Removed pre R3.14 Makefiles.

## 2.0.0.1 (28 Feb 2007)
[View diff](https://github.com/epics-extensions/ca-gateway/compare/R2-0-0-0...R2-0-0-1)

 * Fixed return code for gddAppType acks.

## Original changelog entries dating back to 1998

    Wed Feb 18 09:10:14 CST 1998
        Upon USR1 signal gateway now executes commands specified in a
        gateway.command file. 
        Incorporated latest changes to access security in gateAsCa.cc

    Tue Apr 21 22:38:59 CDT 1998
        Real name is now used for access security pattern matching.
        Fixed PV Pattern Report 
        New gdd api changes

    Tue Dec 22 12:53:15 CST 1998
        Tagged current CVS version as Gateway0_1 before commit.
        Current version has ENUM hack changes.
        Fixed bug with removing items from pv_con_list.
        Core dumps, but infrequently.
        Has been in production use for several weeks.
        Will be tagged Gateway0_2 after commit.

    Tue Dec 22 13:15:08 CST 1998
        This version has much debugging printout (inside #if's).
        Changed gateVc::remove -> vcRemove and add -> vcAdd.
          Eliminates warnings about hiding private ancestor functions on Unix.
          (Warning is invalid.)
        Now compiles with no warnings for COMPILR=STRICT on Solaris.
        Made changes to speed it up:
          Put #if around ca_add_fd_registration.
            Also eliminates calls to ca_pend in fdCB.
          Put #if DEBUG_PEND around calls to checkEvent, which calls ca_pend.
          Changed mainLoop to call fdManager::process with delay=0.
          Put explicit ca_poll in the mainLoop.
          All these changes eliminate calls to poll() which was predominant
            time user.  Speed up under load is as much as a factor of 5. Under
            no load it runs continuously, however, rather than sleeping in
            poll().
        Added #if NODEBUG around calls to Gateway debug routines (for speed).
        Changed ca_pend(GATE_REALLY_SMALL) to ca_poll for aesthetic reasons.
        Added timeStamp routine to gateServer.cc.
        Added line with PID and time stamp to log file on startup.
        Changed freopen for stderr to use "a" so it doesn't overwrite the log.
        Incorporated Ralph Lange changes by hand.
          Changed clock_gettime to osiTime to avoid unresolved reference.
          Fixed his gateAs::readPvList to eliminate core dump.
          Made other minor fixes.
        Did minor cleanup as noticed problems.
        This version appears to work but has debugging (mostly turned off).
        Will be tagged Gateway0_3 after commit.
