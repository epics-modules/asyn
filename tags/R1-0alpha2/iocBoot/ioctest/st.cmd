test_registerRecordDeviceDriver(pdbbase)

echoDriverInit(echo,1.0)
processModuleInit(processModule,echo,1)
testEcho(echo,0,oneA0,5,10.0)
testEcho(echo,0,twoA0,3,10.0)
testEcho(echo,1,oneA1,5,10.0)
testEcho(echo,1,twoA1,3,10.0)
asynReport("",3)
