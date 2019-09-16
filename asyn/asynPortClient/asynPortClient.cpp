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
#include <stdexcept>
#include <epicsThread.h>

#define epicsExportSharedSymbols
#include <shareLib.h>
#include "asynPortDriver.h"
#include "asynPortClient.h"

//static const char *driverName = "asynPortClient";

/** Constructor for asynParamClient class
  * \param[in] portName  The name of the asyn port to connect to
  * \param[in] addr The address on the asyn port to connect to
  * \param[in] asynInterfaceType The name of the asynInterface to connect to (e.g.asynInt32, asynOctet, etc.)
  * \param[in] drvInfo  The drvInfo string to identify which property of the port is being connected to
  * \param[in] timeout The default timeout for all communications between the client and the port driver 
*/
asynParamClient::asynParamClient(const char *portName, int addr, const char *asynInterfaceType, const char *drvInfo, 
                       double timeout)
    : pasynUser_(NULL), pasynUserSyncIO_(NULL), timeout_(timeout), portName_(epicsStrDup(portName)),
      addr_(addr), asynInterfaceType_(epicsStrDup(asynInterfaceType)), drvInfo_(NULL)
    ,interruptPvt_(NULL)
{
    asynStatus status;
    asynInterface *pinterface;
    asynDrvUser *pDrvUser;

    if (drvInfo) drvInfo_=epicsStrDup(drvInfo);
    pasynUser_ = pasynManager->createAsynUser(0,0);
    status = pasynManager->connectDevice(pasynUser_, portName, addr);
    if (status) {
        throw std::runtime_error(std::string("connectDevice failed:").append(pasynUser_->errorMessage));
    }
    pasynInterface_ = pasynManager->findInterface(pasynUser_, asynInterfaceType, 1);
    if (!pasynInterface_) {
        throw std::runtime_error(std::string("findInterface failed:").append(asynInterfaceType));
    }
    if (!drvInfo) return;
    pinterface = pasynManager->findInterface(pasynUser_, asynDrvUserType, 1);
    if (!pinterface) return;
    pDrvUser = (asynDrvUser *)pinterface->pinterface;
    status = pDrvUser->create(pinterface->drvPvt, pasynUser_, drvInfo, 0, 0);
    if (status) {
        throw std::runtime_error(std::string("drvUser->create failed:"));
    }
}

/** Destructor for asynParamClient class
  * Frees all allocated resources 
*/
asynParamClient::~asynParamClient()
{
    if (portName_) free(portName_);
    if (asynInterfaceType_) free(asynInterfaceType_);
    if (drvInfo_) free(drvInfo_);
    if (pasynUser_) pasynManager->freeAsynUser(pasynUser_);
}

/** Reports the properties of this client
*/
void asynParamClient::report(FILE *fp, int details)
{
    fprintf(fp, "\n");
    fprintf(fp, "portName=%s\n", portName_);
    fprintf(fp, "addr=%d\n", addr_);
    fprintf(fp, "asynInterfaceType=%s\n", asynInterfaceType_);
    fprintf(fp, "drvInfo=%s\n", drvInfo_);
    fprintf(fp, "pasynUser=%p\n", pasynUser_);
}


asynPortClient::asynPortClient(const char *portName, double timeout)
{
    pPort_ = (asynPortDriver*)findAsynPortDriver(portName);
    if (!pPort_) {
        throw std::runtime_error(std::string("findAsynPortDriver cannot find port driver: ").append(portName));
    }
  
    paramMaps_ = (paramMap_t**)calloc(pPort_->maxAddr, sizeof(paramMap_t*));
   
    for (int list=0; list<pPort_->maxAddr; list++) {
        int numParams;
        pPort_->getNumParams(list, &numParams);
        paramMap_t *pMap = new paramMap_t;
        paramMaps_[list] = pMap;
        for (int param=0; param<numParams; param++) {
            const char *paramName;
            asynParamType paramType;
            pPort_->getParamName(list, param, &paramName);
            pPort_->getParamType(list, param, &paramType);
            asynParamClient *paramClient;
            switch (paramType) {
              case asynParamInt32:
                  paramClient = (asynParamClient*) new asynInt32Client(portName, list, paramName, timeout);
                  break;
              case asynParamUInt32Digital:
                  paramClient = (asynParamClient*) new asynUInt32DigitalClient(portName, list, paramName, timeout);
                  break;
              case asynParamFloat64:
                  paramClient = (asynParamClient*) new asynFloat64Client(portName, list, paramName, timeout);
                  break;
              case asynParamOctet:
                  paramClient = (asynParamClient*) new asynOctetClient(portName, list, paramName, timeout);
                  break;
              case asynParamInt8Array:
                  paramClient = (asynParamClient*) new asynInt8ArrayClient(portName, list, paramName, timeout);
                  break;
              case asynParamInt16Array:
                  paramClient = (asynParamClient*) new asynInt16ArrayClient(portName, list, paramName, timeout);
                  break;
              case asynParamInt32Array:
                  paramClient = (asynParamClient*) new asynInt32ArrayClient(portName, list, paramName, timeout);
                  break;
              case asynParamFloat32Array:
                  paramClient = (asynParamClient*) new asynFloat32ArrayClient(portName, list, paramName, timeout);
                  break;
              case asynParamFloat64Array:
                  paramClient = (asynParamClient*) new asynFloat64ArrayClient(portName, list, paramName, timeout);
                  break;
              case asynParamGenericPointer:
                  paramClient = (asynParamClient*) new asynGenericPointerClient(portName, list, paramName, timeout);
                  break;
              default:
                  throw std::runtime_error(std::string("asynPortClient unknown paramType for paramName: ").append(paramName));
                  break;

            }
            (*pMap)[paramName] = paramClient;
        }
    }
}

asynPortClient::~asynPortClient()
{
}
    
void asynPortClient::report(FILE *fp, int details)
{
    for (int list=0; list<pPort_->maxAddr; list++) {
        fprintf(fp, "\nasynPortClient list %d\n", list);
        paramMap_t map = *paramMaps_[list];
        paramMap_t::iterator it;
        for (it=map.begin(); it!=map.end(); it++) {
            asynParamClient* pClient = it->second;
            pClient->report(fp, details);
        }
    }
}

asynStatus asynPortClient::write(std::string paramName, epicsInt32 value, int addr)
{
    asynInt32Client *pClient = (asynInt32Client*)(*paramMaps_[addr])[paramName];
    if (strcmp(pClient->getAsynInterfaceType(), asynInt32Type) != 0) {
        throw std::runtime_error(std::string("asynPortClient int32 write incorrect interface ").append(pClient->getAsynInterfaceType()));
    }
    return pClient->write(value);
}

asynStatus asynPortClient::read(std::string paramName, epicsInt32 *value, int addr)
{
    asynInt32Client *pClient = (asynInt32Client*)(*paramMaps_[addr])[paramName];
    if (strcmp(pClient->getAsynInterfaceType(), asynInt32Type) != 0) {
        throw std::runtime_error(std::string("asynPortClient int32 read incorrect interface ").append(pClient->getAsynInterfaceType()));
    }
    return pClient->read(value);
}

asynStatus asynPortClient::write(std::string paramName, epicsFloat64 value, int addr)
{
    asynFloat64Client *pClient = (asynFloat64Client*)(*paramMaps_[addr])[paramName];
    if (strcmp(pClient->getAsynInterfaceType(), asynFloat64Type) != 0) {
        throw std::runtime_error(std::string("asynPortClient float64 write incorrect interface ").append(pClient->getAsynInterfaceType()));
    }
    return pClient->write(value);
}

asynStatus asynPortClient::read(std::string paramName, epicsFloat64 *value, int addr)
{
    asynFloat64Client *pClient = (asynFloat64Client*)(*paramMaps_[addr])[paramName];
    if (strcmp(pClient->getAsynInterfaceType(), asynFloat64Type) != 0) {
        throw std::runtime_error(std::string("asynPortClient float64 read incorrect interface ").append(pClient->getAsynInterfaceType()));
    }
    return pClient->read(value);
}

asynStatus asynPortClient::write(std::string paramName, const char *value, int addr)
{
    asynOctetClient *pClient = (asynOctetClient*)(*paramMaps_[addr])[paramName];
    if (strcmp(pClient->getAsynInterfaceType(), asynOctetType) != 0) {
        throw std::runtime_error(std::string("asynPortClient octet write incorrect interface ").append(pClient->getAsynInterfaceType()));
    }
    return pClient->write(value);
}

asynStatus asynPortClient::read(std::string paramName, char *value, size_t bufferLen, int addr)
{
    asynOctetClient *pClient = (asynOctetClient*)(*paramMaps_[addr])[paramName];
    if (strcmp(pClient->getAsynInterfaceType(), asynOctetType) != 0) {
        throw std::runtime_error(std::string("asynPortClient octet read incorrect interface ").append(pClient->getAsynInterfaceType()));
    }
    size_t nActual;
    int eomReason;
    return pClient->read(value, bufferLen, &nActual, &eomReason);
}

asynParamClient* asynPortClient::getParamClient(std::string paramName, int addr)
{
    return (*paramMaps_[addr])[paramName];
}
