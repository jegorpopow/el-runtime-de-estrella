#include "common.h"

int main(void) {
    StellaGcConfig cfg = config(4096);
    stella_gc_init(&cfg);
    StellaValue obj = stella_gc_alloc(&data1);
    StellaValue r   = obj;
    stella_gc_push_root(&r);
    stella_gc_write_managed(&r, 0, NULL);
    return 0;
}