#include <assert.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdalign.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdnoreturn.h>
#include <stella/gc.h>
#include <string.h>
#include "stella/runtime.h"

/*
  Infrastructure
*/

/// Parses size_t from venv
bool read_env_size(char const* name, size_t* out_value) {
    char const* raw = getenv(name);
    if (raw == NULL || *raw == '\0') {
        return false;
    }

    errno           = 0;
    char* end       = NULL;
    uintmax_t value = strtoumax(raw, &end, 10);

    if (end == raw || *end != '\0' || errno != 0 || value > SIZE_MAX) {
        return false;
    } else {
        *out_value = (size_t)value;
        return true;
    }
}

/// Parses float from venv
bool read_env_float(char const* name, float* out_value) {
    char const* raw = getenv(name);
    if (raw == NULL || *raw == '\0') {
        return false;
    }

    errno       = 0;
    char* end   = NULL;
    float value = strtof(raw, &end);

    if (end == raw || *end != '\0' || errno != 0) {
        return false;
    }

    *out_value = value;
    return true;
}

/// Panic
static noreturn void not_implemented(char const* message) {
    fprintf(stderr, "Not implented: %s\n", message);
    exit(1);
}

/// Format and write message if debug traces are enabled
static void gc_debug_log(char const* format, ...) {
#ifdef STELLA_GC_DEBUG
    va_list args;
    va_start(args, format);
    printf("[gc] ");
    vprintf(format, args);
    printf("\n");
    fflush(stdout);
#endif
}

/// Assert, which throws stella_abi_violation
static void ensure_abi(bool cond, char const* msg) {
    if (!cond) {
        stella_abi_violation(msg);
    }
}

/**
   Arithmetic and address manipulation utils
*/

static size_t max_size(size_t a, size_t b) {
    return (a > b) ? a : b;
}

static bool add_overflows(size_t left, size_t right) {
    return left > SIZE_MAX - right;
}

static bool multiply_overflows(size_t left, size_t right) {
    return right != 0 && left > SIZE_MAX / right;
}

static size_t rounded_size(size_t size, size_t alignment) {
    size_t remainder = size % alignment;
    if (remainder == 0) {
        return size;
    }
    size_t padding = alignment - remainder;
    if (add_overflows(size, padding)) {
        stella_abi_violation("aligned object size overflows size_t");
    }
    return size + padding;
}

/**
Stella object manipulation utilties
*/

typedef StellaValue* StellaRoot;

/// Iterates managed fields by slot pointer
#define STELLA_OBJECT_MANAGED_FIELDS_FOR(object, field)               \
    for (StellaSlot* field = (object)->fields,                        \
                     *_stella_guard =                                 \
                         field + (object)->descriptor->managed_count; \
         field < _stella_guard; ++field)

/// Iterates primitive fields by slot pointer
#define STELLA_OBJECT_PRIMITIVE_FIELDS_FOR(object, field)                    \
    for (StellaSlot* field =                                                 \
             (object)->fields + (object)->descriptor->managed_count,         \
                     *_stella_guard = (object)->fields +                     \
                                      (object)->descriptor->managed_count +  \
                                      (object)->descriptor->primitive_count; \
         field < _stella_guard; ++field)

/// Iterates unused slots by slot pointers
#define STELLA_OBJECT_UNUSED_SLOTS_FOR(object, field)                      \
    for (StellaSlot* field = (object)->fields +                            \
                             (object)->descriptor->managed_count +         \
                             (object)->descriptor->primitive_count,        \
                     *_stella_guard = (object)->fields +                   \
                                      (object)->descriptor->slot_capacity; \
         field < _stella_guard; ++field)

/// Ensures descriptor invariants
static void validate_descriptor(StellaObjectDescriptor const* descriptor) {
    ensure_abi(descriptor != NULL, "null object descriptor");
    ensure_abi(
        descriptor->abi_version == STELLA_C_GC_ABI_VERSION,
        "incompatible descriptor ABI version"
    );

    ensure_abi(
        descriptor->kind >= STELLA_OBJECT_DATA &&
            descriptor->kind <= STELLA_OBJECT_INDIRECTION,
        "unknown object kind"
    );

    uint32_t const known_flags =
        STELLA_DESCRIPTOR_STATIC | STELLA_DESCRIPTOR_UPDATABLE;
    ensure_abi(
        (descriptor->flags & ~known_flags) == 0, "unknown descriptor flag"
    );
    ensure_abi(
        !((descriptor->flags & STELLA_DESCRIPTOR_STATIC) != 0 &&
          (descriptor->flags & STELLA_DESCRIPTOR_UPDATABLE) != 0),
        "static descriptor is marked updatable"
    );
    ensure_abi(
        !add_overflows(descriptor->managed_count, descriptor->primitive_count),
        "active field count overflows size_t"
    );
    ensure_abi(
        descriptor->managed_count + descriptor->primitive_count <=
            descriptor->slot_capacity,
        "active fields exceed slot capacity"
    );

    bool const is_executable = descriptor->kind == STELLA_OBJECT_CLOSURE ||
                               descriptor->kind == STELLA_OBJECT_THUNK;
    bool const is_inexecutable = descriptor->kind == STELLA_OBJECT_DATA ||
                                 descriptor->kind == STELLA_OBJECT_INDIRECTION;

    ensure_abi(
        !(is_executable && descriptor->entry_code == NULL),
        "executable descriptor has no entry code"
    );

    ensure_abi(
        !(is_inexecutable && descriptor->entry_code != NULL),
        "non-executable descriptor has entry code"
    );

    ensure_abi(
        !(descriptor->kind == STELLA_OBJECT_THUNK &&
          descriptor->entry_arity != 0),
        "thunk descriptor has non-zero entry arity"
    );
    ensure_abi(
        !(is_inexecutable && descriptor->entry_arity != 0),
        "non-executable descriptor has non-zero entry arity"
    );
}

/// Returns object size in bytes, including header and fields
static size_t object_size(StellaObjectDescriptor const* descriptor) {
    ensure_abi(
        !multiply_overflows(descriptor->slot_capacity, sizeof(StellaSlot)),
        "object slot capacity overflows size_t"
    );
    size_t slots = descriptor->slot_capacity * sizeof(StellaSlot);
    ensure_abi(
        !add_overflows(sizeof(StellaObject), slots),
        "object size overflows size_t"
    );
    return sizeof(StellaObject) + slots;
}

/// Returns address of the next object in case of dense packing
static StellaValue next_object(StellaValue object) {
    return (StellaValue)((char*)object + object_size(object->descriptor));
}

/// Fills header
static void initialise_object(
    StellaValue object,
    StellaObjectDescriptor const* descriptor,
    uintptr_t gc_word
) {
    object->descriptor = descriptor;
    object->gc_word    = gc_word;
    object->fields =
        descriptor->slot_capacity == 0 ? NULL : (StellaSlot*)(object + 1);
}

/// Fills slots with default (NULL or 0 depends on kind) values
static void nullify_object(StellaValue object) {
    STELLA_OBJECT_MANAGED_FIELDS_FOR(object, slot) {
        slot->managed = NULL;
    }
    STELLA_OBJECT_PRIMITIVE_FIELDS_FOR(object, slot) {
        slot->primitive = 0;
    }
    STELLA_OBJECT_UNUSED_SLOTS_FOR(object, slot) {
        slot->primitive = 0;
    }
}

/// Fills slots of `value` with values from `source` slots
static void copy_object(StellaValue dest, StellaValue source) {
    ensure_abi(
        dest->descriptor == source->descriptor,
        "copy to object with different structure"
    );

    if (dest->descriptor->slot_capacity != 0) {
        memcpy(
            dest->fields, source->fields,
            dest->descriptor->slot_capacity * sizeof(StellaSlot)
        );
    }
}

/**
 Bakers algorithm implementation
*/

/// C++ std::vector-like interface for stacks of roots
// empty_roots_vector(capacity) - constructs empty stack, reserves memory
// ROOTS_VECTOR_FOR - iterates over stack elements by StellaRoot pointer
// roots_vector_push - put new root to stack top
// roots_vector_pop - get root from stack top
typedef struct {
    StellaRoot* roots;
    size_t size;
    size_t capacity;
    size_t max_size;
} roots_vector;

#define ROOTS_VECTOR_FOR(vec, slot)                                         \
    for (StellaRoot* slot           = (vec).roots,                          \
                     *_stella_guard = (vec).size ? (vec).roots + (vec).size \
                                                 : (vec).roots;             \
         slot < _stella_guard; ++slot)

static roots_vector empty_roots_vector(size_t initial_capacity) {
    if (initial_capacity == 0) {
        return (roots_vector){
            .roots = NULL, .size = 0, .capacity = 0, .max_size = 0
        };
    }

    StellaValue** allocated = calloc(sizeof *allocated, initial_capacity);
    ensure_abi(allocated != NULL, "root stack exhaustion");
    return (roots_vector){.roots    = allocated,
                          .size     = 0,
                          .capacity = initial_capacity,
                          .max_size = 0};
}

static void roots_vector_push(roots_vector* vec, StellaValue* root) {
    ensure_abi(vec->size <= vec->capacity, "Roots stack corrupted");
    if (vec->size == vec->capacity) {
        size_t base_capacity = vec->capacity == 0 ? 8 : vec->capacity;
        ensure_abi(
            !multiply_overflows(base_capacity, 2),
            "root stack capacity overflows size_t"
        );
        size_t new_capacity = base_capacity * 2;
        ensure_abi(
            !multiply_overflows(new_capacity, sizeof(vec->roots[0])),
            "root stack allocation overflows size_t"
        );
        StellaValue** allocated =
            realloc(vec->roots, new_capacity * sizeof(vec->roots[0]));
        ensure_abi(allocated != NULL, "root stack exhaustion");
        vec->roots    = allocated;
        vec->capacity = new_capacity;
    }

    vec->roots[vec->size++] = root;
    vec->max_size           = max_size(vec->max_size, vec->size);
}

static StellaObject** roots_vector_pop(roots_vector* vec) {
    ensure_abi(vec->size > 0, "roots stack underflow");
    StellaValue* root = vec->roots[vec->size - 1];
    vec->size--;
    return root;
}

static void roots_vector_destroy(roots_vector* vec) {
    if (vec->roots != NULL) {
        free(vec->roots);
        vec->roots    = NULL;
        vec->capacity = 0;
        vec->size     = 0;
    } else {
        ensure_abi(
            vec->capacity == 0 && vec->size == 0,
            "roots stack invariants violation"
        );
    }
}

/// Memory span representation (semiinterval of addresses)
// region_end - return one-past-last pointer for region
// belongs_to - checks whether pointrer lays inside the region
typedef struct {
    char* base;
    size_t length_bytes;
} memory_region;

static memory_region const EMPTY_REGION = {.base = NULL, .length_bytes = 0};

static char* region_end(memory_region region) {
    return region.base + region.length_bytes;
}

static bool belongs_to(void* pointer, memory_region region) {
    // Technically UB, but who cares?
    return pointer != NULL && (char*)pointer >= region.base &&
           (char*)pointer < region_end(region);
}

typedef struct {
    memory_region first;
    memory_region second;
} memory_region_pair;

/// Creates a memory region with known begin and end
memory_region memory_region_from_bounds(char* start, char* end) {
    ensure_abi(start <= end, "Wrong memory region bounds");
    return (memory_region){.base         = start,
                           .length_bytes = (size_t)(end - start)};
}

/// Allocates two consequent memory_regions of length to_bytes
memory_region_pair allocate_semispaces(size_t length_bytes) {
    ensure_abi(
        !multiply_overflows(length_bytes, 2),
        "semispace size too large for 2 * length_bytes to be allocated"
    );

    length_bytes = rounded_size(
        length_bytes, max_size(alignof(StellaObject), alignof(StellaSlot))
    );

    char* base = malloc(2 * length_bytes);
    ensure_abi(base != NULL, "Semispaces allocation failed");
    return (memory_region_pair){
        .first  = {.base = base, .length_bytes = length_bytes},
        .second = {.base = base + length_bytes, .length_bytes = length_bytes}
    };
}

/// Deallocates two consequent memory_regions
void destroy_semispaces(memory_region_pair semispaces) {
    if (semispaces.first.base < semispaces.second.base) {
        free(semispaces.first.base);
    } else {
        free(semispaces.second.base);
    }
}

/// Algorithm-specific settings
typedef struct {
    size_t scan_per_step;  // Number of object scanned on collection point
    size_t forced_scan_threshold;  // Number of free bytes to force non-budgeted
                                   // scanning
    int collection_sites;  // Bitmask of types of collection points to perform
                           // scanning steps
} bakers_gc_config;

/// Garbage collection context
typedef struct {
    // Managed memory
    memory_region from_space;
    memory_region to_space;

    // Algorithm variables
    void* scan;
    void* next;
    void* limit;

    // Performing flip
    bool flip;

    // Roots
    roots_vector dynamic_roots;
    roots_vector permanent_roots;

    // Configs
    StellaGcConfig stella_config;
    bakers_gc_config config;

    // Stats
    struct {
        size_t read_barriers;
        size_t collection_intiations;
        size_t collections_finished;
        size_t failed_allocations;
        size_t forced_scannings;
        size_t non_budgeted_scans;
        size_t budgeted_scans;
        size_t copies_performed;
        size_t occupied_bytes;
        size_t max_occupied_bytes;
    } stats;

    memory_region_pair from_space_objects;  // [start:next] and [limit:end]
                                            // subregions of from-space
} bakers_garbage_collector;

/// Returns number of unoccupied bytes in to-space
static ptrdiff_t free_space_left(bakers_garbage_collector* gc) {
    return (char*)(gc->limit) - (char*)(gc->next);
}

/// Set stats.occupied_bytes, updated stats.max_occupied_bytes
static void update_occupied_bytes(bakers_garbage_collector* gc, size_t n) {
    gc->stats.occupied_bytes = n;
    gc->stats.max_occupied_bytes =
        max_size(gc->stats.max_occupied_bytes, gc->stats.occupied_bytes);
}

/// (Re)intialise to-/from- spaces, sets invariants
static void set_spaces(
    bakers_garbage_collector* gc,
    memory_region from_space,
    memory_region to_space
) {
    gc->from_space = from_space;
    gc->to_space   = to_space;
    gc->next       = gc->to_space.base;
    gc->scan       = gc->to_space.base;
    gc->limit      = region_end(gc->to_space);
}

/// Creates new garbage collector
bakers_garbage_collector create_garbage_collector(
    StellaGcConfig const* stella_config,
    bakers_gc_config const* bakers_config
) {
    memory_region_pair semispaces =
        allocate_semispaces(stella_config->max_heap_bytes);
    bakers_garbage_collector gc = {
        .flip            = false,
        .dynamic_roots   = empty_roots_vector(0),
        .permanent_roots = empty_roots_vector(0),
        .stella_config   = *stella_config,
        .config          = *bakers_config,
        .stats =
            {
                .collection_intiations =
                    1,  // Initial state is an empty collection
            },
        .from_space_objects = {
            // No object in from-space of empty heap
            .first  = {.base = semispaces.first.base, .length_bytes = 0},
            .second = {.base = region_end(semispaces.first), .length_bytes = 0}
        }
    };
    set_spaces(&gc, semispaces.first, semispaces.second);
    return gc;
}

/// Destroys garbage collector, frees used resources
void destroy_garbage_collector(bakers_garbage_collector* gc) {
    destroy_semispaces((memory_region_pair){.first  = gc->to_space,
                                            .second = gc->from_space});
    gc->to_space   = EMPTY_REGION;
    gc->from_space = EMPTY_REGION;
    gc->scan       = NULL;
    gc->next       = NULL;
    gc->limit      = NULL;

    ensure_abi(
        gc->dynamic_roots.size == 0,
        "runtime shutdown with dynamic roots registered"
    );
    roots_vector_destroy(&gc->dynamic_roots);
    roots_vector_destroy(&gc->permanent_roots);
}

static uintptr_t const NO_FORWARDING = (uintptr_t)NULL;

// Return address to which `value` is forwarded
static StellaValue forwarded_address(StellaValue value) {
    static_assert(
        sizeof(StellaValue) == sizeof value->gc_word,
        "GC word does can not contain pointer"
    );
    ensure_abi(
        value->gc_word != NO_FORWARDING,
        "get forwarding of non-forwarded object"
    );
    return (StellaValue)(value->gc_word);
}

/// Checks whether `value` was forwarded
static bool is_forwarded(StellaValue value) {
    return value->gc_word != NO_FORWARDING;
}

/// Forwards `value` to `target`, sets flag in value header
static void forward_to(StellaValue value, StellaValue target) {
    ensure_abi(!is_forwarded(value), "double forwarding occured");
    ensure_abi(target != NULL, "forwarding to null");
    value->gc_word = (uintptr_t)target;
}

/// Copies `p` and list of referenced by it objects from from-space to to-space
/// via semi-dfs
static void chase(bakers_garbage_collector* gc, StellaValue p) {
    ensure_abi(p != NULL, "Null pointer exception");
    while (p != NULL) {
        ensure_abi(belongs_to(p, gc->from_space), "chase called from to-space");

        size_t record_size = object_size(p->descriptor);
        if (free_space_left(gc) < record_size) {
            // Freshly allocated (possibly reachable) + reachable from roots
            // during initiations is bigger than max_heap_size, no guarantee of
            // successfull garbage collection
            stella_gc_out_of_memory(record_size);
        }

        // Copy object to to-space
        StellaValue q = gc->next;
        initialise_object(q, p->descriptor, NO_FORWARDING);
        copy_object(q, p);
        forward_to(p, q);
        gc->next = next_object(q);

        gc_debug_log(
            "Copied object of size %zu from %p to %p",
            object_size(p->descriptor), p, q
        );
        gc->stats.copies_performed++;

        // SemiDFS branch, visits non-forwarded neighbours
        p = NULL;
        STELLA_OBJECT_MANAGED_FIELDS_FOR(q, slot) {
            if (belongs_to(slot->managed, gc->from_space) &&
                !is_forwarded(slot->managed)) {
                p = slot->managed;
                break;
            }
        }
    }
}

/// Forwards reference `v`, copies refrences object, if needed
static StellaValue forward(bakers_garbage_collector* gc, StellaValue v) {
    if (!belongs_to(v, gc->from_space)) {
        return v;
    }
    if (is_forwarded(v)) {
        return forwarded_address(v);
    }

    chase(gc, v);
    ensure_abi(is_forwarded(v), "Chase produced non-forwarded object");
    return forwarded_address(v);
}

/// Proccesses one `grey` object in to-space in FIFO order
static void scan(bakers_garbage_collector* gc) {
    gc_debug_log("Scanning %p", gc->scan);

    StellaValue u = gc->scan;
    STELLA_OBJECT_MANAGED_FIELDS_FOR(u, f) {
        f->managed = forward(gc, f->managed);
    }
    gc->scan = next_object(u);
}

/// Collection point type
typedef enum {
    CS_ALLOCATION    = 1,
    CS_READ_BARRIER  = 2,
    CS_WRITE_BARRIER = 4,
    CS_UPDATE        = 8,
} collection_site_t;

/// Runs budgeted for each garbage collection point amount of steps
static void
scan_budgeted(bakers_garbage_collector* gc, collection_site_t collection_site) {
    if (gc->config.collection_sites & collection_site) {
        gc_debug_log("Performing budgeted scanning steps");
        for (size_t i = 0; i < gc->config.scan_per_step; i++) {
            if (gc->scan < gc->next) {
                scan(gc);
                gc->stats.budgeted_scans++;
            }
        }
    }
}

// Frees from-space, switches from- and to- spaces, forwards roots
static void flip(bakers_garbage_collector* gc) {
    ensure_abi(!gc->flip, "recursive flip occured");
    gc->flip = true;

    if (gc->scan < gc->next) {
        // Forced scanning, non-budgeted scans
        gc_debug_log(
            "Forcing non-budgeted scanning of %zi bytes",
            (char*)gc->next - (char*)gc->scan
        );
        gc->stats.forced_scannings++;
        while (gc->scan < gc->next) {
            scan(gc);
            gc->stats.non_budgeted_scans++;
        }
    }

    ensure_abi(
        gc->scan == gc->next,
        "non-scanned objects left before swap of spaces"
    );  // From-space contains only garbage

    gc->stats.collections_finished++;
    gc->stats.collection_intiations++;

    // Swap spaces
    gc->from_space_objects.first =
        memory_region_from_bounds(gc->to_space.base, gc->next);
    gc->from_space_objects.second =
        memory_region_from_bounds(gc->limit, region_end(gc->to_space));
    set_spaces(gc, gc->to_space, gc->from_space);

    update_occupied_bytes(
        gc, gc->from_space_objects.first.length_bytes +
                gc->from_space_objects.second.length_bytes
    );

    // Forward roots
    ROOTS_VECTOR_FOR(gc->permanent_roots, root) {
        if (**root != NULL) {
            **root = forward(gc, **root);
        }
    }
    ROOTS_VECTOR_FOR(gc->dynamic_roots, root) {
        if (**root != NULL) {
            **root = forward(gc, **root);
        }
    }

    gc->flip = false;
}

/// Reserves `size` bytes in the end of to-space
static void* allocate(bakers_garbage_collector* gc, size_t size) {
    uintmax_t free_space = free_space_left(gc);
    if (free_space < size || free_space < gc->config.forced_scan_threshold) {
        // No space left, new iteration needed
        flip(gc);
    }

    if ((uintmax_t)free_space_left(gc) < size) {
        gc->stats.failed_allocations++;
        stella_gc_out_of_memory(size);
    }

    gc->limit = (char*)gc->limit - size;
    update_occupied_bytes(gc, gc->stats.occupied_bytes + size);
    ensure_abi(
        (uintptr_t)gc->limit % alignof(StellaObject) == 0,
        "non-aligned allocation occured"
    );
    return gc->limit;
}

/// Performs managed read by forwarding pointer
static StellaValue
read_barrier(bakers_garbage_collector* gc, StellaValue object, size_t index) {
    ensure_abi(
        index < object->descriptor->managed_count,
        "Barrier called on non-managed field"
    );
    StellaValue forwarded = forward(gc, object->fields[index].managed);
    if (forwarded != object->fields[index].managed) {
        gc->stats.read_barriers++;
    }
    object->fields[index].managed = forwarded;
    return forwarded;
}

/**
   Statistics
*/

typedef struct {
    // Allocation stats
    size_t allocated_bytes;     // Total stella_gc_alloc demand
    size_t allocated_objects;   // Total stella_gc_alloc call count
    size_t failed_allocations;  // Number of failed allocations, not more than 1

    // Collection stats
    size_t collections_intiatiated;  // Number of started collections, starting
                                     // configuration is considered a collection
                                     // start
    size_t collections_finished;     // Number of ended collections,
                                     // shall be collections_intiations + 1

    // Occupied bytes stats (in terms of inceremental collecting)
    size_t occupied_bytes;          // Currently occupied bytes
    size_t maximum_occupied_bytes;  // Maximum occupied bytes

    // Scanning stats
    size_t budgeted_steps;     // Number of object scanned during budgeted steps
    size_t budget_violations;  // Number of forced (non-budgeted) scanning
                               // initiations
    size_t non_budgeted_steps;  // Number of object scanned over budget

    // Operations counters
    size_t managed_reads;              // Managed opcount
    size_t managed_writes;             // Managed opcount
    size_t primitive_reads;            // Primitive opcount
    size_t primitive_writes;           // Primitive opcount
    size_t read_barrier_activations;   // Read barrier calls
    size_t write_barrier_activations;  // Write barrier calls (always 0)

    // Rooots stats
    size_t dynamic_root_depth;          // Current number of dynamic roots
    size_t maximum_dynamic_root_depth;  // Maximal depth of stack of roots
    size_t permanent_root_count;        // Current number of dynamic roots
} gc_stats;

/// Dumps statistics to `dest`
static void print_gc_stats(gc_stats const* stats, FILE* dest) {
    if (stats == NULL || dest == NULL) {
        return;
    }

    fprintf(dest, "allocated bytes: %zu\n", stats->allocated_bytes);
    fprintf(dest, "allocated objects: %zu\n", stats->allocated_objects);
    fprintf(dest, "failed allocations: %zu\n", stats->failed_allocations);

    fprintf(
        dest, "collections initiated: %zu\n", stats->collections_intiatiated
    );
    fprintf(dest, "collections finished: %zu\n", stats->collections_finished);

    fprintf(dest, "occupied bytes: %zu\n", stats->occupied_bytes);
    fprintf(
        dest, "maximum occupied bytes: %zu\n", stats->maximum_occupied_bytes
    );

    fprintf(dest, "budgeted steps: %zu\n", stats->budgeted_steps);
    fprintf(dest, "budget violations: %zu\n", stats->budget_violations);
    fprintf(dest, "non-budgeted steps: %zu\n", stats->non_budgeted_steps);

    fprintf(dest, "managed reads: %zu\n", stats->managed_reads);
    fprintf(dest, "managed writes: %zu\n", stats->managed_writes);
    fprintf(dest, "primitive reads: %zu\n", stats->primitive_reads);
    fprintf(dest, "primitive writes: %zu\n", stats->primitive_writes);
    fprintf(
        dest, "read barrier activations: %zu\n", stats->read_barrier_activations
    );
    fprintf(
        dest, "write barrier activations: %zu\n",
        stats->write_barrier_activations
    );

    fprintf(dest, "dynamic roots: %zu\n", stats->dynamic_root_depth);
    fprintf(
        dest, "maximum dynamic roots: %zu\n", stats->maximum_dynamic_root_depth
    );
    fprintf(dest, "permanent roots: %zu\n", stats->permanent_root_count);
}

/**
  State reporting utils
*/

/// Print global variables of garbage collector
static void print_gc_variables(bakers_garbage_collector* gc, FILE* output) {
    if (gc == NULL || output == NULL) {
        return;
    }

    fprintf(
        output, "To-space:   [%p; %p), total %zu bytes\n",
        (void*)gc->to_space.base, (void*)region_end(gc->to_space),
        gc->to_space.length_bytes
    );
    fprintf(
        output, "From-space: [%p; %p), total %zu bytes\n",
        (void*)gc->from_space.base, (void*)region_end(gc->from_space),
        gc->from_space.length_bytes
    );
    fprintf(
        output, "Scan = %p (to-space + %zi)\n", (void*)gc->scan,
        (char*)gc->scan - gc->to_space.base
    );
    fprintf(
        output, "Next = %p (to-space + %zi)\n", (void*)gc->next,
        (char*)gc->next - gc->to_space.base
    );
    fprintf(
        output, "Limit = %p (to-space + %zi)\n", (void*)gc->limit,
        (char*)gc->limit - gc->to_space.base
    );
    fprintf(output, "Free to-space space = %zi\n", free_space_left(gc));
    fprintf(
        output, "%s\n", gc->flip ? "Is in a flip state" : "Is not in flip state"
    );
}

static void dump_object(StellaValue object, FILE* output) {
    if (object == NULL || output == NULL) {
        return;
    }

    StellaObjectDescriptor const* descriptor = object->descriptor;

    fprintf(
        output, "Object at [%p: %p) (%zi bytes):\n", object,
        next_object(object), object_size(descriptor)
    );
    if (object->gc_word == NO_FORWARDING) {
        fprintf(output, "\tNot forwarded");
    } else {
        fprintf(output, "\tForwarded to %p", forwarded_address(object));
    }
    fprintf(output, ", raw gc_word =  %" PRIuPTR "\n", object->gc_word);
    fprintf(
        output, "\tDescriptor: %s\n",
        descriptor->debug_name == NULL ? "(unnamed)" : descriptor->debug_name
    );
    fprintf(output, "\tKind: %" PRIu32 "\n", descriptor->kind);
    fprintf(output, "\tSlots count: %zu\n", descriptor->slot_capacity);
    fprintf(output, "\tManaged fields (%zu):\n", descriptor->managed_count);
    STELLA_OBJECT_MANAGED_FIELDS_FOR(object, field) {
        fprintf(
            output, "\t\tmanaged[%zi] = %p\n", field - object->fields,
            field->managed
        );
    }
    fprintf(output, "\tPrimitive fields (%zu):\n", descriptor->primitive_count);
    STELLA_OBJECT_PRIMITIVE_FIELDS_FOR(object, field) {
        fprintf(
            output, "\t\tprimitive[%zi] = %" PRIuPTR "\n",
            field - (object->fields + descriptor->managed_count),
            field->primitive
        );
    }
}

static void dump_region(memory_region region, FILE* output) {
    if (output == NULL) {
        return;
    }
    StellaValue object = (StellaValue)region.base;
    StellaValue last   = (StellaValue)region_end(region);
    while (object < last) {
        dump_object(object, output);
        object = next_object(object);
    }
}

static void dump_memory(bakers_garbage_collector* gc, FILE* output) {
    if (gc == NULL || output == NULL) {
        return;
    }

    fprintf(output, "======== FROM-SPACE ========\n");
    size_t occupied_from_space_bytes =
        gc->from_space_objects.first.length_bytes +
        gc->from_space_objects.second.length_bytes;
    fprintf(
        output, "=== Occupated bytes: %zu, Free bytes: %zu ===\n",
        occupied_from_space_bytes,
        gc->from_space.length_bytes - occupied_from_space_bytes
    );
    dump_region(gc->from_space_objects.first, output);
    dump_region(gc->from_space_objects.second, output);

    fprintf(output, "========= TO-SPACE =========\n");
    fprintf(
        output, "=== Occupated bytes: %zu, Free bytes: %zu ===\n",
        gc->to_space.length_bytes - free_space_left(gc), free_space_left(gc)
    );
    fprintf(output, "----- SCANNED OBJECTS ------\n");
    fprintf(
        output, "----- %zi bytes ------\n", (char*)gc->scan - gc->to_space.base
    );
    dump_region(
        memory_region_from_bounds(gc->to_space.base, (char*)gc->scan), output
    );
    fprintf(output, "------------ QUEUE ---------\n");
    fprintf(
        output, "----- %zi bytes ------\n", (char*)gc->next - (char*)gc->scan
    );
    dump_region(
        memory_region_from_bounds((char*)gc->scan, (char*)gc->next), output
    );
    fprintf(output, "---------- ALLOCATED -------\n");
    fprintf(
        output, "----- %zi bytes ------\n",
        region_end(gc->to_space) - (char*)gc->limit
    );
    dump_region(
        memory_region_from_bounds((char*)gc->limit, region_end(gc->to_space)),
        output
    );
}

/**
   Stella GC global state
*/

enum Lifecycle { LIFECYCLE_UNINITED, LIFECYCLE_ACTIVE, LIFECYCLE_SHUTDOWN };
static enum Lifecycle g_lifeycle = LIFECYCLE_UNINITED;

static void ensure_active() {
    ensure_abi(
        g_lifeycle == LIFECYCLE_ACTIVE,
        "Collector operation outside its active lifetime"
    );
}

static bakers_garbage_collector g_gc = {};

/// Succesfull API calls counters
struct {
    size_t primitive_reads;
    size_t primitive_writes;
    size_t managed_reads;
    size_t managed_writes;
    size_t allocations;
    size_t byte_allocation;
} g_ops_counter = {};

/// Collects current stats from garbage collector and operations counters
gc_stats collect_stats() {
    return (gc_stats){
        .allocated_bytes    = g_ops_counter.byte_allocation,
        .allocated_objects  = g_ops_counter.allocations,
        .failed_allocations = g_gc.stats.failed_allocations,

        .collections_intiatiated = g_gc.stats.collection_intiations,
        .collections_finished    = g_gc.stats.collections_finished,

        .occupied_bytes         = g_gc.stats.occupied_bytes,
        .maximum_occupied_bytes = g_gc.stats.max_occupied_bytes,

        .budgeted_steps     = g_gc.stats.budgeted_scans,
        .budget_violations  = g_gc.stats.forced_scannings,
        .non_budgeted_steps = g_gc.stats.non_budgeted_scans,

        .primitive_reads           = g_ops_counter.primitive_reads,
        .primitive_writes          = g_ops_counter.primitive_writes,
        .managed_reads             = g_ops_counter.managed_reads,
        .managed_writes            = g_ops_counter.managed_writes,
        .read_barrier_activations  = g_gc.stats.read_barriers,
        .write_barrier_activations = 0,

        .dynamic_root_depth         = g_gc.dynamic_roots.size,
        .maximum_dynamic_root_depth = g_gc.dynamic_roots.max_size,
        .permanent_root_count       = g_gc.permanent_roots.size,
    };
}

/*
   Stella ABI implementation

   Vide Stella API for function semantics and contarcts
*/

void stella_gc_init(StellaGcConfig const* config) {
    ensure_abi(
        g_lifeycle == LIFECYCLE_UNINITED, "collector initialised more than once"
    );
    ensure_abi(config != NULL, "null collector configuration");
    ensure_abi(
        config->abi_version == STELLA_C_GC_ABI_VERSION,
        "incompatible collector ABI version"
    );
    ensure_abi(config->max_heap_bytes != 0, "zero maximum heap size");

    // Parse algorithm specific flags
    size_t budgeted_steps   = 0;
    float scan_threshold    = 0;
    size_t collection_sites = 0;
    bool custom_steps       = read_env_size("STELLA_GC_STEPS", &budgeted_steps);
    bool custom_threshold =
        read_env_float("STELLA_GC_TRESHOLD", &scan_threshold);
    bool custom_collection_sites =
        read_env_size("STELLA_GC_COLLECTION_POINTS", &collection_sites);

    ensure_abi(
        !custom_collection_sites || ((collection_sites & CS_ALLOCATION) != 0),
        "Allocation should always perform scanning"
    );
    bakers_gc_config bakers_config = {
        .scan_per_step = custom_steps ? budgeted_steps : 1,
        .forced_scan_threshold =
            custom_threshold
                ? (int)(config->max_heap_bytes * (1.0 - scan_threshold))
                : 0,
        .collection_sites =
            custom_collection_sites ? (int)collection_sites : CS_ALLOCATION
    };

    g_gc       = create_garbage_collector(config, &bakers_config);
    g_lifeycle = LIFECYCLE_ACTIVE;
}

void stella_gc_shutdown(void) {
    ensure_active();
    destroy_garbage_collector(&g_gc);
    g_lifeycle = LIFECYCLE_SHUTDOWN;
    gc_debug_log("Shut down");
}

void stella_gc_push_root(StellaValue* root) {
    ensure_active();
    ensure_abi(root != NULL, "null root slot");
    roots_vector_push(&g_gc.dynamic_roots, root);
}

void stella_gc_pop_root(StellaValue* root) {
    ensure_active();
    ensure_abi(g_gc.dynamic_roots.size > 0, "dynamic root stack underflow");
    StellaValue* actual_root = roots_vector_pop(&g_gc.dynamic_roots);
    ensure_abi(actual_root == root, "dynamic roots popped out of order");
}

void stella_gc_register_permanent_root(StellaValue* root) {
    ensure_active();
    ensure_abi(root != NULL, "null root slot");
    roots_vector_push(&g_gc.permanent_roots, root);
}

StellaValue stella_gc_alloc(StellaObjectDescriptor const* descriptor) {
    ensure_active();
    validate_descriptor(descriptor);
    ensure_abi(
        (descriptor->flags & STELLA_DESCRIPTOR_STATIC) == 0,
        "heap allocation requested with static descriptor"
    );

    scan_budgeted(&g_gc, CS_ALLOCATION);

    size_t requested_size = object_size(descriptor);
    StellaValue result    = allocate(&g_gc, requested_size);
    g_ops_counter.allocations++;
    g_ops_counter.byte_allocation += requested_size;

    initialise_object(result, descriptor, NO_FORWARDING);
    nullify_object(result);

    return result;
}

void stella_object_init_managed(
    StellaValue object,
    size_t index,
    StellaValue value
) {
    ensure_active();
    ensure_abi(object != NULL, "NULL object in init managed");
    ensure_abi(
        index < object->descriptor->managed_count,
        "managed initialisation index out of range"
    );
    object->fields[index].managed = value;
}

void stella_object_init_primitive(
    StellaValue object,
    size_t index,
    StellaPrimitive value
) {
    ensure_active();
    ensure_abi(object != NULL, "NULL object in init primitive");
    ensure_abi(
        index < object->descriptor->primitive_count,
        "managed initialisation index out of range"
    );
    size_t physical = object->descriptor->managed_count + index;
    object->fields[physical].primitive = value;
}

StellaValue stella_gc_read_managed(StellaValue* object_root, size_t index) {
    ensure_active();
    ensure_abi(object_root != NULL, "Null object root in read managed");

    scan_budgeted(&g_gc, CS_READ_BARRIER);

    StellaValue object = *object_root;
    ensure_abi(object != NULL, "NULL object in read managed");
    ensure_abi(
        index < object->descriptor->managed_count,
        "managed initialisation index out of range"
    );

    g_ops_counter.managed_reads++;
    return read_barrier(&g_gc, object, index);
}

void stella_gc_write_managed(
    StellaValue* object_root,
    size_t index,
    StellaValue* value_root
) {
    ensure_active();
    ensure_abi(object_root != NULL, "NULL object root in write managed");
    ensure_abi(value_root != NULL, "NULL value root in write managed");

    scan_budgeted(&g_gc, CS_WRITE_BARRIER);

    StellaValue object = *object_root;
    StellaValue value  = *value_root;
    ensure_abi(object != NULL, "NULL object in write managed");
    ensure_abi(
        index < object->descriptor->managed_count,
        "managed initialisation index out of range"
    );
    ensure_abi(
        !(object->descriptor->flags & STELLA_DESCRIPTOR_STATIC),
        "managed write to static object"
    );

    g_ops_counter.managed_writes++;
    object->fields[index].managed = value;
}

StellaPrimitive stella_object_read_primitive(StellaValue object, size_t index) {
    ensure_active();
    ensure_abi(object != NULL, "NULL object in read primitive");
    ensure_abi(
        index < object->descriptor->primitive_count,
        "managed initialisation index out of range"
    );
    g_ops_counter.primitive_reads++;
    size_t physical = object->descriptor->managed_count + index;
    return object->fields[physical].primitive;
}

void stella_object_write_primitive(
    StellaValue object,
    size_t index,
    StellaPrimitive value
) {
    ensure_active();
    ensure_abi(object != NULL, "NULL object in write primitive");
    ensure_abi(
        index < object->descriptor->primitive_count,
        "managed initialisation index out of range"
    );
    ensure_abi(
        !(object->descriptor->flags & STELLA_DESCRIPTOR_STATIC),
        "primitive write to static object"
    );
    g_ops_counter.primitive_writes++;
    size_t physical = object->descriptor->managed_count + index;
    object->fields[physical].primitive = value;
}

void stella_gc_update_object(
    StellaValue* object_root,
    StellaObjectDescriptor const* new_descriptor,
    StellaValue* const new_managed_roots[],
    StellaPrimitive const new_primitives[]
) {
    ensure_active();
    ensure_abi(object_root != NULL, "NULL object root in update object");

    scan_budgeted(&g_gc, CS_UPDATE);

    StellaValue object = *object_root;
    ensure_abi(object != NULL, "Null object in update object");

    StellaObjectDescriptor const* old_descriptor = object->descriptor;
    validate_descriptor(new_descriptor);
    ensure_abi(
        (old_descriptor->flags & STELLA_DESCRIPTOR_UPDATABLE) != 0 &&
            (new_descriptor->flags & STELLA_DESCRIPTOR_STATIC) == 0,
        "incompatible object-layout update flags"
    );
    ensure_abi(
        old_descriptor->slot_capacity == new_descriptor->slot_capacity,
        "object-layout update changes slot capacity"
    );
    ensure_abi(
        new_descriptor->managed_count == 0 || new_managed_roots != NULL,
        "missing managed values for object-layout update"
    );
    ensure_abi(
        new_descriptor->primitive_count == 0 || new_primitives != NULL,
        "missing primitive values for object-layout update"
    );

    for (size_t i = 0; i < new_descriptor->managed_count; ++i) {
        object->fields[i].managed = *new_managed_roots[i];
        g_ops_counter.managed_writes++;
    }

    for (size_t i = 0; i < new_descriptor->primitive_count; ++i) {
        size_t physical                    = new_descriptor->managed_count + i;
        object->fields[physical].primitive = new_primitives[i];
        g_ops_counter.primitive_writes++;
    }

    for (size_t i =
             new_descriptor->managed_count + new_descriptor->primitive_count;
         i < new_descriptor->slot_capacity; ++i) {
        object->fields[i].primitive = 0;
    }

    object->descriptor = new_descriptor;
}

void stella_gc_print_statistics(FILE* output) {
    ensure_active();
    ensure_abi(output != NULL, "null statistics output stream");

    gc_stats stats = collect_stats();
    print_gc_stats(&stats, output);
}

void stella_gc_print_state(FILE* output) {
    ensure_active();
    ensure_abi(output != NULL, "null state output stream");

    fprintf(output, "Garbage Collector State:\n");
    print_gc_variables(&g_gc, output);
    fprintf(output, "Roots:\n");
    dump_memory(&g_gc, output);
    fprintf(output, "Memory:\n");
    stella_gc_print_roots(output);
}

void stella_gc_print_roots(FILE* output) {
    ensure_active();
    ensure_abi(output != NULL, "null roots output stream");

    ROOTS_VECTOR_FOR(g_gc.dynamic_roots, slot) {
        StellaRoot root   = *slot;
        StellaValue value = *root;

        fprintf(
            output, "dynamic root %zi: slot=%p value=%p\n",
            (slot - g_gc.dynamic_roots.roots), (void*)root, (void*)value
        );
    }

    ROOTS_VECTOR_FOR(g_gc.permanent_roots, slot) {
        StellaRoot root   = *slot;
        StellaValue value = *root;

        fprintf(
            output, "permanent root %zi: slot=%p value=%p\n",
            (slot - g_gc.permanent_roots.roots), (void*)root, (void*)value
        );
    }
}
