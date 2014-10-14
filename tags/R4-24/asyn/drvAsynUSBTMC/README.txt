      EPICS ASYN support for USB TMC (Test and Measurement Class) devices
===============================================================================

Based on:
  1) "Universal Serial Bus Test and Measurement Class Specification (USBTMC)",
     Revision 1.0.
  2) "Universal Serial Bus Test and Measurement Class, Subclass USB488
     Specification (USBTMC-USB488)", Revision 1.0.

Requires libusb-1.0, available from: http://www.libusb.org/
    For best results use version 16 or higher.

Build Issues
============
1.  Linker reports that libusb_strerror is undefined?
    Check if you have multiple versions of libusb-1.0 installed.  The build
    system may be finding the header for a newer version (that does supply
    libusb_strerror) but then links against an older library (that does not
    supply libusb_strerror).

Acknowledgements
================
Thanks go out to Florian Sorgenfrei and Damien Lynch for
their patience and suggestions in testing this code.


Eric Norum <wenorum@lbl.gov>
