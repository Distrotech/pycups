from distutils.core import setup, Extension
VERSION="1.9.57"
setup (name="pycups",
       version=VERSION,
       ext_modules=[Extension("cups",
                              ["cupsmodule.c", "cupsconnection.c",
                               "cupsppd.c", "cupsipp.c"],
                              libraries=["cups"],
                              define_macros=[("VERSION", '"%s"' % VERSION)])])
