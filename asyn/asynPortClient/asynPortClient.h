#ifndef asynPortClient_H
#define asynPortClient_H

#include <stdexcept>
#include <string>

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
#include <asynDrvUser.h>

#define DEFAULT_TIMEOUT 1.0


/** Base class for asyn port clients; handles most of the bookkeeping for writing an asyn port client
  * with standard asyn interfaces. */
class epicsShareClass asynPortClient {
public:
    asynPortClient(const char *portName, int addr, const char* asynInterfaceType, const char *drvInfo, double timeout);
    virtual ~asynPortClient();
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


/** Class for asyn port clients to communicate on the asynInt32 interface */
class epicsShareClass asynInt32Client : public asynPortClient {
public:
    /** Constructor for asynInt32Client class
      * \param[in] portName  The name of the asyn port to connect to
      * \param[in] addr      The address on the asyn port to connect to
      * \param[in] drvInfo   The drvInfo string to identify which property of the port is being connected to
      * \param[in] timeout   The default timeout for all communications between the client and the port driver 
    */
    asynInt32Client(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynPortClient(portName, addr, asynInt32Type, drvInfo, timeout) {
        pInterface_ = (asynInt32 *)pasynInterface_->pinterface;
        if (pasynInt32SyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo)) 
            throw std::runtime_error(std::string("pasynInt32SyncIO->connect failed"));
    };
    /** Destructor for asynInt32Client class.  Disconnects from port, frees resources. */
    virtual ~asynInt32Client() { 
        if (pInterface_ && interruptPvt_)
            pInterface_->cancelInterruptUser(pasynInterface_->drvPvt, pasynUser_, interruptPvt_);
        pasynInt32SyncIO->disconnect(pasynUserSyncIO_); 
    }; 
    /** Reads an epicsInt32 value from the port driver
      * \param[out] value  The value read from the port driver */
    virtual asynStatus read(epicsInt32 *value) { 
        return pasynInt32SyncIO->read(pasynUserSyncIO_, value, timeout_);
    };
    /** Writes an epicsInt32 value to the port driver
      * \param[in] value  The value to write to the port driver */
    virtual asynStatus write(epicsInt32 value) { 
        return pasynInt32SyncIO->write(pasynUserSyncIO_, value, timeout_); 
    };
    /** Returns the lower and upper limits of the range of values from the port driver
      * \param[out] low   The low limit
      * \param[out] high  The high limit */
    virtual asynStatus getBounds(epicsInt32 *low, epicsInt32 *high) { 
        return pasynInt32SyncIO->getBounds(pasynUserSyncIO_, low, high); 
    };
    /** Registers an interruptCallbackInt32 function that the driver will call when there is a new value
      * \param[in] pCallback  The address of the callback function */
    virtual asynStatus registerInterruptUser(interruptCallbackInt32 pCallback) { 
        if(interruptPvt_!=NULL) return asynError;
        return pInterface_->registerInterruptUser(pasynInterface_->drvPvt, pasynUser_,
                                                  pCallback, this, &interruptPvt_); 
    };
private:
    asynInt32 *pInterface_;
};


/** Class for asyn port clients to communicate on the asynUInt32Digital interface */
class epicsShareClass asynUInt32DigitalClient : public asynPortClient {
public:
    /** Constructor for asynUInt32DigitalClient class
      * \param[in] portName  The name of the asyn port to connect to
      * \param[in] addr      The address on the asyn port to connect to
      * \param[in] drvInfo   The drvInfo string to identify which property of the port is being connected to
      * \param[in] timeout   The default timeout for all communications between the client and the port driver 
    */
    asynUInt32DigitalClient(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynPortClient(portName, addr, asynUInt32DigitalType, drvInfo, timeout) {
        pInterface_ = (asynUInt32Digital *)pasynInterface_->pinterface;
        if (pasynUInt32DigitalSyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo))
            throw std::runtime_error(std::string("pasynInt32SyncIO->connect failed"));
    };
    /** Destructor for asynInt32Client class.  Disconnects from port, frees resources. */
    virtual ~asynUInt32DigitalClient() {
        pasynUInt32DigitalSyncIO->disconnect(pasynUserSyncIO_); 
    }; 
    /** Reads an epicsUInt32 value from the port driver
      * \param[out] value  The value read from the port driver 
      * \param[in]  mask   The mask to use when reading the value */
    virtual asynStatus read(epicsUInt32 *value, epicsUInt32 mask) { 
        return pasynUInt32DigitalSyncIO->read(pasynUserSyncIO_, value, mask, timeout_); 
    };
    /** Writes an epicsUInt32 value to the port driver
      * \param[in] value  The value to write to the port driver
      * \param[in] mask   The mask to use when writing the value */
    virtual asynStatus write(epicsUInt32 value, epicsUInt32 mask){ 
        return pasynUInt32DigitalSyncIO->write(pasynUserSyncIO_, value, mask, timeout_); 
    };
    /** Sets the interrupt mask for the specified interrupt reason in the driver
      * \param[in] mask   The interrupt mask
      * \param[in] reason The interrupt reason */
    virtual asynStatus setInterrupt(epicsUInt32 mask, interruptReason reason) { 
        return pasynUInt32DigitalSyncIO->setInterrupt(pasynUserSyncIO_, mask, reason, timeout_); 
    };
    /** Clears the interrupt mask in the driver
      * \param[in] mask  The interrupt mask */
    virtual asynStatus clearInterrupt(epicsUInt32 mask) { 
        return pasynUInt32DigitalSyncIO->clearInterrupt(pasynUserSyncIO_, mask, timeout_); 
    };
    /** Gets the current interrupt mask for the specified reason from the driver
      * \param[out] mask   The interrupt mask
      * \param[in]  reason The interrupt reason */
    virtual asynStatus getInterrupt(epicsUInt32 *mask, interruptReason reason) { 
        return pasynUInt32DigitalSyncIO->getInterrupt(pasynUserSyncIO_, mask, reason, timeout_); 
    };
    /** Registers an interruptCallbackUInt32Digital function that the driver will call when there is a new value
      * \param[in] pCallback  The address of the callback function
      * \param[in] mask       The mask to use when determining whether to do the callback */
    virtual asynStatus registerInterruptUser(interruptCallbackUInt32Digital pCallback, epicsUInt32 mask) { 
        return pInterface_->registerInterruptUser(pasynInterface_->drvPvt, pasynUser_,
                                                  pCallback, this, mask, &interruptPvt_); 
    };
private:
    asynUInt32Digital *pInterface_;
};


/** Class for asyn port clients to communicate on the asynFloat64 interface */
class epicsShareClass asynFloat64Client : public asynPortClient {
public:
    /** Constructor for asynFloat64Client class
      * \param[in] portName  The name of the asyn port to connect to
      * \param[in] addr      The address on the asyn port to connect to
      * \param[in] drvInfo   The drvInfo string to identify which property of the port is being connected to
      * \param[in] timeout   The default timeout for all communications between the client and the port driver 
    */
    asynFloat64Client(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynPortClient(portName, addr, asynFloat64Type, drvInfo, timeout) {
        pInterface_ = (asynFloat64 *)pasynInterface_->pinterface;
        if (pasynFloat64SyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo))
            throw std::runtime_error(std::string("pasynFloat64SyncIO->connect failed"));
    };
    /** Destructor for asynFloat64Client class.  Disconnects from port, frees resources. */
    virtual ~asynFloat64Client() { 
        pasynFloat64SyncIO->disconnect(pasynUserSyncIO_); 
    }; 
    /** Reads an epicsFloat64 value from the port driver
      * \param[out] value  The value read from the port driver */
    virtual asynStatus read(epicsFloat64 *value) {
        return pasynFloat64SyncIO->read(pasynUserSyncIO_, value, timeout_); 
    };
    /** Writes an epicsFloat64 value to the port driver
      * \param[in] value  The value to write to the port driver */
    virtual asynStatus write(epicsFloat64 value) { 
        return pasynFloat64SyncIO->write(pasynUserSyncIO_, value, timeout_); 
    };
    /** Registers an interruptCallbackFloat64 function that the driver will call when there is a new value
      * \param[in] pCallback  The address of the callback function */
    virtual asynStatus registerInterruptUser(interruptCallbackFloat64 pCallback) { 
        return pInterface_->registerInterruptUser(pasynInterface_->drvPvt, pasynUser_,
                                                  pCallback, this, &interruptPvt_); 
    };
private:
    asynFloat64 *pInterface_;
};


/** Class for asyn port clients to communicate on the asynOctet interface */
class epicsShareClass asynOctetClient : public asynPortClient {
public:
    /** Constructor for asynOctetClient class
      * \param[in] portName  The name of the asyn port to connect to
      * \param[in] addr      The address on the asyn port to connect to
      * \param[in] drvInfo   The drvInfo string to identify which property of the port is being connected to
      * \param[in] timeout   The default timeout for all communications between the client and the port driver 
    */
    asynOctetClient(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynPortClient(portName, addr, asynOctetType, drvInfo, timeout) {
        pInterface_ = (asynOctet *)pasynInterface_->pinterface;
        if (pasynOctetSyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo)) 
            throw std::runtime_error(std::string("pasynOctetSyncIO->connect failed"));
    };
    /** Destructor for asynOctetClient class.  Disconnects from port, frees resources. */
    virtual ~asynOctetClient() { 
        pasynOctetSyncIO->disconnect(pasynUserSyncIO_); 
    }; 
    /** Writes a char buffer to the port driver
      * \param[in]  buffer     The characters to write to the port driver
      * \param[in]  bufferLen  The size of the buffer
      * \param[out] nActual    The number of characters actually written */
    virtual asynStatus write(const char *buffer, size_t bufferLen, size_t *nActual) { 
        return pasynOctetSyncIO->write(pasynUserSyncIO_, buffer, bufferLen, timeout_, nActual); 
    };
    /** Reads a char buffer from the port driver
      * \param[out] buffer     The characters read from the port driver
      * \param[in]  bufferLen  The size of the buffer
      * \param[out] nActual    The number of characters actually read
      * \param[out] eomReason  The end of message reason, i.e. why the read terminated */
    virtual asynStatus read(char *buffer, size_t bufferLen, size_t *nActual, int *eomReason) {
        return pasynOctetSyncIO->read(pasynUserSyncIO_, buffer, bufferLen, timeout_, nActual, eomReason); 
    };
    /** Writes a char buffer to the port driver and reads the response as an atomic operation
      * \param[in]  writeBuffer     The characters to write to the port driver
      * \param[in]  writeBufferLen  The size of the write buffer
      * \param[out] readBuffer      The characters read from the port driver
      * \param[in]  readBufferLen   The size of the read buffer
      * \param[out] nBytesOut       The number of characters actually written
      * \param[out] nBytesIn        The number of characters actually read
      * \param[out] eomReason       The end of message reason, i.e. why the read terminated */
    virtual asynStatus writeRead(const char *writeBuffer, size_t writeBufferLen, char *readBuffer, size_t readBufferLen, 
                                 size_t *nBytesOut, size_t *nBytesIn, int *eomReason) { 
        return pasynOctetSyncIO->writeRead(pasynUserSyncIO_, writeBuffer, writeBufferLen, readBuffer, readBufferLen,
                                           timeout_, nBytesOut, nBytesIn, eomReason); 
    };
    /** Flushes the input buffer in the port driver */
    virtual asynStatus flush() { 
        return pasynOctetSyncIO->flush(pasynUserSyncIO_); 
    };
    /** Sets the input end-of-string terminator in the driver
      * \param[in]  eos     The input EOS string
      * \param[in]  eosLen  The size of the EOS string */
    virtual asynStatus setInputEos(const char *eos, int eosLen) { 
        return pasynOctetSyncIO->setInputEos(pasynUserSyncIO_, eos, eosLen); 
    };
    /** Gets the input end-of-string terminator from the driver
      * \param[out]  eos     The input EOS string
      * \param[out]  eosSize The maximum size of the EOS string
      * \param[out]  eosLen  The actual size of the EOS string */
    virtual asynStatus getInputEos(char *eos, int eosSize, int *eosLen) { 
        return pasynOctetSyncIO->getInputEos(pasynUserSyncIO_, eos, eosSize, eosLen); 
    };
    /** Sets the output end-of-string terminator in the driver
      * \param[in]  eos     The output EOS string
      * \param[in]  eosLen  The size of the EOS string */
    virtual asynStatus setOutputEos(const char *eos, int eosLen) { 
        return pasynOctetSyncIO->setOutputEos(pasynUserSyncIO_, eos, eosLen); 
    };
    /** Gets the output end-of-string terminator from the driver
      * \param[out]  eos     The output EOS string
      * \param[out]  eosSize The maximum size of the EOS string
      * \param[out]  eosLen  The actual size of the EOS string */
    virtual asynStatus getOutputEos(char *eos, int eosSize, int *eosLen) { 
        return pasynOctetSyncIO->getOutputEos(pasynUserSyncIO_, eos, eosSize, eosLen); 
    };
    /** Registers an interruptCallbackOctet function that the driver will call when there is a new value
      * \param[in] pCallback  The address of the callback function */
    virtual asynStatus registerInterruptUser(interruptCallbackOctet pCallback) { 
        return pInterface_->registerInterruptUser(pasynInterface_->drvPvt, pasynUser_,
                                                  pCallback, this, &interruptPvt_); 
    };
private:
    asynOctet *pInterface_;
};
  

/** Class for asyn port clients to communicate on the asynInt8Array interface */
class epicsShareClass asynInt8ArrayClient : public asynPortClient {
public:
    /** Constructor for asynInt8Array class
      * \param[in] portName  The name of the asyn port to connect to
      * \param[in] addr      The address on the asyn port to connect to
      * \param[in] drvInfo   The drvInfo string to identify which property of the port is being connected to
      * \param[in] timeout   The default timeout for all communications between the client and the port driver 
    */
    asynInt8ArrayClient(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynPortClient(portName, addr, asynInt8ArrayType, drvInfo, timeout) {
        pInterface_ = (asynInt8Array *)pasynInterface_->pinterface;
        if (pasynInt8ArraySyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo)) 
            throw std::runtime_error(std::string("pasynInt8ArraySyncIO->connect failed"));
    };
    /** Destructor for asynInt8Array class.  Disconnects from port, frees resources. */
    virtual ~asynInt8ArrayClient() {
        pasynInt8ArraySyncIO->disconnect(pasynUserSyncIO_);
    };
    /** Reads an epicsInt8 array from the port driver
      * \param[out] value     The array to read from the port driver 
      * \param[in]  nElements The number of elements in the array 
      * \param[out] nIn       The number of array elements actual read */
    virtual asynStatus read(epicsInt8 *value, size_t nElements, size_t *nIn) {
        return pasynInt8ArraySyncIO->read(pasynUserSyncIO_, value, nElements, nIn, timeout_);
    };
    /** Writes an epicsInt8 array to the port driver
      * \param[in] value      The array to write to the port driver 
      * \param[in] nElements  The number of elements in the array */
    virtual asynStatus write(epicsInt8 *value, size_t nElements) {
        return pasynInt8ArraySyncIO->write(pasynUserSyncIO_, value, nElements, timeout_);
    };
    /** Registers an interruptCallbackInt8Array function that the driver will call when there is a new value
      * \param[in] pCallback  The address of the callback function */
    virtual asynStatus registerInterruptUser(interruptCallbackInt8Array pCallback) { 
        return pInterface_->registerInterruptUser(pasynInterface_->drvPvt, pasynUser_,
                                                      pCallback, this, &interruptPvt_); 
    };
private:
    asynInt8Array *pInterface_;
};


/** Class for asyn port clients to communicate on the asynInt16Array interface */
class epicsShareClass asynInt16ArrayClient : public asynPortClient {
public:
    /** Constructor for asynInt16Array class
      * \param[in] portName  The name of the asyn port to connect to
      * \param[in] addr      The address on the asyn port to connect to
      * \param[in] drvInfo   The drvInfo string to identify which property of the port is being connected to
      * \param[in] timeout   The default timeout for all communications between the client and the port driver 
    */
    asynInt16ArrayClient(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynPortClient(portName, addr, asynInt16ArrayType, drvInfo, timeout) {
        pInterface_ = (asynInt16Array *)pasynInterface_->pinterface;
        if (pasynInt16ArraySyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo)) 
            throw std::runtime_error(std::string("pasynInt16ArraySyncIO->connect failed"));
    };
    /** Destructor for asynInt16Array class.  Disconnects from port, frees resources. */
    virtual ~asynInt16ArrayClient() {
        pasynInt16ArraySyncIO->disconnect(pasynUserSyncIO_);
    };
    /** Reads an epicsInt16 array from the port driver
      * \param[out] value     The array to read from the port driver 
      * \param[in]  nElements The number of elements in the array 
      * \param[out] nIn       The number of array elements actual read */
    virtual asynStatus read(epicsInt16 *value, size_t nElements, size_t *nIn) {
        return pasynInt16ArraySyncIO->read(pasynUserSyncIO_, value, nElements, nIn, timeout_);
    };
    /** Writes an epicsInt16 array to the port driver
      * \param[in] value      The array to write to the port driver 
      * \param[in] nElements  The number of elements in the array */
    virtual asynStatus write(epicsInt16 *value, size_t nElements) {
        return pasynInt16ArraySyncIO->write(pasynUserSyncIO_, value, nElements, timeout_);
    };
    /** Registers an interruptCallbackInt16Array function that the driver will call when there is a new value
      * \param[in] pCallback  The address of the callback function */
    virtual asynStatus registerInterruptUser(interruptCallbackInt16Array pCallback) { 
        return pInterface_->registerInterruptUser(pasynInterface_->drvPvt, pasynUser_,
                                                       pCallback, this, &interruptPvt_); 
    };
private:
    asynInt16Array *pInterface_;
};


/** Class for asyn port clients to communicate on the asynInt32Array interface */
class epicsShareClass asynInt32ArrayClient : public asynPortClient {
public:
    /** Constructor for asynInt32Array class
      * \param[in] portName  The name of the asyn port to connect to
      * \param[in] addr      The address on the asyn port to connect to
      * \param[in] drvInfo   The drvInfo string to identify which property of the port is being connected to
      * \param[in] timeout   The default timeout for all communications between the client and the port driver 
    */
    asynInt32ArrayClient(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynPortClient(portName, addr, asynInt32ArrayType, drvInfo, timeout) {
        pInterface_ = (asynInt32Array *)pasynInterface_->pinterface;
        if (pasynInt32ArraySyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo)) 
            throw std::runtime_error(std::string("pasynInt32ArraySyncIO->connect failed"));
    };
    /** Destructor for asynInt32Array class.  Disconnects from port, frees resources. */
    virtual ~asynInt32ArrayClient() {
        pasynInt32ArraySyncIO->disconnect(pasynUserSyncIO_);
    };
    /** Reads an epicsInt32 array from the port driver
      * \param[out] value     The array to read from the port driver 
      * \param[in]  nElements The number of elements in the array 
      * \param[out] nIn       The number of array elements actual read */
    virtual asynStatus read(epicsInt32 *value, size_t nElements, size_t *nIn) {
        return pasynInt32ArraySyncIO->read(pasynUserSyncIO_, value, nElements, nIn, timeout_);
    };
    /** Writes an epicsInt32 array to the port driver
      * \param[in] value      The array to write to the port driver 
      * \param[in] nElements  The number of elements in the array */
    virtual asynStatus write(epicsInt32 *value, size_t nElements) {
        return pasynInt32ArraySyncIO->write(pasynUserSyncIO_, value, nElements, timeout_);
    };
    /** Registers an interruptCallbackInt32Array function that the driver will call when there is a new value
      * \param[in] pCallback  The address of the callback function */
    virtual asynStatus registerInterruptUser(interruptCallbackInt32Array pCallback) { 
        return pInterface_->registerInterruptUser(pasynInterface_->drvPvt, pasynUser_,
                                                  pCallback, this, &interruptPvt_); 
    };
private:
    asynInt32Array *pInterface_;
};


/** Class for asyn port clients to communicate on the asynFloat32Array interface */
class epicsShareClass asynFloat32ArrayClient : public asynPortClient {
public:
    /** Constructor for asynFloat32Array class
      * \param[in] portName  The name of the asyn port to connect to
      * \param[in] addr      The address on the asyn port to connect to
      * \param[in] drvInfo   The drvInfo string to identify which property of the port is being connected to
      * \param[in] timeout   The default timeout for all communications between the client and the port driver 
    */
    asynFloat32ArrayClient(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynPortClient(portName, addr, asynFloat32ArrayType, drvInfo, timeout) {
        pInterface_ = (asynFloat32Array *)pasynInterface_->pinterface;
        if (pasynFloat32ArraySyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo)) 
            throw std::runtime_error(std::string("pasynFloat64ArraySyncIO->connect failed"));
    };
    /** Destructor for asynFloat32Array class.  Disconnects from port, frees resources. */
    virtual ~asynFloat32ArrayClient() {
        pasynFloat32ArraySyncIO->disconnect(pasynUserSyncIO_);
    };
    /** Reads an epicsFloat32 array from the port driver
      * \param[out] value     The array to read from the port driver 
      * \param[in]  nElements The number of elements in the array 
      * \param[out] nIn       The number of array elements actual read */
    virtual asynStatus read(epicsFloat32 *value, size_t nElements, size_t *nIn) {
        return pasynFloat32ArraySyncIO->read(pasynUserSyncIO_, value, nElements, nIn, timeout_);
    };
    /** Writes an epicsFloat32 array to the port driver
      * \param[in] value      The array to write to the port driver 
      * \param[in] nElements  The number of elements in the array */
    virtual asynStatus write(epicsFloat32 *value, size_t nElements) {
        return pasynFloat32ArraySyncIO->write(pasynUserSyncIO_, value, nElements, timeout_);
    };
    /** Registers an interruptCallbackFloat32Array function that the driver will call when there is a new value
      * \param[in] pCallback  The address of the callback function */
    virtual asynStatus registerInterruptUser(interruptCallbackFloat32Array pCallback) { 
        return pInterface_->registerInterruptUser(pasynInterface_->drvPvt, pasynUser_,
                                                  pCallback, this, &interruptPvt_); 
    };
private:
    asynFloat32Array *pInterface_;
};


/** Class for asyn port clients to communicate on the asynFloat64Array interface */
class epicsShareClass asynFloat64ArrayClient : public asynPortClient {
public:
    /** Constructor for asynFloat64Array class
      * \param[in] portName  The name of the asyn port to connect to
      * \param[in] addr      The address on the asyn port to connect to
      * \param[in] drvInfo   The drvInfo string to identify which property of the port is being connected to
      * \param[in] timeout   The default timeout for all communications between the client and the port driver 
    */
    asynFloat64ArrayClient(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynPortClient(portName, addr, asynFloat64ArrayType, drvInfo, timeout) {
        pInterface_ = (asynFloat64Array *)pasynInterface_->pinterface;
        if (pasynFloat64ArraySyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo)) 
            throw std::runtime_error(std::string("pasynFloat64ArraySyncIO->connect failed"));
    };
    /** Destructor for asynFloat64Array class.  Disconnects from port, frees resources. */
    virtual ~asynFloat64ArrayClient() {
        pasynFloat64ArraySyncIO->disconnect(pasynUserSyncIO_);
    };
    /** Reads an epicsFloat64 array from the port driver
      * \param[out] value     The array to read from the port driver 
      * \param[in]  nElements The number of elements in the array 
      * \param[out] nIn       The number of array elements actual read */
    virtual asynStatus read(epicsFloat64 *value, size_t nElements, size_t *nIn) {
        return pasynFloat64ArraySyncIO->read(pasynUserSyncIO_, value, nElements, nIn, timeout_);
    };
    /** Writes an epicsFloat64 array to the port driver
      * \param[in] value      The array to write to the port driver 
      * \param[in] nElements  The number of elements in the array */
    virtual asynStatus write(epicsFloat64 *value, size_t nElements) {
        return pasynFloat64ArraySyncIO->write(pasynUserSyncIO_, value, nElements, timeout_);
    };
    /** Registers an interruptCallbackFloat64Array function that the driver will call when there is a new value
      * \param[in] pCallback  The address of the callback function */
    virtual asynStatus registerInterruptUser(interruptCallbackFloat64Array pCallback) { 
        return pInterface_->registerInterruptUser(pasynInterface_->drvPvt, pasynUser_,
                                                  pCallback, this, &interruptPvt_); 
    };
private:
    asynFloat64Array *pInterface_;
};


/** Class for asyn port clients to communicate on the asynGenericPointer interface */
class epicsShareClass asynGenericPointerClient : public asynPortClient {
public:
    /** Constructor for asynGenericPointer class
      * \param[in] portName  The name of the asyn port to connect to
      * \param[in] addr      The address on the asyn port to connect to
      * \param[in] drvInfo   The drvInfo string to identify which property of the port is being connected to
      * \param[in] timeout   The default timeout for all communications between the client and the port driver 
    */
    asynGenericPointerClient(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynPortClient(portName, addr, asynGenericPointerType, drvInfo, timeout) {
        pInterface_ = (asynGenericPointer *)pasynInterface_->pinterface;
        if (pasynGenericPointerSyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo)) 
            throw std::runtime_error(std::string("pasynGenericPointerSyncIO->connect failed"));
    };
    /** Destructor for asynGenericPointer class.  Disconnects from port, frees resources. */
    virtual ~asynGenericPointerClient() {
        pasynGenericPointerSyncIO->disconnect(pasynUserSyncIO_);
    };
    /** Reads an generic pointer from the port driver
      * \param[out] pointer  The pointer to read from the port driver */
    virtual asynStatus read(void *pointer) {
        return pasynGenericPointerSyncIO->read(pasynUserSyncIO_, pointer, timeout_);
    };
    /** Writes a generic pointer to the port driver
      * \param[in] pointer   The pointer to write to the port driver */
    virtual asynStatus write(void *pointer) {
        return pasynGenericPointerSyncIO->write(pasynUserSyncIO_, pointer, timeout_);
    };
    /** Registers an interruptCallbackGenericPointer function that the driver will call when there is a new value
      * \param[in] pCallback  The address of the callback function */
    virtual asynStatus registerInterruptUser(interruptCallbackGenericPointer pCallback) { 
        return pInterface_->registerInterruptUser(pasynInterface_->drvPvt, pasynUser_,
                                                  pCallback, this, &interruptPvt_); 
    };
private:
    asynGenericPointer *pInterface_;
};


/** Class for asyn port clients to communicate on the asynOption interface */
class epicsShareClass asynOptionClient : public asynPortClient {
public:
    /** Constructor for asynOption class
      * \param[in] portName  The name of the asyn port to connect to
      * \param[in] addr      The address on the asyn port to connect to
      * \param[in] drvInfo   The drvInfo string to identify which property of the port is being connected to
      * \param[in] timeout   The default timeout for all communications between the client and the port driver 
    */
    asynOptionClient(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynPortClient(portName, addr, asynOptionType, drvInfo, timeout) {
        pInterface_ = (asynOption *)pasynInterface_->pinterface;
        if (pasynOptionSyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo)) 
            throw std::runtime_error(std::string("pasynOptionSyncIO->connect failed"));
    };
    /** Destructor for asynOption class.  Disconnects from port, frees resources. */
    virtual ~asynOptionClient() {
        pasynOptionSyncIO->disconnect(pasynUserSyncIO_);
    };
    /** Get an option from the port driver
      * \param[in]  key       The key to read from the port driver
      * \param[out] value     The value to read from the port driver
      * \param[in]  maxChars  The size of value */
    virtual asynStatus getOption(const char *key, char *value, int maxChars) {
        return pasynOptionSyncIO->getOption(pasynUserSyncIO_, key, value, maxChars, timeout_);
    };
    /** Sets an option in the port driver
      * \param[in]  key       The key to set in the port driver
      * \param[out] value     The value to set in the port driver */
    virtual asynStatus setOption(const char *key, const char *value) {
        return pasynOptionSyncIO->setOption(pasynUserSyncIO_, key, value, timeout_);
    };
private:
    asynOption *pInterface_;
};


/** Class for asyn port clients to communicate on the asynEnum interface */
class epicsShareClass asynEnumClient : public asynPortClient {
public:
    /** Constructor for asynEnum class
      * \param[in] portName  The name of the asyn port to connect to
      * \param[in] addr      The address on the asyn port to connect to
      * \param[in] drvInfo   The drvInfo string to identify which property of the port is being connected to
      * \param[in] timeout   The default timeout for all communications between the client and the port driver 
    */
    asynEnumClient(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynPortClient(portName, addr, asynEnumType, drvInfo, timeout) {
        pInterface_ = (asynEnum *)pasynInterface_->pinterface;
        if (pasynEnumSyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo)) 
            throw std::runtime_error(std::string("pasynEnumSyncIO->connect failed"));
    };
    /** Destructor for asynEnum class.  Disconnects from port, frees resources. */
    virtual ~asynEnumClient() {
        pasynEnumSyncIO->disconnect(pasynUserSyncIO_);
    };
    /** Reads enum strings, values, and severities from the port driver
      * \param[out] strings     The enum strings to read from the driver
      * \param[out] values      The enum values to read from the driver
      * \param[out] severities  The enum severities to read from the driver
      * \param[in] nElements    The number of elements in the strings, value, and severities arrays
      * \param[in] nIn          The number of elements actually read from the driver */
    virtual asynStatus read(char *strings[], int values[], int severities[], size_t nElements, size_t *nIn) {
        return pasynEnumSyncIO->read(pasynUserSyncIO_, strings, values, severities, nElements, nIn, timeout_);
    };
    /** Writes enum strings, values, and severities to the port driver
      * \param[out] strings     The enum strings to write to the driver
      * \param[out] values      The enum values to write to the driver
      * \param[out] severities  The enum severities to write to the driver
      * \param[in] nElements    The number of elements in the strings, value, and severities arrays */
    virtual asynStatus write(char *strings[], int values[], int severities[], size_t nElements) {
        return pasynEnumSyncIO->write(pasynUserSyncIO_, strings, values, severities, nElements, timeout_);
    };
private:
    asynEnum *pInterface_;
};


/** Class for asyn port clients to communicate on the asynCommon interface */
class epicsShareClass asynCommonClient : public asynPortClient {
public:
    /** Constructor for asynCommon class
      * \param[in] portName  The name of the asyn port to connect to
      * \param[in] addr      The address on the asyn port to connect to
      * \param[in] drvInfo   The drvInfo string to identify which property of the port is being connected to
      * \param[in] timeout   The default timeout for all communications between the client and the port driver 
    */
    asynCommonClient(const char *portName, int addr, const char *drvInfo, double timeout=DEFAULT_TIMEOUT)
    : asynPortClient(portName, addr, asynCommonType, drvInfo, timeout) {
        pInterface_ = (asynCommon *)pasynInterface_->pinterface;
        if (pasynCommonSyncIO->connect(portName, addr, &pasynUserSyncIO_, drvInfo)) 
            throw std::runtime_error(std::string("pasynCommonSyncIO->connect failed"));
    };
    /** Destructor for asynCommon class.  Disconnects from port, frees resources. */
    virtual ~asynCommonClient() {
        pasynCommonSyncIO->disconnect(pasynUserSyncIO_);
    };
    /** Calls the report method in the driver 
      * \param[in] fp       The file pointer to write the report to
      * \param[in] details  The level of detail for the report */
    virtual void report(FILE *fp, int details) {
        pasynCommonSyncIO->report(pasynUserSyncIO_, fp, details);
    };
    /** Calls the connect method in the driver which attempts to connect to the port device */
    virtual asynStatus connect() {
        return pasynCommonSyncIO->connectDevice(pasynUserSyncIO_);
    };
    /** Calls the disconnect method in the driver disconnects from the port device */
    virtual asynStatus disconnect() {
        return pasynCommonSyncIO->disconnectDevice(pasynUserSyncIO_);
    };
private:
    asynCommon *pInterface_;
};  
    
#endif
