#define SER_RS485_ENABLED         0x1
#define SER_RS485_RTS_ON_SEND     0x2
#define SER_RS485_RTS_AFTER_SEND  0x4

#define TIOCGRS485      0x542E
#define TIOCSRS485      0x542F

struct serial_rs485 {
  int flags;
  unsigned int delay_rts_before_send;
  unsigned int delay_rts_after_send;
};

int ioctl( int fd, unsigned long request, void* ptr ) {
  return 0;
}
