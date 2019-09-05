
#ifndef ftdiDriver_H
#define ftdiDriver_H

#include <ftdi.h>
#include <libusb.h>

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
    FTDIDriverStatus setLatency(const int latency);
    FTDIDriverStatus connectFTDI();
    FTDIDriverStatus flush();
    FTDIDriverStatus write(const unsigned char *buffer, int bufferSize, size_t *bytesWritten, int timeout);
    FTDIDriverStatus read(unsigned char *buffer, int bufferSize, size_t *bytesRead, int timeout);
    FTDIDriverStatus disconnectFTDI();
    virtual ~FTDIDriver();

  private:
    struct ftdi_context *ftdi_;
    int connected_;
    int vendor_;
    int product_;
    int baudrate_;
    int latency_;
    off_t got_;

};


#endif


