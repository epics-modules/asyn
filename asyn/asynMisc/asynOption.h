#define asynOptionType "asynOption"

/*The following are generic methods to set/get device options*/
typedef struct asynOption {
    asynStatus (*setOption)(void *drvPvt, asynUser *pasynUser,
                                const char *key, const char *val);
    asynStatus (*getOption)(void *drvPvt, asynUser *pasynUser,
                                const char *key, char *val, int sizeval);
}asynOption;

