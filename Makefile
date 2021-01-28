all:
	gcc src/vendotek.c src/vendotek-dbg.c -o vendotek-dbg -Wall -Wno-format
	gcc src/vendotek.c src/vendotek-cli.c -o vendotek-cli -Wall -Wno-format
