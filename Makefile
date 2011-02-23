NAME=pycups
VERSION:=$(shell python setup.py --version)
RPMCONFIGDIR:=$(shell rpm -E "%{_rpmconfigdir}" 2>/dev/null || :)

SOURCES=cupsmodule.c cupsconnection.c cupsppd.c cupsipp.c setup.py \
	cupsppd.h cupsipp.h cupsconnection.h cupsmodule.h

DIST=Makefile test.py \
	examples \
	COPYING NEWS README TODO ChangeLog

cups.so: $(SOURCES)
	python setup.py build
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

install:	install-rpmhook
	ROOT= ; \
	if [ -n "$$DESTDIR" ]; then ROOT="--root $$DESTDIR"; fi; \
	python setup.py install $$ROOT

install-rpmhook:
	if [ -n "$(RPMCONFIGDIR)" ]; then \
		RPMCONFIG="$$DESTDIR$(RPMCONFIGDIR)" ; \
		mkdir -p "$$RPMCONFIG"/fileattrs ; \
		install -m0644 psdriver.attr "$$RPMCONFIG"/fileattrs/ ; \
		install -m0644 postscriptdriver.prov "$$RPMCONFIG"/ ; \
	fi

.PHONY: doc clean dist install install-rpmhook

