
#include <stdio.h>
#include <stdlib.h>
#include <err.h>

extern void
agx_disassemble(void *_code, size_t maxlen, FILE *fp);

int main(int argc, char **argv)
{
	--argc;
	++argv;
	if (argc != 2)
		errx(1, "usage: disasm-bin FILE hex-offset");	

	FILE *f = fopen(argv[0], "rb");
	if (!f)
		err(2, "input file");

	off_t offset = strtol(argv[1], NULL, 16);
	fseek(f, offset, SEEK_SET);

	char buf[4096];
	int n = fread(buf, 1, sizeof(buf), f);
	fclose(f);

	agx_disassemble(buf, n, stdout);
	return 0;
}
