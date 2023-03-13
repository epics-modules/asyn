[![Github Actions](https://github.com/epics-modules/asyn/actions/workflows/ci-scripts.yml/badge.svg)](https://github.com/epics-modules/asyn/actions/workflows/ci-scripts.yml)
[![Appveyor Build Status](https://ci.appveyor.com/api/projects/status/github/epics-modules/asyn?branch=master&svg=true)](https://ci.appveyor.com/project/MarkRivers/asyn)

asyn
====
This is a general purpose facility for interfacing device specific
code to low level drivers. asynDriver allows non-blocking device support that works
with both blocking and non-blocking drivers.

A primary target for asynDriver is EPICS IOC device support but, other than using
libCom, most of it is independent of EPICS.

Documentation and release notes can be found here:
* [Documentation](https://epics-modules.github.io/asyn)
* [Release notes](RELEASE.md)
