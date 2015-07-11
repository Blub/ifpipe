VERSION := 1.0
PREFIX := /usr
BINDIR := $(PREFIX)/bin
SHAREDIR := $(PREFIX)/share
MANDIR := $(SHAREDIR)/man
MAN1DIR := $(MANDIR)/man1

CPPFLAGS += '-DIFPIPE_VERSION="$(VERSION)"'

all: ifpipe

ifpipe: ifpipe.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $<

clean:
	rm -f ifpipe

install: ifpipe
	install -dm0755 $(DESTDIR)$(BINDIR)
	install -m0755 ifpipe $(DESTDIR)$(BINDIR)/ifpipe
	install -dm0755 $(DESTDIR)$(MAN1DIR)
	install -m0644 ifpipe.1 $(DESTDIR)$(MAN1DIR)/ifpipe.1
