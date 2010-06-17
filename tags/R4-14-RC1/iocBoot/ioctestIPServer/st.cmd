< envPaths

dbLoadDatabase("../../dbd/testIPServer.dbd")
testIPServer_registerRecordDeviceDriver(pdbbase)

#The following command starts a server on port 5001
drvAsynIPServerPortConfigure("P5001","localhost:5001",2,0,0,0)
drvAsynIPServerPortConfigure("P5002","localhost:5002",1,0,0,0)

#asynSetTraceFile("P5001",-1,"")
asynSetTraceIOMask("P5001",-1,0x2)
#asynSetTraceMask("P5001",-1,0xff)

dbLoadRecords("../../db/testIPServer.db", "P=testIPServer:")

iocInit()

ipEchoServer("P5001")
seq("ipSNCServer", "P=testIPServer:, PORT=P5002")

