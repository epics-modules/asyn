#!../../bin/_TARGETARCH_/_NAME_

## You may have to change _NAME_ to something else
## everywhere it appears in this file

< envPaths

cd ${TOP}

## Register all support components
dbLoadDatabase("dbd/_NAME_.dbd")
_NAME__registerRecordDeviceDriver(pdbbase)

## Load record instances
dbLoadRecords("db/_NAME_.db","user=_USER_Host")

cd ${TOP}/iocBoot/${IOC}
iocInit()

## Start any sequence programs
#seq sncExample,"user=_USER_Host"
