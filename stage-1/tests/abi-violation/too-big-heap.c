#include <stdint.h>
#include "common.h"

// Fails only in case of preallocation
int main(void) {
    StellaGcConfig cfg = config(SIZE_MAX);
    stella_gc_init(&cfg);
    return 0;
}