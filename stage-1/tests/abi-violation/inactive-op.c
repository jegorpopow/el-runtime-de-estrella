#include "common.h"

int main(void) {
    StellaGcConfig cfg = config(4096);
    stella_gc_init(&cfg);
    stella_gc_shutdown();
    StellaValue v = NULL;
    stella_gc_push_root(&v);
    return 0;
}