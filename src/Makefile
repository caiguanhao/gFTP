ifeq ($(shell if test -d /usr/lib/geany; then echo true; else echo false; fi), true)
    libdir = /usr/lib
else
    ifeq ($(shell if test -d /usr/lib/x86_64-linux-gnu/geany; then echo true; else echo false; fi), true)
        libdir = /usr/lib/x86_64-linux-gnu
    else
        $(error You need to install Geany first.);
    endif
endif

all: gFTP.so

gFTP.so: gFTP.c
	gcc -shared -Wall -fPIC -o "gFTP.so" "gFTP.c" `pkg-config libxml-2.0 --cflags --libs libcurl geany`

clean:
	rm -f gFTP.so

install: gFTP.so
	sudo cp gFTP.so $(libdir)/geany/gFTP.so

uninstall:
	sudo rm -f $(libdir)/geany/gFTP.so
