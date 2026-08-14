XA = xa

.PHONY: all clean

all: vm/vm sys.bin

vm/vm:
	$(MAKE) -C vm

sys.bin sys.lst sys.lbl: sys.s
	$(XA) -C -XMASM -XCA65 -P sys.lst -l sys.lbl -o sys.bin sys.s

clean:
	$(MAKE) -C vm clean
	rm -f sys.bin sys.lst sys.lbl

