      EPICS ASYN support for HiSLIP device
===============================================================================

Based on:

  1) IVI-6.1: IVI High-Speed LAN Instument Protocol


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
