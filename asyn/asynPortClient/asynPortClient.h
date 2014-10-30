#ifndef asynPortClient_H
#define asynPortClient_H

#include <epicsTypes.h>
#include <epicsString.h>

#include <asynDriver.h>
#include <asynInt32.h>
#include <asynInt32SyncIO.h>
#include <asynUInt32Digital.h>
#include <asynUInt32DigitalSyncIO.h>
#include <asynFloat64.h>
#include <asynFloat64SyncIO.h>
#include <asynOctet.h>
#include <asynOctetSyncIO.h>

class asynClient {
public:
    asynClient(const char *portName, int addr, const char* asynInterfaceType, const char *drvInfo, double timeout);
    virtual ~asynClient();
    void setTimeout(double timeout)
        { timeout_ = timeout; };
    void report(FILE *fp, int details);
protected:
    asynUser *pasynUser_;
    asynUser *pasynUserSyncIO_;
    asynInterface *pasynInterface_;
    double timeout_;
    char *portName_;
    int addr_;
    char *asynInterfaceType_;
    char *drvInfo_;
    void *drvPvt;
    void *interruptPvt_;
};

class asynInt32Client : public asynClient {
public:
    asynInt32Client(const char *portName, int addr, const char *drvInfo, double timeout);
    virtual ~asynInt32Client()
        { pasynInt32SyncIO->disconnect(pasynUserSyncIO_); }; 
    virtual asynStatus read(epicsInt32 *value) 
        { return pasynInt32SyncIO->read(pasynUserSyncIO_, value, timeout_); };
    virtual asynStatus write(epicsInt32 value)
        { return pasynInt32SyncIO->write(pasynUserSyncIO_, value, timeout_); };
    virtual asynStatus getBounds(epicsInt32 *low, epicsInt32 *high)
        { return pasynInt32SyncIO->getBounds(pasynUserSyncIO_, low, high); };
    virtual asynStatus registerInterruptUser(interruptCallbackInt32 pCallback)
        { return pasynInt32_->registerInterruptUser(pasynInterface_->drvPvt, pasynUser_,
                                                    pCallback, this, &interruptPvt_); };
private:
    asynInt32 *pasynInt32_;
};

class asynUInt32DigitalClient : public asynClient {
public:
    asynUInt32DigitalClient(const char *portName, int addr, const char *drvInfo);
    virtual ~asynUInt32DigitalClient()
        { pasynUInt32DigitalSyncIO->disconnect(pasynUserSyncIO_); }; 
    virtual asynStatus read(epicsUInt32 *value, epicsUInt32 mask)
        { return pasynUInt32DigitalSyncIO->read(pasynUserSyncIO_, value, mask, timeout_); };
    virtual asynStatus write(epicsUInt32 value, epicsUInt32 mask)
        { return pasynUInt32DigitalSyncIO->write(pasynUserSyncIO_, value, mask, timeout_); };
    virtual asynStatus setInterrupt(epicsUInt32 mask, interruptReason reason)
        { return pasynUInt32DigitalSyncIO->setInterrupt(pasynUserSyncIO_, mask, reason, timeout_); };
    virtual asynStatus clearInterrupt(epicsUInt32 mask)
        { return pasynUInt32DigitalSyncIO->clearInterrupt(pasynUserSyncIO_, mask, timeout_); };
    virtual asynStatus getInterrupt(epicsUInt32 *mask, interruptReason reason)
        { return pasynUInt32DigitalSyncIO->getInterrupt(pasynUserSyncIO_, mask, reason, timeout_); };
    virtual asynStatus registerInterruptUser(interruptCallbackUInt32Digital pCallback, epicsUInt32 mask)
        { return pasynUInt32Digital_->registerInterruptUser(pasynInterface_->drvPvt, pasynUser_,
                                                    pCallback, this, mask, &interruptPvt_); };
private:
    asynUInt32Digital *pasynUInt32Digital_;
};

class asynFloat64Client : public asynClient {
public:
    asynFloat64Client(const char *portName, int addr, const char *drvInfo);
    virtual ~asynFloat64Client()
        { pasynFloat64SyncIO->disconnect(pasynUserSyncIO_); }; 
    virtual asynStatus read(epicsFloat64 *value)
        { return pasynFloat64SyncIO->read(pasynUserSyncIO_, value, timeout_); };
    virtual asynStatus write(epicsFloat64 value)
        { return pasynFloat64SyncIO->write(pasynUserSyncIO_, value, timeout_); };
    virtual asynStatus registerInterruptUser(interruptCallbackFloat64 pCallback)
        { return pasynFloat64_->registerInterruptUser(pasynInterface_->drvPvt, pasynUser_,
                                                    pCallback, this, &interruptPvt_); };
private:
    asynFloat64 *pasynFloat64_;
};

class asynOctetClient : public asynClient {
public:
    asynOctetClient(const char *portName, int addr, const char *drvInfo);
    virtual ~asynOctetClient()
        { pasynOctetSyncIO->disconnect(pasynUserSyncIO_); }; 
    virtual asynStatus read(char *value, size_t maxChars, size_t *nActual, int *eomReason)
        { return pasynOctetSyncIO->read(pasynUserSyncIO_, value, maxChars, nActual, eomReason, timeout_); };
    virtual asynStatus write(const char *value, size_t maxChars, size_t *nActual)
        { return pasynOctetSyncIO->write(pasynUserSyncIO_, value, maxChars, nActual, timeout_); };
    virtual asynStatus flush()
        { return pasynOctetSyncIO->flush(pasynUserSyncIO_, timeout_); };
    virtual asynStatus setInputEos(const char *eos, int eosLen)
        { return pasynOctetSyncIO->setInputEos(pasynUserSyncIO_, eos, eosLen timeout_); };
    virtual asynStatus getInputEos(char *eos, int eosSize, int *eosLen)
        { return pasynOctetSyncIO->getInputEos(pasynUserSyncIO_, eos, eosSize, eosLen timeout_); };
    virtual asynStatus setOutputEos(const char *eos, int eosLen)
        { return pasynOctetSyncIO->setOutputEos(pasynUserSyncIO_, eos, eosLen timeout_); };
    virtual asynStatus getOutputEos(char *eos, int eosSize, int *eosLen)
        { return pasynOctetSyncIO->getOutputEos(pasynUserSyncIO_, eos, eosSize, eosLen timeout_); };
private:
    asynOctet *pasynOctet_;
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
