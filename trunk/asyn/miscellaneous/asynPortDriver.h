#ifndef asynPortDriver_H
#define asynPortDriver_H

#include <epicsTypes.h>
#include <asynStandardInterfaces.h>

typedef struct {
    int param;
    const char *paramString;
} asynParamString_t;

#ifdef __cplusplus

#define asynCommonMask          0x00000001
#define asynDrvUserMask         0x00000002
#define asynOptionMask          0x00000004
#define asynInt32Mask           0x00000008
#define asyUInt32DigitalMask    0x00000010
#define asynFloat64Mask         0x00000020
#define asynOctetMask           0x00000040
#define asynInt8ArrayMask       0x00000080
#define asynInt16ArrayMask      0x00000100
#define asynInt32ArrayMask      0x00000200
#define asynFloat32ArrayMask    0x00000400
#define asynFloat64ArrayMask    0x00000800
#define asynGenericPointerMask  0x00001000


typedef enum { paramUndef, paramInt, paramDouble, paramString} paramType;

typedef struct
{
    paramType type;
    union
    {
        double dval;
        int    ival;
        char  *sval;
    } data;
} paramVal;

class paramList {
public:
    paramList(int startVal, int nVals, asynStandardInterfaces *pasynInterfaces);
    ~paramList();
    asynStatus setInteger(int index, int value);
    asynStatus setDouble(int index, double value);
    asynStatus setString(int index, const char *string);
    asynStatus getInteger(int index, int *value);
    asynStatus getDouble(int index, double *value);
    asynStatus getString(int index, int maxChars, char *value);
    asynStatus callCallbacks(int addr);
    asynStatus callCallbacks();
    void report();

private:    
    asynStatus setFlag(int index);
    asynStatus intCallback(int command, int addr, int value);
    asynStatus doubleCallback(int command, int addr, double value);
    asynStatus stringCallback(int command, int addr, char *value);
    int startVal;
    int nVals;
    int nFlags;
    asynStandardInterfaces *pasynInterfaces;
    int *flags;
    paramVal *vals;
};

class asynPortDriver {
public:
    asynPortDriver(const char *portNameIn, int maxAddrIn, int paramTableSize, int interfaceMask, int interruptMask,
                   int asynFlags, int autoConnect, int priority, int stackSize);
    virtual ~asynPortDriver();
    virtual asynStatus getAddress(asynUser *pasynUser, const char *functionName, int *address); 
    virtual asynStatus findParam(asynParamString_t *paramTable, int numParams, const char *paramName, int *param);
    virtual asynStatus readInt32(asynUser *pasynUser, epicsInt32 *value);
    virtual asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
    virtual asynStatus getBounds(asynUser *pasynUser, epicsInt32 *low, epicsInt32 *high);
    virtual asynStatus readFloat64(asynUser *pasynUser, epicsFloat64 *value);
    virtual asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value);
    virtual asynStatus readOctet(asynUser *pasynUser, char *value, size_t maxChars,
                         size_t *nActual, int *eomReason);
    virtual asynStatus writeOctet(asynUser *pasynUser, const char *value, size_t maxChars,
                          size_t *nActual);
    virtual asynStatus readInt8Array(asynUser *pasynUser, epicsInt8 *value, 
                                        size_t nElements, size_t *nIn);
    virtual asynStatus writeInt8Array(asynUser *pasynUser, epicsInt8 *value,
                                        size_t nElements);
    virtual asynStatus doCallbacksInt8Array(epicsInt8 *value,
                                        size_t nElements, int reason, int addr);
    virtual asynStatus readInt16Array(asynUser *pasynUser, epicsInt16 *value,
                                        size_t nElements, size_t *nIn);
    virtual asynStatus writeInt16Array(asynUser *pasynUser, epicsInt16 *value,
                                        size_t nElements);
    virtual asynStatus doCallbacksInt16Array(epicsInt16 *value,
                                        size_t nElements, int reason, int addr);
    virtual asynStatus readInt32Array(asynUser *pasynUser, epicsInt32 *value,
                                        size_t nElements, size_t *nIn);
    virtual asynStatus writeInt32Array(asynUser *pasynUser, epicsInt32 *value,
                                        size_t nElements);
    virtual asynStatus doCallbacksInt32Array(epicsInt32 *value,
                                        size_t nElements, int reason, int addr);
    virtual asynStatus readFloat32Array(asynUser *pasynUser, epicsFloat32 *value,
                                        size_t nElements, size_t *nIn);
    virtual asynStatus writeFloat32Array(asynUser *pasynUser, epicsFloat32 *value,
                                        size_t nElements);
    virtual asynStatus doCallbacksFloat32Array(epicsFloat32 *value,
                                        size_t nElements, int reason, int addr);
    virtual asynStatus readFloat64Array(asynUser *pasynUser, epicsFloat64 *value,
                                        size_t nElements, size_t *nIn);
    virtual asynStatus writeFloat64Array(asynUser *pasynUser, epicsFloat64 *value,
                                        size_t nElements);
    virtual asynStatus doCallbacksFloat64Array(epicsFloat64 *value,
                                        size_t nElements, int reason, int addr);
    virtual asynStatus readGenericPointer(asynUser *pasynUser, void *pointer);
    virtual asynStatus writeGenericPointer(asynUser *pasynUser, void *pointer);
    virtual asynStatus doCallbacksGenericPointer(void *pointer, int reason, int addr);
    virtual asynStatus drvUserCreate(asynUser *pasynUser, const char *drvInfo, 
                                     const char **pptypeName, size_t *psize);
    virtual asynStatus drvUserGetType(asynUser *pasynUser,
                                        const char **pptypeName, size_t *psize);
    virtual asynStatus drvUserDestroy(asynUser *pasynUser);
    virtual void report(FILE *fp, int details);
    virtual asynStatus connect(asynUser *pasynUser);
    virtual asynStatus disconnect(asynUser *pasynUser);
   
    virtual asynStatus setIntegerParam(int index, int value);
    virtual asynStatus setIntegerParam(int list, int index, int value);
    virtual asynStatus setDoubleParam(int index, double value);
    virtual asynStatus setDoubleParam(int list, int index, double value);
    virtual asynStatus setStringParam(int index, const char *value);
    virtual asynStatus setStringParam(int list, int index, const char *value);
    virtual asynStatus getIntegerParam(int index, int * value);
    virtual asynStatus getIntegerParam(int list, int index, int * value);
    virtual asynStatus getDoubleParam(int index, double * value);
    virtual asynStatus getDoubleParam(int list, int index, double * value);
    virtual asynStatus getStringParam(int index, int maxChars, char *value);
    virtual asynStatus getStringParam(int list, int index, int maxChars, char *value);
    virtual asynStatus callParamCallbacks();
    virtual asynStatus callParamCallbacks(int list, int addr);
    virtual void reportParams();

    char *portName;
    int maxAddr;
    paramList **params;
    epicsMutexId mutexId;

    /* The asyn interfaces this driver implements */
    asynStandardInterfaces asynStdInterfaces;
    
    /* asynUser connected to ourselves for asynTrace */
    asynUser *pasynUserSelf;
};

#endif /* cplusplus */
    
#endif
