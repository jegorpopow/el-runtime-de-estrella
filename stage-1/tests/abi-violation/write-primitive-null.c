#include "common.h"

int main(void) {
    StellaGcConfig cfg = config(4096);
    stella_gc_init(&cfg);
    stella_object_write_primitive(NULL, 0, 42);
    return 0;
}