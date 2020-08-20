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
        cls.hostname=b"172.28.68.228"    # Keysight
        # cls.hostname=b"169.254.100.192"  # Kikusui
        
    def test_connection(self):
        dev=HiSLIP()
        dev.connect(self.hostname)
        dev.write(self.Query1)
        self.assertTrue(dev.read() != None)
        del dev
        time.sleep(1)
        return 0
    
    def test_SRQ_status_byte(self):
        dev=HiSLIP(self.hostname)
        dev.device_clear(0)
        time.sleep(1)
        dev.write(self.EnableSRQ)
        dev.write(self.Query2)
        dev.wait_Service_Request(10000)
        #dev.check_SRQ()
        st=dev.status_query()
        print (hex(st))
        self.assertEqual(st & 0x70, 80) # SRQ(64)+ MAV(16)
        dev.read()
        st=dev.status_query()
        self.assertEqual(st &0x70, 0)
        del dev
        time.sleep(1)
        return 0
    
    def test_device_clear(self):
        dev=HiSLIP(self.hostname)
        dev.write(self.Query1)
        dev.device_clear(0)
        time.sleep(1)
        with self.assertRaises(RuntimeError):
            dev.read(timeout=3000)
        del dev
        time.sleep(1)
        return 0
    
    def test_Interrupted(self):
        dev=HiSLIP(self.hostname)
        idn=dev.ask(self.Query1)
        dev.device_clear(0)
        time.sleep(1)
        if dev.get_overlap_mode() == 0:
            dev.write(self.Query1)
            dev.write(self.Query2)
            r=dev.read()
            self.assertEqual(int(r[1]), 1)
        dev.device_clear(0)
        del dev
        time.sleep(1)
        return 0
    
    def test_OverlappedMode(self):
        dev=HiSLIP(self.hostname)
        idn=dev.ask(self.Query1)
        dev.device_clear(1)
        time.sleep(1)
        if dev.get_overlap_mode() == 1:
            dev.write(self.Query1)
            dev.write(self.Query2)
            self.assertEqual(dev.read()[1], idn)
            self.assertEqual(int(dev.read()[1]), 1)
        dev.device_clear(0)
        del dev
        time.sleep(1)
        return 0
    import subprocess
    
    # def test_locking(self):
    #     def check1():
    #         dev=HiSLIP(self.hostname)
            
    #     dev=HiSLIP(self.hostname)
    #     dev.request_lock()
    #     c.subprocess(
    #             del dev
    #             dev=HiSLIP(self.hostname)
    #             dev.write(Query1)
    #             dev.read()
    #             dev.status_query()
    #         else:
    #             os.wait()

    def test_lock_info(self):
        dev=HiSLIP(self.hostname)
        ret=dev.request_lock(b"shared")
        self.assertEqual(ret,1)
        info=dev.lock_info()
        self.assertEqual(info, (0,1))
        ret=dev.request_lock()
        self.assertEqual(ret,1)
        info=dev.lock_info()
        self.assertEqual(info, (1,1))
        
        ret=dev.release_lock()
        self.assertEqual(ret,1)
        info=dev.lock_info()
        self.assertEqual(info, (0,1))
        ret=dev.release_lock()
        self.assertEqual(ret,2)
        info=dev.lock_info()
        self.assertEqual(info, (0,0))
        del dev
        time.sleep(1)
        return 0

if __name__ == "__main__":
    unittest.main()
        

    
