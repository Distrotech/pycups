from distutils.core import setup, Extension
setup (name="cups", version="1.0",
       ext_modules=[Extension("cups",
                              ["cupsmodule.c", "cupsconnection.c",
                               "cupsppd.c"],
                              libraries=["cups"])])
