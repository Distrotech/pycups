NAME=pycups
VERSION=1.9.24
TAG=`echo $(NAME)-$(VERSION) | tr . _`

PYTHONVERS = python2.4

SOURCES=cupsmodule.c cupsconnection.c cupsppd.c setup.py \
	cupsppd.h cupsconnection.h cupsmodule.h

DIST=Makefile test.py \
	examples/cupstree.py \
	COPYING NEWS README TODO ChangeLog

cups.so: $(SOURCES)
	CFLAGS=-DVERSION=\\\"$(VERSION)\\\" python setup.py build
	mv build/lib*/$@ .

clean:
	-rm -rf build cups.so *.pyc *~

tag:
	cvs tag -c $(TAG)

dist:
	cvs export -r $(TAG) $(NAME)
	mkdir $(NAME)-$(VERSION)
	cd $(NAME); cp -a $(SOURCES) $(DIST) ../$(NAME)-$(VERSION); cd ..
	tar jcf $(NAME)-$(VERSION).tar.bz2 $(NAME)-$(VERSION)
	rm -rf $(NAME)-$(VERSION) $(NAME)

install:
	ROOT= ; \
	if [ -n "$$DESTDIR" ]; then ROOT="--root $$DESTDIR"; fi; \
	python setup.py install $$ROOT

.PHONY: clean tag dist install

