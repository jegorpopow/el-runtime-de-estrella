#include "common.h"

int main(void) {
    StellaGcConfig cfg = config(4096);
    stella_gc_init(&cfg);
    StellaValue v = NULL;
    stella_gc_push_root(&v);
    stella_gc_read_managed(&v, 0);
    return 0;
}