#include "common.h"

int main(void) {
    StellaGcConfig cfg = config(4096);
    stella_gc_init(&cfg);
    StellaObjectDescriptor bad = data2;
    bad.managed_count          = 3;
    stella_gc_alloc(&bad);
    return 0;
}