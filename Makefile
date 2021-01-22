clean:
	rm -f wrap.dylib demo-bin

wrap.dylib:
	clang lib/*.c wrap/*.c -I lib/ -dynamiclib -o wrap.dylib -framework IOKit -g -Wall -Werror -Wextra -Wno-unused-variable -Wno-unused-function

demo-bin:
	clang lib/*.c demo/*.c disasm/*.c -I lib/ -I /opt/X11/include -L /opt/X11/lib/ -lX11 -o demo-bin -framework IOKit -g -Wall -Werror -Wextra -Wno-unused-variable -Wno-unused-function
