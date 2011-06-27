from distutils.core import setup, Extension
import sys
VERSION="1.9.57"
libraries=["cups"]

if sys.platform == "darwin":
	libraries.append ("iconv")

setup (name="pycups",
       version=VERSION,
       ext_modules=[Extension("cups",
                              ["cupsmodule.c", "cupsconnection.c",
                               "cupsppd.c", "cupsipp.c"],
                              libraries=libraries,
                              define_macros=[("VERSION", '"%s"' % VERSION)])])
