< envPaths

dbLoadDatabase("../../dbd/testIPServer.dbd")
testIPServer_registerRecordDeviceDriver(pdbbase)

#The following command starts a server on port 5000
drvAsynIPServerPortConfigure("P5000","localhost:5000",2,0,0,0)

asynSetTraceFile("P5000",-1,"")
asynSetTraceMask("P5000",-1,0xff)
asynSetTraceIOMask("P5000",-1,0x2)

dbLoadRecords("../../db/asynRecord.db", "P=testIPServer:, R=asyn1, PORT=junk, ADDR=0, IMAX=80, OMAX=80")

iocInit()

testIPServer("P5000")

