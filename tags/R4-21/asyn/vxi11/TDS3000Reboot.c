/* TDS3000Reboot.c */

/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/* REBOOT code for Tektronix TDS3000 Oscilloscopes */

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>

#include <osiSock.h>
#include <epicsThread.h>

int TDS3000Reboot(char * inetAddr)
{
    struct sockaddr_in serverAddr;
    SOCKET fd;
    int status;
    int nbytes;
    char *url;
    int urlLen;

    url = "GET /resetinst.cgi HTTP/1.0\n\n";
    urlLen = (int)strlen(url);
    errno = 0;
    fd = epicsSocketCreate(PF_INET, SOCK_STREAM, 0);
    if(fd == -1) {
        printf("can't create socket %s\n",strerror(errno));
        return(-1);
    }
    memset((char*)&serverAddr, 0, sizeof(struct sockaddr_in));
    serverAddr.sin_family = PF_INET;
    /* 80 is http port */
    status = aToIPAddr(inetAddr,80,&serverAddr);
    if(status) {
        printf("aToIPAddr failed\n");
        return(-1); 
    }
    errno = 0;
    status = connect(fd,(struct sockaddr*)&serverAddr, sizeof(serverAddr));
    if(status) {
        printf("can't connect %s\n",strerror (errno));
        epicsSocketDestroy(fd);
        return(-1);
    }
    nbytes = send(fd,url,urlLen,0);
    if(nbytes!=urlLen) printf("nbytes %d expected %d\n",nbytes,urlLen);
    epicsSocketDestroy(fd);
    return(0);
}
