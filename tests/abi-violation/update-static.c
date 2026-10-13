#include "common.h"

static StellaObjectDescriptor const stat2 = {
    .abi_version     = STELLA_C_GC_ABI_VERSION,
    .kind            = STELLA_OBJECT_DATA,
    .flags           = STELLA_DESCRIPTOR_STATIC,
    .entry_arity     = 0,
    .managed_count   = 2,
    .primitive_count = 0,
    .slot_capacity   = 2,
    .entry_code      = NULL,
    .debug_name      = "stat2"
};

int main(void) {
    StellaGcConfig cfg = config(4096);
    stella_gc_init(&cfg);
    StellaValue obj = stella_gc_alloc(&upd2);
    StellaValue r   = obj;
    stella_gc_push_root(&r);
    stella_gc_update_object(&r, &stat2, NULL, NULL);
    return 0;
}