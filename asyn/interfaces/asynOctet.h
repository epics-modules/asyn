/* Interface supported by low level octet drivers. */
#define asynOctetType "asynOctet"
typedef struct asynOctet{
    asynStatus (*read)(void *drvPvt,asynUser *pasynUser,
                       char *data,int maxchars,int *nbytesTransfered);
    asynStatus (*write)(void *drvPvt,asynUser *pasynUser,
                        const char *data,int numchars,int *nbytesTransfered);
    asynStatus (*flush)(void *drvPvt,asynUser *pasynUser);
    asynStatus (*setEos)(void *drvPvt,asynUser *pasynUser,
                         const char *eos,int eoslen);
    asynStatus (*getEos)(void *drvPvt,asynUser *pasynUser,
                        char *eos, int eossize, int *eoslen);
}asynOctet;

