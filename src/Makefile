MACHINE := $(shell uname -m)
ifeq ($(MACHINE), x86_64)
libdir = /usr/lib/x86_64-linux-gnu
endif
ifeq ($(MACHINE), i686)
libdir = /usr/lib
endif

all: gFTP.so

gFTP.so: gFTP.c
	gcc -shared -Wall -fPIC -o "gFTP.so" "gFTP.c" `pkg-config libxml-2.0 --cflags --libs libcurl geany`

clean:
	rm gFTP.so

install: gFTP.so
	sudo cp gFTP.so $(libdir)/geany/gFTP.so

uninstall:
	sudo rm $(libdir)/geany/gFTP.so
