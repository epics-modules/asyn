/*
 * testAsynPortClient.cpp
 * 
 * Program that creates an asyn IP port driver without an IOC and communicates over standard asyn interfaces
 *
 * Author: Mark Rivers
 *
 * Created October 20, 2014
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <epicsString.h>
#include <drvAsynIPPort.h>
#include <asynPortClient.h>

int main(int argc, char **argv)
{
  char input[10000], inputEos[10], outputEos[10];
  size_t nIn, nOut;
  int eomReason;
  
  if (argc < 3) {
      printf("Usage: testAsynPortClient hostInfo outputString [outputEos] [inputEos]\n");
      return 0;
  }
  
  drvAsynIPPortConfigure("IP", argv[1], 0, 0, 0);
  asynOctetClient ip = asynOctetClient("IP", 0, 0);
  
  if (argc > 3) {
      epicsStrnRawFromEscaped(outputEos, sizeof(outputEos), argv[3], strlen(argv[3]));
      ip.setOutputEos(outputEos, strlen(outputEos));
  }
  if (argc > 4) {
      epicsStrnRawFromEscaped(inputEos, sizeof(inputEos), argv[4], strlen(argv[4]));
      ip.setInputEos(inputEos, strlen(inputEos));
  }
  ip.write(argv[2], strlen(argv[2]), &nOut);
  ip.read(input, sizeof(input), &nIn, &eomReason);
  
  printf("Sent %d bytes, output:\n%s\n", (int)nOut, argv[2]);
  printf("Received %d bytes, eomReason=%d, response:\n%s\n", (int)nIn, eomReason, input);
  
}
