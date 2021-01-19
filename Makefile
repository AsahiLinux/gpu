wrap.dylib:
	clang lib/*.c wrap/*.c -I lib/ -dynamiclib -o wrap.dylib -framework IOKit -g -Wall -Werror -Wextra -Wno-unused-variable -Wno-unused-function
