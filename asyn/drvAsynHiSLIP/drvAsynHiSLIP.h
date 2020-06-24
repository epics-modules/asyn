/*
 * ASYN support for HiSLIP
 *

 ***************************************************************************
 * Copyright (c) 2020 N. Yamamoto <noboru.yamamoto@kek.jp>
 * based on AsynUSBTMC supoort by
 * Copyright (c) 2013 W. Eric Norum <wenorum@lbl.gov>                      *
 * This file is distributed subject to a Software License Agreement found  *
 * in the file LICENSE that is included with this distribution.            *
 ***************************************************************************
 */
//-*- coding:utf-8 -*-
#define NDEBUG 1
#define DEBUG 1

#include <assert.h>

#include <string.h>
#include <errlog.h>
#include <poll.h>
#include <endian.h> // network endian is "be".
#include <sys/types.h>
//#include <bits/stdint-intn.h>


#include <osiUnistd.h>
#include <osiSock.h>

#include <epicsTypes.h>
#include <epicsStdio.h>
#include <epicsString.h>
#include <epicsEvent.h>
#include <epicsMessageQueue.h>
#include <epicsMutex.h>
#include <epicsThread.h>
#include <epicsExport.h>
#include <cantProceed.h>
#include <iocsh.h>

#include <asynDriver.h>
#include <asynDrvUser.h>
#include <asynOctet.h>
#include <asynInt32.h>

#include "HiSLIPMessage.h"
