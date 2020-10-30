===============================================================================
EPICS ASYN support for HiSLIP device
===============================================================================

What is this?
==================
This module provides EPICS asyn Octet driver for HiSLIP protocol.

It should work with Stream Device library as sameway as USBTMC and VXI11.
SRQ is supported also throgh asyn octet driver.


HiSLIP protocol is implemented in cPyHiSLIP/HiSLIPMessage.{cpp,h} files.
These files does not have direct dependency on EPICS, and can be used with any other
programs. As an example, cPyHiSLIP/cPyHiSLIP.{pxd,pyx} and setup.py are provided to
build python modules for HiSLIP device. 

This moduled is based on the specification descibed in:

  IVI-6.1: IVI High-Speed LAN Instument Protocol (Rev. 1.1, Feb.24, 2011)[#]_

  
.. [#] The latest version  IVI-6.1 Rev.2.0 was publised in Feb.23, 2020. New features, "Encrypted connections" and "Client and server authentication" are not supported by this version of drAsynHiSLIP library.


How to use
=============

For EPICS
--------------



It is assumed that this module will be used togather with the Stream Deivce.
You may need to adjust  configure/RELEASE adn configure/CONFIG_SITE files
for your environment. In some Linux systems, you need to turn on TIRPC switch to true
in configure/CONFIG_SITE.

To add this drvAsynHiSLIP support into StreamApp. After building a libasyn library
with drvAsynHiSLIP, you must add the following
line to the Makefile in the StreamApp directory:

  streamApp_DBD += drvAsynHiSLIP.dbd

In the iocsh start up command, you neeed to configure asyn port
for HiSLIP device. The "HiSLIPConfigure" command, like "vxi11Configure" for
VXI-11 devices is provided.:

  # HiSLIPConfigure "port name", "host address", max_message_size, "priority"
  HiSLIPConfigure "L0","172.28.68.228", 1048560, 0 # Keysight DSOX1204A

A port name can be any ID string if you just uses Stream driver.
However, if you may want to use SRQ with this suppor, you better to
stick with devGPIB/asyn port name convention, i.e. "L<n>".


As Python module
----------------------
At first, you need to build and install Python module based on this library.
To do so, go to the cPyHiSLIP directory under asyn/drvAsynHiSLIP directory, then
issue:

  python3 -m pip build clean install

You must have pip module and Cython tool installed. You might need to give
appropriate priviledge to install the module in the proper location.




Build Issues
============
  HiSLIP portocol is implemented in HiSPLIPMessage.{cpp,h}.
  cPyHiSLIP provides python module to access HiSLIP devices. It uses cython to wrap c/c++ librarlies.
  It can be used without any componets from EPICS.
  drvAsynHiSLIP provide asyOctet interface to access device. SRQ(IO Intr) is implemented also as asynOcted driver. So, only SI record can be used with SRQ.
  
Acknowledgements
================
This is based on USB TMC support by Eric Norum <wenorum@lbl.gov>.
And also PyHiSLIP modules by Levshinovskiy Mikhail(https://github.com/llemish/PyHiSLIP) is used
as a reference for the implementation.


LXI Ports, Protocols, and Services

https://www.lxistandard.org/About/LXI-Protocols.aspx


LXI_HiSLIP_Extended_Function_Test_Procedures_v1_01.pdf
