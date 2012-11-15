ifeq ($(shell if test -d /usr/lib/geany; then echo true; else echo false; fi), true)
    libdir = /usr/lib
else
    ifeq ($(shell if test -d /usr/lib/x86_64-linux-gnu/geany; then echo true; else echo false; fi), true)
        libdir = /usr/lib/x86_64-linux-gnu
    else
        $(error You need to install Geany first.);
    endif
endif

filename = gFTP.so

ifeq ($(MAKECMDGOALS),)
	ifeq ($(shell if test -f $(libdir)/geany/gFTP.so; then echo true; else echo false; fi), true)
		ifeq ($(shell read -p "'$(libdir)/geany/gFTP.so' already exists. \
		Do you want to create a new instance? [y/N] " REPLY; echo $$REPLY), y)
			filename = gFTP-$(shell date +%Y%m%d%H%M%S).so
		endif
	endif
endif

all: $(filename)

$(filename): gFTP.c
	gcc -shared -Wall -fPIC -o "$(filename)" "gFTP.c" `pkg-config libxml-2.0 --cflags --libs libcurl geany`

clean:
	rm -f gFTP*.so

install:
	sudo cp gFTP*.so $(libdir)/geany/
	sudo chmod 644 $(libdir)/geany/gFTP*.so

uninstall:
	sudo rm -f $(libdir)/geany/gFTP*.so
