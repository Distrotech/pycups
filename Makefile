NAME=pycups
VERSION=1.9.36

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
	svn export . $(NAME)
	mkdir $(NAME)-$(VERSION)
	cd $(NAME); cp -a $(SOURCES) $(DIST) ../$(NAME)-$(VERSION); cd ..
	tar jcf $(NAME)-$(VERSION).tar.bz2 $(NAME)-$(VERSION)
	rm -rf $(NAME)-$(VERSION) $(NAME)

install:
	ROOT= ; \
	if [ -n "$$DESTDIR" ]; then ROOT="--root $$DESTDIR"; fi; \
	python setup.py install $$ROOT

.PHONY: doc clean dist install

