NAME=pycups
VERSION=1.9.46

SOURCES=cupsmodule.c cupsconnection.c cupsppd.c setup.py \
	cupsppd.h cupsconnection.h cupsmodule.h

DIST=Makefile test.py \
	examples \
	COPYING NEWS README TODO ChangeLog

cups.so: $(SOURCES)
	CFLAGS=-DVERSION=\\\"$(VERSION)\\\" python setup.py build
	mv build/lib*/$@ .

doc:	cups.so
	rm -rf html
	epydoc -o html --html $<

clean:
	-rm -rf build cups.so *.pyc *~

dist:
	rm -rf $(NAME)-$(VERSION)
	mkdir $(NAME)-$(VERSION)
	cp -a $(SOURCES) $(DIST) $(NAME)-$(VERSION)
	tar jcf $(NAME)-$(VERSION).tar.bz2 $(NAME)-$(VERSION)
	rm -rf $(NAME)-$(VERSION)

install:
	ROOT= ; \
	if [ -n "$$DESTDIR" ]; then ROOT="--root $$DESTDIR"; fi; \
	python setup.py install $$ROOT

.PHONY: doc clean dist install

