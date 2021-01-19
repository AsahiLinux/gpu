wrap.dylib:
	clang lib/tiling.c wrap/*.c -dynamiclib -o wrap.dylib -framework IOKit -g -Wall -Werror -Wextra -Wno-unused-variable -Wno-unused-function
