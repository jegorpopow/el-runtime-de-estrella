#include "common.h"

int main(void) {
    StellaGcConfig cfg = config(4096);
    stella_gc_init(&cfg);
    stella_object_init_managed(NULL, 0, NULL);
    return 0;
}