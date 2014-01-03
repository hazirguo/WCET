CC=gcc
CFLAGS= -O2
DIS=dis
OBJECTS=main.o machine.o misc.o readfile.o  symbol.o eval.o

dis: $(OBJECTS)
	$(CC) $(CFLAGS) $(OBJECTS) -o $(DIS)
clean:
	rm dis $(OBJECTS)


main.o: readfile.h machine.h host.h misc.h machine.def
machine.o: host.h misc.h machine.h  machine.def eval.h regs.h
eval.o: host.h misc.h eval.h machine.h  machine.def
misc.o: host.h misc.h machine.h  machine.def
readfile.o: ecoff.h readfile.h machine.h host.h misc.h  machine.def symbol.h
symbol.o: host.h ecoff.h symbol.h machine.h misc.h  machine.def


