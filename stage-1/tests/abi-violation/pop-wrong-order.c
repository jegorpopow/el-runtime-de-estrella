#include "common.h"

int main(void) {
    StellaGcConfig cfg = config(4096);
    stella_gc_init(&cfg);
    StellaValue a = NULL, b = NULL;
    stella_gc_push_root(&a);
    stella_gc_push_root(&b);
    stella_gc_pop_root(&a);
    return 0;
}
