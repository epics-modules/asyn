
#ifndef ftdiDriver_H
#define ftdiDriver_H

#ifdef HAVE_LIBFTDI1
#include <libftdi1/ftdi.h>
#else
#include <ftdi.h>
#endif

#include <usb.h>

# ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif

#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>

typedef enum e_FTDIDriverStatus
{
  FTDIDriverSuccess,
  FTDIDriverTimeout,
  FTDIDriverError
} FTDIDriverStatus;

/**
 * The FTDIDriver class provides a wrapper around the libftdi1 library.
 * It simplifies creating FTDI connections and provides a simple read/write/flush
 * interface.  Setting up an FTDI chip USB connection can be configured with a
 * Vendor ID, Product ID, baudrate & latency
 *
 * @author Philip Taylor (pbt@observatorysciences.co.uk)
 */
class FTDIDriver {

  public:
    FTDIDriver();
    FTDIDriverStatus setVPID(const int vendor, const int product);
    FTDIDriverStatus setBaudrate(const int baudrate);
    FTDIDriverStatus setBits(enum ftdi_bits_type bits);
    FTDIDriverStatus setStopBits(enum ftdi_stopbits_type sbits);
    FTDIDriverStatus setParity(enum ftdi_parity_type parity);
    FTDIDriverStatus setBreak(enum ftdi_break_type brk);
    FTDIDriverStatus setFlowControl(int flowctrl);
    FTDIDriverStatus setLatency(const int latency);
    FTDIDriverStatus setLineProperties(enum ftdi_bits_type bits,
      enum ftdi_stopbits_type sbits, enum ftdi_parity_type parity,
      enum ftdi_break_type brk);
    int getBaudRate(void);
    enum ftdi_bits_type getBits(void);
    enum ftdi_stopbits_type getStopBits(void);
    enum ftdi_parity_type getParity(void);
    enum ftdi_break_type getBreak(void);
    int getFlowControl(void);
    FTDIDriverStatus connectFTDI();
    FTDIDriverStatus flush();
    FTDIDriverStatus write(const unsigned char *buffer, int bufferSize, size_t *bytesWritten, int timeout);
    FTDIDriverStatus read(unsigned char *buffer, size_t bufferSize, size_t *bytesRead, int timeout);
    FTDIDriverStatus disconnectFTDI();
    virtual ~FTDIDriver();

  private:
    struct ftdi_context *ftdi_;
    int connected_;
    int vendor_;
    int product_;
    int baudrate_;
    enum ftdi_bits_type bits_;
    enum ftdi_stopbits_type sbits_;
    enum ftdi_parity_type parity_;
    enum ftdi_break_type break_;
    int flowctrl_;
    int latency_;
    off_t got_;

};


#endif


