README for cPyHiSLIP

Author

    山本　昇 Noboru Yamamoto

Organization

    高エネルギー加速器研究機構 加速器研究施設 Accelerator Control Group,
    Accelerator Laboratory, KEK, JAPAN J-PARCセンタ 加速器ディビジョン
    制御グループ Control Groups Accelerator Division JPARC Center

Address

    〒305-0801 茨城県つくば市大穂1-1 1-1 Oho Tsukuba, Ibaraki, JAPAN
    〒319-1195 茨城県那珂郡東海村大字白方2-4 J-PARC中央制御棟 2-4
    Shirakata, Tokai, Naka Ibaraki, 319-1195 JAPAN

How to install

you need cython to build cPyHiSLIP module from cPyHiSLIP.pyx and
cPyHiSLIP.pxd. If you dont have installed cython, try:

  pip install cython

or

  python -m pip install cython

To build and install the module, run:

  python setup.py install
