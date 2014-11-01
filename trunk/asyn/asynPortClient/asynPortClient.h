#ifndef asynPortClient_H
#define asynPortClient_H

#include <stdexcept>

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
#include <asynInt8Array.h>
#include <asynInt8ArraySyncIO.h>
#include <asynInt16Array.h>
#include <asynInt16ArraySyncIO.h>
#include <asynInt32Array.h>
#include <asynInt32ArraySyncIO.h>
#include <asynFloat32Array.h>
#include <asynFloat32ArraySyncIO.h>
#include <asynFloat64Array.h>
#include <asynFloat64ArraySyncIO.h>
#include <asynGenericPointer.h>
#include <asynGenericPointerSyncIO.h>
#include <asynEnum.h>
#include <asynEnumSyncIO.h>
#include <asynOption.h>
#include <asynOptionSyncIO.h>
#include <asynCommonSyncIO.h>

#define DEFAULT_TIMEOUT 1.0


class epicsShareClass asynClient {
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


class epicsShareClass asynInt32Client : public asynClient {
public:
    asynInt32Client(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynClient(portName, addr, asynInt32Type, drvInfo, timeout) {
        pInterface_ = (asynInt32 *)pasynInterface_->pinterface;
        if (pasynInt32SyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo)) 
            throw std::runtime_error(std::string("pasynInt32SyncIO->connect failed"));
    };
    virtual ~asynInt32Client() { 
        pasynInt32SyncIO->disconnect(pasynUserSyncIO_); 
    }; 
    virtual asynStatus read(epicsInt32 *value) { 
        return pasynInt32SyncIO->read(pasynUserSyncIO_, value, timeout_);
    };
    virtual asynStatus write(epicsInt32 value) { 
        return pasynInt32SyncIO->write(pasynUserSyncIO_, value, timeout_); 
    };
    virtual asynStatus getBounds(epicsInt32 *low, epicsInt32 *high) { 
        return pasynInt32SyncIO->getBounds(pasynUserSyncIO_, low, high); 
    };
    virtual asynStatus registerInterruptUser(interruptCallbackInt32 pCallback) { 
        return pInterface_->registerInterruptUser(pasynInterface_->drvPvt, pasynUser_,
                                                  pCallback, this, &interruptPvt_); 
    };
private:
    asynInt32 *pInterface_;
};


class epicsShareClass asynUInt32DigitalClient : public asynClient {
public:
    asynUInt32DigitalClient(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynClient(portName, addr, asynUInt32DigitalType, drvInfo, timeout) {
        pInterface_ = (asynUInt32Digital *)pasynInterface_->pinterface;
        if (pasynInt32SyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo))
            throw std::runtime_error(std::string("pasynInt32SyncIO->connect failed"));
    };
    virtual ~asynUInt32DigitalClient() {
        pasynUInt32DigitalSyncIO->disconnect(pasynUserSyncIO_); 
    }; 
    virtual asynStatus read(epicsUInt32 *value, epicsUInt32 mask) { 
        return pasynUInt32DigitalSyncIO->read(pasynUserSyncIO_, value, mask, timeout_); 
    };
    virtual asynStatus write(epicsUInt32 value, epicsUInt32 mask){ 
        return pasynUInt32DigitalSyncIO->write(pasynUserSyncIO_, value, mask, timeout_); 
    };
    virtual asynStatus setInterrupt(epicsUInt32 mask, interruptReason reason) { 
        return pasynUInt32DigitalSyncIO->setInterrupt(pasynUserSyncIO_, mask, reason, timeout_); 
    };
    virtual asynStatus clearInterrupt(epicsUInt32 mask) { 
        return pasynUInt32DigitalSyncIO->clearInterrupt(pasynUserSyncIO_, mask, timeout_); 
    };
    virtual asynStatus getInterrupt(epicsUInt32 *mask, interruptReason reason) { 
        return pasynUInt32DigitalSyncIO->getInterrupt(pasynUserSyncIO_, mask, reason, timeout_); 
    };
    virtual asynStatus registerInterruptUser(interruptCallbackUInt32Digital pCallback, epicsUInt32 mask) { 
        return pInterface_->registerInterruptUser(pasynInterface_->drvPvt, pasynUser_,
                                                  pCallback, this, mask, &interruptPvt_); 
    };
private:
    asynUInt32Digital *pInterface_;
};


class epicsShareClass asynFloat64Client : public asynClient {
public:
    asynFloat64Client(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynClient(portName, addr, asynFloat64Type, drvInfo, timeout) {
        pInterface_ = (asynFloat64 *)pasynInterface_->pinterface;
        if (pasynFloat64SyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo))
            throw std::runtime_error(std::string("pasynFloat64SyncIO->connect failed"));
    };
    virtual ~asynFloat64Client() { 
        pasynFloat64SyncIO->disconnect(pasynUserSyncIO_); 
    }; 
    virtual asynStatus read(epicsFloat64 *value) {
        return pasynFloat64SyncIO->read(pasynUserSyncIO_, value, timeout_); 
    };
    virtual asynStatus write(epicsFloat64 value) { 
        return pasynFloat64SyncIO->write(pasynUserSyncIO_, value, timeout_); 
    };
    virtual asynStatus registerInterruptUser(interruptCallbackFloat64 pCallback) { 
        return pInterface_->registerInterruptUser(pasynInterface_->drvPvt, pasynUser_,
                                                  pCallback, this, &interruptPvt_); 
    };
private:
    asynFloat64 *pInterface_;
};


class epicsShareClass asynOctetClient : public asynClient {
public:
    asynOctetClient(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynClient(portName, addr, asynOctetType, drvInfo, timeout) {
        pInterface_ = (asynOctet *)pasynInterface_->pinterface;
        if (pasynOctetSyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo)) 
            throw std::runtime_error(std::string("pasynOctetSyncIO->connect failed"));
    };
    virtual ~asynOctetClient() { 
        pasynOctetSyncIO->disconnect(pasynUserSyncIO_); 
    }; 
    virtual asynStatus write(const char *buffer, size_t bufferLen, size_t *nActual) { 
        return pasynOctetSyncIO->write(pasynUserSyncIO_, buffer, bufferLen, timeout_, nActual); 
    };
    virtual asynStatus read(char *buffer, size_t bufferLen, size_t *nActual, int *eomReason) {
        return pasynOctetSyncIO->read(pasynUserSyncIO_, buffer, bufferLen, timeout_, nActual, eomReason); 
    };
    virtual asynStatus writeRead(const char *writeBuffer, size_t writeBufferLen, char *readBuffer, size_t readBufferLen, 
                                 size_t *nBytesOut, size_t *nBytesIn, int *eomReason) { 
        return pasynOctetSyncIO->writeRead(pasynUserSyncIO_, writeBuffer, writeBufferLen, readBuffer, readBufferLen,
                                           timeout_, nBytesOut, nBytesIn, eomReason); 
    };
    virtual asynStatus flush() { 
        return pasynOctetSyncIO->flush(pasynUserSyncIO_); 
    };
    virtual asynStatus setInputEos(const char *eos, int eosLen) { 
        return pasynOctetSyncIO->setInputEos(pasynUserSyncIO_, eos, eosLen); 
    };
    virtual asynStatus getInputEos(char *eos, int eosSize, int *eosLen) { 
        return pasynOctetSyncIO->getInputEos(pasynUserSyncIO_, eos, eosSize, eosLen); 
    };
    virtual asynStatus setOutputEos(const char *eos, int eosLen) { 
        return pasynOctetSyncIO->setOutputEos(pasynUserSyncIO_, eos, eosLen); 
    };
    virtual asynStatus getOutputEos(char *eos, int eosSize, int *eosLen) { 
        return pasynOctetSyncIO->getOutputEos(pasynUserSyncIO_, eos, eosSize, eosLen); 
    };
    virtual asynStatus registerInterruptUser(interruptCallbackOctet pCallback) { 
        return pInterface_->registerInterruptUser(pasynInterface_->drvPvt, pasynUser_,
                                                  pCallback, this, &interruptPvt_); 
    };
private:
    asynOctet *pInterface_;
};
  

class epicsShareClass asynInt8ArrayClient : public asynClient {
public:
    asynInt8ArrayClient(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynClient(portName, addr, asynInt8ArrayType, drvInfo, timeout) {
        pInterface_ = (asynInt8Array *)pasynInterface_->pinterface;
        if (pasynInt8ArraySyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo)) 
            throw std::runtime_error(std::string("pasynInt8ArraySyncIO->connect failed"));
    };
    virtual ~asynInt8ArrayClient() {
        pasynInt8ArraySyncIO->disconnect(pasynUserSyncIO_);
    };
    virtual asynStatus read(epicsInt8 *value, size_t nElements, size_t *nIn) {
        return pasynInt8ArraySyncIO->read(pasynUserSyncIO_, value, nElements, nIn, timeout_);
    };
    virtual asynStatus write(epicsInt8 *value, size_t nElements) {
        return pasynInt8ArraySyncIO->write(pasynUserSyncIO_, value, nElements, timeout_);
    };
    virtual asynStatus registerInterruptUser(interruptCallbackInt8Array pCallback) { 
        return pInterface_->registerInterruptUser(pasynInterface_->drvPvt, pasynUser_,
                                                      pCallback, this, &interruptPvt_); 
    };
private:
    asynInt8Array *pInterface_;
};


class epicsShareClass asynInt16ArrayClient : public asynClient {
public:
    asynInt16ArrayClient(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynClient(portName, addr, asynInt16ArrayType, drvInfo, timeout) {
        pInterface_ = (asynInt16Array *)pasynInterface_->pinterface;
        if (pasynInt16ArraySyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo)) 
            throw std::runtime_error(std::string("pasynInt16ArraySyncIO->connect failed"));
    };
    virtual ~asynInt16ArrayClient() {
        pasynInt16ArraySyncIO->disconnect(pasynUserSyncIO_);
    };
    virtual asynStatus read(epicsInt16 *value, size_t nElements, size_t *nIn) {
        return pasynInt16ArraySyncIO->read(pasynUserSyncIO_, value, nElements, nIn, timeout_);
    };
    virtual asynStatus write(epicsInt16 *value, size_t nElements) {
        return pasynInt16ArraySyncIO->write(pasynUserSyncIO_, value, nElements, timeout_);
    };
    virtual asynStatus registerInterruptUser(interruptCallbackInt16Array pCallback) { 
        return pInterface_->registerInterruptUser(pasynInterface_->drvPvt, pasynUser_,
                                                       pCallback, this, &interruptPvt_); 
    };
private:
    asynInt16Array *pInterface_;
};


class epicsShareClass asynInt32ArrayClient : public asynClient {
public:
    asynInt32ArrayClient(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynClient(portName, addr, asynInt32ArrayType, drvInfo, timeout) {
        pInterface_ = (asynInt32Array *)pasynInterface_->pinterface;
        if (pasynInt32ArraySyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo)) 
            throw std::runtime_error(std::string("pasynInt32ArraySyncIO->connect failed"));
    };
    virtual ~asynInt32ArrayClient() {
        pasynInt32ArraySyncIO->disconnect(pasynUserSyncIO_);
    };
    virtual asynStatus read(epicsInt32 *value, size_t nElements, size_t *nIn) {
        return pasynInt32ArraySyncIO->read(pasynUserSyncIO_, value, nElements, nIn, timeout_);
    };
    virtual asynStatus write(epicsInt32 *value, size_t nElements) {
        return pasynInt32ArraySyncIO->write(pasynUserSyncIO_, value, nElements, timeout_);
    };
    virtual asynStatus registerInterruptUser(interruptCallbackInt32Array pCallback) { 
        return pInterface_->registerInterruptUser(pasynInterface_->drvPvt, pasynUser_,
                                                  pCallback, this, &interruptPvt_); 
    };
private:
    asynInt32Array *pInterface_;
};

class epicsShareClass asynFloat32ArrayClient : public asynClient {
public:
    asynFloat32ArrayClient(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynClient(portName, addr, asynFloat32ArrayType, drvInfo, timeout) {
        pInterface_ = (asynFloat32Array *)pasynInterface_->pinterface;
        if (pasynFloat32ArraySyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo)) 
            throw std::runtime_error(std::string("pasynFloat64ArraySyncIO->connect failed"));
    };
    virtual ~asynFloat32ArrayClient() {
        pasynFloat32ArraySyncIO->disconnect(pasynUserSyncIO_);
    };
    virtual asynStatus read(epicsFloat32 *value, size_t nElements, size_t *nIn) {
        return pasynFloat32ArraySyncIO->read(pasynUserSyncIO_, value, nElements, nIn, timeout_);
    };
    virtual asynStatus write(epicsFloat32 *value, size_t nElements) {
        return pasynFloat32ArraySyncIO->write(pasynUserSyncIO_, value, nElements, timeout_);
    };
    virtual asynStatus registerInterruptUser(interruptCallbackFloat32Array pCallback) { 
        return pInterface_->registerInterruptUser(pasynInterface_->drvPvt, pasynUser_,
                                                  pCallback, this, &interruptPvt_); 
    };
private:
    asynFloat32Array *pInterface_;
};


class epicsShareClass asynFloat64ArrayClient : public asynClient {
public:
    asynFloat64ArrayClient(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynClient(portName, addr, asynFloat64ArrayType, drvInfo, timeout) {
        pInterface_ = (asynFloat64Array *)pasynInterface_->pinterface;
        if (pasynFloat64ArraySyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo)) 
            throw std::runtime_error(std::string("pasynFloat64ArraySyncIO->connect failed"));
    };
    virtual ~asynFloat64ArrayClient() {
        pasynFloat64ArraySyncIO->disconnect(pasynUserSyncIO_);
    };
    virtual asynStatus read(epicsFloat64 *value, size_t nElements, size_t *nIn) {
        return pasynFloat64ArraySyncIO->read(pasynUserSyncIO_, value, nElements, nIn, timeout_);
    };
    virtual asynStatus write(epicsFloat64 *value, size_t nElements) {
        return pasynFloat64ArraySyncIO->write(pasynUserSyncIO_, value, nElements, timeout_);
    };
    virtual asynStatus registerInterruptUser(interruptCallbackFloat64Array pCallback) { 
        return pInterface_->registerInterruptUser(pasynInterface_->drvPvt, pasynUser_,
                                                  pCallback, this, &interruptPvt_); 
    };
private:
    asynFloat64Array *pInterface_;
};


class epicsShareClass asynGenericPointerClient : public asynClient {
public:
    asynGenericPointerClient(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynClient(portName, addr, asynGenericPointerType, drvInfo, timeout) {
        pInterface_ = (asynGenericPointer *)pasynInterface_->pinterface;
        if (pasynGenericPointerSyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo)) 
            throw std::runtime_error(std::string("pasynGenericPointerSyncIO->connect failed"));
    };
    virtual ~asynGenericPointerClient() {
        pasynGenericPointerSyncIO->disconnect(pasynUserSyncIO_);
    };
    virtual asynStatus read(void *pointer) {
        return pasynGenericPointerSyncIO->read(pasynUserSyncIO_, pointer, timeout_);
    };
    virtual asynStatus write(void *pointer) {
        return pasynGenericPointerSyncIO->write(pasynUserSyncIO_, pointer, timeout_);
    };
    virtual asynStatus registerInterruptUser(interruptCallbackGenericPointer pCallback) { 
        return pInterface_->registerInterruptUser(pasynInterface_->drvPvt, pasynUser_,
                                                  pCallback, this, &interruptPvt_); 
    };
private:
    asynGenericPointer *pInterface_;
};


class epicsShareClass asynOptionClient : public asynClient {
public:
    asynOptionClient(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynClient(portName, addr, asynOptionType, drvInfo, timeout) {
        pInterface_ = (asynOption *)pasynInterface_->pinterface;
        if (pasynOptionSyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo)) 
            throw std::runtime_error(std::string("pasynOptionSyncIO->connect failed"));
    };
    virtual ~asynOptionClient() {
        pasynOptionSyncIO->disconnect(pasynUserSyncIO_);
    };
    virtual asynStatus getOption(const char *key, char *value, int maxChars) {
        return pasynOptionSyncIO->getOption(pasynUserSyncIO_, key, value, maxChars, timeout_);
    };
    virtual asynStatus setOption(const char *key, const char *value) {
        return pasynOptionSyncIO->setOption(pasynUserSyncIO_, key, value, timeout_);
    };
private:
    asynOption *pInterface_;
};


class epicsShareClass asynEnumClient : public asynClient {
public:
    asynEnumClient(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynClient(portName, addr, asynEnumType, drvInfo, timeout) {
        pInterface_ = (asynEnum *)pasynInterface_->pinterface;
        if (pasynEnumSyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo)) 
            throw std::runtime_error(std::string("pasynEnumSyncIO->connect failed"));
    };
    virtual ~asynEnumClient() {
        pasynEnumSyncIO->disconnect(pasynUserSyncIO_);
    };
    virtual asynStatus read(char *strings[], int values[], int severities[], size_t nElements, size_t *nIn) {
        return pasynEnumSyncIO->read(pasynUserSyncIO_, strings, values, severities, nElements, nIn, timeout_);
    };
    virtual asynStatus write(char *strings[], int values[], int severities[], size_t nElements) {
        return pasynEnumSyncIO->write(pasynUserSyncIO_, strings, values, severities, nElements, timeout_);
    };
private:
    asynEnum *pInterface_;
};


class epicsShareClass asynCommonClient : public asynClient {
public:
    asynCommonClient(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynClient(portName, addr, asynCommonType, drvInfo, timeout) {
        pInterface_ = (asynCommon *)pasynInterface_->pinterface;
        if (pasynCommonSyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo)) 
            throw std::runtime_error(std::string("pasynCommonSyncIO->connect failed"));
    };
    virtual ~asynCommonClient() {
        pasynCommonSyncIO->disconnect(pasynUserSyncIO_);
    };
    virtual void report(FILE *fp, int details) {
        pasynCommonSyncIO->report(pasynUserSyncIO_, fp, details);
    };
    virtual asynStatus connect() {
        return pasynCommonSyncIO->connectDevice(pasynUserSyncIO_);
    };
    virtual asynStatus disconnect() {
        return pasynCommonSyncIO->disconnectDevice(pasynUserSyncIO_);
    };
private:
    asynCommon *pInterface_;
};  
    
#endif
