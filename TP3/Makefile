CC = cc
CFLAGS = -Wall -Werror
CREME_VER = 1.0.0
CREME_SO = libcreme.so.$(CREME_VER)

default : servudp cliudp $(CREME_SO) servbeuip clibeuip biceps

$(CREME_SO) : creme.c creme.h
	$(CC) $(CFLAGS) -fPIC -c -o creme.o creme.c
	$(CC) -shared -Wl,-soname,libcreme.so.1 -o $(CREME_SO) creme.o
	ln -sf $(CREME_SO) libcreme.so.1
	ln -sf libcreme.so.1 libcreme.so

cliudp : cliudp.c
	$(CC) $(CFLAGS) -o cliudp cliudp.c

servudp : servudp.c
	$(CC) $(CFLAGS) -o servudp servudp.c

servbeuip : servbeuip.c creme.h $(CREME_SO)
	$(CC) $(CFLAGS) -o servbeuip servbeuip.c -L. -lcreme -Wl,-rpath,'$$ORIGIN'

clibeuip : clibeuip.c creme.h $(CREME_SO)
	$(CC) $(CFLAGS) -o clibeuip clibeuip.c -L. -lcreme -Wl,-rpath,'$$ORIGIN'


# Compilações com e sem TRACE
biceps : biceps.c creme.h $(CREME_SO)
	$(CC) $(CFLAGS) -o biceps biceps.c -L. -lcreme -Wl,-rpath,'$$ORIGIN' -lpthread

biceps-trace : biceps.c creme.h $(CREME_SO)
	$(CC) $(CFLAGS) -DTRACE_ON -o biceps-trace biceps.c -L. -lcreme -Wl,-rpath,'$$ORIGIN' -lpthread


clean :
	rm -f cliudp servudp servbeuip clibeuip biceps creme.o libcreme.so libcreme.so.1 libcreme.so.*

