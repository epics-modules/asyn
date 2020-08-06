#!python3
# -*- coding:utf-8 -*-
"""

"""
import unittest

import os,sys

from cPyHiSLIP import HiSLIP

class HiSLIPFunctionTestMethods(unittest.TestCase):
    Query1=b"*IDN?"
    Query2=b"*OPC?"
    EnableSRQ=b"*ESE 32;*SRE 48"
    
    @classmethod
    def setUpClass(cls):
        cls.hostname=b"172.28.68.228"
        
    def test_connection(self):
        dev=HiSLIP()
        dev.connect(self.hostname)
        dev.write(self.Query1)
        self.assertTrue(dev.read() != None)
        del dev
        
    def test_SRQ_status_byte(self):
        dev=HiSLIP(self.hostname)
        dev.write(self.EnableSRQ)
        dev.write(self.Query2)
        dev.wait_Service_Request(10000)
        st=dev.status_query()
        self.assertEqual(st&0x20,0x20)
        dev.read()
        st=dev.status_query()
        self.assertEqual(st&0x20,0x00)
        del dev
        
    def test_deivce_celar(self):
        dev=HiSLIP(self.hostname)
        dev.write(self.Query1)
        dev.device_clear()
        asserRaises(dev.read(timeout=3000),RuntimeError)
        del dev
        
    def test_Interrupt(self):
        dev=HiSLIP(self.hostname)
        idn=dev.ask(self.Query1)[1]
        if not dev.get_overlap_mode( ):
            dev.write(self.Query1)
            dev.write(self.Query2)
            r=dev.read()
            self.assertEqual(r, "1")
        else:
            dev.write(self.Query1)
            dev.write(self.Query2)
            r=dev.read()
            self.assertEqual(r, idn)
            self.assertEqual(dev.read(), "1")
        del dev
        
    # def test_locking(self):
    #     with HiSLIP(self.hostname) as dev:
    #         dev.request_lock()
    #         if os.fork():
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
        ret=dev.request_lock()
        self.assertEqual(ret,0)
        ret=dev.release_lock()
        self.assertEqual(ret,1)            
        ret=dev.release_lock()
        self.assertEqual(ret,2)            
        del dev
        
if __name__ == "__main__":
    unittest.main()
        

    
