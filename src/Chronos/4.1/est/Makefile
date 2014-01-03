CC=gcc
CFLAGS=-g -DMEM_FREE
#CFLAGS=-g -D_DEBUG -DMEM_FREE 
#CFLAGS=-g -DMEM_FREE
#CFLAGS=-g -O2 -U_FORTIFY_SOURCE
#CFLAGS=-pg

SS_SRC=$(wildcard ss/*.c)
SRC=main.c common.c isa.c readfile.c cfg.c tcfg.c loops.c options.c \
pipeline.c exegraph.c estimate.c cache.c bpred.c ilp.c \
scp_tscope.c scp_address.c scp_cache.c\
infeasible.c reg.c symexec.c conflicts.c infdump.c address.c unicache.c $(SS_SRC)
OBJ=$(SRC:.c=.o)

all: $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o est

clean:
	rm -f *.o est ss/*.o

depend:
	makedepend -Y -- $(SRC)

# DO NOT DELETE

scp_address.o: scp_address.h cache.h infeasible.h tcfg.h loops.h symexec.h
scp_cache.o: scp_cache.h scp_address.h cache.h infeasible.h tcfg.h loops.h symexec.h
scp_tscope.o: scp_address.h cache.h infeasible.h tcfg.h loops.h symexec.h
main.o: cfg.h isa.h bpred.h common.h tcfg.h cache.h infeasible.h 
common.o: common.h
isa.o: isa.h
readfile.o: cfg.h isa.h
cfg.o: common.h cfg.h isa.h
tcfg.o: common.h tcfg.h cfg.h isa.h
loops.o: common.h loops.h tcfg.h cfg.h isa.h
options.o: common.h
pipeline.o: cfg.h isa.h cache.h bpred.h common.h tcfg.h loops.h pipeline.h
exegraph.o: bpred.h common.h tcfg.h cfg.h isa.h cache.h pipeline.h exegraph.h
estimate.o: bpred.h common.h tcfg.h cfg.h isa.h pipeline.h exegraph.h cache.h
cache.o: common.h tcfg.h cfg.h isa.h cache.h bpred.h loops.h
bpred.o: bpred.h common.h tcfg.h cfg.h isa.h
ilp.o: tcfg.h cfg.h isa.h loops.h bpred.h common.h cache.h pipeline.h
infeasible.o: cfg.h infeasible.h
reg.o: infeasible.h
symexec.o: cfg.h isa.h infeasible.h
conflicts.o: cfg.h isa.h infeasible.h
infdump.o: cfg.h isa.h infeasible.h
ss/eval.o: ss/host.h ss/misc.h ss/eval.h ss/machine.h ss/machine.def
ss/loader.o: ss/host.h ss/misc.h ss/machine.h ss/machine.def ss/loader.h
ss/loader.o: ss/ecoff.h
ss/machine.o: ss/host.h ss/misc.h ss/machine.h ss/machine.def ss/eval.h
ss/machine.o: ss/regs.h
ss/misc.o: ss/host.h ss/misc.h ss/machine.h ss/machine.def
ss/my_opt.o: ss/misc.h ss/host.h ss/machine.h ss/machine.def ss/options.h
ss/my_opt.o: ss/resource.h bpred.h common.h tcfg.h cfg.h isa.h
ss/options.o: ss/host.h ss/misc.h ss/options.h
ss/ss_exegraph.o: bpred.h common.h tcfg.h cfg.h isa.h cache.h pipeline.h
ss/ss_exegraph.o: exegraph.h bpred.h cache.h pipeline.h ss/machine.h
ss/ss_exegraph.o: ss/host.h ss/misc.h ss/machine.def ss/ss_machine.h common.h
ss/ss_isa.o: common.h isa.h ss/ss_isa.h ss/machine.h ss/host.h ss/misc.h
ss/ss_isa.o: ss/machine.def
ss/ss_machine.o: common.h ss/ss_machine.h
ss/ss_readfile.o: ss/ecoff.h ss/symbol.h ss/host.h ss/machine.h ss/misc.h
ss/ss_readfile.o: ss/machine.def cfg.h isa.h common.h
ss/symbol.o: ss/host.h ss/ecoff.h ss/symbol.h ss/machine.h ss/misc.h
ss/symbol.o: ss/machine.def
