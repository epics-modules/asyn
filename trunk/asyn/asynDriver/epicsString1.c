/*************************************************************************\
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
*     National Laboratory.
* Copyright (c) 2002 The Regents of the University of California, as
*     Operator of Los Alamos National Laboratory.
* EPICS BASE Versions 3.13.7
* and higher are distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
\*************************************************************************/
/*epicsString.c*/
/*Authors: Jun-ichi Odagiri and Marty Kraimer*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <epicsStdio.h>

#define epicsExportSharedSymbols
#include "epicsString1.h"

epicsShareFunc int epicsShareAPI epicsStrSnPrintEscaped(
    char *outbuf, int outsize, const char *inbuf, int inlen)
{
   int outCapacity = outsize;
   int len;

   while (inlen-- && (outsize > 0))  {
       char c = *inbuf++;
       switch (c) {
       case '\a':  len = epicsSnprintf(outbuf, outsize, "\\a"); break;
       case '\b':  len = epicsSnprintf(outbuf, outsize, "\\b"); break;
       case '\f':  len = epicsSnprintf(outbuf, outsize, "\\f"); break;
       case '\n':  len = epicsSnprintf(outbuf, outsize, "\\n"); break;
       case '\r':  len = epicsSnprintf(outbuf, outsize, "\\r"); break;
       case '\t':  len = epicsSnprintf(outbuf, outsize, "\\t"); break;
       case '\v':  len = epicsSnprintf(outbuf, outsize, "\\v"); break;
       case '\\':  len = epicsSnprintf(outbuf, outsize, "\\\\"); ; break;
       /*? does not follow C convention because trigraphs no longer important*/
       case '\?':  len = epicsSnprintf(outbuf, outsize, "?"); break;
       case '\'':  len = epicsSnprintf(outbuf, outsize, "\\'"); break;
       case '\"':  len = epicsSnprintf(outbuf, outsize, "\\\""); break;
       default:
           if (isprint(c))
               len = epicsSnprintf(outbuf, outsize, "%c", c);
           else
               len = epicsSnprintf(outbuf, outsize, "\\%03o", (unsigned char)c);
           break;
       }
       outsize -= len;
       outbuf += len;
   }
   return(outCapacity - outsize);
}
