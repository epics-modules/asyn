dbLoadDatabase("../../dbd/test.dbd")
test_registerRecordDeviceDriver(pdbbase)

echoDriverInit(echo,1.0)
processModuleInit(processModule,echo,1)

dbLoadRecords("../../db/asynTrace.db","ioc=mrk,port=echo,addr=0")
dbLoadRecords("../../db/asynTrace.db","ioc=mrk,port=echo,addr=1")
iocInit()
