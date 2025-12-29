XA = xa

.PHONY: all clean

all: vm sys.bin

vm: vm.c
	$(CC) -g -o vm vm.c

sys.bin sys.lbl: sys.s fs/sysvm.n
	$(XA) -C -XMASM -XCA65 -l sys.lbl -o sys.bin sys.s

clean:
	rm -f vm
	rm -f sys.bin sys.lbl

