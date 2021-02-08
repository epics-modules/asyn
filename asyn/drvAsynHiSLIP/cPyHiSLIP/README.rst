README for cPyHiSLIP
======================


:Author: 山本　昇 
         Noboru Yamamoto
            
:Organization:  高エネルギー加速器研究機構
                加速器研究施設
                Accelerator Control Group,
                Accelerator Laboratory,
                KEK, JAPAN
                J-PARCセンタ
                加速器ディビジョン
                制御グループ
                Control Groups
                Accelerator Division
                JPARC Center
                
:Address: 〒305-0801
          茨城県つくば市大穂1-1
          1-1 Oho
          Tsukuba, Ibaraki,
          JAPAN
          〒319-1195
          茨城県那珂郡東海村大字白方2-4
          J-PARC中央制御棟
          2-4 Shirakata, Tokai, Naka
          Ibaraki, 319-1195
          JAPAN


How to install
--------------
you need cython to build cPyHiSLIP module from cPyHiSLIP.pyx and cPyHiSLIP.pxd.
If you dont have installed cython, try:

  pip install cython

or (I personally recomend this way):

  python -m pip install cython


To build and install the module, run:

 python setup.py build install

You may need a privilege for installation.


How to Use
--------------
A very Simple example:

 import cPyHiSLIP
 device=cPyHiSLIP.HiSLIP(b"xxx.yyy.zzz.ttt")
 print (dev.ask(b"*IDN?")

Note that arguments to the method  are given as bytes, but not strings.
A return value from the method is also anbyte array, not strings.

Methods , .write(), .read() and .ask() , are provided for the communication
through sync channe.

For the communication throug async channel, specfied methods are privided,
such as, device_clear, status_query, trigger_device, remote-local, request_lock,
release_lock, lock_info.

An interrupt mode can be actiavated by:

  dev.device_clear(1)

SRQ support
++++++++++++++++




 
Async designe memo
----------------------

async channelで送受信されるメッセージ

c<s AsyncInterrupted
c<s AsyncServiceRequest

<-> Error
<-> FatalError

c>s AsyncLock
c<s AsyncLockResoponse

c>s AsynLockInfo
c<s AsynLockInfoResponse

c>s AsyncRemoteLocalControl
c<s AsyncRemoteLocalRespon

c>s AsyncDeviceClear
c<s AsyncDeviceClearAcknowledge


c>s AsyncMaximumMessageSize
c<s AsyncMaximumMessageSizeResponse

c>s GetDescriptors
c<s GetDescriptorsResponse

c>s AsyncInitialize
c<s AsyncInitializeResponse

c>s AsyncStatusQuery
c<s AsyncStatusResponse


c>s AsyncStartTLS
c<s AsyncStartTLSResponse

c>s AsyncEndTLS
c<s AsyncEndTLSResponse

c<->s VendorSpecific
