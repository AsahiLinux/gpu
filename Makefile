all: wrap.dylib demo-bin disasm-bin
.PHONY: clean all
.SUFFIXES:

clean:
	rm -f wrap.dylib demo-bin

CFLAGS := -g -Wall -Werror -Wextra -Wno-unused-variable -Wno-unused-function
WRAP_SRCS := $(wildcard lib/*.c)\
             $(wildcard wrap/*.c)

wrap.dylib: $(WRAP_SRCS) Makefile
	clang -o $@ $(WRAP_SRCS) -I lib/ -dynamiclib -framework IOKit $(CFLAGS)

DEMO_SRCS := $(wildcard lib/*.c)\
             $(wildcard demo/*.c)\
             $(wildcard disasm/*.c)

demo-bin: $(DEMO_SRCS) Makefile
	clang -o $@ $(DEMO_SRCS) -I lib/ -I /opt/X11/include -L /opt/X11/lib/ -lX11 -framework IOKit $(CFLAGS)

DISASM_SRCS := $(wildcard disasm/*.c)\
             disasm-driver.c

disasm-bin: $(DISASM_SRCS) Makefile
	clang -o $@ $(DISASM_SRCS) $(CFLAGS)
