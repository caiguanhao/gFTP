libdir = $(shell geany --print-prefix|sed -n 3p)/geany

ifeq ($(shell if test -d $(libdir); then echo true; else echo false; fi), false)
$(error You need to install Geany first);
endif

filename = gFTP.so

all: $(filename)

$(filename): gFTP.c
	gcc -shared -Wall -fPIC -o "$(filename)" "gFTP.c" `pkg-config libxml-2.0 --cflags --libs libcurl geany`

clean:
	rm -f gFTP*.so

install:
	cp gFTP*.so $(libdir)
	chmod 644 $(libdir)/gFTP*.so

uninstall:
	rm -f $(libdir)/gFTP*.so
