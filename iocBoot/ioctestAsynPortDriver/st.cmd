dbLoadDatabase("../../dbd/testAsynPortDriver.dbd")
testAsynPortDriver_registerRecordDeviceDriver(pdbbase)

testAsynPortDriverConfigure("testAPD", 1000)

dbLoadRecords("../../db/testAsynPortDriver.db","P=testAPD:,R=scope1:,PORT=testAPD,ADDR=0,TIMEOUT=1,NPOINTS=1000")
#asynSetTraceMask("testAPD",0,0xff)
asynSetTraceIOMask("testAPD",0,0x2)
iocInit()
