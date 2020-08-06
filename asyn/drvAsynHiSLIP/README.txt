EPICS ASYN support for HiSLIP device

What is this?

This module provides EPICS asyn Octet driver for HiSLIP protocol.

It should work with Stream Device library as sameway as USBTMC and
VXI11. SRQ is supported also throgh asyn octet driver.

HiSLIP protocol is implemented in cPyHiSLIP/HiSLIPMessage.{cpp,h} files.
These files does not have direct dependency on EPICS, and can be used
with any other programs. As an example, cPyHiSLIP/cPyHiSLIP.{pxd,pyx}
and setup.py are provided to build python modules for HiSLIP device.

This moduled is based on the specification descibed in:

  IVI-6.1: IVI High-Speed LAN Instument Protocol (Rev. 1.1, Feb.24,
  2011)[1]

How to use

For EPICS

It is assumed that this module will be used togather with the Stream
Deivce. To add this drvAsynHiSLIP support into StreamApp. You must add
the following line to the Makefile in the StreamApp directory:

  streamApp_DBD += drvAsynHiSLIP.dbd

Build Issues

  HiSLIP portocol is implemented in HiSPLIPMessage.{cpp,h}. cPyHiSLIP
  provides python module to access HiSLIP devices. It uses cython to
  wrap c/c++ librarlies. It can be used without any componets from
  EPICS. drvAsynHiSLIP provide asyOctet interface to access device.
  SRQ(IO Intr) is implemented also as asynOcted driver. So, only SI
  record can be used with SRQ.

Acknowledgements

This is based on USB TMC support by Eric Norum <wenorum@lbl.gov>. And
also PyHiSLIP modules by Levshinovskiy
Mikhail(https://github.com/llemish/PyHiSLIP) is used as a reference for
the implementation.

[1] IVI-6.1 Rev.2.0 was publised in Feb.23, 2020.
