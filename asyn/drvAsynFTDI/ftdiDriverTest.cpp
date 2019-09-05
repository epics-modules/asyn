/* ftdiDriverTest.cpp

*/

#include <ostream>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <ftdiDriver.h>

  int npointFormatReadSingle(unsigned char *buffer, uint32_t address);
  int32_t nPointFormatInteger(unsigned char *buffer);
  int npointFormatWriteSingle(unsigned char *buffer, uint32_t address, int32_t value);

int main(int argc, char **argv)
{
    int f = 0, i, n, timel;
    size_t bwritten, bread;
    int vendor, product, baudrate;
    struct timeval stime;
    struct timeval ctime;
    unsigned char readbuff[10], writebuff[6];
    int32_t posn;
    
    FTDIDriverStatus status;

    while ((i = getopt(argc, argv, "v:p:b:w::")) != -1)
    {
        switch (i)
        {
            case 'v':
                vendor = strtoul(optarg, NULL, 0);
                break;
            case 'p':
                product = strtoul(optarg, NULL, 0);
                break;
            case 'b':
                baudrate = strtoul(optarg, NULL, 0);
                break;
            default:
                fprintf(stderr, "usage: %s [-i interface] [-v vid] [-p pid] [-b baudrate] [-w [pattern]]\n", *argv);
                exit(-1);
        }
    }

    FTDIDriver *ftdiDriver = new FTDIDriver();
    status = ftdiDriver->setVPID(0x403, 0x6010);
    if (status != FTDIDriverSuccess) return(status);
    status = ftdiDriver->setBaudrate(3000000);
    if (status != FTDIDriverSuccess) return(status);
    status = ftdiDriver->setLatency(2);
    if (status != FTDIDriverSuccess) return(status);
    status = ftdiDriver->connectFTDI();
    if (status != FTDIDriverSuccess) return(status);
    
// Connected OK. get stage axis range
    npointFormatReadSingle(writebuff, 0x11831078);
    status = ftdiDriver->write(writebuff, 6, &bwritten, 2000);
    if (status != FTDIDriverSuccess) return(status);
    status = ftdiDriver->read(readbuff, 10, &bread, 2000);
    npointFormatReadSingle(writebuff, 0x11831334);
    for (i=0; i<100; i++)
    {
      status = ftdiDriver->write(writebuff, 6, &bwritten, 2000);
      if (status != FTDIDriverSuccess) return(status);
      status = ftdiDriver->read(readbuff, 10, &bread, 2000);
      printf("%d. ",i);
      for (n=0; n < bread; n++) printf("%02X", readbuff[n]);
      printf("\n");
      posn = nPointFormatInteger(&(readbuff[5]));
      printf("position value = %d\n", posn);
    }
    if (status != FTDIDriverSuccess) return(status);
 
    return FTDIDriverSuccess;
}

  int npointFormatReadSingle(unsigned char *buffer, uint32_t address)
  { 
  /* Set up single location read at specified address */ 
     buffer[0] = 0xA0;
     buffer[1] =  address & 0x000000ff;
     buffer[2] =  (address & 0x0000ff00) >> 8;
     buffer[3] =  (address & 0x00ff0000) >> 16;
     buffer[4] =  (address & 0xff000000) >> 24;
     buffer[5] = 0x55;
     return 0;
  }

  int npointFormatWriteSingle(unsigned char *buffer, uint32_t address, int32_t value)
  { 
  /* Set up single location write at specified address */ 
     buffer[0] = 0xA2;
     buffer[1] =  address & 0x000000ff;
     buffer[2] =  (address & 0x0000ff00) >> 8;
     buffer[3] =  (address & 0x00ff0000) >> 16;
     buffer[4] =  (address & 0xff000000) >> 24;
     buffer[5] =  value & 0x000000ff;
     buffer[6] =  (value & 0x0000ff00) >> 8;
     buffer[7] =  (value & 0x00ff0000) >> 16;
     buffer[8] =  (value & 0xff000000) >> 24;
     buffer[9] = 0x55;  
     return 0;
  }


  int32_t nPointFormatInteger(unsigned char *buffer)
  {
     int32_t value = 0;
   // Reverse 4 bytes in 32 bit integer  
     value =  buffer[0];
     value += buffer[1] << 8;
     value += buffer[2] << 16;
     value += buffer[3] << 24;
     return (value);
 }
     
     
