#!python3
# -*- coding:utf-8 -*-
"""
LXI HiSLIP Test Procedures
LXI HiSLIP Extended Function
Revision 1.01
20 October, 2011 Edition
"""
import unittest

import os,sys,time
import subprocess

from cPyHiSLIP import HiSLIP

class HiSLIPFunctionTestMethods(unittest.TestCase):
    """
    ESE 0:OPC/(1:Reqest Control)/2:Query Error/3:Dev.Dep.Error/4:Execution Error/5:Command Error/(6:User Request)/7:PowerOn
    ESE=32=0x20 : Command Error
    SRE/STB bit 4(0x10):Message Avairlable bit 5(0x20):ESB bit, bit 6(0x40) RQS

            bit 2:0x04 Error/Event Available for some device. or MSG
            bit 3:Questionable Data 
    https://studfile.net/preview/6372846/page:24/
    """
    
    Query1=b"*IDN?"
    Query2=b"*OPC?"
    EnableSRQ=b"*ESE 32;*SRE 48;" # command Error , MAV/ESB
    
    @classmethod
    def setUpClass(cls):
        #cls.hostname=b"172.28.68.228"    # Keysight
        #cls.hostname=b"169.254.29.115"    # Keysight
        cls.hostname=b"169.254.100.192"  # Kikusui
        
    def setUp(self):
        self.dev=HiSLIP()
        self.dev.connect(self.hostname)

    def tearDown(self):
        self.dev.device_clear(0)
        del self.dev
        time.sleep(3)

    def test_connection(self):
        print("test_connection")
        self.dev.write(self.Query1)
        self.assertTrue(self.dev.read() != None)
        print("end of test_connection")
        return 0
    
    def test_SRQ_status_byte(self):
        print("test_SRQ_status_byte")
        print("test_SRQ_status_byte:EnableSRQ")
        self.dev.write(self.EnableSRQ)
        print("test_SRQ_status_byte:sendQuery{}".format(self.Query2))
        self.dev.write(self.Query2)
        print("test_SRQ_status_byte:waitSRQ")
        self.dev.wait_Service_Request(10000)
        # print("test_SRQ_status_byte:checkSRQ")
        # self.dev.check_SRQ()
        print("test_SRQ_status_byte:status_Query")
        st=self.dev.status_query()
        print ("Status Query:",hex(st),hex(st&0x70))
        self.assertEqual(st & 0x70, 80) # SRQ(64)+ MAV(16)
        print("test_SRQ_status_byte:readResutl")
        r=self.dev.read()
        print("test_SRQ_status_byte:readResutl {}".format(r))
        print("test_SRQ_status_byte:statusQuery")
        st=self.dev.status_query()
        self.assertEqual(st &0x70, 0)
        print("end test_SRQ_status_byte")
        return 0
    
    def test_lock_info(self):
        print("test_lock_info")
        ret=self.dev.request_lock(b"shared") # shared lock
        self.assertEqual(ret,1)
        info=self.dev.lock_info()
        self.assertEqual(info, (0,1))
        
        ret=self.dev.request_lock()# get exclusive lock
        self.assertEqual(ret,1)
        
        info=self.dev.lock_info()
        self.assertEqual(info, (1,1))

        ret=self.dev.request_lock()# try to get lock already locked
        self.assertEqual(ret,3) # error!
        
        info=self.dev.lock_info()
        self.assertEqual(info, (1,1))

        ret=self.dev.release_lock() # release exclusive lock
        self.assertEqual(ret,1)
        info=self.dev.lock_info()
        self.assertEqual(info, (0,1))
        
        ret=self.dev.release_lock() # release shared lock
        self.assertEqual(ret,2)
        info=self.dev.lock_info()
        self.assertEqual(info, (0,0))
        print("end test_lock_info")
        return 0

    def test_InterruptedMode(self):
        print("test_InterruptedMode")
        idn=self.dev.ask(self.Query1)
        self.dev.device_clear(0)
        time.sleep(1)
        if self.dev.get_overlap_mode() == 0:
            self.dev.write(self.Query1)
            self.dev.write(self.Query2)
            r=self.dev.read()
            self.assertEqual(int(r[1]), 1)
        print("end test_Interrupted")
        return 0
    
    def test_OverlappedMode(self):
        print("test_OverlappedMode")
        idn=self.dev.ask(self.Query1)
        self.dev.device_clear(1)
        time.sleep(1)
        if self.dev.get_overlap_mode() == 1:
            self.dev.write(self.Query1)
            self.dev.write(self.Query2)
            self.assertEqual(self.dev.read()[1], idn)
            self.assertEqual(int(self.dev.read()[1]), 1)
        print("end test_OverlappedMode")
        return 0
    
    
    # def test_locking(self):
    #     def check1():
    #     self.dev.request_lock()
    #     c.subprocess(
    #             del dev
    #             dev=HiSLIP(self.hostname)
    #             self.dev.write(Query1)
    #             self.dev.read()
    #             self.dev.status_query()
    #         else:
    #             os.wait()

    def test_device_clear(self):
        print("test_device_clear")
        print("device_clear:Query")
        self.dev.write(self.Query1)
        print("device_clear",self.dev.read())
        self.dev.device_clear(0)
        print("device_clear done")
        time.sleep(1)
        with self.assertRaises(RuntimeError):
            self.dev.read(timeout=3000)
        print("end test_device_clear")
        return 0
    
def suite():
    suite = unittest.TestSuite()
    suite.addTest(HiSLIPFunctionTestMethods("test_connection"))
    suite.addTest(HiSLIPFunctionTestMethods("test_device_clear"))
    suite.addTest(HiSLIPFunctionTestMethods("test_SRQ_status_byte"))
    # suite.addTest(HiSLIPFunctionTestMethods("test_OverlappedMode"))
    # suite.addTest(HiSLIPFunctionTestMethods("test_lock_info"))
    # suite.addTest(HiSLIPFunctionTestMethods("test_InterruptedMode"))
    return suite

if __name__ == '__main__':
    #unittest.main()
    runner = unittest.TextTestRunner()
    runner.run(suite())
    

