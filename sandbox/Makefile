all: host.so stub.elf

host.so: host.c
	clang -shared -fPIC $^ -O2 -o $@ -fuse-ld=mold -Wl,--spare-program-headers=7

stub.elf: add.c stub.c
	x86_64-lfi-linux-musl-clang $^ -O2 -o $@ -static-pie

libadd.a: add.o
	ar rcs $@ $<
add.o: add.c
	x86_64-lfi-linux-musl-clang add.c -c -O2
