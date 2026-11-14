#ifndef DIOCPU_CACHE_H
#define DIOCPU_CACHE_H

#include "memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DIOCPU_CACHE_MAX_SETS       64u
#define DIOCPU_CACHE_MAX_WAYS       4u
#define DIOCPU_CACHE_MAX_LINE_BYTES 32u

typedef enum {
    CACHE_WRITE_BACK = 0,
    CACHE_WRITE_THROUGH
} CacheWritePolicy;

typedef enum {
    CACHE_REPL_LRU = 0,
    CACHE_REPL_FIFO
} CacheReplPolicy;

typedef enum {
    CACHE_STATUS_OK = 0,
    CACHE_STATUS_INVALID_ARGUMENT,
    CACHE_STATUS_INVALID_CONFIG,
    CACHE_STATUS_OUT_OF_RANGE
} CacheStatus;

typedef struct {
    uint32_t size_bytes;
    uint32_t line_size;
    uint32_t associativity;
    CacheWritePolicy write_policy;
    CacheReplPolicy repl_policy;
    uint8_t hit_latency;
    uint8_t miss_penalty;
    uint8_t writeback_penalty;
} CacheConfig;

typedef struct {
    bool valid;
    bool dirty;
    uint32_t tag;
    uint64_t last_used_seq;
    uint64_t inserted_seq;
    uint8_t data[DIOCPU_CACHE_MAX_LINE_BYTES];
} CacheLine;

typedef struct {
    uint64_t accesses;
    uint64_t reads;
    uint64_t writes;
    uint64_t hits;
    uint64_t misses;
    uint64_t read_hits;
    uint64_t read_misses;
    uint64_t write_hits;
    uint64_t write_misses;
    uint64_t evictions;
    uint64_t writebacks;
    uint64_t stall_cycles;
    uint64_t total_latency_cycles;
} CacheStats;

typedef struct {
    CacheConfig config;
    uint32_t num_sets;
    uint32_t offset_bits;
    uint32_t index_bits;
    uint64_t access_seq;
    CacheLine sets[DIOCPU_CACHE_MAX_SETS][DIOCPU_CACHE_MAX_WAYS];
    CacheStats stats;
} CacheUnit;

typedef struct {
    bool enabled;
    CacheUnit icache;
    CacheUnit dcache;
} CacheSubsystem;

CacheConfig cache_default_icache_config(void);
CacheConfig cache_default_dcache_config(void);
bool cache_config_is_valid(const CacheConfig *config);

CacheStatus cache_unit_init(CacheUnit *unit, const CacheConfig *config);
void cache_unit_reset_lines(CacheUnit *unit);

CacheStatus cache_subsystem_init(CacheSubsystem *sub);
void cache_subsystem_reset(CacheSubsystem *sub);
CacheStatus cache_subsystem_configure(
    CacheSubsystem *sub,
    const CacheConfig *icfg,
    const CacheConfig *dcfg
);

uint8_t cache_unit_peek_latency(
    const CacheUnit *unit,
    uint32_t address,
    uint32_t length,
    bool is_write
);

CacheStatus cache_icache_fetch(
    CacheSubsystem *sub,
    const Memory *memory,
    uint32_t address,
    uint32_t length,
    uint8_t *out_latency_cycles
);

CacheStatus cache_dcache_read16(
    CacheSubsystem *sub,
    Memory *memory,
    uint32_t address,
    uint16_t *out_value,
    uint8_t *out_latency_cycles
);

CacheStatus cache_dcache_write16(
    CacheSubsystem *sub,
    Memory *memory,
    uint32_t address,
    uint16_t value,
    uint8_t *out_latency_cycles
);

CacheStatus cache_dcache_flush(
    CacheSubsystem *sub,
    Memory *memory,
    uint64_t *out_writebacks
);

void cache_dcache_invalidate_range(
    CacheSubsystem *sub,
    uint32_t address,
    uint32_t length
);

double cache_hit_rate(const CacheStats *stats);
double cache_miss_rate(const CacheStats *stats);
double cache_amat(const CacheUnit *unit);

size_t cache_format_stats(
    const CacheUnit *unit,
    const char *label,
    char *buffer,
    size_t size
);

#endif

