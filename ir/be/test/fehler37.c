#include <stdio.h>

int a[10];

int main() {
	int *p = &a[0];
	int *q = &a[9];

	printf("%u\n", p - q);
}
