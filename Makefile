VERSION=1.9.3
TAG=pycups-`echo $(VERSION) | tr . _`

PYTHONVERS = python2.4

SOURCES=cupsmodule.c cupsconnection.c cupsppd.c setup.py \
	cupsppd.h cupsconnection.h cupsmodule.h

DIST=Makefile test.py \
	COPYING NEWS README TODO ChangeLog

cups.so: $(SOURCES)
	python setup.py build
	mv build/lib*/$@ .

clean:
	-rm -rf build cups.so *.pyc *~

tag:
	cvs tag -c $(TAG)

dist:
	cvs export -r $(TAG) pycups
	mkdir pycups-$(VERSION)
	cd pycups; cp -a $(SOURCES) $(DIST) ../pycups-$(VERSION); cd ..
	tar jcf pycups-$(VERSION).tar.bz2 pycups-$(VERSION)
	rm -rf pycups-$(VERSION) pycups

install:
	python setup.py install

.PHONY: clean tag dist install

