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

#define BUFF_SIZE 10

epicsShareFunc int epicsShareAPI epicsSnStrPrintEscaped(
    char *outbuff, const char *inbuff, int outlen, int inlen)
{
   int nout=0;
   char buffer[BUFF_SIZE];
   int len;

   *outbuff = '\0';
   while (inlen--) {
       char c = *inbuff++;
       switch (c) {
       case '\a':  epicsSnprintf(buffer, BUFF_SIZE, "\\a"); break;
       case '\b':  epicsSnprintf(buffer, BUFF_SIZE, "\\b"); break;
       case '\f':  epicsSnprintf(buffer, BUFF_SIZE, "\\f"); break;
       case '\n':  epicsSnprintf(buffer, BUFF_SIZE, "\\n"); break;
       case '\r':  epicsSnprintf(buffer, BUFF_SIZE, "\\r"); break;
       case '\t':  epicsSnprintf(buffer, BUFF_SIZE, "\\t"); break;
       case '\v':  epicsSnprintf(buffer, BUFF_SIZE, "\\v"); break;
       case '\\':  epicsSnprintf(buffer, BUFF_SIZE, "\\\\"); ; break;
       /*? does not follow C convention because trigraphs no longer important*/
       case '\?':  epicsSnprintf(buffer, BUFF_SIZE, "?"); break;
       case '\'':  epicsSnprintf(buffer, BUFF_SIZE, "\\'"); break;
       case '\"':  epicsSnprintf(buffer, BUFF_SIZE, "\\\""); break;
       default:
           if (isprint(c))
               epicsSnprintf(buffer, BUFF_SIZE, "%c", c);
           else
               epicsSnprintf(buffer, BUFF_SIZE, "\\%03o", (unsigned char)c);
           break;
       }
       len = strlen(buffer);
       if ((nout + len) > (outlen-1)) return(nout);
       nout += len;
       strcat(outbuff, buffer);
   }
   return(nout);
}
