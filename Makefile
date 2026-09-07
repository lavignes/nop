XA = xa

.PHONY: all clean asm vm

all: asm vm sys.bin

asm:
	$(MAKE) -C asm

vm:
	$(MAKE) -C vm

sys.bin sys.lst sys.lbl: sys.s
	$(XA) -XMASM -XCA65 -P sys.lst -l sys.lbl -o sys.bin sys.s

clean:
	$(MAKE) -C asm clean
	$(MAKE) -C vm clean
	rm -f sys.bin sys.lst sys.lbl

