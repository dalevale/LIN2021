#include <linux/errno.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdio.h>
#define __NR_LEDCTL	333

long ledctl(unsigned int mask) {
	return (long) syscall(__NR_LEDCTL, mask);
}

int main(int argc, char* argv[]) {
	int mask;	
	int ret = 0;
	if ((argc == 2) && sscanf(argv[1], "%x", &mask)==1)
		ret = ledctl(mask);
	else
		printf("Usage: ./ledctl_invoke <ledmask> \n");
	return ret;
}