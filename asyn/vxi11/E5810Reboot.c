/* E5810Reboot.c */

/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/* REBOOT code for HP E5810 LAN to GPIB server
 * Author: Marty Kraimer.
 * Extracted from Code by Benjamin Franksen and Stephanie Allison
 *****************************************************************************/

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>

#include <osiSock.h>
#include <epicsThread.h>

char *defaultPassword = "E5810";
int E5810Reboot(char * inetAddr,char *password)
{
    struct sockaddr_in serverAddr;
    SOCKET fd;
    int status;
    int nbytes;

    if(password==0 || strlen(password)<2) password = defaultPassword;
    errno = 0;
    fd = epicsSocketCreate(PF_INET, SOCK_STREAM, 0);
    if(fd == -1) {
        printf("can't create socket %s\n",strerror(errno));
        return(-1);
    }
    memset((char*)&serverAddr, 0, sizeof(struct sockaddr_in));
    serverAddr.sin_family = PF_INET;
    /* 23 is telnet port */
    status = aToIPAddr(inetAddr,23,&serverAddr);
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
    nbytes = send(fd,"reboot\n",7,0);
    if(nbytes!=7) printf("nbytes %d expected 7\n",nbytes);
    epicsThreadSleep(1.0);
    nbytes = send(fd,password,(int)strlen(password),0);
    if(nbytes!=strlen(password)) 
        printf("nbytes %d expected %d\n",nbytes,(int)strlen(password));
    epicsThreadSleep(1.0);
    nbytes = send(fd,"\ny\n",3,0);
    if(nbytes!=3) printf("nbytes %d expected 3\n",nbytes);
    epicsThreadSleep(1.0);
    epicsSocketDestroy(fd);
    epicsThreadSleep(20.0);
    return(0);
}
