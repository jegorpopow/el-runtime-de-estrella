#include "common.h"

int main(void) {
    StellaGcConfig cfg = config(4096);
    stella_gc_init(&cfg);
    StellaValue v = NULL;
    stella_gc_pop_root(&v);
    return 0;
}
