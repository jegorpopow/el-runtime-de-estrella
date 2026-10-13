#include <stdint.h>
#include "common.h"

int main(void) {
    StellaGcConfig cfg = config(4096);
    stella_gc_init(&cfg);
    StellaObjectDescriptor bad = {
        .abi_version     = STELLA_C_GC_ABI_VERSION,
        .kind            = STELLA_OBJECT_DATA,
        .flags           = 0,
        .entry_arity     = 0,
        .managed_count   = 0,
        .primitive_count = 0,
        .slot_capacity   = SIZE_MAX,
        .entry_code      = NULL,
        .debug_name      = "overflow"
    };
    stella_gc_alloc(&bad);
    return 0;
}
