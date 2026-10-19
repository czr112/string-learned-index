#include <stdlib.h>
#include <string.h>
volatile unsigned allocation_shared_sink;
int allocation_shared_fixture(void) {
    unsigned char* a=malloc(61);unsigned char* b=calloc(3,23);
    if(!a||!b)return 0;
    memset(a,7,61);a=realloc(a,127);if(!a)return 0;
    allocation_shared_sink=a[0]+b[0];free(a);free(b);return 1;
}
