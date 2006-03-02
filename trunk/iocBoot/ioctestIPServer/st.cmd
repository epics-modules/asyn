< envPaths

dbLoadDatabase("../../dbd/testIPServer.dbd")
testIPServer_registerRecordDeviceDriver(pdbbase)

#The following command starts a server on port 5000
drvAsynIPServerPortConfigure("P5000","localhost:5000",2,0,0,0)
drvAsynIPServerPortConfigure("P5001","localhost:5001",1,0,0,0)

#asynSetTraceFile("P5000",-1,"")
#asynSetTraceMask("P5000",-1,0xff)
#asynSetTraceIOMask("P5000",-1,0x2)

dbLoadRecords("../../db/testIPServer.db", "P=testIPServer:")

iocInit()

ipEchoServer("P5000")
seq("ipSNCServer", "P=testIPServer:, PORT=P5001")

