# asynDriver: Release Notes

## Release 4-44-2 (March 28, 2023)
- devEpics
  - Fix additional problems with waveform, aai, and aao records due to missing initialization of private member variables
    in devAsynXXXArray.cpp.

## Release 4-44-1 (March 15, 2023)
- devEpics
  - Fix problem with waveform out and aao records due to missing initialization of a private member variable
    in devAsynXXXArray.cpp.
- testErrorsApp
  - Add code to test the asynXXXArray->write() functions.

## Release 4-44 (March 13, 2023)
- devEpics
  - Rewrite the device support for arrays.
    Previously it used a large C macro to be able to support int8, int16, int32, float32, and float64
    data types without a lot of repetitive code.  This was changed to use a C++ template file.
  - Previously only the waveform record was supported for arrays.  Added support for aai and aoo records.
  - Add new devEpicsPvt functions (from Michael Davidsaver):
    - Avoids leaking DBENTRY during initialization.
    - Use dbInitEntry() from record rather than dbFindRecord() when available.
    - Reduces duplicate code.
- Documentation:
  - Converted all HTML files in documentation/ to Sphinx RST files in docs/source.
  - It uses the Read The Docs theme (sphinx-rtd-theme) which provides a panel
    on the left side of the screen for easy navigation.
  - Added Github actions to publish the Sphinx generated documentation to Github Pages.
  - The new home page for the asyn documentation is https://epics-modules.github.io/asyn.
  - Converted documentation/RELEASE_NOTES.html to RELEASE.md.
- testErrorsApp
  - Add aai and aao records to databases in order to test new device support for these.
- Fix testing on Github Actions by forcing TIRPC=YES, required for modern Linux releases.

## Release 4-43 (September 12, 2022)
- devEpics
    - Change logic for output records with asyn:READBACK so they set udf=0 in the outputcallbackcallback()
      function, and not in the processXX function. This is needed to correctly clear the
      UDF status and severity.
        - This problem is happening because for EPICS output records the only way to clear
          a UDF alarm is to put a value into the record, either from an external put from
          a CA or DB link (in which case code in the dbAccess.c dbPut() routine clears UDF
          and the record would get processed) or by reading a value through the DOL link during
          record processing triggered by some other means.
        - In both cases UDF will have been cleared before the call to checkAlarms() in the
          boRecord::process() routine, which is where the UDF field is checked.
        - Unfortunately for asyn all output records call the checkAlarms() routine before
          the device support's write_bo() routine, so clearing UDF in processBo() is too late
          for this process cycle, the UDF_ALARM has already been registered.
        - This also means that if the bo record is configured with non-zero alarms in ZSV
          or OSV the initial read wouldn't trigger the appropriate STATE_ALARM either, since
          they get set in that routine as well. 
- drvAsynUSBTMC
    - Add support for output terminator.
    - Increase buffer size to 1 MB.
    - Add Remote Enable (REN), Local Lockout (LLO), and Go To Local (GTL) support.
- asynPortDriver
    - Fixed initialization problem for UInt32 parameters. Initially setting a parameter
      to 0 left the corresponding PV undefined because the callback wasn't called. Thanks
      to Erico Nogueira for this fix.
    - Removed "using std::logic_error" from parameter header files. This is bad practice.
      This change could potentially break downstream module that would now have to use
      std::logic_error, rather than just logic_error. This is unlikely since the param
      classes are essential private to asynPortDriver. Thanks to Luka Krmpotic for this
      fix.
- drvAsynIPServerPort
    - Allow specifying an interface to bind the IP server port. The second argument
      to drvAsynIPServerPortConfigure allows specifying a hostname or IP address for multi-homed
      machines. If you want to specify any interface, use "localhost:port", an empty host
      ":port" or the address "0.0.0.0:port". The address has to be an existing interface
      on the host. If the name cannot be resolved, the call will fail. If you need the
      loopback interface, use "127.0.0.1:port". Thanks to Lutz Rossa for this.
- asynShellCommands
    - Provide usage information for asynSetTraceMask and asynSetTraceIOMask. This is
      helpful for remembering how to specify the bit masks numerically or symbolically.
      Thanks to Michael Davidsaver for this.
- asynManager
    - Added asynDisconnected and asynDisabled in strStatus(). Thanks to Kristian Loki
      for this.
- configure/RELEASE
    - All external modules are commented out. Users will need to uncomment the ones
      to be used.
- drvVxi11
    - Fix potential VXI connection resource leak. Thanks to Freddie Akeroyd for this.
- .ci
    - Change from Travis to Github Actions. Thanks to Michael Davidsaver for this.
    - Add support from sscan, calc, and ipac. Thanks to Freddie Akeroyd for this.
- devGPIB and streamSCPI template files
    - Changed DB_INSTALLS+= to DB+= to work with recent versions of EPICS.

## Release 4-42 (May 10, 2021)
- drvProligixGPIB
    - New driver for the Proligix GPIB-Ethernet controller. Thanks to Eric Norum for
      this. This $200 device makes GPIB devices available over Ethernet. It uses a proprietary
      protocol, not VXI-11. 
- asynPortDriver
    - Added checks for invalid list (<0 or &ge;maxAddr).
    - Added new error code asynParamInvalidList that is returned if the list passed
      is not valid.
- devCommonGPIB.c
    - Added 0 termination after strncpy().
- asynInterposeEos
    - Improve efficiency when eosOutLen is 0.
- asynRecord
    - Minor changes to avoid compiler warnings.

## Release 4-41 (September 19, 2020)
- Many files
    - Changed the code and the Makefile to avoid using shareLib.h from EPICS base. Added
      a new header file asynAPI.h that is used to control whether functions, classes,
      and variables are definined internally to the asyn library or externally. This is
      the mechanism now used in EPICS base 7. It makes it much easier to avoid mistakes
      in the order of include files that cause external functions to be accidentally exported
      in the asyn DLL or shareable library. This should work on all versions of base,
      and have no impact on user code.
- devAsynInt32.c, devAsynUInt32Digital.c
    - Replaced strncpy()) with memcpy() to avoid compiler warnings on newer versions
      of gcc (e.g. 8.3.1).

## Release 4-40-1 (August 24, 2020)
- drvAsynIPPort
    - Fixed the logic for using SO_REUSEPORT or SO_RESUSEADDR. In R4-40 RTEMS and Windows
      used SO_REUSEADDR and all others used SO_REUSEPORT. This does not work because Linux
      kernels before 3.9 also do not support SO_REUSEPORT. Now if SO_REUSEPORT is not
      defined then SO_REUSEADDR is used instead.

## Release 4-40 (July 17, 2020)
- ci-scripts
  - Updated to v3.0.1 of ci-scripts.
  - Added native Windows builds (VS2017) on Travis.
  - Added AppVeyor builds for testing on many Windows configuations.
- asynPortDriver
  - Fixed error in writeInt64 method.
  - Added asynParamSet class.
  - Added new constuctor that uses asynParamSet.
  - Added new utility template function findDerivedAsynPortDriver() that returns a
    pointer to an asynPortDriver derived class object from its name.
- Build system
  - Fixed logic for when CALC and SSCAN modules are needed.
- asynManager
  - Take connect timestamp *after* connection attempt. autoConnectDevice() has
    a built-in protection that forces at least 2 seconds between connection attempts.
    However, previously the timestamp was read *before* starting the connection,
    so if the connection attempt takes about 2 seconds (or more) to timeout then that
    2 second do-not-connect window was already expired (or about to expire) by the time
    connectAttempt() returns. Now we read the timestamp *after* connectAttempt()
    returns. 
  - Made 2 changes to queueLockPort.
      - If the queue request times out then return asynTimeout rather than asynError.
      - If the call to pasynManager->queueRequest fails then return the actual failure
        status rather than asynError.
    These changes improve the alarm handling when a device becomes unavailable. Previously
    it would toggle between TIMEOUT alarm when an actual read request timed out and
    READ alarm when a queue request timed out. This caused too many alarms. Previously
    if the port was Disconnected then it was returning asynError which sets READ alarm.
    In this case it should be returning asynDisconnected which sets COM alarm. 
- asynTrace
  - Changed the output with ASYN_TRACEINFO_SOURCE. Previously the file name included
    the complete path to the file, which could be very long. The path is now removed.
- asynPortClient
  - Fix bug in asynUInt32DigitalClient::registerInterruptUser().
- asynRecord
  - Fixed problem updating the CNCT field. Previously if an attempt to connect with
    CNCT failed CNCT stayed in the Connected state, rather than going back to Disconnected
    immediately.
- drvAsynIPPort
  - Added new tcp&, udp& and udp*& protocols that specify that the SO_RESUSEPORT flag
    should be used. <a href="https://lwn.net/Articles/542629/">This article</a> explains
    the use of the SO_REUSEPORT option for both TCP and UDP. The particular use case
    that motivated the addition of the SO_REUSEPORT to this driver is discussed in <a
      href="https://github.com/epics-modules/asyn/issues/108">this Github issue</a>.
    On Windows and RTEMS SO_REUSEADDR is used instead of SO_RESUSEPORT, but it should
    have the same effect.
- unittest/Makefile
  - Fixed to link with EPICS_BASE_IOC_LIBS unless EPICS_LIBCOM_ONLY=YES.
- devEpics
  - Fixed bug in devAsynOctet when using output records with info tag asynREADBACK=1
    and asynFIFO not specified. It was supposed to be creating a ring buffer with one
    element, but was not, so the readback did not work.
- Many files
  - Removed tabs and trailing white space.
  - Minor changes to avoid compiler warnings.

## Release 4-39 (February 24, 2020)
- Travis CI
- Added ci-scripts module to provide much more complete testing. Thanks to Ralph
  Lange for this.
    - Against EPICS 7.0 / 3.15.7 / 3.14.12.8 / 3.14.12.2
    - Using g++ and clang
    - Static and dynamic builds
    - Cross-compilation to Windows using MinGW
    - Cross-compilation to RTEMS 4.9 and 4.10 (including running the tests under qemu)
    - On MacOS
    - EPICS_LIBCOM_ONLY builds
- FTDI driver
  - Added minimal SPI support for AD9915. Thanks to Juri Tagger for this.
- asynPortDriver
  - Fixes to callbackThread for problems when calling the asynPortDriver destructor.
- devEpics
  - Added asynOctet support for lso, lsi, printf, and sCalcout records. Thanks to
    Freddie Akeroyd for this.
- asynInterposeEos
  - asynInterposeEos checks each byte for EOS. If it cannot find it and nRead &ge;
    maxchars, then previously it set the eom to ASYN_EOM_CNT using a bitwise OR. In
    this way, multiple flags can be propagated to the requester of the read. Since ASYN_EOM_CNT
    is not an end flag, but other two (ASYN_EOM_EOS and ASYN_EOM_END) are, it does not
    make sense to propagate a combination of end and non-end flag. Thus if the nRead &ge; maxchars is true, 
    now set eom = ASYN_EOM_CNT, i.e. without bitwise OR. This
    fixes a problem with StreamDevice on some VXI-11 devices. Thanks to Jernej Varlec
    for this.
- OPI files
  - Added .bob files for CS-Studio (Phoebus). These are autoconverted from medm .adl
    files

## Release 4-38 (January 3, 2020)
- devEpics
  - Added asynInt64 support for longout, longin, ao, and ai records. This allows these
    records to be used to communicate with drivers on the asynInt64 interface. This
    can be useful when running versions of base prior to 3.16.1 where the int64out and
    int64in records are not available. The ao and ai records can exactly represent up
    to 52-bit integers, while the longin and longout are limited to 32-bit integers.
- FTDI driver
  - Fixed missing argument to the iocsh drvAsynFTDIPortConfigure() command.
- drvVxi11
  - Fixed spurious connection error message.

## Release 4-37 (October 18, 2019)
- Added new 64-bit integer support
- asynDriver adds new asynInt64, asynInt64SyncIO, and asynInt64Array interfaces.
- asynDriver.h now does the typedef of epicsInt64 and epicsUInt64 when __STDC_VERSION__
  < 199901L on EPICS 3.14. This allows the Int64 interfaces to be built as long
  as the compiler supports the `long long` and `unsigned long long`
  data types.
- devEpics.dbd is now constructed at build time rather than being a static file.
  This enables Int64 device support to only be included on EPICS 3.16.1 and later.
- asynPortDriver adds support for 64-bit integers:
    - New parameter types asynParamInt64 and asynParamInt64Array.
    - New methods setInteger64Param(), getInteger64Param(), readInt64(), writeInt64(),
      getBounds64(), readInt64Array(), writeInt64Array(), doCallbacksInt64Array().
    - New masks for constructor asynInt64Mask, asynInt64ArrayMask.
- testErrors test application
  - Changes to driver, database and medm screens to test the Int64 interfaces. This
    uses the 64-bit device support which must be commented of out st.cmd if running
    on EPICS versions prior to 3.16.1.
- New FTDI driver
  - This driver allows much greater control over USB ports for serial communication
    than the standard Linux /dev/ttyUSBx driver. Thanks to Bruno Martins for this.
- devEpics
  - Fixes to only print an error message when the status of a write or read operation
    changes, not each time there is an error. Thanks to Ben Franksen for this.
- asynRecord
  - Fixes to allow changing the HOSTINFO for the asynIPPort driver when the port is
    not connected. Thanks to Krisztián Löki for this.
- Many files
  - Changes to avoid compiler warnings.

## Release 4-36 (August 8, 2019)
- asynManager
  - Improved debugging output when scheduling queue request timeout.
- drvAsynIPPort
  - Improved diagnostic messages.
- asynPortDriver
  - Added new parseAsynUser() method. Changed all readXXX and writeXXX methods to
    use this, rather than getAddress().
  - Use asynPortDriver::getAddress() in callback functions, rather than pasynManager::getAddress().
    This allows the getAddress() to be overridden in derived classes.
  - Fix to prevent a potentially locked mutex from being destroyed, as well as use-after-free
    bugs on other members of asynPortDriver. Thanks to Martin Konrad for this.
- asynInterposeDelay, asynInteposeEcho
  - Fixes to compile on Visual Studio 2010.

## Release 4-35 (March 18, 2019)
  - devAsynInt32, devAsynFloat64, devAsynUInt32Digital, devAsynOctet
  - Fixed a deadlock problem when asyn:READBACK was used on output records.
  - In devAsynOctet there was still an isssue if asyn:READBACK=1 was used without
    asyn:FIFO, i.e. when ring buffers were not enabled. The code now forces a minimum
    ring buffer size of 1 if asyn:READBACK=1 to avoid the deadlock. asyn:FIFO can still
    be used to select a larger ring buffer size. 
  - drvAsynIPPort
  - Fixed a problem with the disconnectOnReadTimeout option. Previously it would disconnect
    even if pasynUser->timeout was 0. This is not logical, and meant this option could
    not be used with StreamDevice, because StreamDevices flushes the input by reading
    with a timeout of 0 to support I/O Intr scanned records.
  - Updated the documentation to say that in order to receive UDP broadcast messages
    the `localPort` parameter in the hostInfo string in `drvAsynIPPortConfigure`
    must be specified. Example: `drvAsynIPPortConfigure("BD","255.255.255.255:1234:3956
      UDP",0,0,0)` will listen for broadcast messages on port 3956. If the port
    is only to be used to receive broadcast messages then the UDP protocol should be
    specified. If the port is also to be used to send UDP broadcasts then the UDP* protocol
    must be specified. Example: `drvAsynIPPortConfigure("BD","255.255.255.255:1234:3956
      UDP*",0,0,0)`. In this case it will send the broadcast messages on UDP port
    1234 and listen for broadcast messages on UDP port 3956.
  - asynShellCommands
  - Enhancement to allow using strings for the mask arguments of asynSetTraceMask,
    asynSetTraceIOMask, and asynSetTraceInfoMask shell functions. The mask can be specified
    as an integer (previous behavior) or as symbolic names connected with + or |. Spaces
    are allowed but require quotes. The symbolic names are like the macro names in asyn.h,
    but not case sensitive and the prefixes ASYN_, TRACE_, TRACEIO_, and TRACEINFO_
    are optional. Thanks to Dirk Zimoch for this. Examples:
    ```
    asynSetTraceMask port,0,ASYN_TRACE_ERROR 
    asynSetTraceIOMask port,0,ascii+escape 
    asynSetTraceInfoMask port,0,1+port+TRACEINFO_SOURCE|ASYN_TRACEINFO_THREAD
    ```
- asynPortClient.h, asynPortClient.cpp
  - Renamed asynPortClient base class to asynParamClient. This is the class from which
    asynInt32Client, asynFloat64Client, etc. are derived.
  - The new asynPortClient class connects to a specific asynPortDriver object. It
    creates an asynParamClient derived class object for each of the parameters in that
    driver. It uses the std::map class to map between the parameter name key and the
    asynParamClient object for that parameter. It also defines overloaded write() and
    read() methods that take a paramName argument and the value to be written or pointer
    to read into. The data type of the value or pointer must match the parameter type
    or a run-time exception will be thrown. This new class is more convenient to use
    because it is no longer necessary to create the asynInt32Client, asynFloat64Client,
    etc. objects for each parameter to be accessed.
- asynPortDriver
  - Added asynPortDriver::getNumParams() method to get the number of parameters currently
    defined.
- asynInterposeDelay
  - New interpose driver that waits for a user-specified time after sending each character
    before sending the next one. Some poorly designed devices require this. Thanks to
    Dirk Zimoch.
- asynInterposeEcho
  - New interpose driver that waits for a device to echo each character before sending
    the next one. Some poorly designed devices require this. Thanks to Dirk Zimoch.

## Release 4-34 (September 13, 2018)
- devAsynFloat64.c, devAsynInt32.c
    - Added support for SCAN=I/O Intr with asynFloat64Average and asynInt32Average device
      support. Previously only periodic scanning or manual processing was supported. The
      I/O Intr support computes the average and processes the record once NumAverage callback
      readings have been received. The SVAL field in the ai record is used to set NumAverage.
      This is rather a kluge, but there is not another good way to communicate this value
      to the device support, while allowing it to be changed at run-time.
- drvAsynIPPort.c
    - Improved error reporting. Thanks to Dirk Zimoch for this.
- asynInterposeEos.c
    - Improved asynTrace output to print bytes read and eom, and print warning if low-level
      driver returns error.
- asynInterposeCom.c
    - Added support for the asynOption ixon (XON/XOFF) for ports communicating via the
      RFC 2217 Telnet protocol. In this case, as noted in the standard, ixon implies both
      outbound and inbound flow control.
- asynPortDriver
    - Fixed uninitialized value in paramVal.
- asynShellCommands.h
    - Add asynOctetDisconnect and asynWaitConnect.
- drvVxi11.c
    - Fixed logic for FLAG_NO_SRQ. Error was introduced in R4-31.
- CONFIG_SITE
    - Optionally include $(SUPPORT)/configure/CONFIG_SITE.
    - Older Linux and Cygwin distributions have packages (e.g. glibc-headers) that put
      the rpc include files in /usr/include/rpc. Newer Linux and Cygwin distributions
      use the tirpc package instead. For such systems define TIRPC=YES.
- .travis.yml
    - Fixed to fetch Debian files from nsls2 with https not http.

## Release 4-33 (January 27, 2018)
- devAsynFloat64.c
    - Added support for ASLO/AOFF scaling and SMOO value smoothing to the float64 device
      support for ai and ao records. SMOO only applies to ai records. devAsynFloat64 directly
      writes to the record's VAL/OVAL fields, so the conversion routines in the record
      that use RVAL/VAL don't work.
- devAsynInt32.c, devAsynUInt32Digital.c, devAsynFloat64.c, devAsynOctet.c
    - Fixed a problem with output records that have the asyn:READBACK info tag, i.e.
      output records that update on callbacks from the driver. Previously it did not correctly
      distinguish between record processing due to a driver callback (in which case it
      should not call the driver) and normal record processing (in which case the driver
      should be called). A new test application, testOutputCallbackApp, was added to test
      this. It allows testing all combinations of the following 6 settings for all 4 of
      these device support files:
        - Synchronous driver, i.e. ASYN_CANBLOCK not set.
        - Asynchronous driver, i.e. ASYN_CANBLOCK is set.
        - Driver callback is done in the write() operation.
        - Driver callback is done in a separate thread. The callbacks are triggered with
          the TriggerCallbacks record.
        - Single callback is done in each operation.
        - Multiple callbacks are done in each operation. The NumCallbacks record selects
          the number of callbacks.

      The callback values are the following. For the longout records (asynInt32 and asynUInt32Digital)
      and the ao record (asynFloat64) the callback value is N+1 where N the current record
      value. Thus if one writes 10 to the longout record and NumCallbacks is 5 it will
      immediately do callbacks with the values 11, 12, 13, 14, 15. For the bo record the
      value toggles between 0 and 1 for each callback. For the stringout record the value
      is a string with the format "Value=N". N increments by 1 on each callback as for
      the longout records. For each output record there is a corresponding input record
      with SCAN=I/O Intr. If the test is working correctly the output records should always
      have the same values as the input records. 
- devAsynOctet.c
    - Fixed bug where the record alarm status was not set correctly if a write or read
      failed with a synchronous driver.
- asynManager.c
    - Improved error messages from autoconnect attempts. Messages are now printed only
      when the autoconnect status changes, rather than each time there is a failure. Thanks
      to Ben Franksen for this.
- drvAsynIPServerPort.c
    - Changed the logic so it creates maxClients drvAsynIPPort drivers at initialization
      rather than on-demand when clients connect. This makes it possible to use these
      ports before iocInit, which is much more useful. They can be used in input and output
      links in EPICS records, for example. This change should be backwards compatible,
      since it was transparent when the ports were created.
    - The first drvAsynIPPort created is now named PORT_NAME:0 rather than PORT_NAME:1,
      where PORT_NAME is the name of the drvAsynIPServer port created by this driver.
      This might break backwards compatibility, but clients were normally getting the
      name from a callback, so probably not.
    - Set the socket option SO_REUSEADDR when creating the listening socket. Previously
      if a client was connected and the IOC exited it could not be restarted again immediately.
      One needed to wait for the operating system to time out the connection. This change
      allows the IOC to be run again with the same listening port immediately, without
      waiting for the operating system timeout.
    - Added an exit handler to close the listen socket and delete the private structure.
    - Improved the report() function so it shows all the drvAsynIPPort drivers that
      were created.
- testIPServerApp
    - Added a new datbase testIPServer1.db for testing the enhancements to drvAsynIPServerPort.
      It contains a stringin record and a stringout record that use the drvAsynIPPort
      created by drvAsynIPServerPort.
    - Added a new startup script, iocBoot/ioctestIPServer/st.cmd.testIPServer1 which
      creates a drvAsynIPServerPort called P5001 on localhost:5001. It loads the testIPServer1.db
      database connected to the drvAsynIPPort created by P5001, called P5001:0. One can
      telnet to port 5001 on the IOC computer. All text typed in the telnet session will
      go into the stringin record. Text written to the stringout record will appear in
      the telnet session.
- drvVXI11.c
    - Improved error messages from connection attempts. Messages are now printed only
      when the connection status changes, rather than each time there is a connection
      failure. Thanks to Ben Franksen for this.
- testErrorsApp
    - Previously the user-defined alarm status and severity were only set on callbacks,
      i.e. input records with SCAN=I/O Intr and output records with info tag asyn:READBACK=1.
      Now all calls to the read() and write() methods in each interface also return the
      user-defined alarm status and severity in pasynUser.
- asynPortClient
    - Fixed typo in constructor for asynUInt32DigitalClient, it was calling asynInt32SyncIO->connect
      rather than asynUInt32DigitalClient->connect.
- OPI screens
    - Added new opi/Makefile. This automatically converts all opi/medm/*.adl files into
      corresponding files in edm/autoconvert, caqtdm/autoconvert, and boy/autoconvert.
      It uses new RULES_OPI and CONFIG_SITE.linux-x86_64 files in synApps/support/configure
      directory. The upper-level edm, caqtdm, and boy directories should be used only
      for manually tweaked files.
    - The medm adl files were improved to make the autoconverted files work better.
      The size of text graphics and text update widgets were set to the actual text size,
      and text update widgets were set to the correct datatype, e.g. string type for enum
      PVs. 
- Documentation
    - asynDriver.html has been improved. All level 3 headings are now links in the Contents
      at the start of the file. This makes it much easier to find a particular topic and
      jump to it quickly. Many of the test applications are now documented, including
      medm screen shots to show how they work.

## Release 4-32 (September 15, 2017)
- asynManager.c
    - Fixes to queueLockPort() which is used by the asynXXXSyncIO interfaces.
        - Previously queueLockPort() did not specify a timeout value or timeout callback
          function in the call to pasynManager->queueRequest().
        - This meant that if a port disconnected while a lock request was queued it would
          hang until the port reconnected. For example, calls to the asynXXXSyncIO functions
          would not return until the port reconnected.
        - Added a timeout callback for the queueRequest in queueLockPort(). This callback
          function prints a warning message with ASYN_TRACE_WARNING. It sets the psynUser->auxStatus
          field to asynTimeout so that queueLockPort() detects an error and returns an error
          status without locking the port. Callers must be sure to check the return status
          of queueLockPort and not call any port driver functions if it does. This was already
          done in all of the asynXXXSyncIO functions.
        - The timeout for the queueLockPort queue request is determined as follows
            - Each port now has a queueLockPortTimeout value. This is set to a default of 2.0
              seconds when the port is created.
            - This can be changed with a new iocsh command
              ```
              asynSetQueueLockPortTimeout(portName, timeout)
              ```
            - If the pasynUser->timeout that is passed for a particular I/O operation is larger
              than the port timeout value this larger value is used instead.
    - In traceVprintIOSource if the traceIOMask is 0 or traceTruncateSize &le; 0 output
      a newline, otherwise output is garbled
- asynPortDriver
    - Replaced "int" with "epicsInt32" in many places for consistency and to avoid compiler
      warnings on some architectures. Thanks to Scott Baily for this.
    - Added new asynPortDriver::getParamType() methods. Thanks to Mark Davis for this.
    - Added a new asynPortDriver constructor that removes the paramTableSize argument.
      Marked the previous constructor as EPICS_DEPRECATED. Added a new initialize() method
      that both constructors call to do the actual work. Changed all of the asynPortDriver
      example programs and unit test programs to use the new constructor. Thanks to Martin
      Konrad for this.
- Test application *Main.cpp files
    - The main programs in all of the test applications have been updated to the version
      in the template files in EPICS base 3.15.5. This includes a call to epicsExit()
      after the iocsh() returns. This is needed for epicsAtExit to work correctly on some
      platforms, including Windows.
- Files in configure/ directories
    - The Makefile, CONFIG, CONFIG_SITE, and RULES* files in configure/, makeSupport/top/configure/,
      and documentation/HowToDoSerial/AB300/configure/ have been updated to the versions
      in the template files in EPICS base 3.15.5. The new versions are better suited to
      site-specific customization.

## Release 4-31 (February 18, 2017)
- VXI-11
    - Added option to omit SRQ channel.
- Build system
    - Minor change to build with mingw.
- Unit tests
    - Added unit tests in asynPortDriver/unittest.
- asynPortDriver
    - Changed the parameter list to be an std::vector. This means that it is no longer
      necessary to pass the number of parameters in the constructor because the list grows
      with each call to createParam().
    - Changed the internal storage of string parameters from char * to std::string.
      Added new setStringParam() and getStringParam() methods that take an std::string&
      argument rather than char *. These do not require specifying a maximum string length,
      unlike the char * methods.
    - Changed the constructor so that it throws an std::runtime_error exception in the
      case of any error, rather than simply calling "return" and creating a non-functional
      asynPortDriver object, which it did previously. It still prints an error message
      as it has done previously.
- drvAsynIPPort
    - Fixed bugs in Unix sockets.

## Release 4-30 (August 23, 2016)
- asynDriver
    - Added new fields to the asynUser structure. These are `int alarmStatus`
      and `int alarmSeverity`. They are intended to pass alarm information
      back to device support, indepenent of the status return from the write() or read()
      methods or the auxStatus field used for callbacks.
- devEpics
    - Previously when the status return or auxStatus were not asynSuccess then device
      support did not update the value, and set the record STAT and SEVR based upon rules
      in the generic device support. If the new alarmStatus and alarmSeverity fields are
      0 (default) then the current behavior is retained. If these fields are non-zero,
      then the record STAT and SEVR will be set to these values, independent of the status
      return or the auxStatus values. This allows a driver to set the record value, while
      still setting the STAT and SEVR to values other than NO_ALARM. The behavior of the
      function asynStatusToEpicsAlarm() was changed, and this function is now always called
      at the end of the process() function, even if the status or auxStatus is asynSuccess.
- asynPortDriver
    - Added new methods setParamAlarmStatus(), setParamAlarmSeverity, getParamAlarmStatus(),
      getParamAlarmSeverity to write and read the new pasynUser->alarmStatus and pasynUser->alarmSeverity
      fields.
    - Changed the constructor so that it sets the ASYN_MULTIDEVICE flag if maxAddr>1
      even if the caller neglected to set it.
    - Fixed destructor eliminate memory leaks. Thanks to Henrique Almeida.
- devAsynOctet
    - Added support for initial readback from driver for stringout and waveform output
      records to support bumpless reboots. This support is only enabled if the info tag
      asyn:INITIAL_READBACK is present for these records in the database. The testErrorsApp
      application was updated to test this feature.
    - Check that FTVL is CHAR or UCHAR and return an error in init_record if it is not.
    - Changed the waveform record device support asynOctetWrite to only support string
      output. The numchars argument in pasynOctet->write() is obtained by calling strnlen().
      This makes it consistent with the behavior of the stringout record. 
    - Added a new waveform record device support asynOctetWriteBinary. This passes numchars=NORD
      and does not call strnlen(), so it should be used for binary data.
- drvAsynSerialPort
    - Added support for RS-485 on Linux. This is only supported on ports with hardware
      RS-485 capability, and only on more recent Linux kernels. Thanks to Florian Feldbauer.
      The following new options were added for the asynOption interface in this driver.
        - rs485_enable Y/N
        - rs485_rts_on_send Y/N
        - rs485_rts_after_send Y/N
        - rs485_delay_rts_before_send msec_delay
        - rs485_delay_rts_after_send msec_delay
- testErrorsApp
    - Added new records AlarmStatus and AlarmSeverity that allow this driver to test
      the new alarm handling behavior.

## Release 4-29 (February 17, 2016)
- drvAsynIPPort
    - Revert the change made in R4-27 to drvAsynIPPortConfigure that changed the noProcessEos
      flag to userFlags. The final argument to drvAsynIPPortConfigure is now noProcessEos
      again.
    - Implemented the asynOption interface which allows configuring options using key/value
      pairs. This interface is used in the drvAsynSerialPort driver for configuring baud
      rate, stop bits, etc. It is also used on drvAsynIPPort for configuring these same
      items if the port was configured with the COM qualifier, used for Ethernet/Serial
      terminal servers that use the TELNET RFC 2217 protocol. For that drvAsynIPPort uses
      the asynInterposeCOM to interpose the asynOctet and asynOption interfaces.
    - drvAsynIPPort now also directly supports the asynOption interface for 2 key/value
      pairs.
        - key="disconnectOnReadTimeout", value="Y" or "N". This option replaces the USERFLAG_CLOSE_ON_READ_TIMEOUT
          that was introduced in R4-27. The advantage of using the asynOption interface is
          that this behavior can now be changed at run-time, rather than being set once when
          the driver is created.
        - key="hostInfo", value="host:port[:localport] [protocol]", i.e. the Internet host
          name, port number, optional local port number and optional protocol. This uses the
          same syntax as the drvAsynIPPortConfigure command. This allows changing at run time
          the Internet host and port to which this asyn port is connected. The only restriction
          is that the setting of the COM (TELNET RFC 2217) protocol cannot be changed from
          that specified with drvAsynIPPortConfigure. This is because if COM is specified
          in the drvAsynIPPortConfigure command then asynOctet and asynOption interpose interfaces
          are used, and asynManager does not support removing interpose interfaces.
    - Improved debugging output with ASYN_TRACEIO_DRIVER. Previously there was no asynTrace
      output if recv() or recvfrom() returned &le;0. It is often useful to know if these
      functions returned 0, so the asynTrace output is now only suppressed if these functions
      return <0.<br>
      There was previously no ASYN_TRACEIO_DRIVER output from the flushIt() function,
      which repeatedly calls recv() until the return value is &le;0. ASYN_TRACEIO_DRIVER
      will now print a message with the total number of bytes flushed in this function,
      if the number is greater than 0.
- asynRecord
    - Added 2 new record fields to read and control the new drvAsynIPPort options:
        - DRTO (Disconnect on Read Timeout)
        - HOSTINFO (hostInfo string)
    - Added new OPI screen (asynIPPortSetup.[adl,.opi,.edl,.ui]). This screen allows
      display and control of the new DRTO and HOSTINFO fields. It also allows display
      and control of the serial port parameters for drvAsynIPPorts created with the COM
      (RFC 2217) protocol. There was previously no GUI to control these options for IP
      ports. This screen is accessed via the More menu in asynRecord.adl.
- asynPortDriver
    - Add paramName to error messages for easier debugging.
    - Throw exception if NULL pointer is passed to setStringParam().
- asynDriver
    - asynTrace now does flush after each write on files, previously it only did this
      on stdin and stderr. This prevents trace information in files being lost if the
      application crashes.
    - Fixed syntax of call to cantProceed().
    - Added definition of ASYN_EXCEPTION_STRINGS which provides strings corresponding
      to each asynException enum value.
- testBroadcastApp
    - Added new test application testBroadcastBurst. This application periodically sends
      a burst of broadcast packets. The number of packets per burst and the interval between
      bursts can be specified on the command line. It is useful for testing devices to
      see how tolerant they are of broadcast bursts.
- testConnectApp
    - Added missing extern "C" required to build dynamically on Windows.
- Many source files
    - Replaced tab characters with spaces. Tabs in source files are bad practice.

## Release 4-28 (December 12, 2015)
- drvAsynIPPort
    - Fixed problem with UDP broadcast sockets. Previously sending broadcast messages
      worked correctly, but any responses from clients to those messages were rejected
      by the IOC system. This is because connect() and send() were being called. This
      is not correct, in this case connect() should not be called and sendto() should
      be called rather than send(). Also changed the code to call recvfrom() rather than
      recv()for UDP sockets. This returns the source address information, which is now
      printed with the source message length and contents if ASYN_TRACEIO_DRIVER is set.
- Test applications  
    - New test application, testOutputReadbackApp. Tests that the initial values of output
      records are set correctly.
    - New test application directory, testBroadcastApp. It has 2 applications for sending
      broadcasts and reading responses. One uses asyn, and the other native OS calls.
      The native OS call version only builds on Linux. This is used for testing the UDP
      broadcast problem fixed in this release.

## Release 4-27 (October 7, 2015)
- Repository location
   - The source code repository was moved from <a href="https://svn.aps.anl.gov/epics/asyn">
      https://svn.aps.anl.gov/epics/asyn</a> to <a href="https://github.com/epics-modules/asyn">
        https://github.com/epics-modules/asyn</a>. This will make it much easier for
      others to collaborate in the development of asyn.
- drvAsynSerialPort
    - Fix to save the previous values of the baud rate and termios structures before
      attempting to change them. If the change fails then the previous values are restored.
      Previously if a call to setOption() failed, then subsequent calls to getOption()
      would return the value that failed. Now subsequent calls to getOption() will return
      the previous valid option, which is presumably the one that the underlying OS driver
      is still using. Thanks to Ron Sluiter for the initial fix to this problem.
- drvAsynSerialPortWin32
    - Fix to automatically prefix COM port names with "\\.\" unless the port name already
      begins with "\\.\". This is valid syntax for all COM ports and is required for all
      ports except COM1-COM9. Note that when passing the port string from the iocsh if
      the port name is in quotes then backslash characters should not be escaped, e.g.
      "\\.\COM10". If the port name is not in quotes then the backslash characters must
      be escaped, e.g. \\\\.\\COM10. Thanks to Freddie Akeroyd for the initial version
      of this fix.
- drvAsynIPPort
    - Added the option to automatically close the port when there is a read timeout.
      This was done by changing the syntax of the drvAsynIPConfigure command from
      ```
      drvAsynIPPortConfigure("portName","hostInfo",priority,noAutoConnect,noProcessEos)
      ```
      to
      ```
      drvAsynIPPortConfigure("portName","hostInfo",priority,noAutoConnect,userFlags)
      ```
      userFlags: bit-wise ORed:
        - Bit 0 is USERFLAG_NO_PROCESS_EOS. If 0 then asynInterposeEosConfig is called specifying
          both processEosIn and processEosOut. 
        - Bit 1 is USERFLAG_CLOSE_ON_READ_TIMEOUT. If 1 then the (TCP) socket will be closed
          when read() returns a timeout. 
        - Bit 2--31 are reserved, set to zero. 
      This change is backwards compatible since bit 0 is the same as the previous noProcessEos.
      Thanks to Torsten Bogershausen for this change. 
- devEpics
    - Changed error reporting if pasynManager->queueRequest returned an error. Previously
      error messages were printed on every call, which would cause a large number of messages
      if the port was disconnected and records were periodically processing. Now an error
      message is only printed when the status return from pasynManager->queueRequest changes,
      so there will be single message for each record when a port disconnects and a single
      message when it reconnects.
    - Changed error handling when any of the following errors are encountered:
        - pasynManager->queueRequest returns an error
        - The I/O call to the driver (e.g. pasynInt32->read()) returns an error.
        - The interrupt callback function is passed an error code in pasynUser->auxStatus

      Previously such errors did correctly set the record alarm status and severity. However,
      they did not return an error code to the function that called the device support
      write() or read() function. They also updated the record VAL or RVAL field when
      there was an error, which they should not. Now these functions return an error code
      and do not update VAL or RVAL when there is an error. 
- asynPortDriver
    - Changed the status return from pasynManager->queueRequest from asynError to asynDisconnected
      if a port with ASYN_CANBLOCK=0 is disconnected.
- asynShellCommands
    - Added new command asynSetMinTimerPeriod, and corresponding C function. This command
      is currently only implemented on the WIN32 architectures, and simply prints an error
      message on all other architectures. It allows reducing the minimum time for epicsThreadSleep()
      from the default of 10 ms to the minimum supported Windows system value. On most
      systems this is 1 ms. Note that epicsThreadSleepQuantum() does not seem to report
      the correct value whether or not this function is called. On my system it always
      reports 0.0156 seconds. Note also that this function really has nothing to do with
      asyn, and the equivalent functionality should probably be moved into EPICS base
      at some time.
- Deadlock issue
    - There is a potential deadlock issue when using input records with SCAN=I/O Intr.
      This is discussed in the <a href="KnownProblems.html">KnownProblems document</a>.
      Thanks to Ambroz Bizjak for finding this problem.

## Release 4-26 (February 15, 2015)
- devEpics
    - Added capability for output records to update their values with a callback from
      the driver. This is enabled by adding the following info tag to the output record:<br>
      `info(asyn:READBACK, "1")`<br>
      The output record value will update any time the driver does a callback with a new
      value. The output record alarm status and severity will also update to reflect the
      value passed by the driver in pasynUser->auxStatus. The driver callbacks use the
      same ring buffer mechanism that input records do. The default ring buffer size is
      10 for all records except stringin, stringout, and waveform records, for which the
      default is 0.
    - Updated the test application testErrorsApp to demonstrate and test output record
      updates.
    - Added ring buffer support to devAsynOctet. The default ring buffer size is 0 for
      all records in devAsynOctet, i.e. stringin, stringout, waveform in and out. Non-zero
      values can be specified with the asyn:FIFO info tag.
    - Changed the info tag that is used to control the size of the ring buffer for driver
      callbacks. The tag has been changed from FIFO to asyn:FIFO, i.e. adding the asyn:
      namespace to prevent future conflicts with any other facility. <em>NOTE:</em>This
      change is not backwards compatible, any databases using the FIFO info tag will need
      to be changed to asyn:FIFO. It is not expected that this will affect many users,
      since the default ring buffer size is used in nearly all cases.
- asynPortDriver
    - Bug fix: If setParamStatus() was called for an array or generic pointer parameter
      then callParamCallbacks() could return prematurely and not do the callbacks for
      all modified parameters.

## Release 4-25 (December 10, 2014)
- devAsynOctet
    Fixed 2 bugs:
    - The interrupt callback function for stringin records (e.g. with SCAN=I/O Intr)
      did not null-terminate the string if the driver returned MAX_STRING_SIZE or more
      characters. Thanks to Freddie Akeroyd for fixing this.
    - The interrupt callback function for stringin and waveform records were not properly
      locking the record when modifying the record fields.
- devAsynXXXArray
  - Added optional support for ring buffers with waveform records. Ring buffers were
    added to asyn device support for scalar (non-array) records in R4-10. To enable
    ring buffer support on a waveform record the record info tag FIFO can be set to
    a value greater than 0. For example this line in a db file for a waveform record
    sets a ring buffer size of 20 elements.
    ```
    info("FIFO", "20") 
    ```
    Ring buffers are only used when records have SCAN=I/O Intr. They allow the record
    to process all of the arrays from a rapid burst of callbacks from the driver. However,
    because Channel Access does not provide any buffering for arrays, even if the record
    processes for each new array callback, Channel Access will not necessarily send
    events for each value, because it just sends the current record value when the CA
    callbacks are done.
  - A new test application, testArrayRingBufferApp was added to test this array ring
    buffer support. A new iocBoot/ioctestRingBuffer directory was also added.
- Interfaces
  - Added new asynOptionSyncIO interface. This interface is needed so that the asynOption
    interface can be called synchronously when it is OK to block.
- Building asyn with only using libCom from EPICS base
  - It has always been asserted that except for devEpics asyn only really depends on
    libCom from EPICS base. People who are interested in using asyn drivers from other
    control systems, want to minimize the dependencies of libraries from EPICS base.
    The following lines have been added to asyn/configure/CONFIG_SITE:
    ```
    -------------------------------------------------------------------------------
    # If you want to build asyn so the only dependency on EPICS base is libCom then set the following flag
    #EPICS_LIBCOM_ONLY=YES
    -------------------------------------------------------------------------------
    ```
    If EPICS_LIBCOM_ONLY is YES then the build is done so that only libCom is needed.
    This does the following:
    - Omits building all of the device support for EPICS records
    - Omits building the test applications that create IOC applications
    - Sets asyn_LIBS to Com rather than $(EPICS_BASE_IOCS_LIBS) when creating the library
    - Changes the logic in asynPortDriver which uses interruptAccept. If EPICS_LIBCOM_ONLY
      is set then dbDefs.h is not included and interruptAccept is defined as a static
      variable set to 1.
  - A new C++ class was added, asyn/asynPortClient/asynPortClient.cpp. This class makes
    it easy to write a C++ application that starts existing asyn port drivers and communicates
    with them over standard asyn interfaces without running an IOC.
  - A new test application directory was added testAsynPortClient. This tests running
    C++ applications that communicate with asyn port drivers without running an IOC.
    This currently contains a single test application, testAsynIPPortClient.cpp. This
    program creates an asynIPPort driver, and uses the command line arguments to set
    the hostInfo string, a single command string to send to the server, and optionally
    the input and output EOS. It then prints out the response from the server. There
    are 3 example shell scipts that show how to use testAsynIPPortClient to communicate
    with a Web server, XPS motor controller, and a telnet host respectively.<br>
- drvAsynUSBTMC
  - Bruno Martins found and fixed a problem with data transfers that spanned multiple
    USB packets.

## Release 4-24 (October 14, 2014)
- drvAsynIPPort.c
  - Added capability to specify the local port that the server should use for the connection.
    Normally the local host choses a random local port that it binds to and passes to
    the server. There are a few servers that only accept a specific local port or range
    of local ports, for which this capability is required. The new syntax is:
    ```
    <host>:<port>[:localPort] [protocol]
    ```
    For example
    ```
    164.54.160.100:5000:10101 UDP
    ```
    where 10101 is the optional local port number.
- devEpics
  - Fixed all initialization routines so that if there is an error they do the following:
    - Call recGblSetSevr(precord,LINK_ALARM,INVALID_ALARM)
    - Set precord->pact=1
    - return(INIT_ERROR), where INIT_ERROR=-1
  - Thanks to Nick Rees for these fixes.
- Many source files
    Fixed problem with location of #define epicsExportSharedSymbols and/or #include
    <epicsExport.h>. In previous versions these were placed immediately before
    the #include statements defining symbols for that *source file* . However,
    this was incorrect, they must be placed before all of the #include statements defining
    symbols *for that DLL* . This mistake causes the same symbol being defined
    with both dllExport and dllImport when building the DLL. The Visual Studio compiler
    does not even warn about this error, but it produces a fatal error with the GCC
    compiler under Cygwin.

## Release 4-23 (June 16, 2014)
- asynManager.c
    Fixed a bug in pasynManager->memMalloc. It could return a pointer that was not
    a multiple of 8 bytes. This led to subtle problems on some architectures (e.g. ARM)
    if a double was stored in the memory returned by memMalloc. Fixed the code so the
    pointer is always a multiple of 16 bytes (for future safety).
- drvAsynUSBTMC
    - Added driver for USB TMC (Test and Measurement Class) devices.
    - Works on any system that provides <a href="http://libusb.info">libusb-1.0</a>.
    - Requires no kernel modifications.
    - Tested with StreamDevice on Darwin, Linux and Windows.
    - Enabled by setting DRV_USBTMC=YES in configure/CONFIG_SITE.
- drvAsynSerialPort
    Fixed bug: the port was not always being disconnected when the OS returned serious
    errors. This was preventing USB serial devices from disconnecting and reconnecting
    properly.
- drvAsynSerialPortWin32 (Windows serial port driver)
 - Fixed 2 bugs in pasynOctet->read() when pasynUserTimeout=0:
    - It was not returning immediately, it was waiting 16 ms.
    - If there were no characters to be read it was not returning asynTimeout, it was
      returning asynSuccess. This prevented StreamDevice from working correctly.
- drvVxi11
  - Fixed several 32/64 bit issues.
- drvAsynIPServerPort
  - Added support for UDP servers, in addition to TCP servers. Thanks to Helge Brands
    and Dirk Zimoch from PSI for this.
- drvLinuxGpib
  - Fix to Makefile to link with system libgpib library.
- devEpics (asynInt32)
  - Sign-extend positive or unsigned values as well as negative values when DTYP=asynInt32
    and asynMask value is non-zero.
- devEpics (asynUInt32Digital)
  - Apply mask to longin and longout records.
- asynPortDriver
  - Undid the change in R4-22 that changed the asynStdInterfaces from protected to private.
    That change was not compatible with areaDetector R1-9-1.
  - Removed paramList.h and paramList.cpp, put the code into asynPortDriver.cpp. These
    two classes had many interdependencies, so needed to include each other's header
    files. This was difficult when building dynamically on Windows, where imported and
    exported symbols need to be distinguished.
  - Added links to asynPortDriver.html to detailed tutorial on how to write a driver
    using asynPortDriver.
- asynOctetSyncIO
  - Fixed a bug that could cause the values of *nbytesOut, *nbytesIn, and *eomReason
    to be garbage if an I/O error occurred.
- Many files
  - Handled exporting symbols consistently, which is important when building dynamically
    for Windows with Visual Studio. Eliminated all references to epicsSharedSymbols,
    now just #include <epicsexport.h> just before the #include for the header
    file that defines symbols for this file, and after all other #include statements.
    Thanks to Peter Heesterman for the initial version of this fix.
- configure directory
  - Updated the files in the configure directory to the versions from EPICS base 3.14.12.3.
    Paths to EPICS base and other modules are still defined in configure/RELEASE. However,
    other configuration values, such as LINUX_GPIB and DRV_USBTMC are now defined in
    configure/CONFIG_SITE, not in RELEASE.

## Release 4-22 (October 30, 2013
- asynDriver
  - Added support functions for setting timestamps in asyn port drivers. These can be
    used to set the timestamp when the port driver received data. The driver can then
    set the asynUser->timeStamp field to this value for all input records on read
    and callback operations. Records that have TSE=-2 will have this timestamp. There
    is support for registering a user-supplied function to provide the timestamp, which
    will override the default source that just calls epicsTimeGetCurrent().
  - Added the following new functions to pasynManager for timestamp support.
    ```
    asynStatus (*registerTimeStampSource)(asynUser *pasynUser, void *userPvt, timeStampCallback callback);
    asynStatus (*unregisterTimeStampSource)(asynUser *pasynUser);
    asynStatus (*updateTimeStamp)(asynUser *pasynUser);
    asynStatus (*getTimeStamp)(asynUser *pasynUser, epicsTimeStamp *pTimeStamp);
    asynStatus (*setTimeStamp)(asynUser *pasynUser, const epicsTimeStamp *pTimeStamp);
    ```
  - Added the following shell commands for timestamp support.
    ```
    asynRegisterTimeStampSource(const char *portName, const char *functionName);
    asynUnregisterTimeStampSource(const char *portName);
    ```
  - Added a new bit to asynTraceMask, ASYN_TRACE_WARNING. This is intended to be used
    for messages that are less serious than ASYN_TRACE_ERROR, but more serious than
    ASYN_TRACE_FLOW.
  - Added new asynTraceInfoMask. This mask controls the information printed at the beginning
    of each message by asynPrint and asynPrintIO. Thanks to Ulrik Pedersen for help
    with this. The mask is defined with the following bits:
      ```
      #define ASYN_TRACEINFO_TIME 0x0001
      #define ASYN_TRACEINFO_PORT 0x0002
      #define ASYN_TRACEINFO_SOURCE 0x0004
      #define ASYN_TRACEINFO_THREAD 0x0008
      ```
    - ASYN_TRACEINFO_TIME prints what has been printed in previous versions of asyn,
      the date and time of the message.
    - ASYN_TRACEINFO_PORT prints [port,addr,reason], where port is the port name, addr
      is the asyn address, and reason is pasynUser->reason. These are the 3 pieces
      of "addressing" information in asyn.
    - ASYN_TRACEINFO_SOURCE prints the file name and line number, i.e. [__FILE__,__LINE__]
      where the asynPrint statement occurs.
    - ASYN_TRACEINFO_THREAD prints the thread name, thread ID and thread priority, i.e.
      [epicsThreadGetNameSelf(), epicsThreadGetIdSelf(), epicsThreadGetPrioritySelf()].
  - Added a new shell command to control this mask.
    ```
    asynSetTraceInfoMask port,addr,mask
    ```
  - Added asynTrace information to the output of asynReport if details &ge;1.
- asynOctetSyncIO
  - Use simple lock/unlock operations rather than queueLockPort/queueUnlockPort for
    end-of-string manipulations (setInputEos, getInputEos, setOutputEos, getOutputEos).
    This ensures that these operations can take place even with the device disconnected.
- devEpics
  - Finished the support for setting the record TIME field for input records, both for
    read operations and for callback operations (i.e. records with SCAN=I/O Intr). This
    work was begun in R4-20, but it was not complete. Fixes were made in devAsynOctet,
    devAsynInt32, devAsynFloat64, and devAsynUInt32Digital.
  - Changed the ring buffer overflow messages that are printed in devAsynInt32, devAsynFloat64,
    and devAsynUInt32Digital. These now use the new ASYN_TRACE_WARNING mask. Prior to
    asyn R4-20 these used ASYN_TRACE_ERROR, and in asyn R4-20 and R4-21 they used ASYN_TRACEIO_DEVICE.
- drvAsynIPPort
  - Fixed a bug in calling poll(). Previously the status return from poll() was not
    being checked; it was assumed that when poll() returned the port either had new
    data or had timed out. This is not correct, because poll() can return prematurely
    with errno=EINTR if a Posix signal occurs before data is received or the timeout
    expires. This can happen, for example, when the Posix high-resolution timer routines
    (timer_create, etc.) are used in the IOC. The Prosilica vendor library uses the
    Posix timer routines, and there were problems using asyn IP ports in IOCs that were
    also running Prosilica cameras. The problem was fixed by calling poll() again if
    it returns EINTR and the desired timeout time has not expired since poll() was first
    called.
  - Added support for AF_UNIX sockets on systems that provide them.
- drvAsynSerialPort
  - Support for all baud rates supported by the operating system.
  - Fix for XON/XOFF support from Dirk Zimoch.
- Changes to work with EPICS base 3.15
  - Removed include of asyn.dbd, from drvAsynSerialPort.dbd, drvAsynIPPort.dbd, drvVxi11.dbd,
    and drvGsIP488.dbd.
  - Fixed Makefile rule for vxi11intr.h for parallel builds.
- drvLinuxGpib
  - Fixes for eomReason and EOS handling from Dirk Zimoch.
- asynOctetSyncIO interface
  - Removed the openSocket() function from this interface. This method really did not
    belong in this interface, since it just wrapped the call to drvAsynIPPortConfigure().
- asynRecord
  - Added 4 additional baud rates to the BAUD field (460800, 576000, 921600, 1152000).
    However, the BAUD field can still only support 16 fixed baud rates because it is
    type DBF_ENUM.
  - Added a new LBAUD (long baud rate) field of type DBF_LONG. This allows selection
    of any baud rate, not just those in the BAUD menu. Changing the BAUD field changes
    the LBAUD field. Changing the LBAUD field changes the BAUD field to the appropriate
    selection if it is a supported value, or to "Unknown" if not.
  - Added support for ASYN_TRACE_WARNING and asynTraceInfoMask, including the opi screens.
- asynPortDriver
  - Added new virtual methods to support Eos operations on the asynOctet interface.
    These are setInputEosOctet(), getInputEosOctet(), setOutputEosOctet(), getOutputEosOctet().
    Changed the report() method to print the current values of the inputEos and outputEos.
  - Added new virtual methods for timestamp support. These are updateTimeStamp(), setTimeStamp(),
    getTimeStamp().
  - Added new function getAsynStdInterfaces() to access the asynStdInterfaces structure,
    which is now private.
  - Changed the functions that do callbacks when callParamCallbacks() is called to call
    pasynManager->getTimeStamp() and set the pasynUser->timestamp field to this
    value in the callbacks.
  - Changed the base class readXXX() functions (e.g. readInt32(), readFloat64(), etc.)
    to call pasynManager->getTimeStamp() and set the pasynUser->timestamp field
    to this value. The readXXX() functions in derived classes should also do this, so
    that records with TSE=-2 will get the timestamp from the driver.
- testErrorApp, iocTestErrors
  - Added an example of a user-supplied timestamp source.
- testAsynPortDriver
  - Added calls to lock() and unlock() in simTask. This was previously a serious omission
    in this example driver. Thanks to Kay Kasemir for spotting this.

## Release 4-21 (February 18, 2013)
- asynDriver
  - Restored the original versions of pasynManager->lockPort and unlockPort that
    were used in asyn prior to R4-14. These versions just call epicsMutexLock and epicsMutexUnlock.
    In R4-14 these versions were replaced with versions that queued requests to lock
    the port. The R4-14 versions fixed a problem with the interfaces/asynXXXSyncIO functions,
    but it has become clear that the original versions are useful in some circumstances.
    The change was done as follows:
    - The lockPort and unlockPort functions in R4-20 were renamed to pasynManager->queueLockPort
      and queueUnlockPort.
    - The interfaces/asynXXXSyncIO functions were all changed to call the queueLockPort
      and queueUnlockPort, so they function identically to how they have since R4-14.
    - The versions of lockPort and unlockPort that existed prior to R4-14 were restored
      to pasynManager.
  - Changed the report() function so that if details<0 then asynManager does not
    print information for each device (address). It calls pasynCommon->report(-details)
    in this case so driver report functions will not be affected.
  - Changed the asynTrace print, printIO, vprint, vprintIO functions so they use EPICS_PRINTF_STYLE.
    This causes the GCC (version 3.0 and higher) and clang compilers to check the agreement
    of format strings with function arguments when using asynPrint() and asynPrintIO(),
    just as they do with printf(). This is very helpful in finding errors, and uncovered
    a number in asyn itself, which have been fixed.
- devGpib
  - Changed event code readback to support both the original 'short' VAL field (EPICS
    3.14 and earlier) and the new 'string' VAL field (EPICS 3.15 and later).
  - Added support for DSET_AIRAW and DSET_AORAW definitions.
  - Replace strcpy with strncpy in devCommonGpib.c to reduce possibility of errors.
    Use NELEMENTS macro in devSkeletonGpib.c rather than hardcoding. Thanks to Andrew
    Johnson for these.
- devEpics
  - Improved the initMbboDirect function in devAsynUInt32Digital.c. If an initial value
    is read successfully from the driver it now sets the .Bn fields in the record. It
    also sets VAL rather than RVAL and returns 2 rather than 0. Thanks to Andrew Johnson
    for this.
  - Fixed a bug in devAsynUInt32Digital.c, which was missing a call to epicsMutexLock
    in interruptCallbackInput. Thanks to Angus Gratton for this fix.
  - Fixed a bug in devAsynXXXArray.h to handle case of multiple interrupt callbacks
    between record processing. Previously this would result in a call to the asynXXXArray->read()
    in the driver, which is not correct. The asynXXXArray device support does not have
    a ring buffer, so multiple interrupt callbacks between processing results in data
    being "lost", i.e. the record processes more than once with the same data. This
    is not really an error, but we now issue an ASYN_TRACEIO_DEVICE warning. This is
    analogous to ring buffer overflow for non-array data types.
  - Changed the "ring buffer overflow" messages from "ASYN_TRACE_ERROR" to "ASYN_TRACEIO_DEVICE",
    for devAsynFloat64. This was done for other device support in R4-20, but float64
    was overlooked.
- Interfaces
  - Added writeRead and writeReadOnce functions to the asynGenericPointer interface.
    Thanks to Florian Feldbauer for this addition.
  - Fixed a bug in asynCommonSyncIO that could cause a crash if the connect() function
    returned an error.
- asynPortDriver
  - Added support for the asynOption interface. Added code to demonstrate and test this
    to the testErrors test application.
  - Added new method asynPortDriver::flushOctet(), which implements asynOctet::flush().
    The base class implementation reproduces the behavior of asynOctetBase.c::flushIt,
    i.e. it calls pasynOctet->read() repeatedly with a timeout of 0.05 seconds until
    it gets no data back. But now drivers can implement their own version of flush()
    if a different behavior is desired, which was not previously possible.
  - Changed the code so that the length of string parameters returned in readOctet and
    octetCallback is now strlen(string)+1, rather than strlen(string). The length thus
    now includes the terminating nil. This fixes problems with clients that request
    long strings or subscribe to monitors with a length of 0, but don't check for a
    nil terminator.
  - Changed the meaning of the "details" argument in the asynPortDriver::report() function.
    The new meaning is:
    - 0 = no details
    - &ge;1: print details for parameter list (address) 0
    - &ge;2: print details for all parameters lists (addresses)
    - &ge;3: print interrupt callback information
  - Changed the connect() and disconnect() methods to return an error if the device
    address specified by the pasynUser is invalid (i.e. <-1 or >MAX_ADDR-1).
  - Fixed problem that was causing dynamic builds (e.g. SHARED_LIBRARIES=YES) to fail
    on Windows.
- asynPortDriver/exceptions
  - Changes to allow compiling with the old vxWorks Tornado 2.0.2.1 compiler. Thanks
    to Dirk Zimoch for this fix.
- Other
  - Many minor changes to avoid compiler warnings on Linux, vxWorks, and WIN32. Thanks
    to Dirk Zimoch for many of these.

## Release 4-20 (August 30, 2012)
- asynManager
  - Fixed a bug that caused a deadlock if pasynManager->lockPort was called multiple
    times without calling pasynManager->unlockPort in between. Thanks to Sebastian
    Marsching from "aquenos GmbH" for this fix.
- devEpics
  - Added support for setting the record timestamp from the driver, using a new field,
    pasynUser->timeStamp. The driver can set this field in read and callback operations.
  - Fixed a long-standing bug in devAsynXXXArray support for input waveform records
    with SCAN=I/O Intr. The data were being copied to the record without using dbScanLock.
    This meant that the data could change while the record was processing.
  - Changed the "ring buffer overflow" messages from "ASYN_TRACE_ERROR" to "ASYN_TRACEIO_DEVICE",
    so they do not appear by default. These messages are not really errors, but warnings
    that record processing it not keeping up with the rate of driver callbacks for records
    with SCAN=I/O Intr.
- asynPortDriver
  - Fixed a bug when adding a parameter that already existed. Thanks to Hinko Kocevar
    for fixing this.
- Documentation
  - Added new document "HowToDoSerial_StreamDevice.html".
- Test programs
  - Added new test program, testConnectApp. It uses asynPortDriver, and has a polling
    thread that sends a user-defined string to a device and reads the response. It can
    be used to test the behavior when the device is disconnected and then reconnects,
    etc.
- Build system
  - Added a rule in asyn/Makefile to fix a problem that could cause parallel make to
    fail.

## Release 4-19 (May 21, 2012)
- Interfaces
  - Added a new interface, asynEnum. This interface is designed to allow drivers to
    set the strings, values, and severities for record enum fields. This can be done
    both at iocInit(), in init_record() with the pasynEnumSyncIO->read() function,
    and after iocInit via callbacks to device support.
- devEpics
  - Added support for the new asynEnum interface for bo, bi, mbbo, and mbbi records
    in the asynInt32 and asynUInt32Digital device support. These records now attempt
    to read the initial values of the strings, values (mbbo and mbbi only) and severities
    for the enum fields. They also support callbacks on the asynEnum interface, so that
    enum values can be set dynamically at run-time.
  - Improved the support for setting the alarm status of records. Previously for records
    that were not I/O Intr scanned STAT was always to READ_ALARM or WRITE_ALARM, and
    SEVR was set to INVALID_ALARM. A new function, pasynEpicsUtils->asynStatusToEpicsAlarm()
    was added that converts asynStatus values to EPICS alarm values. This allows records
    to have STAT=TIMEOUT_ALARM, DISABLE_ALARM, etc. More values of STAT can be supported
    in the future by adding more values to the asynStatus enum.
  - Previously it was not possible for input records with SCAN=I/O Intr to have their
    alarm status set at all. This support has been added. Device support now uses the
    pasynUser->auxStatus field in the pasynUser passed to the callback function.
    If auxStatus != asynSuccess then the record alarm STAT and SEVR are set to values
    based on the asynStatus. asyn port drivers can now signal error status to clients
    in callback functions by setting pasynUser->auxStatus to asynSuccess, asynTimeout,
    asynError, etc. This change should be backwards compatible with all drivers because
    the pasynUser that is used for the callbacks is private to the callback function,
    and the auxStatus field is initialized to 0, which is asynSuccess.
  - Added new waveform record device support, asynInt32TimeSeries and asynFloat64TimeSeries.
    These use callbacks from the driver on those respective interfaces to collect a
    time series of values in a waveform record. Added new medm file asynTimeSeries.adl
    for this, and added an example waveform record to testEpics/Db/devInt32.db.
- asynRecord
  - Fixed bugs that caused crashes if SCAN=I/O Intr was set at iocInit.
- asynDriver and asynShellCommands
  - Added support for I/O redirection to the "dbior" and "asynPrint" commands.
- asynPortDriver
  - Added support for the new asynEnum interface described above.
  - Added support in asynPortDriver for passing status information to clients in callbacks.
    Each parameter in the parameter library now has an associated asynStatus variable.
    New functions setParamStatus() and getParamStatus() are provided to access this
    variable. For example, if setParamStatus(paramIndex, asynError) is called then callParamCallbacks()
    will cause any input records with SCAN=I/O Intr to go into alarm state.
  - Moved asynPortDriver from the miscellaneous directory to its own directory. Improved
    the internals, but did not change the API. Thanks to John Hammonds for this.
- Other
  - Added a new test application, testErrorsApp. This application uses a driver based
    on asynPortDriver to test error handling for all interfaces and all records support
    by the asyn standard device support (asyn/devEpics). It can be used to test error
    handling of records with both periodic scanning and I/O Intr scanning. It also tests
    the new asynEnum interface for setting enum strings, values, and severities at iocInit.
  - Removed the newline terminator from all messages in pasynUser->errorMessage.
    This formatting does not belong in the error message. Thanks to Lewis Muir for this.
  - drvAsynIPServerPort. Added call to epicsSocketEnableAddressReuseDuringTimeWaitState
    which fixes problems when the IOC is restarted and the port is still in TIME_WAIT
    state. Thanks to Lewis Muir for this.

## Release 4-18 (November 2, 2011)
- Miscellaneous
  - Changes to avoid compiler warnings on 64-bit Darwin.
  - Changes to avoid compiler warnings and errors on older Solaris compiler.
  - Changed non-standard __VAR_ARGS__ to __VA_ARGS__ in asynPrint macro.
  - Added titles to EDM screens.
  - Added CSS BOY screens.
  - Fixed build problem. It was rebuilding the VXI11 code with rpcgen each time make
    was run, even if nothing had changed.
- asynPortDriver
  - Cleaned up logic for callbacks on asynUInt32Digital interface. It was not doing
    callbacks if the value had not changed but an interrupt had occured for bits in
    the mask. This would happen when interrupts were only enabled on the falling or
    rising edges but not both. Added an additional form of setUIntDigitalParam that
    takes an interruptMask argument. In the previous releases drivers were calling setUInt32Interrupt
    for callbacks which is not correct. That function should only be called by device
    support to tell the driver which interrupts to recognize. These problems made the
    quadEM not work correctly with the ipUnidig.
- drvAsynSerialPort
  - Bug fix on WIN32. It was waiting forever when timeout=0 when it should return whatever
    characters are available without waiting at all.

## Release 4-17 (August 3, 2011)
- asynRecord
  - Fixed asynRecord.dbd to include promptgroup for fields that need it. Removed the
    SOCK field and asynSocketSetup.adl; the record no longer supports creating sockets,
    which can easily be done with the iocsh drvAsynIPPortConfigure command.
  - Added IXON, IXOFF, and IXANY fields for new XON/XOFF support on serial ports. These
    fields were added to the asynSerialPortSetup.adl medm screen.
- devAsynXXXArray
  - For waveform output records the device support was always writing NELM elements,
    rather than NORD elements.
- asynInterposeCom.c
  - Added missing include file, which caused asyn not to build on EPICS base versions
    before 3.14.10.
- vxi11core_xdr.c
  - Removed unneeded declarations of 'register int32_t *buf' from many functions. Removed
    register keyword from remaining instances, because address was being taken, which
    is illegal in C, and was causing errors on vxWorks Pentium cross-compiler.
- asynPortDriver
  - Added a global function, findAsynPortDriver(const char *portName), that returns
    a pointer to an asynPortDriver object given the asyn port name.
  - Added 3 new asynPortDriver methods: setUInt32DigitalInterrupt, clearUInt32Interrupt,
    getUInt32Interrupt. These were needed to complete the asynUInt32Digital support.
- asynShellCommands
  - The asynOctetSetInputEos, asynOctetSetOutputEos, asynOctetShowInputEos and asynOctetShowOutputEos
    commands now take effect even if the port is not connected. This makes startup scripts
    more robust in the face of devices that are not accessible at IOC startup. Removed
    the unused "drvInfo" parameter from each of these functions.
- drvAsynIPPort
  - Host name lookup is now deferred until port connection time. This makes startup
    scripts robust in the face of a device that is offline at IOC startup and has been
    offline for so long that it's DNS entry has been deleted.
  - Prevent reconnects during IOC shutdown. The IP Port exithandler runs before record
    scanning stops. In that interval, if a record is scanned then it will trigger a
    reconnect, and new connection can be shutdown without sending data, or without waiting
    for a reply. Some embedded TCP/IP stacks have problems dealing with this. Thanks
    to Michael Davidsaver for this fix.
- drvAsynSerialPort
  - Added support for local serial ports on Windows, i.e.win32-x86 and windows-x64 architectures.
    Previously Windows local serial ports were only supported on the cygwin-x86 architecture.
  - Added support for serial line software handshake flags (ixon/ixoff/xany) on most
    architectures (e.g. Linux, Cygwin, Darwin, WIN32; see: man stty) and vxWorks (ixon
    only). Thanks to Dirk Zimoch for this.
- Miscelleaneous
  - Changed asyn/Makefile to fix build dependencies for rpcgen of vxi11. Thanks to Michael
    Davidsaver for this fix.

## Release 4-16 (January 17, 2011)
- drvAsynIPPort
  - Support has been added for terminal servers which support the TELNET RFC 2217`protocol.
    To communicate with such devices specify "COM" as the protocol in the drvAsynIPPortConfigure
    command. This allows port parameters (speed, parity, etc.) to be set using an asynRecord
    or asynSetOption commands just as for local serial ports.
- drvVxi11
  - Fix from Benjamin Franksen to fix problem with reconnection.
  - The 'timeout' argument to vxi11Configure has been changed from double to string.
    This allows vxi11Configure to be called directly from the vxWorks shell.

## Release 4-15 (December 8, 2010)
- vxi11
  - The third argument to the vxi11Configure command is now a bit-map. The least significant
    bit (value 0x1) remains the 'recover with IFC' control. The next-to-least significant
    bit (value 0x2) when set will cause all devices to be locked when a connection is
    made. This allows for cooperative exclusive access to devices.
- asynManager
  - Fixed memory leak in lockPort() when an error occured, was not calling freeAsynUser().
    Thanks to Andrew Starritt for finding this.
- devEpics
  - Bug fix for devAsynInt32 and devAsynFloat64: it was not freeing the mutex in processAiAverage
    if numAverage==0, i.e. there had been no callbacks from the driver since the record
    last processed. This would hang the next thread that tried to take the mutex, typically
    the driver callback thread.
  - Bug fix for devAsynXXXArray: it was calling drvUserCreate in the port driver even
    if there was no userParam in the link, which could crash the driver.
- Many files
  - Changes to allow building dynamically on WIN32 (i.e. making DLLs).
- Makefiles
  - Changes to allow building on Cygwin 1.7.x or 1.5.x; replaced rpc with $(CYGWIN_RPC_LIB),
    which allows it to link with rpc on 1.5.x and tirpc on 1.7.x. You need to add one
    of the following 2 lines to `base/configure/os/CONFIG_SITE.cygwin-x86.cygwin-x86`
    ```
    For Cygwin 1.7.x:
    CYGWIN_RPC_LIB = tirpc

    For Cygwin 1.5.x
    CYGWIN_RPC_LIB = rpc
    ```

## Release 4-14 (July 29, 2010)
- asynDriver
  - Fixed bugs in connection management. Releases 4-11 through 4-13 had the following
    problems:
    - If a port was multi-device and auto-connect, and was only accessed using SyncIO
      calls, then asynManager would never connect to the devices. If regular queued requests
      were used, which happens if an asyn record is connected to that port and device,
      then it would connect once the first queue request was done.
    - For all auto-connect ports there was a race condition, such that the port might
      or might not be connected when the first queued request or SyncIO call was done
      on that port. This arose because when the port registered its asynCommon interface,
      asynManager queued a connection request on that port. But if that connection request
      callback, which executes in the portThread, had not yet occurred when a queue request
      or SyncIO call was made, then those operations would be rejected because the port
      was not yet connected.
  - These problems were fixed by doing the following:
    - When the port registers its asynCommon interface and asynManager queues the connection
      request, it now waits for a short time for the connection callback to complete.
      The default time is 0.5 seconds, but this time can be changed with a call to the
      new function pasynManager->setAutoConnectTimeout(double timeout). This function
      can be accessed from the iocsh shell with the new asynSetAutoConnectTimeout(double
      timeout) command. This short timeout is designed to allow devices time to connect
      if they are available, but not to excessively slow down booting of the IOC by waiting,
      for example, for the system timeout on TCP connections. Note that this change means
      that it is now very likely that the pasynCommon->connect() call will occur as
      soon as the asynCommon interface is registered. As noted in the R4-12 release notes,
      this means that the driver must have already done all initialization required for
      the asynCommon->connect() callback before it registers the asynCommon interface!
    - There is an additional new function, pasynManager->waitConnect(asynUser *pasynUser,
      double timeout), which will wait for the for the port to connect, up to the specified
      timeout. This function can be called from the iocsh with the new command asynWaitConnect(const
      char *portName, double timeout). This function makes it possible to wait longer
      for a port to connect then the current value of the global autoconnect timeout described
      above.
  - Fixed problems with the SyncIO calls, which were caused by the implementation of
    pasynManager->lockPort():
    - The SyncIO calls (e.g. asynOctetSyncIO) are implemented by calling pasynManager->lockPort(),
      executing the I/O in the current thread, and then calling pasynManager->unlockPort().
      These SyncIO functions are designed to be called from threads that are allowed to
      block, such as SNL programs, or other drivers. The problem with the previous implementation
      was that pasynManager->lockPort() immediately took the port mutex when it was
      available, rather than queueing a request to take the mutex. This could lead to
      one thread effectively getting exclusive access to the port, even if other threads
      had queued requests or tried to do SyncIO calls themselves. For example, if a device
      could send unsolicited input, then one might create a thread that simply called
      pasynOctetSyncIO->read() with a short timeout in a tight loop. The problem with
      this was that as soon as that thread released the port mutex when the read timed
      out, it would take the mutex again, blocking other threads that were trying to access
      the port. Previously the only solution to this problem was to add a short
      epicsThreadSleep() in the read loop.
    - This problem has been fixed by reimplementing pasynManager->lockPort(), which
      now queues a request to access the port and then blocks until the queue request
      callback runs in the portThread. When the queue request runs, the thread that called
      pasynManager->lockPort() executes, and the portThread blocks, until pasynManager->unlockPort()
      is called.
    - Note that this change to lockPort() does change its functionality in one respect:
      previously it was OK to call lockPort() on a port that was not connected. This is
      no longer possible, because the queueRequest call in lockPort will now return an
      error if the port is not connected.
    - The change to lockPort did not require any changes to the asynXXXSyncIO functions
      except asynCommonSyncIO. The asynCommonSyncIO->connectDevice and asynCommonSyncIO->connectDevice
      calls cannot use lockPort() any more, because as noted above it does not work with
      disconnected ports. Rather, these functions now directly queue a connection request
      with a private callback function to connect or disconnect the port.
- vxi11
  - Fixed a bug in driver initialization. The driver had not completed all required
    initialization before it called pasynGpib->registerPort. Because pasynGpib->registerPort
    registers the asynCommon interface, that now normally triggers an immediate callback
    to vxiConnect, and the driver was not yet properly initialized to handle that callback.
  - Added an additional example driver, asynPortTest, that uses asynPortDriver. It implements
    the asynInt32, asynFloat64, and asynOctet interfaces to communicate with the echo
    server using asynOctetSyncIO calls. This tests nested SyncIO calls. Added a new
    startup script, database and medm screen for testing this new driver.
- devGpib
  - Added a call to asynOctet->flush() just before the call to asynOctet->write()
    operation when doing write/read operations. This eliminates any stale input that
    may have already been sent by the device and would otherwise be incorrectly returned
    by the read operation.
- interposeEOS
  - Now behaves properly even when eomReason is NULL.
- testIPServerApp
  - Changed ipEchoServer2.c to eliminate the epicsThreadSleep(0.01) in the listener
    thread. This sleep is no longer necessary because of the change to lockPort described
    above, so the example program was changed to test and demonstrate this.

## Release 4-13-1 (May 20, 2010)
- asynPortDriver
  - Fixed bug in getXXXParam. It was not returning error status when a parameter was
    undefined. This caused device support to use undefined values for output records,
    because the initial read from the driver during device support initialization did
    not return an error it should have.

## Release 4-13 (April 1, 2010)
- asyn/Makefile
  - Change some dependencies to fix parallel (-j) make problem.
- drvAsynIPPort
  - A return of 0 from a read of a TCP stream is treated as an END condition rather
    than as an error. This makes it easier to handle devices that close the connection
    at the end of a reply.
  - Support has been added for devices such as web servers that require a connect at
    the beginning of each transaction. To enable this behaviour, specify "http" as the
    protocol in the drvAsynIPPortConfigure command and ensure that each transaction
    ends with a read that detects the broken connection from the device. Note that the
    device will always appear connected. The connect/disconnect around each transaction
    is handled within the drvAsynIPPort driver.
- drvAsynIPServerPort
  - Corrected the documentation to state that only the TCP/IP protocol is supported,
    not UDP.
- asynGpib
  - Fix problem with NULL-pointer dereferences.
- drvVxi11
  - Bug fix from Benjamin Franksen for devices that don't support IRQ.
- devEPICS
  - The ring buffer code in devAsynFloat64.c, devAsynInt32.c and devAsynUInt32Digital.c
    has been improved. Previously when ring buffer overflow occurred during a callback
    the new value was simply discarded. This meant that when the record processed at
    the end of a rapid burst of callbacks it would not contain the most recent value.
    Now when overflow occurs the oldest value is removed from from the ring buffer and
    the new value is added, so that the record will contain the most recent callback
    value after a burst.
  - asynUInt32Digital: asynMask values implying shifts greater than 16 bits are now
    supported.
- asynRecord
  - Fixed bug which caused an error when writing or reading in binary format if the
    driver did not implement the get(Input/Output)Eos functions. This bug was introduced
    when readRaw and writeRaw were removed from asynOctet in release 4-10.
- Additional makeSupport.pl template
  - makeSupport.pl -t streamSCPI <name> creates skeleton stream protocol and database
    files for a SCPI (IEEE-488.2) device.
- asynPortDriver
  - Fixed bug in readInt32Array, not taking lock where needed.
  - Added drvInfo strings to parameter lists, and new methods to support this: createParam(),
    findParam(), getParamName(). The report functions now print out the drvInfo strings,
    which is very useful. The base class drvUserCreate() method can now be used without
    reimplementing in derived classes, because the parameter names are now available
    to the base class. Removed asynParamString_T and drvUserCreateParam(), which are
    no longer needed. The testAsynPortDriver test application has been updated to use
    these new features.

## Release 4-12 (August 19, 2009)
- asynManager
  - A problem was introduced in R4-11 by not starting the autoconnect process until
    iocInit(), and operations that do not use the XXXSyncIO functions thus fail before
    iocInit(). This means, for example, that calls to asynSetOption() to set serial
    port parameters fail if done in a startup script before iocInit(). R4-12 fixes this
    problem by decoupling autoconnect operations from iocInit(). NOTE: The first call
    to the pasynCommon->connect() function now happens almost immediately after pasynManager->registerInterface()
    is called for the asynCommon interface. This timing is different from all previous
    asyn releases, and it means that port drivers must initialize everything required
    by asynCommon->connect() before they register the asynCommon interface. This
    may require minor re-ordering of the initialization sequence in some drivers.
- drvAsynSerialPort
  - Requests to set end-of-string values or serial port parameters are accepted even
    when the port is not connected. The request takes effect when the port is connected.
    This makes IOC startup more robust in the face of network or USB to serial adapters
    that may be unavailable on startup.
- asynInt32Average, asynFloat64Average
  - If record is processed before new data have arrived (numAverage==0) set record to
    UDF/INVALID, set UDF to TRUE and leave value unchanged.

## Release 4-11 (June 16, 2009)
- asynManager
  - asynReport at detail level 0 now reports only disconnected subaddresses.
  - The autoconnect code has undergone considerable modification. When a port is registered
    with autoConnect true, or whenever a port disconnect exception is raised on an autoConnect
    port, an attempt at connection occurs immediately followed by retry attempts at
    20 second intervals. Attempts to queue requests to a disconnected port (even an
    autoConnect port) will be rejected. These changes have been made to reduce the occurences
    of 'connection flurries' and to ensure that requests do not languish in the queue
    when connections are broken.
  - Setting the trace mask or trace I/O mask for a port now also sets the trace mask
    or trace I/O mask for every device associated with that port.
  - Passing a NULL pasynUser argument to the setTraceMask and setTraceIOMask will set
    the asynBase (default) trace mask or trace I/O mask. To do this from the iocsh pass
    a zero length portName string to the iocsh commands asynSetTraceMask or asynSetTraceIOMask.
- asynDriver.h
  - Add new version macros (ASYN_VERSION, ASYN_REVISION and ASYN_MODIFICATION). These
    are guaranteed to be numeric.
  - Add new asynStatus codes, <strong>asynDisconnected</strong> and <strong>asynDisabled</strong>.
    Attempts to queue a request to a disconnected or disabled port return these codes,
    respectively. Future changes to record support may propogate these to the record
    alarm status field.
- asynInterposeEOS
  - Increased the size of the input buffer from 600 to 2048 bytes.
- VXI-11
  - Fix from Takashi Asakawa, Yokogawa Electric Corporation, Japan to allow link identifiers
    with a value of zero.
- devEpics (devAsynInt32, devAsynFloat64, devAsynUInt32Digital)
    - Ignore callbacks when interruptAccept=0, because waiting for interruptAccept (the
      previous behavior) hangs iocInit with synchronous drivers. The first record process
      on input records will now read directly from driver, which will get the latest value.
    - Added mutex around epicsRingBytes calls
    - Added ASYN_TRACEIO_DEVICE when reading value from ring buffer
- testIPServerApp
  - Add some cleanup code to eliminate memory/socket leaks.
- asynOctetSyncIO
  - The asynOctetSyncIO <strong>openSocket</strong> method will be removed in the next
    release. This method is redundant since it is no different than calling the <strong>
      connect</strong> method with the port name from a previous drvAsynIPPortConfigure
    command.
- asynPortDriver
  - A new C++ base class called <strong>asynPortDriver</strong> from which real asyn
    port drivers can be derived. It greatly simplifies the code required to write a
    new asyn port driver. It is documented <a href="asynPortDriver.html">here</a>.
- testAsynPortDriverApp
  - A new test application to demonstrate the use of the new asynPortDriver C++ class.
    This driver simulates a simple digital oscilloscope, and includes a C++ driver,
    EPICS database, medm screen, and an example iocBoot directory in ioctestAsynPortDriver.
    It is described in the documentation for <a href="asynPortDriver.html">asynPortDriver</a>.

## Release 4-10 (Sept. 2, 2008)
- asynOctet
  - Ths asynOctet <strong>writeRaw</strong> and <strong>readRaw</strong> methods have
    been removed. In most cases, if your code now calls readRaw or writeRaw it should
    be safe to simply change these calls to their non-Raw equivalent. If you're paranoid
    about someone interposing the end-of-string processing layer you could add something
    like the following to ensure that there is no end-of-string to match:
    ```
    pasynOctet->setInputEos(asynOctetPvt,pasynUser,NULL,0);
    ```
  - If you need to switch to 'raw' mode for a while and then back to 'eos mode', you
    can use code similar to that in devGpib.c:readArbitraryBlockProgramData:
```
char saveEosBuf[5];
int saveEosLen;
.
.
.
status = pasynOctet->getInputEos(asynOctetPvt,pasynUser,saveEosBuf,sizeof saveEosBuf,&saveEosLen);
if (status != asynSuccess) {
    epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,"Device EOS too long!");
    return -1;
}
if (saveEosLen)
pasynOctet->setInputEos(asynOctetPvt,pasynUser,NULL,0);
.
.
.
.
if (saveEosLen)
    pasynOctet->setInputEos(asynOctetPvt,pasynUser,saveEos,saveEosLen);
```  
    When compiling your code against this new version of asyn you should pay particular
    attention to warning messages of the form "warning: initialization from incompatible
    pointer type". These are a good indication that you're initializing an <strong>asynOctet</strong>
    structure with the old-style I/O methods.

- asynManager
  - Add a strStatus method to convert an asynStatus code to a string.
- drvAsynIPPort
  - Cleaned up timeout handling.
  - Fix memory leak (one epicsAtExit entry was allocated for every connect).
  - asynReport no longer reports as connected a port which has successfully disconnected.
  - Improved diagnostic messages.
- drvAsynIPServerPort
  - Fixed bugs that caused the thread that listened for new connections to exit when
    errors occurred. Such errors included too many simultaneous connections.
- interfaces
  - Added three new array interfaces, asynInt8Array, asynInt16Array, and asynFloat32Array.
    asynInt8Array and asynInt16Array are the same as asynInt32Array except that the
    data types are for epicsInt8 and epicsInt16 respectively. asynFloat32Array is the
    same as asynFloat64Array except that the data type is for epicsFloat32.
  - Added asyn(XXX)ArraySyncIO for synchronous I/O to all array interfaces.
  - Added new asynGenericPointer interface. The datatype for this interface is void*,
    so it can be used to pass a pointer to anything. Includes asynGenericPointerSyncIO
    for synchronous I/O.
  - Added asynStandardInterfaces.h and asynStandardInterfacesBase.c to simplify driver
    initialization when using the standard asyn interfaces defined in asyn/interfaces
    (common, octet, int32, etc.)
- devEpics
  - Undid the change that was done in R4-9 with direct calls to dbScanLock and process
    in the interrupt callback functions. This could lead to deadlocks in some circumstances.
    The original reason for changing from scanIoRequest to (dbScanLock, process) was
    because callback values would be lost if the callbacks came so close together in
    time that the single callback value stored in device support was overwritten before
    scanIoRequest could process the record. This problem has been fixed by adding a
    FIFO (ring buffer) to the device support for the scalar interfaces asynInt32, asynUInt32Digital,
    and asynFloat64. The ring buffer is only created when the record is put into I/O
    Intr scan, so the storage is not allocated for records that are not I/O Intr scanned.
    The ring buffer default size is 10 values, but this can be changed on a per-record
    basis using the dbInfo string "FIFO" with a value such as "100".
  - Added support for bi and bo records for asynInt32 interface. Previously these records
    were only supported for the asynUInt32Digital interface.
  - Added waveform record device support for the asynInt8Array, asynInt16Array, and
    asynFloat32Array interfaces. These are the same as devAsynInt32Array and devAsynFloat64Array
    with changes to the data types.
- asynShellCommands
  - Remove duplicate windows function decorations.
- asynInterposeEos
  - Improved diagnostic messages.
- testIPServerApp/ipSNCServer.st
  - Fixed timeout bug introduced in R4-6 when timeouts of 0.0 and -1.0 were defined.

## Release 4-9 (October 19, 2007)
- devEpics
  - Replaced scanIoRequest with direct call to rset->process in interrupt callback
    routines in all device support. Without this fix if another interrupt occurred before
    the first scanIoRequest was complete bad things could happen. The data from the
    first interrupt would be lost, and the read function in the driver would be called
    when it should not have been.
- drvAsynSerialPort
  - Added support for 28,800 baud for those architectures which support this unusual
    speed.
  - Added stub routines for WIN32 so that a separate DBD file is no longer needed.
- drvAsynIpPort
  - Added short delay in cleanup routine so sockets would close cleanly.
- asynInterposeEos.c
  - Fixed bug which caused asynOctetRead() to return prematurely with eomReason=ASYN_EOM_CNT
    if the port driver returned 600 bytes in readRaw().
- RTEMS
  - Build driver for Greensprings IP-488 if IPAC is defined in configure/RELEASE.
- VXI11
  - Added stub routines for WIN32 so that a separate DBD file is no longer needed.
- VXI-11
  - Avoid duplicate clnt_destroy operations.

## Release 4-8 (April 28, 2007)
- devEpics/devAsynInt32
  - Added support for link specification of
    ```
    @asynMask(portName,addr,nbits,timeout)drvParams
    ```
    in addition to the previous support for
    ```
    @asyn(portName,addr,timeout)drvParams
    ```
    This allows device support to work with drivers that cannot return meaningful values
    in pasynInt32->getBounds because they do not know the range of the device. This
    is true, for example of Modbus ADCs. The nbits parameter is defined as follows:
    ```
      nbits > 0  Device is unipolar with a range from 0 to 2^nbits-1
      nbits < 0  Device is bipolar with a range from -2^(abs(nbits)-1) to 2^((abs(nbits)-1)-1
      Values returned on the asynInt32 interface will be sign extended
      using the sign bit (e.g. bit abs(nbits)-1 starting at bit 0).
    ```
- devEpics/devAsynInt32Array, devAsynFloat64Array
  - Added support for callbacks from driver to device support. This allows waveform
    records to have I/O Intr scanning, as already supported for other records in devEpics.
- devEpics/asynEpicsUtils
  - Changed parser so it requires an exact match to "asyn(" or "asynMask(". Previously
    it tolerated other characters before the "(", and in particular it accepted "asynMask("
    when "asyn(" was expected. Note that this change could cause problems with database
    files if they did not follow the documented syntax, which has no white space between
    "asyn" or "asynMask" and the "(" character.
- asynManager
  - Fix errors in format strings for asynPrint.
  - Temporary fix to asynReport thread for Cygwin. If the amount of output is small
    the thread exists for a very short time, and this causes a crash. The fix is a short
    wait, but it should really be fixed in base/src/libCom/osi/os/posix/osdThread.c.
- testIPServerApp
  - Fix problem with Makefile.
- devGpib
  - Add more SCPI commands to devGpib template.
- drvAsynIPPort
  - Close sockets on application exit. This is very important for vxWorks, otherwise
    sockets are not closed cleanly which often leads to problems when the IOC reboots.
- drvGsIP488
  - Hold off SRQ callbacks until iocInit.

## Release 4-7 (February 28, 2007)
- drvAsynSerialPort
  - Clean up operation on POSIX/termios systems (everything but vxWorks). The old mechanism
    was prone to polling during read operations rather than using the termios read timeout
    mechanism.
- devGpib
  - asynRecord sets line-buffering on trace file.
  - Peter Mueller provided code to remove/restore a device from/to the SRQ polling list.
- drvGsIP488.c
  - Clean up dangling 'default' statement.
- devGpib
  - Fixed error in GPIBACMD operations.
- linuxGpib
  - Patches from Gasper Jansa to improve option handling.
- devEpics
  - Fixed null pointer dereference for all device support when SCAN=I/O Intr and asyn
    port could not be found.
- asynRecord
  - Fixed buffer overflow error when NRRD>40 and IFMT=ASCII.
- asynGpib
  - Read method now sets return status and *eomReason properly.
- drvAsynIPPort/drvAsynSerialPort
  - *eomReason now set to ASYN_EOM_CNT when read count has been satisfied.
  - Fix timeout settings on RTEMS.
  - Add support for UDP broadcasts. Specify "UDP*" and the network broadcast address
    in the port configuration command:<br>
    `drvAsynIPPortConfigure("L0", "192.168.1.255:1234 UDP*", 0, 0, 0)`
- drvAsynSerialPort
  - Full support for new timeout semantics (timeout<0 means "wait forever for characters
    to arrive", timeout=0 means "return characters immediately available", timeout>0
    means "return a timeout status if no characters are received within the specified
    number of seconds").

## Release 4-6 (June 12, 2006)
- drvAsynIPPort/drvAsynSerialPort
  - Fixed NULL pointer dereference.
- drvAsynIPPort
  - Previous versions of drvAsynIPPort.c (1.29 and earlier, asyn R4-5 and earlier) attempted
    to allow 2 things:
    - Use an EPICS timer to time-out an I/O operation, such as send(), recv() and connect().
    - Periodically check (every 5 seconds) during a long I/O operation to see if the
      operation should be cancelled.
  - Item 1) above was not really implemented because there is no portable robust way
    to abort a pending I/O operation. So the timer set a flag which was checked after
    the poll() was complete to see if the timeout had occured. This was not robust,
    because there were competing timers (timeout timer and poll) which could fire in
    the wrong order.
  - Item 2) was not implemented, because asyn has no mechanism to issue a cancel request
    to a driver which is blocked on an I/O operation.
  - Since neither of these mechanisms was working as designed, the driver has been re-written
    to simplify it. If one or both of these are to be implemented in the future the
    code as of version 1.29 should be used as the starting point.
  - If pasynUser->timeout < 0 an infinite timeout is now used.
  - Fixed bug so that ports connected with a file descriptor in pasynUser->reason
    execute code to set timeouts.
  - Fixed bug to return error if pasynCommon->connect is called when port already
    connected.
- asynTrace
  - Added two new functions which are related to pasynTrace->print and pasynTrace->printIO
    the way vprintf is related to printf.
    - pasynTrace->vprint Same as pasynTrace->print except that instead of a variable
      of arguments it takes a va_list argument as its last parameter.
    - pasynTrace->vprintIO Same as pasynTrace->printIO except that instead of
      a variable of arguments it takes a va_list argument as its last parameter.
- asynManager
  - Changed pasynManager->connectDevice for ports which have the properties autoConnect=1
    and isConnected=0. In this case a request is queued to call asynCommon->connect
    for that port. This ensures that ports that have a pasynUser connected to them will
    report being connected even if no I/O has yet been done. Previously such ports reported
    a disconnected state until the first I/O or operation such as setTraceMask. This
    was confusing.
  - Clarify documentation on meaning of pasynUser->timeout. Previously there was
    no documented method of specifying an "infinite" timeout to a driver, and the meaning
    of timeout=0.0 was not defined. The new definitions are:
  
    &gt; 0.0 Wait for up to timeout seconds for the I/O to complete
  
    = 0.0 Peform any I/O that can be done without blocking. Return timeout error if
    no I/O can be done without blocking.
  
    < 0.0 Infinite timeout. Wait forever for I/O to complete.
- devEpics
  - Fixed bugs with asynFloat64Average device support. The wrong interrupt function
    was being called, and UDF was not being cleared.

## Release 4-5 (April 17, 2006)
- memMalloc/memFree
  - memMalloc was allocating the amount of memory the caller requested rather than the
    amount required for the freeList. If memFree was called and the memory reallocated
    to a user requesting a larger size, memory corruption occured. This is fixed.
- SyncIO routines
  - If the connect call fails the asynUser is no longer freed. Instead a message is
    put into asynUser.errorMessage. The caller must call disconnect in order to free
    the storage for the asynUser.
  - The SyncIO routines no longer call asynPrint if there is an error and there is a
    valid asynUser available. Rather they return an error message in pasynUser->errorMessage.
    The SyncIO*Once functions still call asynPrint for errors, because they do not have
    a way of returning an error message.
- Serial, TCP/UDP/IP
  - Handle 0-length write requests.
- TCP/UDP/IP
  - Added drvAsynIPServerPort to support TCP and UDP socket servers.
  - Added iocBoot/testIPServer to test TCP server support.
  - drvAsynIPPort now closes TCP sockets when remote system closes connection.
  - drvAsynIPPort connect function now uses pasynUser->reason as a file descriptor
    if it is > 0. This allows drvAsynIPServerPort to re-use asyn ports it creates.
  - Made drvAsynIPPort add null byte at end of input if there is room.
  - Made drvAsynIPPort:readRaw set eomReason to 0. It was not setting eomReason at all
    previously.
- Serial
  - Made drvAsynSerialPort add null byte at end of input if there is room.
  - Made drvAsynSerialPort:readRaw set eomReason to 0. It was not setting eomReason
    at all previously.
- Interfaces
  - Added asynCommonSyncIO for synchronous support of the asynCommon interface.
- VME GPIB
  - Add delay loops to get these boards to work with faster VME CPU modules.

## Release 4-4 (December 15, 2005)
- VXI11
  - Better support was provided for VXI-11.3 controllers, i.e. talking directly to an
    ethernet port on an instrument. In particular a TDS3054B was tested.
  - WARNING: The VXI-11.1 ansd VXI-11.3 standards do NOT allow access to GPIB lines,
    i.e. conmmands like Untalk/Unlisten are not possible. The previous support issued
    these commands after each read or write. Some really old GPIB devices may fail.
    If so the device specific code must be modified to sent these commands separately.
- win32
  - Changes were made to allow asyn to build on native Windows (win32-x86) architecture.
  - There are two asyn components that do not yet work on win32-x86.
    - Local serial ports. The asyn uses the "termios" API for serial ports, and termios
      is not available for native Windows.
    - VXI-11. The asyn VXI-11 support uses the Sun XDR API, which is not available for
      native Windows.
  - Users who want to use local serial ports or VXI-11 on Windows can use the Cygwin
    EPICS build (cygwin-x86).
- devGpib: devGpibConvertExample
  - An example of how to implement convert routines for devGpib support modules is available
    in asyn/devGpib/devGpibConvertExample.c
- devAsynOctet.c
  - The UDF field is now set FALSE when the VAL field is updated.

## Release 4-3 (September 15, 2005)
- asynManager
    - lock/unlock renamed to blockProcessCallback/unblockProcessCallback
      - The names have been changed and now these methods only work for asynchronous ports.
        An error is returned if blockProcessCallback is called for a synchronous port.
    - lockPort/unlockPort
      - These are new asynManager methods. They can be used in place of queueRequest if
        the caller can block. They have been added to make it easier to implement a driver
        with one addressing scheme that is a asynUser of a driver with a different addressing
        scheme. For example a multi-drop serial driver can be implemented that calls a standard
        serial driver.
    - asynLockPortNotify
      - This is a new interface for driver's that call other drivers.
- cancelInterruptUser
  - The cancelInterruptUser methods of all interfaces has been changed from
    ```
     asynStatus (*cancelInterruptUser)(void *registrarPvt, asynUser *pasynUser);
    ``` 
    to
    ```
     asynStatus (*cancelInterruptUser)(void *drvPvt, asynUser *pasynUser, void *registrarPvt);
    ```
- asynTrace
  - The length and size arguments now have type `size_t`.
- devGpib
  - Several improvements were made to devSupportGpib.c. All changes should be transparent
    to code that uses devGpib.
- asynOctetBase
  - The `maxchars` argument to `callInterruptUsers` has been removed.
- asynReport
  - The filename argument has been removed.
- devAsynFloat64
  - For `asynAoFloat64` it now uses `oval` instead of `val`.
- asynFloat64Array, asynInt32Array, asynOctet, asynOctetSyncIO
  - All length and size arguments now have type `size_t`.
- asynFloat64SyncIO, asynInt32SyncIO, asynOctetSyncIO, and asynUInt32DigitalSyncIO
  - These all use `lockPort/unlockPort` instead of `queueRequest`.

## Release 4-2-1 (March 30, 2005)
- devAsynFloat64
  - Device support was not returning 2 (do not convert) for ai records when it should.
    This meant that the VAL field was being set back to 0 by the record after device
    support wrote to it. This bug is fixed.
- asynRecord
  - The record sometimes did not read the current input and output EOS values from the
    driver when it connected. This bug is fixed.

## Release 4-2 (February 15, 2005)
- Acknowledgement
  - Yevgeny A. Gusev has again reported some hard to recognize bugs. He must have spent
    many hours looking at the code. His extra set of very good eyes are much appreciated!!.
    He also thought of the way to handle support that uses one addressing scheme but
    wants to use support that has a different addressing scheme. For example support
    for mult-drop serial that wants to use the standard serial support
- asynInterposeEos
  - If read reads maxchars, it forced the last character to be 0 and returned asynOverflow
    if it wasn't. This is fixed.
- drvAsynSerialPort,drvAsynIPPort - Error reporting
  - These did not properly set an error message in asynUser.errorMessage when they returned
    asynError. This is fixed.
- drvAsynSerialPort - serial port options
  - Changes were made to the way serial port options are handled.
    - initial values
      - Previously defaults were assigned for all options. Now the initial values are fetched
        from either the termios (POSIX) or sioLib (vxWorks).
    - vxWorks clocal and crtscts
      - The vxWorks sioLib uses clocal for what POSIX calls crtscts. The new serial support
        for vxWorks accepts both clocal and crtscts to specify RTSCTS (Request to send,
        Clear to send).
- asynRecord - Serial Port Options
  - This has a new option to set Modem Control.

## Release 4-1 (December 20, 2004)
  - The only code change was to fix the drvAsynIPPort and drvAsynSerialPort segmentation
    faults on cygwin-x86.

## Release 4-0 (November 22, 2004)
- Incompatible Changes.
  - APOLOGY: Many interfaces have changed since release 3-3. This is the reason this
    release is called 4-0.
    - asynManager - Only report has changed. Many new methods have been added. asynManager:report
      takes an additional parameter, portName.
    - asynOctet - The read/write methods are similar to before. The new version has
      sepearate end of string methods for input and output.
    - register based interfaces - Extensive changes have occured.
    - devGpib - Unless special conversion routines call low level drivers, debGpib support
      should work without any changes.
    - devEpics -
        - Extensive changes have occured. The support naming now follows the names of asyn
          interfaces. For example devAsynInt32 contains device support for interface asynInt32.
        - device definitions have changed.
          - Blanks no longer appear in the menu choices. For example.
          ```
          device(ai,INST_IO,devAiAsynInt32,"asyn Int32")
          ```
          is now
          ```
          device(ai,INST_IO,asynAiInt32,"asynInt32")
          ```
    - asynOctetSyncIO
        - All read and write methods now return asynStatus and have additional args to return
          the number of bytes sent/received.
    - asynRecord
        - The IEOS and OEOS fields are set to the current values for the port when the record
          connects to the port. If they are modified after the record connects to the port,
          then the EOS strings will be changed using asynOctet->setOutputEos or asynOctet->setIbputEos.
          IMPORTANT: The values of IEOS and OEOS in the database file are never used, because
          they are modified when the record connects to the port.
- New Features
    - interrupt support -A major new feature is support for interrupts. See asynDriver.html
      for details.
    - linuxGpib - This is support for the linuxGpib open source project. It contains
      support for many linux gpib kernel drivers. The asyn support was provided by Rok
      Sabjan (cosyLab).
    - cancelRequest - If cancelRequest is called while either the process or timeout
      callback is active, it now waits until the callback completes.
    - asynRecord
        - Added PCNCT field to connect/disconnect from port, and to indicate if port is
          currently connected.
        - Added DRVINFO and REASON fields to provide control for the drvUser interface.
        - Added support for register interfaces (asynInt32, asynUInt32Digital, and asynFloat64).
          New I/O fields for this support are I32INP, I32OUT, UI32INP, UI32OUT, UI32MASK,
          F64INP, and F64OUT. The new IFACE field is used to select the currently active interface.
        - Added new fields to indicate if a particular interface is supported by the driver.
          These fields are OCTETIV, I32IV, UI32IV, F64IV, OPTIONIV, and GPIBIV.
        - Added support for I/O Intr scanning for any driver/interface that supports callbacks.
          asynOctet does not yet support callbacks.

## Release 3-3 (August 23, 2004)
- Incompatible Changes.
    - MAY BE MORE - This release has major new features. Implementing the new features
      may have caused some imcompatibilities, This list is likely to grow as existing
      users report problems.
    - queueRequest - If the portDriver does not block then the queue callback is called
      by queueRequest rather than by a separate thread. User code can call canBlock to
      find out how the callback is called.
    - registerPort - The argument multiDevice has been replaced by attributes. Attributes
      currently has two bits ASYN_MULTIDEVICE and ASYN_CANBLOCK. The port driver is responsible
      for setting both bits correctly.
    - setOption/getOption have been moved from asynCommon to a new interface asynOption.
- Major New Features
    - Support for synchronous drivers.
    - Support for register based drivers.
        - Generic register based device support for EPICS records.
        - Additional fields have been added to asynUser.
        - Added pasynManager->memMalloc() and pasynManager->memFree() for allocating
          and freeing memory with a freelist. This is primarily meant to be used with pasynManager->duplicateAsynUser()
          and the new pasynUser->userData field.
- asynDriver.h
  - The following changes have been made
    - userData - this is a new field in asynUser.
    - registerPort - Field multiDevice is replaced by attributes. Currently two attributes
      are defined: ASYN_MULTIDEVICE and ASYN_CANBLOCK.
    - duplicateAsynUser - This is a new method that creates a new asynUser and initializes
      it with values from an existing asynUser. freeAsynUser now puts the asynUser of
      a free list rather than calling free.
    - memMalloc/memFree - New methods that manage storage. It uses a set of free lists
      of different sizes.
    - asynCommon no longer has methods setOption/getOption.
    - asynOctet is now defined in a separate header file.
- asynRecord
  - Removed the GOPT field. This is no longer necessary because the port options are
    automatically read whenever connecting to a port. "special" requests are now queued
    without changing the state of the record, using the new duplicateAsynUser, memMalloc(),
    and memFree() methods. This means that there is no longer a possibility of a special
    request being rejected because the record is busy. It is no longer possible to cancel
    a special request.
- asynGpib
    - asynGpibPort:srqStatus returns asynStatus
    - asynGpibPort:serialPoll returns asynStatus. It now only calls a registered callback
      only if statusByte&0x40 is non zero.
- devSupportGpib
    - setEos is now a method.
    - completeProcess is a new method. This was added to support synchronous drivers.
    - Failures for GPIBREADW|GPIBEFASTIW were not handled properly. This could cause
      and assert failure. This is fixed.
- drvAsynSerialPortFlush
  - Flushes input only.
- asynInterposeEOS
  - The EOS read method now calls the low-level read method only once and returns as
    many characters as the low-level method supplies. This makes the EOS read semantics
    match those of the low-level serial and IP drivers.
- drvVxi11
  - vxi11SetRpcTimeout - now handles fractions of a second properly
- asynRecord
  - A new field has beem added, AQR (Abort Queue Request)
  - The semantics have been changed as follows: process is responsible for all and only
    for I/O operations. Only I/O operations cause the alarm status and severity to change.
    Special is responsible for all other operations performed by asynRecord.

## Release 3-2 (June 22, 2004)
- Changed and obsolete features
    - <strong>INCOMPATIBLE CHANGE --</strong> The units of the `respond2Writes`
      field, if greater than 0, are now seconds rather than milliseconds. This change
      was made so that all time values set in the instrument support initialization routine
      are specified uniformly in units of seconds. Very few instrument support files are
      likely to be affected by this change
    - The contents of asynRecDevDrv.dbd have been placed in asyn.dbd and asynRecDevDrv.dbd
      has been removed. This allows applications to get correct dbior reports and access
      to asynRecords by including any low-level driver .dbd file.
    - The drvAsynTCPPort driver has been renamed drvAsynIPPort since it now supports
      both UDP and TCP protocols. The protocol is selected by adding a "UDP" or "TCP"
      after the "hostname:port" in the drvAsynIPPortConfigure command. A missing protocol
      is taken to be "TCP".
    - Work around 'missing SPD' bug in HP E2050 GPIB/LAN adapter. SRQ handling is much
      more robust on all supported hardware.
- Major New Features
    - National Instruments NI1014 VME GPIB interface is now supported.
    - GPIB - All low level GPIB support (vxi11, gsip488, and ni1014) now fully support
      the GPIB specific features defined by asynGpibDriver.h
    - Implementation of asynTrace is more consistent across the low level drivers.
    - Added makeSupport script and template instrument support. Updated tutorials to
      reflect these additions.

## Release 3-1 (April 29, 2004)
- Changed and obsolete features
    - The drvGenericSerial driver has been split into drvAsynSerialPort and drvAsynTCPPort
      drivers for local and remote serial ports, respectively. End-of-string processing
      has been moved to an interposed interface.
    - The diagnostic facilities previously provided by asynTrace.db have been replaced
      with the much more general asynRecord.db
    - All asynManager,asynCommon, and asynOctet methods except report now return asynStatus.
      Methods that previously returned a different value now have an additional argument
      for this value.
    - Low-level driver read and write methods now return asynStatus and are passed an
      additional argument through which they store the actual number of characters read
      or written.
    - The createSocket method in the asynSyncIO interface has been replaced by openSocket.
      openSocket does not call asynSyncIO->connect(), that must now be done by the
      caller.
    - Removed code for "flush" from gpib drivers. The implementation caused infinite
      loops on devices that speak when not spoken to.
    - asynRecord
        - asynOctetRecord has been renamed to asynRecord
        - TIOM, TMSK, TSIZ, TFIL, AUCT, ENBL, CNCT, ERRS, TINP, NAWT fields have been added
        - Default values of OMAX and IMAX decreased from 512 to 80
        - Options that are unknown for a device (e.g. baud on a Moxa terminal server) are
          shown as Unknown.
        - Many bug fixes and improvements in logic and functionality

## Release 2-1 (April 2, 2004)
- Major New Features
    - Connection Management - A framework for connection management is provided. It
      provides the ability to connect/disconnect to/from a port or port,addr. It also
      provides enable/disable and autoConnect/noAutoConnect. See the asynDriver for details.
    - devAsyn - Generic device support for connect management for a specific device.
    - devAsynGeneric - Generic support for connection management and traceing. This
      support dynamically attaches to a port,addr. Thus one set of records and one medm
      display can be used for all devices connected to an ioc.
    - asynOctetRecord - A generic record and medm display that allows interactive access
      to many asynDriver features.
    - asynSyncIO - A synchronous interface to asynDriver. This can be used by code,
      e.g. SNC programs, that are willing to wait instead of following an asynchronous
      model.
- Changed and obsolete features
    - devAsynTrace is replaced by devAsyn and devAsynGeneric.
    - asynManager.h
        - disconnectDevice has been renamed to disconnect.
        - The interface to low level drivers has been drastically modified in order to support
          the new connection management features. See the asynDriver documentation for details.
    - asynGpib
        - registerPort has additional arguments multiDevice and autoConnect.
        - setPortOption,getPortOption are setOption,getOption

## Release 1-2 (April 1, 2004)
- Promote VXI-11 RPC definition files to vxi11 directory. Use rpcgen to build RPC
  support files for targets for which this is possible.
- Run rpcgen on Solaris with 'multi-thread' flag.
- Use separate GPIB message/response buffer for each port instance.
- Use sscanf to convert GPIB stringin response.
- Fix race condition in GPIB finish routines.

## Release 1-1 (February 5, 2004)
- This release includes support for the following:
  - asynTrace - A trace facility is now implemented.
  - gsIP488 - The Greensprings Industry Pack IP488 is now supported
- Modifications include:
  - Added asynSetPortOption and asynGetPortOption to manipulate port options.
  - Changed serial support to use asynSetPortOption/asynGetPortOption.
  - Added devGPIB GPIBCVTIO commmand type to allow custom conversion routine to perform
    all I/O operations.
  - Changed rules for return value from devGPIB custom conversion routines.
  - Added dbior support.
  - Changed devGPIB to no longer cache EOS.

## Release 1-0alpha2 (November 17, 2003)
- Support Provided in addition to asynDriver and asynGpib
    - devGpib - The Winans/Franksen gpib device support.
    - vxi11 - Support for instruments that support the VXI-11 standard.
    - drvGenericSerial - Support for devices connected to serial ports or to Ethernet/Serial
      converter.
- Future Support
    - Other device support methods especially streams, devSerial, and mpfSerial.
    - NI1014 VME Gpib driver.
    - Industry Pack IP488 Gpib driver.
    - Successor to GI (GPIB Interact).
- Testing
  - The vxi11 support has been tested on the following platforms: Solaris, Linux (redhat
    9), Darwin, Windows XP (Cygwin), and vxWorks. It has been tested with the following
    vxi11 controllers:
    - Agilent E2050A LAN/GPIB Gateway.
      - It's vxiName must start with "hpib" rather than "gpib".
    - Agilent E5810A LAN/GPIB Gateway.
    - Tektronix TDS3014B Scope.
      - When communicating with the Ethernet port it acts like a VXI-11.2 rather than a
        VXI-11.3 device. It seems to just accept any GPIB address. SRQs did not work when
        connecting via the ethernet port but did when communicating via a LAN/GPIB gateway.
  - The generic serial support has been tested with the following:
    - xvWorks with a GreenSprings Octal UART Industry-Pack module on a VME carrier.
    - Linux and Windows XP (Cygwin) with PC hardware serial port (/dev/ttyS0).
    - Solaris hardware serial port (/dev/cua/a).
    - Linux, Solaris, Darwin, vxWorks, and Windows XP (Cygwin) with a Moxa NPort Ethernet/Serial
      converter.
  - Two Device Support modules have been converted from the 3.13 gpib support: DG535
    and TDS3014B Scope.
