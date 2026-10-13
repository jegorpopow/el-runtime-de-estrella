#include "common.h"

int main(void) {
    StellaGcConfig cfg = config(4096);
    stella_gc_init(&cfg);
    StellaValue v       = NULL;
    StellaValue objroot = NULL;
    stella_gc_push_root(&objroot);
    stella_gc_push_root(&v);
    stella_gc_write_managed(&objroot, 0, &v);
    return 0;
}