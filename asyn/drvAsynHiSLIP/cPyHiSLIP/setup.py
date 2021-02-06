#!/usr/bin/env python
"""
Author:Noboru Yamamoto, KEK, Japan (c) 2020

contact info: https://souran.kek.jp/kss/top/

Revision Info:
$Author: $
$Date: $ (isodatesec )
$HGdate:  $
$Header:  $
$Id: setup.py  $
$RCSfile:  $
$Revision: $
$Source:  $

change log:
2020/06/25 : created
"""
import os,platform,re,sys,os.path

from Cython.Distutils.extension import Extension
from Cython.Distutils import build_ext
from Cython.Build import cythonize

# python2/python3
extra=dict()

# if sys.version_info >= (3,):
#     extra['use_2to3'] = True
    
try:
   from distutils.command.build_py import build_py_2to3 as build_py #for Python3
except ImportError:
   from distutils.command.build_py import build_py     # for Python2

from distutils.core import setup
#from distutils.extension import Extension
#
rev="0.2.2.3"
#

sysname=platform.system()

ext_modules=[]

# cPyHiSLIP-2.pyx and cPyHiSLIP-3.pyx are hard links to cPyHiSLIP.pyx
cPyHiSLIP_source_PY2="cPyHiSLIP_2.pyx"
cPyHiSLIP_source_PY3="cPyHiSLIP_3.pyx"

if sys.version_info >= (3,):
   PY3=True
   cPyHiSLIP_source=cPyHiSLIP_source_PY3
else:
   PY3=False
   cPyHiSLIP_source=cPyHiSLIP_source_PY2
    
if not os.path.exists(cPyHiSLIP_source):
   os.link("cPyHiSLIP.pyx", cPyHiSLIP_source)
elif not os.path.samefile("cPyHiSLIP.pyx", cPyHiSLIP_source):
   os.remove(cPyHiSLIP_source)
   os.link("cPyHiSLIP.pyx", cPyHiSLIP_source)
   
ext_modules.append(Extension("cPyHiSLIP", 
                             [ cPyHiSLIP_source, # Cython source. i.e. .pyx
                              ] 
                             ,libraries=[]
                             ,depends=["cPyHiSLIP.pxd"] # Cython interface file
                             ,language="c++"
                             ,cython_cplus=True
                             ,undef_macros =[]   # ["CFLAGS"]
                             ,define_macros=[]  # [("USE_TCP_NODELAY",1)]
                             ,extra_compile_args=["-std=c++14", "-O3"],
                             # c++98/c++11/c++14/c++17/c++2a
                             # py2 : c++14,py3:c++2a
))


ext_modules=cythonize( # generate .c files.
   ext_modules,
   compiler_directives={"language_level":"3" if PY3 else "2"}, # "2","3","3str"
)

setup(name="cPyHiSLIP",
      version=rev,
      author="Noboru Yamamoto, KEK, JAPAN",
      author_email = "Noboru.YAMAMOTO@kek.jp",
      description='A Cython based Python module to control devices over HiSLIP protocol.',
      url="http://www-cont.j-parc.jp/",
      classifiers=['Programming Language :: Python',
                   'Programming Language :: Cython',
                   'Topic :: Scientific/Engineering :: Interface Engine/Protocol Translator',
                   ],
      ext_modules=ext_modules,
      cmdclass = {'build_ext': build_ext,
                  # 'build_py':build_py  # for 2to3 
      },
      py_modules=[ ],
      **extra
)
