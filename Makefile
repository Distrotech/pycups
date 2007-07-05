PYTHONVERS = python2.4

cups.so: cupsmodule.c cupsconnection.c cupsppd.c setup.py
	python setup.py build
	mv build/lib*/$@ .

clean:
	-rm -rf build cups.so *.pyc *~

.PHONY: clean

