/********************************************
 *  ftdiDriver.cpp
 *
 *  Wrapper class for the libftdi1 library
 *  This class provides standard read/write
 *  and flush methods for an FTDI chip USB connection.
 *
 *  Philip Taylor
 *   8 Jul 2013
 *
 ********************************************/
#include <string.h>
#include <stdio.h>
#include "ftdiDriver.h"
#include "epicsThread.h"
#ifndef _MINGW
#ifdef _WIN32
int gettimeofday(struct timeval *tv, struct timezone *tz);
void usleep(__int64 usec);
#else
#include <sys/time.h>
#endif
#endif

/*
 * Uncomment the DEBUG define and recompile for lots of
 * driver debug messages.
 */

/*
#define DEBUG 1
*/

#ifdef DEBUG
#define debugPrint printf
#else
void debugPrint(...){}
#endif


/**
 * Constructor for the FTDI driver.  Accepts a Vendor ID and
 * Product ID. Initializes internal variables.
 *
 * @param vendor - Vendor ID
 * @param product - Product ID
 */
FTDIDriver::FTDIDriver(int mode):spi(mode)
{
  static const char *functionName = "FTDIDriver::FTDIDriver";
  debugPrint("%s : Method called - ", functionName);
  if( spi ) debugPrint("SPI\n");
  else      debugPrint("UART\n");

  // Initialize internal FTDI parameters
  connected_ = 0;
  // Vendor and Product ID set to FTDI FT2232H default values
  vendor_  = 0x0403;
  product_ = 0x6010;
  // Baudrate and latency set to 12 Mb and 2 msecs.
  baudrate_ = 12000000;
  latency_ = 2;
  // Reasonable defaults for line properties and flow control
  bits_ = BITS_8;
  sbits_ = STOP_BIT_1;
  parity_ = NONE;
  break_ = BREAK_OFF;
  flowctrl_ = SIO_DISABLE_FLOW_CTRL;
  spiInit = 0; // Status whether init done
  memset(buf, 0, sizeof(buf));
}

FTDIDriverStatus FTDIDriver::initSPI() {
  unsigned int i = 0;
  unsigned int n = 0;

  FTDIDriverStatus rc = FTDIDriverError;

  static const char *functionName = "FTDIDriver::initSPI";
  debugPrint("%s : Method called, spi=%d\n", functionName, spi);

  ftdi_set_bitmode(ftdi_, 0, 0); // reset
  ftdi_set_bitmode(ftdi_, 0, BITMODE_MPSSE); // enable mpsse on all bits
  ftdi_usb_purge_buffers(ftdi_);
  epicsThreadSleep(0.060);

  i = 0;
  // MASTER RESET
  pinState = Pin::CS|Pin::SYNCIO|Pin::RESET;
  pinDirection = Pin::SK|Pin::DO|Pin::CS|Pin::SYNCIO|Pin::RESET;
  pbuf = (unsigned char *)&buf[0];
  *(pbuf + i++) = TCK_DIVISOR;     // opcode: set clk divisor
  *(pbuf + i++) = 0x05;            // argument: low bit. 60 MHz / (5+1) = 1 MHz
  *(pbuf + i++) = 0x00;            // argument: high bit.
  *(pbuf + i++) = DIS_ADAPTIVE;    // opcode: disable adaptive clocking
  *(pbuf + i++) = DIS_3_PHASE;     // opcode: disable 3-phase clocking
  *(pbuf + i++) = SET_BITS_LOW;    // opcode: set low bits (ADBUS[0-7])
  *(pbuf + i++) = pinState;        // argument: inital pin states
  *(pbuf + i++) = pinDirection;    // argument: pin direction
  n  = ftdi_write_data(ftdi_, pbuf, i);
  rc = (n==i)?FTDIDriverSuccess:FTDIDriverError;
  if( rc== FTDIDriverSuccess ) {
       spiInit = 1;
       debugPrint("%s : Method called: MASTER RESET, pinState=0x%02x\n",
                                                functionName, pinState);
       }
  else debugPrint("%s : Method call FAILED!\n", functionName);
  flush();
  epicsThreadSleep(0.1);

  i = 0; // CLEAR MASTER RESET
  *(pbuf + i++) = SET_BITS_LOW;    // opcode: set low bits (ADBUS[0-7])
  pinState&=~Pin::RESET;
  *(pbuf + i++) = pinState;        // argument: inital pin states
  *(pbuf + i++) = pinDirection;    // argument: pin direction
  n  = ftdi_write_data(ftdi_, pbuf, i);
  rc = (n==i)?FTDIDriverSuccess:FTDIDriverError;
  if( rc== FTDIDriverSuccess ) {
       spiInit = 2;
       debugPrint("%s : Method called: CLEAR RESET, pinState=0x%02x\n",
                                                functionName, pinState);
       }
  else debugPrint("%s : Method call FAILED!\n", functionName);
  flush();
  // Wait 1 ms
  epicsThreadSleep(0.001);

  // CLEAR SYNCIO
  i = 0;
  *(pbuf + i++) = SET_BITS_LOW;    // opcode: set low bits (ADBUS[0-7])
  pinState&=~Pin::SYNCIO;
  *(pbuf + i++) = pinState;        // argument: inital pin states
  *(pbuf + i++) = pinDirection;    // argument: pin direction
  n  = ftdi_write_data(ftdi_, pbuf, i);
  rc = (n==i)?FTDIDriverSuccess:FTDIDriverError;
  if( rc== FTDIDriverSuccess ) {
       spiInit = 3;
       debugPrint("%s : Method called: CLEAR SYNCIO, pinState=0x%02x\n",
                                               functionName, pinState);
       }
  else debugPrint("%s : Method callFAILED!\n", functionName);

  // PULL CS LOW
  i = 0;
  *(pbuf + i++) = SET_BITS_LOW;    // opcode: set low bits (ADBUS[0-7])
  pinState&=~Pin::CS;
  *(pbuf + i++) = pinState;        // argument: inital pin states
  *(pbuf + i++) = pinDirection;    // argument: pin direction
  n  = ftdi_write_data(ftdi_, pbuf, i);
  rc = (n==i)?FTDIDriverSuccess:FTDIDriverError;
  if( rc== FTDIDriverSuccess ) {
       spiInit = 4;
       debugPrint("%s : Method called: PULL CS LOW, pinState=0x%02x\n",
                                                functionName, pinState);
       }
  else debugPrint("%s : Method call FAILED!\n", functionName);

  return rc;
}

/**
 * Setup the Vendor ID and Product ID for the USB device.
 *
 * @param vendor - Vendor ID
 * @param product - Product ID
 * @return - Success or failure.
 */
FTDIDriverStatus FTDIDriver::setVPID(const int vendor, const int product)
{
  static const char *functionName = "FTDIDriver::setVPID";
  debugPrint("%s : Method called: SPI=%d\n", functionName, spi);

  // Store the Vendor ID
  vendor_ = vendor;

  // Store the Product ID
  product_ = product;

  return FTDIDriverSuccess;
}

/**
 * Setup the baudrate.
 *
 * @param baudrate - Baud rate
 * @return - Success or failure.
 */
FTDIDriverStatus FTDIDriver::setBaudrate(const int baudrate)
{
  static const char *functionName = "FTDIDriver::setBaudrate";
  debugPrint("%s : Method called\n", functionName);

  int f;
  if (ftdi_ && (f = ftdi_set_baudrate(ftdi_, baudrate))) {
      debugPrint("Failed to set FTDI baudrate: %d (%s)\n", f, ftdi_get_error_string(ftdi_));
      return FTDIDriverError;
  }

  baudrate_ = baudrate;
  return FTDIDriverSuccess;
}

/**
 * Setup the bits.
 *
 * @param bits - Bits
 * @return - Success or failure.
 */
FTDIDriverStatus FTDIDriver::setBits(enum ftdi_bits_type bits)
{
  static const char *functionName = "FTDIDriver::setBits";
  debugPrint("%s : Method called\n", functionName);
  return setLineProperties(bits, sbits_, parity_, break_);
}

/**
 * Setup the stop bits.
 *
 * @param sbits - Stop bits
 * @return - Success or failure.
 */
FTDIDriverStatus FTDIDriver::setStopBits(enum ftdi_stopbits_type sbits)
{
  static const char *functionName = "FTDIDriver::setStopBits";
  debugPrint("%s : Method called\n", functionName);
  return setLineProperties(bits_, sbits, parity_, break_);
}

/**
 * Setup the parity.
 *
 * @param parity - Parity
 * @return - Success or failure.
 */
FTDIDriverStatus FTDIDriver::setParity(enum ftdi_parity_type parity)
{
  static const char *functionName = "FTDIDriver::setParity";
  debugPrint("%s : Method called\n", functionName);
  return setLineProperties(bits_, sbits_, parity, break_);
}

/**
 * Setup the break.
 *
 * @param brk - Break
 * @return - Success or failure.
 */
FTDIDriverStatus FTDIDriver::setBreak(enum ftdi_break_type brk)
{
  static const char *functionName = "FTDIDriver::setBreak";
  debugPrint("%s : Method called\n", functionName);
  return setLineProperties(bits_, sbits_, parity_, brk);
}

FTDIDriverStatus FTDIDriver::setLineProperties(enum ftdi_bits_type bits,
  enum ftdi_stopbits_type sbits, enum ftdi_parity_type parity, enum ftdi_break_type brk)
{
  static const char *functionName = "FTDIDriver::setLineProperties";
  debugPrint("%s : Method called\n", functionName);

  int f;
  if (ftdi_ && (f = ftdi_set_line_property2(ftdi_, bits, sbits, parity, brk)))
  {
      debugPrint("Failed to set FTDI line parameters: %d (%s)\n", f, ftdi_get_error_string(ftdi_));
      return FTDIDriverError;
  }

  bits_ = bits;
  sbits_ = sbits;
  parity_ = parity;
  break_ = brk;

  return FTDIDriverSuccess;
}

/**
 * Setup the flow control.
 *
 * @param flowctrl - Flow control. Either SIO_DISABLE_FLOW_CTRL or
 *                   a OR combination of SIO_RTS_CTS_HS, SIO_DTR_DSR_HS
 *                   and SIO_XON_XOFF_HS.
 * @return - Success or failure.
 */
FTDIDriverStatus FTDIDriver::setFlowControl(int flowctrl)
{
  static const char *functionName = "FTDIDriver::setFlowControl";
  debugPrint("%s : Method called\n", functionName);

  int f;
  if (ftdi_ && (f = ftdi_setflowctrl(ftdi_, flowctrl)))
  {
      debugPrint("Failed to set FTDI flow control: %d (%s)\n", f, ftdi_get_error_string(ftdi_));
      return FTDIDriverError;
  }
  flowctrl_ = flowctrl;
  return FTDIDriverSuccess;
}

/**
 * Setup the latency.
 *
 * @param latency - FTDI chip latency time. The latency timer counts from the
 * last time data was sent back to the host. If the latency timer expires,
 * the device will send what data it has available to the host regardless
 * of how many bytes it is waiting on. The latency timer will then reset
 * and begin counting again. The default value for the latency timer is 16ms.
 *
 * @return - Success or failure.
 */
FTDIDriverStatus FTDIDriver::setLatency(const int latency)
{
  static const char *functionName = "FTDIDriver::setLatency";
  debugPrint("%s : Method called\n", functionName);

  int f;
  if (ftdi_ && (f = ftdi_set_latency_timer (ftdi_, latency))) {
     debugPrint("Failed to set FTDI latency: %d (%s)\n", f, ftdi_get_error_string(ftdi_));
     return FTDIDriverError;
  }

  latency_ = latency;
  return FTDIDriverSuccess;
}

/**
 * Get the Baud rate.
 *
 * @return - The Baud rate.
 */
int FTDIDriver::getBaudRate(void)
{
  return ftdi_->baudrate;
}

/**
 * Get the bits.
 *
 * @return - The bits.
 */
enum ftdi_bits_type FTDIDriver::getBits(void)
{
  return bits_;
}

/**
 * Get the stop bits.
 *
 * @return - The stop bits.
 */
enum ftdi_stopbits_type FTDIDriver::getStopBits(void)
{
  return sbits_;
}

/**
 * Get the parity.
 *
 * @return - The parity.
 */
enum ftdi_parity_type FTDIDriver::getParity(void)
{
  return parity_;
}

/**
 * Get the break.
 *
 * @return - The break.
 */
enum ftdi_break_type FTDIDriver::getBreak(void)
{
  return break_;
}

/**
 * Get the flow control.
 *
 * @return - The flow control.
 */
int FTDIDriver::getFlowControl(void)
{
  return flowctrl_;
}

/**
 * Attempt to create a connection  Once the connection has
 * been established.
 *
 * @return - Success or failure.
 */
FTDIDriverStatus FTDIDriver::connectFTDI()
{
  int f;
  unsigned int chipid;
  static const char *functionName = "FTDIDriver::connect";
  debugPrint("%s : Method called\n", functionName);

#ifdef HAVE_LIBFTDI1
  struct ftdi_version_info version = ftdi_get_library_version();
  printf("Initialized libftdi %s (major: %d, minor: %d, micro: %d, snapshot ver: %s)\n",
    version.version_str, version.major, version.minor, version.micro, version.snapshot_str);
#else
  printf("Initialized libftdi\n");
#endif

  // Allocate and initialize a new ftdi context
  if ((ftdi_ = ftdi_new()) == NULL)
  {
     debugPrint("ftdi_new failed: %s\n", ftdi_get_error_string(ftdi_));
     return FTDIDriverError;
  }

  // Select interface. In this case just use "ANY"
  if ((f = ftdi_set_interface(ftdi_, INTERFACE_ANY)) != 0)
  {
     debugPrint("ftdi_set_interface failed: %d (%s)\n", f, ftdi_get_error_string(ftdi_));
     return FTDIDriverError;
  }

  // Open the FTDI USB device
  if ((f = ftdi_usb_open(ftdi_, vendor_, product_)) != 0)
  {
     debugPrint("Failed to open FTDI device: %d (%s)\n", f, ftdi_get_error_string(ftdi_));
     return FTDIDriverError;
  }

  // Read out FTDI chip ID
  debugPrint("ftdi->type = %d. ", ftdi_->type);
  ftdi_read_chipid(ftdi_, &chipid);
  printf("FTDI chipid: %X\n", chipid);

  // Set baudrate
  if (setBaudrate(baudrate_))
    return FTDIDriverError;

  // Set line parameters
  if (setLineProperties(bits_, sbits_, parity_, break_))
    return FTDIDriverError;

  // Set FTDI chip latency in millisecs.
  if (setLatency(latency_))
    return FTDIDriverError;

  // Set FTDI flow control
  if (setFlowControl(flowctrl_))
    return FTDIDriverError;

  // Reset device after setting the latency, otherwise the initial communication will result in an error
  if ((f = ftdi_usb_reset (ftdi_)) != 0)
  {
     debugPrint("Failed to reset FTDI: %d (%s)\n", f, ftdi_get_error_string(ftdi_));
     return FTDIDriverError;
  }

  // Purge the send and receive buffers
  if ((f = ftdi_usb_purge_tx_buffer (ftdi_)) != 0)
  {
     debugPrint("Failed to purge FTDI tx buffer: %d (%s)\n", f, ftdi_get_error_string(ftdi_));
     return FTDIDriverError;
  }

  if ((f = ftdi_usb_purge_rx_buffer (ftdi_)) != 0)
  {
     debugPrint("Failed to purge FTDI rx buffer: %d (%s)\n", f, ftdi_get_error_string(ftdi_));
     return FTDIDriverError;
  }

  // Wait 60 ms. for purge to complete
     epicsThreadSleep(0.060);

  if ((f = ftdi_read_data_set_chunksize(ftdi_, 8192)) != 0)
  {
     debugPrint("Failed to set FTDI read chunk size: %d (%s)\n", f, ftdi_get_error_string(ftdi_));
     return FTDIDriverError;
  }

  if ((f = ftdi_write_data_set_chunksize(ftdi_, 8192)) != 0)
  {
     debugPrint("Failed to set FTDI write chunk size: %d (%s)\n", f, ftdi_get_error_string(ftdi_));
     return FTDIDriverError;
  }

  debugPrint("%s : FTDI Connection ready...\n", functionName);
  connected_ = 1;

  if( spi ) {
    debugPrint("INIT SPI-\n");
    FTDIDriver::initSPI();
    }

  return FTDIDriverSuccess;
}

/**
 * Flush the FTDI connection as best as possible.
 *
 * @return - Success or failure.
 */
FTDIDriverStatus FTDIDriver::flush()
{
  int f;
  static const char *functionName = "FTDIDriver::flush";
  debugPrint("%s : Method called\n", functionName);

  if (connected_ == 0){
    debugPrint("%s : FTDI not connected\n", functionName);
    return FTDIDriverError;
  }

  // Clears read & write buffers on the chip and the internal read buffer
  f = ftdi_usb_purge_buffers(ftdi_);
  if (f < 0){
    debugPrint("Failed to purge FTDI buffers: %d (%s)\n", f, ftdi_get_error_string(ftdi_));
    return FTDIDriverError;
  }
  return FTDIDriverSuccess;
}

/**
 * Write data to the connected channel.  A timeout should be
 * specified in milliseconds.
 *
 * @param buffer - The string buffer to be written.
 * @param bufferSize - The number of bytes to write.
 * @param bytesWritten - The number of bytes that were written.
 * @param timeout - A timeout in ms for the write.
 * @return - Success or failure.
 */
FTDIDriverStatus FTDIDriver::write(const unsigned char *buffer, int bufferSize, size_t *bytesWritten, int timeout)
{
  /*********************************************************************************/
  /* Array for collecting Return Codes from various stages of (SPI) write          */
  /* (for compilation of a single debugPrint message at the end). Good to have     */
  /* while debugging MSSP protocol and monitoring pinState with a scope.           */
  /* AD9915 read back data is clocked "back" synchronously while writing under     */
  /* condition that bit[7] of the instruction byte was 1: 'ftdi_transfer_data_done'*/
  /* to AD9915 can fail at different stages if CS, SYNCIO, I/O_UPDATE are not      */
  /* pulled in timely manner. Currently these lines (pinState) are controlled by   */
  /* (hard coded) binary bytes embedded into the 'stream' protocol.                */
  /*********************************************************************************/
  size_t rc[16];
  size_t i = 0;
  int    err =0;

  unsigned short sz = (unsigned short)bufferSize - 1;

  static const char *functionName = "FTDIDriver::write";
  debugPrint("%s : Method called\n", functionName);
  *bytesWritten = 0;

  if (connected_ == 0){
    debugPrint("%s : FTDI not connected\n", functionName);
    return FTDIDriverError;
    }

  debugPrint("%s : Writing: bufferSize=%d", functionName, bufferSize);
  if(  spi ) debugPrint("\n"); // SPI data binary, no ASCII = don't print
  else       debugPrint(" buffer=%s\n", buffer);
  timeval stime;
  timeval ctime;
  gettimeofday(&stime, NULL);
  long mtimeout = (stime.tv_usec / 1000) + timeout;
  long tnow = 0;

  // Set timeout value
  ftdi_->usb_write_timeout = timeout;

#ifdef HAS_LIBFTDI1
  struct ftdi_transfer_control *tc = ftdi_write_data_submit (
    ftdi_, (unsigned char *) buffer, (int) bufferSize
  );
  rc[8] = ftdi_transfer_data_done (tc);
#else
  if( spi ) {
    if( !spiInit ) initSPI();
    i = 0;
    pbuf = (unsigned char *)&buf[0];

    memcpy((pbuf+i), buffer, bufferSize);
    i+= bufferSize;

    debugPrint("Preamble+buffer:");
    for (size_t j = 0; j < i; j++) debugPrint("=[%02X] ", *(pbuf+j));
    debugPrint("\n");

    flush();
    rc[3] = ftdi_write_data(ftdi_, pbuf, i);
    if( rc[3] != i ) err = FTDIDriverError;

    } // if SPI -
  else {
    rc[7] = ftdi_write_data(ftdi_, (unsigned char *) buffer, (int) bufferSize);
    if( rc[7] != i ) err = FTDIDriverError;
    debugPrint("%s : 7. pinState=0x%02x\n",
               functionName, pinState);
    }
#endif

  gettimeofday(&ctime, NULL);
  tnow = ((ctime.tv_sec - stime.tv_sec) * 1000) + (ctime.tv_usec / 1000);
  debugPrint("%s : Time taken for write => %ld ms\n", functionName, (tnow-(mtimeout-timeout)));
  if (rc[3] > 0){
    // Since we write also the address byte byteswritten = sz+1
    *bytesWritten = (size_t)(sz+1);
    debugPrint("%s : %d bytes written err=%d: ", functionName, *bytesWritten, err);
    if( err == FTDIDriverError ) debugPrint("FTDIDriverError\n");
    else                         debugPrint("FTDIDriverSUCCESS\n");
  } else {
    debugPrint("FTDI write error: %d (%s)\n", rc, ftdi_get_error_string(ftdi_));
    *bytesWritten = 0;
    return FTDIDriverError;
  }

  return FTDIDriverSuccess;
}

/**
 * Read data from the connected channel.  A timeout should be
 * specified in milliseconds.  The read method will continue to
 * read data from the channel until either data arrives or the
 * timeout is reached.
 *
 * @param buffer - A string buffer to hold the read data.
 * @param bufferSize - The maximum number of bytes to read.
 * @param bytesWritten - The number of bytes that have been read.
 * @param timeout - A timeout in ms for the read.
 * @return - Success or failure.
 */
FTDIDriverStatus FTDIDriver::read(unsigned char *buffer, size_t bufferSize, size_t *bytesRead, int timeout)
{
  static const char *functionName = "FTDIDriver::read";
  debugPrint("%s : Method called\n", functionName);

  if (connected_ == 0){
    debugPrint("%s : Not connected\n", functionName);
    return FTDIDriverError;
  }

  debugPrint("%s : Reading: bufferSize=%d, timeout=%d\n", functionName, bufferSize, timeout);

  timeval stime;
  timeval ctime;
  gettimeofday(&stime, NULL);
  long mtimeout = (stime.tv_usec / 1000) + timeout;
  long tnow = 0;
  int rc = 0;

  *bytesRead = 0;

  if( spi ) ftdi_usb_purge_tx_buffer(ftdi_);
  while ((*bytesRead == 0) && (tnow < mtimeout)) {
    rc = ftdi_read_data (ftdi_, &buffer[*bytesRead], (bufferSize-*bytesRead));
    debugPrint("%s rc=%d\n", functionName, rc);
    if (rc > 0) *bytesRead+=rc;
    if (*bytesRead == 0) {
      if( spi ) return FTDIDriverTimeout;
      usleep(50);
      gettimeofday(&ctime, NULL);
      tnow = ((ctime.tv_sec - stime.tv_sec) * 1000) + (ctime.tv_usec / 1000);
      }
    }

  if (*bytesRead < bufferSize)
    buffer[*bytesRead] = '\0';

  debugPrint("%s %d Bytes =>\n", functionName, (int)*bytesRead);
  for (int j = 0; j < (int)*bytesRead; j++){
    debugPrint("[%02X] ", buffer[j]);
  }
  debugPrint("\n");

  gettimeofday(&ctime, NULL);
  tnow = ((ctime.tv_sec - stime.tv_sec) * 1000) + (ctime.tv_usec / 1000);
  debugPrint("%s : Time taken for read => %ld ms\n", functionName, (tnow-(mtimeout-timeout)));

  if ((timeout > 0) && (tnow >= mtimeout)){
    debugPrint("%s : TIMEOUT-\n", functionName);
    return FTDIDriverTimeout;
  }
  debugPrint("%s : SUCCESS!\n", functionName);
  return FTDIDriverSuccess;
}


/**
 * Close the connection.
 *
 * @return - Success or failure.
 */
FTDIDriverStatus FTDIDriver::disconnectFTDI()
{
  static const char *functionName = "FTDIDriver::disconnect";
  debugPrint("%s : Method called\n", functionName);

  if (connected_ == 1){
    ftdi_usb_close(ftdi_);
    ftdi_deinit(ftdi_);
    connected_ = 0;
  }
  else {
    debugPrint("%s : Connection was not established\n", functionName);
  }
  return FTDIDriverSuccess;
}

/**
 * Destructor, cleanup.
 */
FTDIDriver::~FTDIDriver()
{
  static const char *functionName = "FTDIDriver::~FTDIDriver";
  debugPrint("%s : Method called\n", functionName);
}

#ifndef _MINGW
#ifdef _WIN32
/* Windows implemenation of gettimeofday and usleep */
#include < time.h >
#include < windows.h >

#if defined(_MSC_VER) || defined(_MSC_EXTENSIONS)
  #define DELTA_EPOCH_IN_MICROSECS  11644473600000000Ui64
#else
  #define DELTA_EPOCH_IN_MICROSECS  11644473600000000ULL
#endif

struct timezone
{
  int  tz_minuteswest; /* minutes W of Greenwich */
  int  tz_dsttime;     /* type of dst correction */
};

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
  FILETIME ft;
  unsigned __int64 tmpres = 0;
  static int tzflag = 0;

  if (NULL != tv)
  {
    GetSystemTimeAsFileTime(&ft);

    tmpres |= ft.dwHighDateTime;
    tmpres <<= 32;
    tmpres |= ft.dwLowDateTime;

    tmpres /= 10;  /*convert into microseconds*/
    /*converting file time to unix epoch*/
    tmpres -= DELTA_EPOCH_IN_MICROSECS;
    tv->tv_sec = (long)(tmpres / 1000000UL);
    tv->tv_usec = (long)(tmpres % 1000000UL);
  }

  if (NULL != tz)
  {
    if (!tzflag)
    {
      _tzset();
      tzflag++;
    }
    tz->tz_minuteswest = _timezone / 60;
    tz->tz_dsttime = _daylight;
  }

  return 0;
}

void usleep(__int64 usec)
{
    HANDLE timer;
    LARGE_INTEGER ft;

    ft.QuadPart = -(10*usec); // Convert to 100 nanosecond interval, negative value indicates relative time

    timer = CreateWaitableTimer(NULL, TRUE, NULL);
    SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
    WaitForSingleObject(timer, INFINITE);
    CloseHandle(timer);
}
#endif
#endif
