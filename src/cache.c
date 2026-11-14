#include "cache.h"

#include <stdio.h>
#include <string.h>

static bool is_power_of_two(uint32_t v)
{
    return v != 0u && (v & (v - 1u)) == 0u;
}

static uint32_t log2_u32(uint32_t v)
{
    uint32_t bits = 0u;
    while (v > 1u) {
        v >>= 1u;
        bits++;
    }
    return bits;
}

static void decode_address(
    const CacheUnit *unit,
    uint32_t address,
    uint32_t *out_tag,
    uint32_t *out_set_idx,
    uint32_t *out_offset
)
{
    uint32_t line_mask = unit->config.line_size - 1u;
    uint32_t set_mask = unit->num_sets - 1u;

    *out_offset = address & line_mask;
    *out_set_idx = (address >> unit->offset_bits) & set_mask;
    *out_tag = address >> (unit->offset_bits + unit->index_bits);
}

static uint32_t find_hit_way(
    const CacheUnit *unit,
    uint32_t set_idx,
    uint32_t tag
)
{
    for (uint32_t w = 0u; w < unit->config.associativity; w++) {
        const CacheLine *line = &unit->sets[set_idx][w];
        if (line->valid && line->tag == tag) {
            return w;
        }
    }
    return unit->config.associativity;
}

static uint32_t select_victim_way(const CacheUnit *unit, uint32_t set_idx)
{
    for (uint32_t w = 0u; w < unit->config.associativity; w++) {
        if (!unit->sets[set_idx][w].valid) {
            return w;
        }
    }

    uint32_t victim = 0u;
    uint64_t best_seq = (unit->config.repl_policy == CACHE_REPL_FIFO)
                            ? unit->sets[set_idx][0].inserted_seq
                            : unit->sets[set_idx][0].last_used_seq;

    for (uint32_t w = 1u; w < unit->config.associativity; w++) {
        uint64_t seq = (unit->config.repl_policy == CACHE_REPL_FIFO)
                           ? unit->sets[set_idx][w].inserted_seq
                           : unit->sets[set_idx][w].last_used_seq;
        if (seq < best_seq) {
            best_seq = seq;
            victim = w;
        }
    }

    return victim;
}

static void writeback_line(
    const CacheUnit *unit,
    uint32_t set_idx,
    const CacheLine *line,
    uint8_t *backing
)
{
    uint32_t line_base =
        ((line->tag << unit->index_bits) | set_idx) << unit->offset_bits;

    for (uint32_t i = 0u; i < unit->config.line_size; i++) {
        uint32_t addr = line_base + i;
        if (addr <= DIOCPU_MEMORY_MAX_ADDRESS) {
            backing[addr] = line->data[i];
        }
    }
}

static void fill_line(
    const CacheUnit *unit,
    uint32_t set_idx,
    uint32_t tag,
    CacheLine *line,
    const uint8_t *backing,
    uint64_t seq
)
{
    uint32_t line_base =
        ((tag << unit->index_bits) | set_idx) << unit->offset_bits;

    memset(line->data, 0, sizeof(line->data));
    for (uint32_t i = 0u; i < unit->config.line_size; i++) {
        uint32_t addr = line_base + i;
        if (addr <= DIOCPU_MEMORY_MAX_ADDRESS) {
            line->data[i] = backing[addr];
        }
    }

    line->valid = true;
    line->dirty = false;
    line->tag = tag;
    line->last_used_seq = seq;
    line->inserted_seq = seq;
}

CacheConfig cache_default_icache_config(void)
{
    CacheConfig cfg;
    cfg.size_bytes = 1024u;
    cfg.line_size = 16u;
    cfg.associativity = 2u;
    cfg.write_policy = CACHE_WRITE_THROUGH;
    cfg.repl_policy = CACHE_REPL_LRU;
    cfg.hit_latency = 1u;
    cfg.miss_penalty = 6u;
    cfg.writeback_penalty = 0u;
    return cfg;
}

CacheConfig cache_default_dcache_config(void)
{
    CacheConfig cfg;
    cfg.size_bytes = 1024u;
    cfg.line_size = 16u;
    cfg.associativity = 2u;
    cfg.write_policy = CACHE_WRITE_BACK;
    cfg.repl_policy = CACHE_REPL_LRU;
    cfg.hit_latency = 1u;
    cfg.miss_penalty = 6u;
    cfg.writeback_penalty = 2u;
    return cfg;
}

bool cache_config_is_valid(const CacheConfig *config)
{
    if (config == NULL) {
        return false;
    }

    if (!is_power_of_two(config->line_size) ||
        config->line_size < 4u ||
        config->line_size > DIOCPU_CACHE_MAX_LINE_BYTES) {
        return false;
    }

    if (!is_power_of_two(config->associativity) ||
        config->associativity == 0u ||
        config->associativity > DIOCPU_CACHE_MAX_WAYS) {
        return false;
    }

    if (!is_power_of_two(config->size_bytes) ||
        config->size_bytes < config->line_size * config->associativity) {
        return false;
    }

    uint32_t num_sets = config->size_bytes / (config->line_size * config->associativity);
    if (!is_power_of_two(num_sets) || num_sets == 0u ||
        num_sets > DIOCPU_CACHE_MAX_SETS) {
        return false;
    }

    if (config->hit_latency == 0u) {
        return false;
    }

    return true;
}

CacheStatus cache_unit_init(CacheUnit *unit, const CacheConfig *config)
{
    if (unit == NULL || config == NULL) {
        return CACHE_STATUS_INVALID_ARGUMENT;
    }

    if (!cache_config_is_valid(config)) {
        return CACHE_STATUS_INVALID_CONFIG;
    }

    memset(unit, 0, sizeof(*unit));
    unit->config = *config;
    unit->num_sets = config->size_bytes / (config->line_size * config->associativity);
    unit->offset_bits = log2_u32(config->line_size);
    unit->index_bits = log2_u32(unit->num_sets);

    return CACHE_STATUS_OK;
}

void cache_unit_reset_lines(CacheUnit *unit)
{
    if (unit == NULL) {
        return;
    }

    memset(unit->sets, 0, sizeof(unit->sets));
    memset(&unit->stats, 0, sizeof(unit->stats));
    unit->access_seq = 0u;
}

CacheStatus cache_subsystem_init(CacheSubsystem *sub)
{
    if (sub == NULL) {
        return CACHE_STATUS_INVALID_ARGUMENT;
    }

    CacheConfig icfg = cache_default_icache_config();
    CacheConfig dcfg = cache_default_dcache_config();

    sub->enabled = false;
    CacheStatus st = cache_unit_init(&sub->icache, &icfg);
    if (st != CACHE_STATUS_OK) {
        return st;
    }

    return cache_unit_init(&sub->dcache, &dcfg);
}

void cache_subsystem_reset(CacheSubsystem *sub)
{
    if (sub == NULL) {
        return;
    }

    cache_unit_reset_lines(&sub->icache);
    cache_unit_reset_lines(&sub->dcache);
}

CacheStatus cache_subsystem_configure(
    CacheSubsystem *sub,
    const CacheConfig *icfg,
    const CacheConfig *dcfg
)
{
    if (sub == NULL) {
        return CACHE_STATUS_INVALID_ARGUMENT;
    }

    if (icfg != NULL) {
        CacheStatus st = cache_unit_init(&sub->icache, icfg);
        if (st != CACHE_STATUS_OK) {
            return st;
        }
    }

    if (dcfg != NULL) {
        CacheStatus st = cache_unit_init(&sub->dcache, dcfg);
        if (st != CACHE_STATUS_OK) {
            return st;
        }
    }

    return CACHE_STATUS_OK;
}

uint8_t cache_unit_peek_latency(
    const CacheUnit *unit,
    uint32_t address,
    uint32_t length,
    bool is_write
)
{
    if (unit == NULL || length == 0u ||
        address > DIOCPU_MEMORY_MAX_ADDRESS ||
        (uint64_t)address + (uint64_t)length > (uint64_t)DIOCPU_MEMORY_SIZE) {
        return 1u;
    }

    uint32_t total = 0u;
    uint32_t pos = address;
    uint32_t rem = length;

    while (rem > 0u) {
        uint32_t tag = 0u;
        uint32_t set_idx = 0u;
        uint32_t offset = 0u;
        decode_address(unit, pos, &tag, &set_idx, &offset);

        uint32_t space = unit->config.line_size - offset;
        uint32_t chunk = (rem < space) ? rem : space;

        total += unit->config.hit_latency;

        uint32_t hit_way = find_hit_way(unit, set_idx, tag);
        if (hit_way == unit->config.associativity) {
            total += unit->config.miss_penalty;
            if (!is_write || unit->config.write_policy == CACHE_WRITE_BACK) {
                uint32_t victim = select_victim_way(unit, set_idx);
                if (unit->sets[set_idx][victim].valid &&
                    unit->sets[set_idx][victim].dirty &&
                    unit->config.write_policy == CACHE_WRITE_BACK) {
                    total += unit->config.writeback_penalty;
                }
            }
        }

        pos += chunk;
        rem -= chunk;
    }

    if (total == 0u) {
        return 1u;
    }
    return (total > 255u) ? 255u : (uint8_t)total;
}

static CacheStatus cache_unit_access(
    CacheUnit *unit,
    uint8_t *backing,
    bool is_write,
    uint32_t address,
    uint8_t *buffer,
    uint32_t length,
    uint8_t *out_latency_cycles
)
{
    if (unit == NULL || backing == NULL || buffer == NULL || length == 0u) {
        return CACHE_STATUS_INVALID_ARGUMENT;
    }

    if (address > DIOCPU_MEMORY_MAX_ADDRESS ||
        (uint64_t)address + (uint64_t)length > (uint64_t)DIOCPU_MEMORY_SIZE) {
        return CACHE_STATUS_OUT_OF_RANGE;
    }

    uint32_t total_cycles = 0u;
    uint32_t pos = address;
    uint32_t buf_off = 0u;
    uint32_t rem = length;

    while (rem > 0u) {
        uint32_t tag = 0u;
        uint32_t set_idx = 0u;
        uint32_t offset = 0u;
        decode_address(unit, pos, &tag, &set_idx, &offset);

        uint32_t space = unit->config.line_size - offset;
        uint32_t chunk = (rem < space) ? rem : space;

        unit->access_seq++;
        unit->stats.accesses++;
        if (is_write) {
            unit->stats.writes++;
        } else {
            unit->stats.reads++;
        }

        uint32_t lat = unit->config.hit_latency;
        uint32_t hit_way = find_hit_way(unit, set_idx, tag);

        if (hit_way < unit->config.associativity) {
            CacheLine *line = &unit->sets[set_idx][hit_way];
            line->last_used_seq = unit->access_seq;
            unit->stats.hits++;

            if (is_write) {
                unit->stats.write_hits++;
                memcpy(&line->data[offset], &buffer[buf_off], chunk);
                if (unit->config.write_policy == CACHE_WRITE_BACK) {
                    line->dirty = true;
                } else {
                    memcpy(&backing[pos], &buffer[buf_off], chunk);
                }
            } else {
                unit->stats.read_hits++;
                memcpy(&buffer[buf_off], &line->data[offset], chunk);
            }
        } else {
            unit->stats.misses++;
            lat += unit->config.miss_penalty;

            if (is_write) {
                unit->stats.write_misses++;
                if (unit->config.write_policy == CACHE_WRITE_THROUGH) {
                    /* Write-Through + Write-No-Allocate: update backing store directly */
                    memcpy(&backing[pos], &buffer[buf_off], chunk);
                } else {
                    /* Write-Back + Write-Allocate */
                    uint32_t victim_idx = select_victim_way(unit, set_idx);
                    CacheLine *victim = &unit->sets[set_idx][victim_idx];
                    if (victim->valid) {
                        unit->stats.evictions++;
                        if (victim->dirty) {
                            writeback_line(unit, set_idx, victim, backing);
                            unit->stats.writebacks++;
                            lat += unit->config.writeback_penalty;
                        }
                    }
                    fill_line(unit, set_idx, tag, victim, backing, unit->access_seq);
                    memcpy(&victim->data[offset], &buffer[buf_off], chunk);
                    victim->dirty = true;
                }
            } else {
                unit->stats.read_misses++;
                uint32_t victim_idx = select_victim_way(unit, set_idx);
                CacheLine *victim = &unit->sets[set_idx][victim_idx];
                if (victim->valid) {
                    unit->stats.evictions++;
                    if (victim->dirty &&
                        unit->config.write_policy == CACHE_WRITE_BACK) {
                        writeback_line(unit, set_idx, victim, backing);
                        unit->stats.writebacks++;
                        lat += unit->config.writeback_penalty;
                    }
                }
                fill_line(unit, set_idx, tag, victim, backing, unit->access_seq);
                memcpy(&buffer[buf_off], &victim->data[offset], chunk);
            }
        }

        if (lat > unit->config.hit_latency) {
            unit->stats.stall_cycles += (uint64_t)(lat - unit->config.hit_latency);
        }
        unit->stats.total_latency_cycles += lat;
        total_cycles += lat;

        pos += chunk;
        buf_off += chunk;
        rem -= chunk;
    }

    if (out_latency_cycles != NULL) {
        *out_latency_cycles = (total_cycles > 255u) ? 255u : (uint8_t)total_cycles;
    }

    return CACHE_STATUS_OK;
}

CacheStatus cache_icache_fetch(
    CacheSubsystem *sub,
    const Memory *memory,
    uint32_t address,
    uint32_t length,
    uint8_t *out_latency_cycles
)
{
    if (sub == NULL || memory == NULL || length == 0u || length > 8u) {
        return CACHE_STATUS_INVALID_ARGUMENT;
    }

    uint8_t scratch[8];
    /* Safe cast: cache_unit_access only reads backing when is_write == false */
    return cache_unit_access(
        &sub->icache,
        (uint8_t *)(uintptr_t)memory->instruction,
        false,
        address,
        scratch,
        length,
        out_latency_cycles
    );
}

CacheStatus cache_dcache_read16(
    CacheSubsystem *sub,
    Memory *memory,
    uint32_t address,
    uint16_t *out_value,
    uint8_t *out_latency_cycles
)
{
    if (sub == NULL || memory == NULL || out_value == NULL) {
        return CACHE_STATUS_INVALID_ARGUMENT;
    }

    uint8_t bytes[2] = {0u, 0u};
    CacheStatus st = cache_unit_access(
        &sub->dcache,
        memory->data,
        false,
        address,
        bytes,
        2u,
        out_latency_cycles
    );
    if (st != CACHE_STATUS_OK) {
        return st;
    }

    *out_value = (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8u));
    return CACHE_STATUS_OK;
}

CacheStatus cache_dcache_write16(
    CacheSubsystem *sub,
    Memory *memory,
    uint32_t address,
    uint16_t value,
    uint8_t *out_latency_cycles
)
{
    if (sub == NULL || memory == NULL) {
        return CACHE_STATUS_INVALID_ARGUMENT;
    }

    uint8_t bytes[2];
    bytes[0] = (uint8_t)(value & 0x00FFu);
    bytes[1] = (uint8_t)(value >> 8u);

    return cache_unit_access(
        &sub->dcache,
        memory->data,
        true,
        address,
        bytes,
        2u,
        out_latency_cycles
    );
}

CacheStatus cache_dcache_flush(
    CacheSubsystem *sub,
    Memory *memory,
    uint64_t *out_writebacks
)
{
    if (sub == NULL || memory == NULL) {
        return CACHE_STATUS_INVALID_ARGUMENT;
    }

    uint64_t flushed = 0u;
    CacheUnit *dc = &sub->dcache;

    for (uint32_t s = 0u; s < dc->num_sets; s++) {
        for (uint32_t w = 0u; w < dc->config.associativity; w++) {
            CacheLine *line = &dc->sets[s][w];
            if (line->valid && line->dirty) {
                writeback_line(dc, s, line, memory->data);
                line->dirty = false;
                dc->stats.writebacks++;
                flushed++;
            }
        }
    }

    if (out_writebacks != NULL) {
        *out_writebacks = flushed;
    }

    return CACHE_STATUS_OK;
}

void cache_dcache_invalidate_range(
    CacheSubsystem *sub,
    uint32_t address,
    uint32_t length
)
{
    if (sub == NULL || length == 0u || address > DIOCPU_MEMORY_MAX_ADDRESS) {
        return;
    }

    CacheUnit *dc = &sub->dcache;
    uint32_t end = address + length;
    if (end > DIOCPU_MEMORY_SIZE) {
        end = DIOCPU_MEMORY_SIZE;
    }

    uint32_t pos = address;
    while (pos < end) {
        uint32_t tag = 0u;
        uint32_t set_idx = 0u;
        uint32_t offset = 0u;
        decode_address(dc, pos, &tag, &set_idx, &offset);

        uint32_t hit_way = find_hit_way(dc, set_idx, tag);
        if (hit_way < dc->config.associativity) {
            dc->sets[set_idx][hit_way].valid = false;
            dc->sets[set_idx][hit_way].dirty = false;
        }

        uint32_t step = dc->config.line_size - offset;
        pos += step;
    }
}

double cache_hit_rate(const CacheStats *stats)
{
    if (stats == NULL || stats->accesses == 0u) {
        return 0.0;
    }

    return (double)stats->hits / (double)stats->accesses;
}

double cache_miss_rate(const CacheStats *stats)
{
    if (stats == NULL || stats->accesses == 0u) {
        return 0.0;
    }

    return (double)stats->misses / (double)stats->accesses;
}

double cache_amat(const CacheUnit *unit)
{
    if (unit == NULL || unit->stats.accesses == 0u) {
        return 0.0;
    }

    return (double)unit->stats.total_latency_cycles / (double)unit->stats.accesses;
}

size_t cache_format_stats(
    const CacheUnit *unit,
    const char *label,
    char *buffer,
    size_t size
)
{
    if (unit == NULL || buffer == NULL || size == 0u) {
        return 0u;
    }

    const char *name = (label != NULL) ? label : "CACHE";
    int written = snprintf(
        buffer,
        size,
        "%s: acc=%llu hit=%llu miss=%llu (%.1f%%) evict=%llu wb=%llu amat=%.2f",
        name,
        (unsigned long long)unit->stats.accesses,
        (unsigned long long)unit->stats.hits,
        (unsigned long long)unit->stats.misses,
        cache_hit_rate(&unit->stats) * 100.0,
        (unsigned long long)unit->stats.evictions,
        (unsigned long long)unit->stats.writebacks,
        cache_amat(unit)
    );

    if (written < 0) {
        buffer[0] = '\0';
        return 0u;
    }

    size_t len = (size_t)written;
    return (len >= size) ? (size - 1u) : len;
}

