#include "common.h"

int main(void) {
    StellaGcConfig cfg = config(4096);
    stella_gc_init(&cfg);
    stella_gc_update_object(NULL, &data2, NULL, NULL);
    return 0;
}
