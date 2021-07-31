CC=gcc
PROG=2n
PREFIX=/usr/local/bin

$(PROG):	2n.c Makefile
	$(CC) $(CFLAGS) -o $(PROG) 2n.c

tcc:
	tcc -o $(PROG) 2n.c

debug:
	$(CC) -g -o $(PROG) 2n.c

install:
	cp $(PROG) $(PREFIX)/$(PROG)

.PHONY: install debug tcc
