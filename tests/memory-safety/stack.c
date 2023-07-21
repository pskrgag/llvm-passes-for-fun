#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s [index]\n", argv[0]);
        return 1;
    }
    int index = atoi(argv[1]);
    int arr[32];
    for (int i = 0; i < 32; ++i) {
        arr[i] = i;
    }

    printf("arr[%d] = 0x%x\n", index, arr[index]);
    return 0;
}
