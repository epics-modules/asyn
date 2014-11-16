/*
 * testAsynIPPortClient.cpp
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

/** Test program that demonstrates how to write C++ program that instantiates an asyn port driver
  * and communicates with it directly over the asyn interfaces without running an EPICS IOC. 
  * It creates an asynIPPort driver, and uses the command line arguments to set
  * the hostInfo string, a single command string to send to the server, and optionally
  * the input and output EOS. It then prints out the response from the server. There
  * are 3 example shell scipts provides that show how to use testAsynIPPortClient to communicate
  * with a Web server, XPS motor controller, and a telnet host respectively.
  * 
  * Usage: testAsynIPPortClient hostInfo outputString [outputEos] [inputEos]
  *
  * Example: testAsynIPPortClient cars.uchicago.edu:80 "GET / HTTP/1.0" "\n\n" */
int main(int argc, char **argv)
{
  char input[10000], inputEos[10], outputEos[10];
  size_t nIn, nOut;
  int eomReason;
  
  if (argc < 3) {
      printf("Usage: testAsynIPPortClient hostInfo outputString [outputEos] [inputEos]\n");
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
