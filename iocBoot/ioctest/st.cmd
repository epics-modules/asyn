test_registerRecordDeviceDriver(pdbbase)

echoDriverInit(echo,1.0)
testEcho(echo,one,5,10.0)
processModuleInit(processModule,echo)
testEcho(echo,two,5,10.0)
