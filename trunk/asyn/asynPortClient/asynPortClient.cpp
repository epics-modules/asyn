/*
 * asynPortClient.cpp
 * 
 * Classes for asyn clients to communicate with asyn drivers
 *
 * Author: Mark Rivers
 *
 * Created October 27, 2014
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define epicsExportSharedSymbols
#include <shareLib.h>
#include "asynInt32SyncIO.h"
#include "asynPortClient.h"

static const char *driverName = "asynPortClient";

#define DEFAULT_TIMEOUT 1.0

asynClient::asynClient(const char *portName, int addr, const char *asynInterface, const char *drvInfo, double timeout)
{
    pasynUser_ = NULL;
    timeout_ = timeout;
    portName_ = epicsStrDup(portName);
    addr_ = addr;
    asynInterface_ = epicsStrDup(asynInterface);
    drvInfo_ = epicsStrDup(drvInfo);
}

asynClient::~asynClient()
{
    if (portName_) free(portName_);
    if (asynInterface_) free(asynInterface_);
    if (drvInfo_) free(drvInfo_);
}

asynInt32Client::asynInt32Client(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynClient(portName, addr, asynInt32SyncIOType, drvInfo, timeout)
{
    asynStatus status;
    status = pasynInt32SyncIO->connect(portName, addr, &pasynUser_, drvInfo);
}

asynInt32Client::~asynInt32Client()
{
    asynStatus status;
    status = pasynInt32SyncIO->disconnect(pasynUser_);
}

asynStatus asynInt32Client::read(epicsInt32 *value)
{
    return pasynInt32SyncIO->read(pasynUser_, value, timeout_);
}

asynStatus asynInt32Client::write(epicsInt32 value)
{
    return pasynInt32SyncIO->write(pasynUser_, value, timeout_);
}

asynStatus asynInt32Client::getBounds(epicsInt32 *low, epicsInt32 *high)
{
    return pasynInt32SyncIO->getBounds(pasynUser_, low, high);
}

