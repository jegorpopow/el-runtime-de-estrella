#include "common.h"
static StellaObjectDescriptor const datap = {
    .abi_version     = STELLA_C_GC_ABI_VERSION,
    .kind            = STELLA_OBJECT_DATA,
    .flags           = 0,
    .entry_arity     = 0,
    .managed_count   = 0,
    .primitive_count = 1,
    .slot_capacity   = 1,
    .entry_code      = NULL,
    .debug_name      = "datap"
};

int main(void) {
    StellaGcConfig cfg = config(4096);
    stella_gc_init(&cfg);
    StellaValue obj = stella_gc_alloc(&datap);
    stella_object_write_primitive(obj, 5, 0);
    return 0;
}