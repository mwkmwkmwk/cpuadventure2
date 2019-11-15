all: emu enemy.nb game.nb pc.nb valis.nb

emu: emu.c
	gcc -O2 emu.c -o emu

%.nb: %.tras tras.py
	python tras.py $< $@
