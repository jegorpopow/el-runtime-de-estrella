#include "common.h"

int main(void) {
    StellaGcConfig cfg = config(0);
    stella_gc_init(&cfg);
    return 0;
}
