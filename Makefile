CC=gcc
DEBUG=true
LDFLAGS=
CFLAGS=

ifeq ($(DEBUG),true)
LDFLAGS+=-g
CFLAGS+=-g
endif

ifeq ($(OS),Windows_NT)
CFLAGS=
LDFLAGS+=-liconv -static
endif

all: fathuman

fathuman: main.o interact.o fatfs/ff.o
	$(CC) $^ -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

clean:
	rm -f fathuman fathuman.exe *.o fatfs/*.o
