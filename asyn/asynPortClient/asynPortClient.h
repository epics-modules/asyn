#ifndef asynPortClient_H
#define asynPortClient_H

#include <epicsTypes.h>
#include <epicsString.h>

#include "asynDriver.h"
#include "asynUInt32Digital.h"

class asynClient {
public:
    asynClient(const char *portName, int addr, const char* asynInterface, const char *drvInfo, double timeout);
    virtual ~asynClient();
    void setTimeout(double timeout);
    void report(FILE *fp, int details);
protected:
    asynUser *pasynUser_;
    double timeout_;
    char *portName_;
    int addr_;
    char *asynInterface_;
    char *drvInfo_;
};

class asynInt32Client : public asynClient {
public:
    asynInt32Client(const char *portName, int addr, const char *drvInfo, double timeout);
    virtual ~asynInt32Client();
    virtual asynStatus read(epicsInt32 *value);
    virtual asynStatus write(epicsInt32 value);
    virtual asynStatus getBounds(epicsInt32 *low, epicsInt32 *high);
};

class asynUInt32DigitalClient : public asynClient {
public:
    asynUInt32DigitalClient(const char *portName, int addr, const char *drvInfo);
    virtual ~asynUInt32DigitalClient();
    virtual asynStatus read(epicsUInt32 *value, epicsUInt32 mask);
    virtual asynStatus write(epicsUInt32 value, epicsUInt32 mask);
    virtual asynStatus setInterrupt(epicsUInt32 mask, interruptReason reason);
    virtual asynStatus clearInterrupt(epicsUInt32 mask);
    virtual asynStatus getInterrupt(epicsUInt32 *mask, interruptReason reason);
};

class asynFloat64Client : public asynClient {
public:
    asynFloat64Client(const char *portName, int addr, const char *drvInfo);
    virtual ~asynFloat64Client();
    virtual asynStatus read(epicsFloat64 *value);
    virtual asynStatus write(epicsFloat64 value);
};

class asynOctetClient : public asynClient {
public:
    asynOctetClient(const char *portName, int addr, const char *drvInfo);
    virtual ~asynOctetClient();
    virtual asynStatus read(char *value, size_t maxChars, size_t *nActual, int *eomReason);
    virtual asynStatus write(asynUser *pasynUser, const char *value, size_t maxChars,
                                        size_t *nActual);
    virtual asynStatus flush();
    virtual asynStatus setInputEos(const char *eos, int eosLen);
    virtual asynStatus getInputEos(char *eos, int eosSize, int *eosLen);
    virtual asynStatus setOutputEos(const char *eos, int eosLen);
    virtual asynStatus getOutputEos(char *eos, int eosSize, int *eosLen);
};
  
class asynInt8ArrayClient : public asynClient {
public:
    asynInt8ArrayClient(const char *portName, int addr, const char *drvInfo);
    virtual ~asynInt8ArrayClient();
    virtual asynStatus read(epicsInt8 *value, size_t nElements, size_t *nIn);
    virtual asynStatus write(epicsInt8 *value, size_t nElements);
};

class asynInt16ArrayClient : public asynClient {
public:
    asynInt16ArrayClient(const char *portName, int addr, const char *drvInfo);
    virtual ~asynInt16ArrayClient();
    virtual asynStatus read(epicsInt16 *value, size_t nElements, size_t *nIn);
    virtual asynStatus write(epicsInt16 *value, size_t nElements);
};

class asynInt32ArrayClient : public asynClient {
public:
    asynInt32ArrayClient(const char *portName, int addr, const char *drvInfo);
    virtual ~asynInt32ArrayClient();
    virtual asynStatus read(epicsInt32 *value, size_t nElements, size_t *nIn);
    virtual asynStatus write(epicsInt32 *value, size_t nElements);
};

class asynFloat32ArrayClient : public asynClient {
public:
    asynFloat32ArrayClient(const char *portName, int addr, const char *drvInfo);
    virtual ~asynFloat32ArrayClient();
    virtual asynStatus read(epicsFloat32 *value, size_t nElements, size_t *nIn);
    virtual asynStatus write(epicsFloat32 *value, size_t nElements);
};

class asynFloat64ArrayClient : public asynClient {
public:
    asynFloat64ArrayClient(const char *portName, int addr, const char *drvInfo);
    virtual ~asynFloat64ArrayClient();
    virtual asynStatus read(epicsFloat64 *value, size_t nElements, size_t *nIn);
    virtual asynStatus write(epicsFloat64 *value, size_t nElements);
};

class asynGenericPointerClient : public asynClient {
public:
    asynGenericPointerClient(const char *portName, int addr, const char *drvInfo);
    virtual ~asynGenericPointerClient();
    virtual asynStatus read(void *pointer);
    virtual asynStatus write(void *pointer);
};

class asynOptionClient : public asynClient {
public:
    asynOptionClient(const char *portName, int addr, const char *drvInfo);
    virtual ~asynOptionClient();
    virtual asynStatus read(const char *key, char *value, int maxChars);
    virtual asynStatus write(const char *key, const char *value);
};

class asynEnumClient : public asynClient {
public:
    asynEnumClient(const char *portName, int addr, const char *drvInfo);
    virtual ~asynEnumClient();
    virtual asynStatus read(char *strings[], int values[], int severities[], size_t nElements, size_t *nIn);
    virtual asynStatus write(char *strings[], int values[], int severities[], size_t nElements);
};

class asynCommonClient : public asynClient {
public:
    asynCommonClient(const char *portName, int addr, const char *drvInfo);
    virtual ~asynCommonClient();
    virtual void report(FILE *fp, int details);
    virtual asynStatus connect();
    virtual asynStatus disconnect();
};  
    
#endif
