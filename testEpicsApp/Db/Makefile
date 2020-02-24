TOP=../..
include $(TOP)/configure/CONFIG
DB += devInt32.db
DB += devDigital.db
DB += devOctet.db

ifdef BASE_3_15
DB += devOctetLs.db
endif
ifdef CALC
DB += devOctetCalc.db
endif
include $(TOP)/configure/RULES
