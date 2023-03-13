asyn Record I/O Example
=======================

This document describes how to use an asyn record to communicate with a message-based instrument.

It is a step-by-step example of how I set up simple instrument control
using only an asyn record. The instrument may be connected to a
local serial port, a USB/Serial adapter, a LAN/Serial adapter, a
network port (raw TCP or VXI-11), a local GPIB interface or a LAN/GPIB adapter.

- Create a new application
  ::

    mkdir serialTest
    cd serialTest
    /usr/local/epics/R3.14.11/bin/darwin-x86/makeBaseApp.pl -l
    /usr/local/epics/R3.14.11/bin/darwin-x86/makeBaseApp.pl -t ioc serialTest<
    /usr/local/epics/R3.14.11/bin/darwin-x86/makeBaseApp.pl -i -t ioc serialTest
  
- Add ASYN support.
  Edit configure/RELEASE and add a line specifying the path to your ASYN installation
  ::

    ASYN=/usr/local/epics/R3.14.11</modules/soft/asyn

- Edit serialTestApp/src/Makefile and add two lines. It should be
  apparent from the template where these lines are to be placed:
  ::

    serialTest_DBD += drvVxi11.dbd
    serialTest_LIBS += asyn

  For a local serial port or a USB/Serial adapter, replace `drvVxi11` with `drvAsynSerialPort`.
  For a 'telnet' style network port (instrument or LAN/Serial adapter), replace `drvVxi11` with `drvAsynIPPort`.
    
- Edit serialTestApp/Db/Makefile and add a line:
  ::

    DB_INSTALLS += $(ASYN)/db/asynRecord.db
    
- Edit iocBoot/iocSerialTest/st.cmd and add lines to configure the
  serial, GPIB or network port and to load an ASYN record. 

- Here's a complete st.cmd file showing how things should look when you're finished.
  You will, of course, have to substitute the IP address of your GPIB/LAN
  adapter and the address of your GPIB device. You'll likely want
  to choose a different value for the PV name prefix macro (P) as
  well. The IMAX and OMAX values should be large enough to handle
  the longest messages you expect.
  ::
  
    #!../../bin/darwin-x86/serialTest
    <envPaths
    cd ${TOP}
    ## Register all support components
    dbLoadDatabase("dbd/serialTest.dbd",0,0)
    serialTest_registerRecordDeviceDriver(pdbbase)
    ## Configure devices
    vxi11Configure("L0","164.54.8.129",0,0.0,"gpib0",0,0)
    ## Load record instances
    dbLoadRecords("db/asynRecord.db","P=norum:,R=asyn,PORT=L0,ADDR=24,IMAX=100,OMAX=100")
    cd ${TOP}/iocBoot/${IOC}
    iocInit()
      
  If you are using a LAN/Serial adapter or network attached device which
  uses a raw TCP ('telnet' style) connection you would replace the `vxi11Configure`
  command with a line like:
  ::
  
    drvAsynIPPortConfigure("L0","192.168.0.23:4001",0,0,0)
  
  If you are using a local serial port or USB/Serial adapter replace the `vxi11Configure`
  command with something like:
  ::
  
    drvAsynSerialPortConfigure("L0","/dev/tty.PL2303-000013FA",0,0,0)
    asynSetOption("L0", -1, "baud", "9600")
    asynSetOption("L0", -1, "bits", "8")
    asynSetOption("L0", -1, "parity", "none")
    asynSetOption("L0", -1, "stop", "1")
    asynSetOption("L0", -1, "clocal", "Y")
    asynSetOption("L0", -1, "crtscts", "Y")
    
- Build the application (run `make` from the application top directory).
- Start the IOC
  ::

    cd iocBoot/iocserialTest
    ../../bin/darwin-x86/serialTest st.cmd   
  
- From another window, start MEDM. Make sure that the P and R macro values match those from st.cmd.
  ::

    medm -x  -macro "P=norum:,R=asyn" /usr/local/epics/R3.14.11/modules/soft/asyn/medm/asynRecord.adl
    
You should see something like the window shown below. I've made a
few changes to the original values including increasing the trace I/O
truncate size to 100 characters, enabling the traceIOEscape display and
turning on traceIODriver debugging.

Main control screen, asynRecord.adl
-----------------------------------   

.. figure:: AsynRecordWindow.png
    :align: center

    **asynRecord.adl**
    
    
Click on the 'More...' button and bring up the "asynOctet Interface
I/O" window. I've made some changes here as well. I've selected
Binary input format and increased the requested input length to
100. If your device messages are 40 characters or less you don't
have to make this change. If you're using a non-GPIB device you
probably need to specify appropriate input and output terminator
characters.

asynOctet I/O screen, asynOctet.adl
-----------------------------------   

.. figure:: AsynOctetWindow.png
    :align: center

    **asynOctet.adl**
    
Try entering some commands. A good one to start with is the SCPI
Device Identification (\*IDN?) command. You can see why I had to
arrange for reply messages longer than the default 40 characters!
    
asynOctet I/O screen, asynOctet.adl
-----------------------------------    

.. figure:: AsynOctetIDN.png
    :align: center

    **asynOctet.adl**

The readback display is truncated. Since I have traceIODriver
enabled I can see the entire message in the IOC console window:
::

  2007/09/20 09:01:15.998 L0 24 vxiWrite
  *IDN?
  2007/09/20 09:01:16.009 L0 24 vxiRead
  KEITHLEY INSTRUMENTS INC.,MODEL 2400,1163289,C30   Mar 17 2006 09:29:29/A02  /K/J\n

If your requirements are modest, you might be done. The asyn
record may be adequate for your application. If not, you should
investigate the
`Streams device package <HowToDoSerial.html>`__
for writing support for your instrument.
