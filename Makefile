XA = xa

.PHONY: all clean

all: vm sys.bin

vm: vm.c
	$(CC) -g -o vm vm.c -ledit

sys.bin sys.lbl: sys.s fs/sysvm.n
	$(XA) -C -XMASM -XCA65 -P sys.lst -o sys.bin sys.s

clean:
	rm -f vm
	rm -f sys.bin sys.lst

