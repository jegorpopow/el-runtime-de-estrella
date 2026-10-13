#include "common.h"

int main(void) {
    StellaGcConfig cfg = config(4096);
    stella_gc_init(&cfg);
    stella_gc_init(&cfg);
    return 0;
}
