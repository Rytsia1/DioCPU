#include "assembler.h"
#include "cache.h"
#include "cpu.h"
#include "decoder.h"
#include "memory.h"
#include "pipeline.h"
#include "stack.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                      \
    do {                                                      \
        if (!(condition)) {                                   \
            fprintf(stderr, "FAILED: %s:%d: %s\n",             \
                    __FILE__, __LINE__, #condition);          \
            return 0;                                         \
        }                                                     \
    } while (0)

static int test_cache_config_and_geometry_validation(void)
{
    CacheConfig good = cache_default_dcache_config();
    CHECK(cache_config_is_valid(&good));

    CacheConfig bad = good;
    bad.line_size = 12u; /* not power of two */
    CHECK(!cache_config_is_valid(&bad));

    bad = good;
    bad.associativity = 3u; /* not power of two */
    CHECK(!cache_config_is_valid(&bad));

    bad = good;
    bad.hit_latency = 0u;
    CHECK(!cache_config_is_valid(&bad));

    bad = good;
    bad.size_bytes = 256u;
    bad.line_size = 32u;
    bad.associativity = 16u; /* > max ways */
    CHECK(!cache_config_is_valid(&bad));

    return 1;
}

static int test_cache_conflict_vs_associative_lru_and_fifo(void)
{
    static Memory mem;
    memory_init(&mem);
    CHECK(memory_data_write16(&mem, 0x0100u, 111u) == MEMORY_STATUS_OK);
    CHECK(memory_data_write16(&mem, 0x0140u, 222u) == MEMORY_STATUS_OK);
    CHECK(memory_data_write16(&mem, 0x0180u, 333u) == MEMORY_STATUS_OK);

    /* Direct-mapped: 64 bytes, 16-byte lines => 4 sets, stride 64 maps to same set */
    CacheSubsystem sub_dm;
    CHECK(cache_subsystem_init(&sub_dm) == CACHE_STATUS_OK);
    sub_dm.enabled = true;
    CacheConfig dm_cfg = {
        .size_bytes = 64u,
        .line_size = 16u,
        .associativity = 1u,
        .write_policy = CACHE_WRITE_BACK,
        .repl_policy = CACHE_REPL_LRU,
        .hit_latency = 1u,
        .miss_penalty = 5u,
        .writeback_penalty = 2u
    };
    CHECK(cache_subsystem_configure(&sub_dm, NULL, &dm_cfg) == CACHE_STATUS_OK);

    uint16_t val = 0u;
    uint8_t lat = 0u;
    CHECK(cache_dcache_read16(&sub_dm, &mem, 0x0100u, &val, &lat) == CACHE_STATUS_OK);
    CHECK(val == 111u && lat == 6u);
    CHECK(cache_dcache_read16(&sub_dm, &mem, 0x0140u, &val, &lat) == CACHE_STATUS_OK);
    CHECK(val == 222u && lat == 6u);
    CHECK(cache_dcache_read16(&sub_dm, &mem, 0x0100u, &val, &lat) == CACHE_STATUS_OK);
    CHECK(val == 111u && lat == 6u); /* evicted by 0x0140 */
    CHECK(sub_dm.dcache.stats.hits == 0u);
    CHECK(sub_dm.dcache.stats.misses == 3u);
    CHECK(sub_dm.dcache.stats.evictions == 2u);

    /* 2-way LRU with 4 sets (128 bytes): 0x0100 and 0x0140 coexist cleanly */
    CacheSubsystem sub_lru;
    CHECK(cache_subsystem_init(&sub_lru) == CACHE_STATUS_OK);
    sub_lru.enabled = true;
    CacheConfig lru_cfg = dm_cfg;
    lru_cfg.size_bytes = 128u;
    lru_cfg.associativity = 2u;
    lru_cfg.repl_policy = CACHE_REPL_LRU;
    CHECK(cache_subsystem_configure(&sub_lru, NULL, &lru_cfg) == CACHE_STATUS_OK);

    CHECK(cache_dcache_read16(&sub_lru, &mem, 0x0100u, &val, &lat) == CACHE_STATUS_OK); /* miss */
    CHECK(cache_dcache_read16(&sub_lru, &mem, 0x0140u, &val, &lat) == CACHE_STATUS_OK); /* miss */
    CHECK(cache_dcache_read16(&sub_lru, &mem, 0x0100u, &val, &lat) == CACHE_STATUS_OK); /* hit, refreshes A */
    CHECK(val == 111u && lat == 1u);
    CHECK(cache_dcache_read16(&sub_lru, &mem, 0x0180u, &val, &lat) == CACHE_STATUS_OK); /* evicts B (0x0140) */
    CHECK(cache_dcache_read16(&sub_lru, &mem, 0x0100u, &val, &lat) == CACHE_STATUS_OK); /* A still hits */
    CHECK(lat == 1u);
    CHECK(sub_lru.dcache.stats.hits == 2u);
    CHECK(sub_lru.dcache.stats.evictions == 1u);

    /* Same sequence with FIFO: hit on A does NOT refresh age, so A is evicted when C arrives */
    CacheSubsystem sub_fifo;
    CHECK(cache_subsystem_init(&sub_fifo) == CACHE_STATUS_OK);
    sub_fifo.enabled = true;
    CacheConfig fifo_cfg = lru_cfg;
    fifo_cfg.repl_policy = CACHE_REPL_FIFO;
    CHECK(cache_subsystem_configure(&sub_fifo, NULL, &fifo_cfg) == CACHE_STATUS_OK);

    CHECK(cache_dcache_read16(&sub_fifo, &mem, 0x0100u, &val, &lat) == CACHE_STATUS_OK); /* miss A */
    CHECK(cache_dcache_read16(&sub_fifo, &mem, 0x0140u, &val, &lat) == CACHE_STATUS_OK); /* miss B */
    CHECK(cache_dcache_read16(&sub_fifo, &mem, 0x0100u, &val, &lat) == CACHE_STATUS_OK); /* hit A */
    CHECK(cache_dcache_read16(&sub_fifo, &mem, 0x0180u, &val, &lat) == CACHE_STATUS_OK); /* miss C, FIFO evicts A */
    CHECK(cache_dcache_read16(&sub_fifo, &mem, 0x0140u, &val, &lat) == CACHE_STATUS_OK); /* B still hits */
    CHECK(lat == 1u);
    CHECK(cache_dcache_read16(&sub_fifo, &mem, 0x0100u, &val, &lat) == CACHE_STATUS_OK); /* A misses under FIFO */
    CHECK(lat == 6u);

    return 1;
}

static int test_cache_write_policies_flush_and_crossline(void)
{
    static DioCPU cpu;
    CHECK(cpu_init(&cpu) == CPU_STATUS_OK);
    CHECK(cpu_set_cache_enabled(&cpu, true) == CPU_STATUS_OK);

    CacheConfig dcfg = {
        .size_bytes = 64u,
        .line_size = 16u,
        .associativity = 1u,
        .write_policy = CACHE_WRITE_BACK,
        .repl_policy = CACHE_REPL_LRU,
        .hit_latency = 1u,
        .miss_penalty = 4u,
        .writeback_penalty = 3u
    };
    CHECK(cpu_configure_cache(&cpu, NULL, &dcfg) == CPU_STATUS_OK);

    /* Write-Back + Write-Allocate: backing memory must stay 0 until eviction or flush */
    uint8_t lat = 0u;
    CHECK(cache_dcache_write16(&cpu.cache, &cpu.memory, 0x0200u, 0xABCDu, &lat) == CACHE_STATUS_OK);
    CHECK(lat == 5u); /* 1 hit + 4 miss allocate */
    uint16_t raw = 0xFFFFu;
    CHECK(memory_data_read16(&cpu.memory, 0x0200u, &raw) == MEMORY_STATUS_OK);
    CHECK(raw == 0u);

    /* Unaligned cross-line 16-bit write at 0x020F..0x0210 spans line 0 and line 1 */
    CHECK(cache_dcache_write16(&cpu.cache, &cpu.memory, 0x020Fu, 0xCAFEu, &lat) == CACHE_STATUS_OK);
    uint16_t readback = 0u;
    CHECK(cache_dcache_read16(&cpu.cache, &cpu.memory, 0x020Fu, &readback, &lat) == CACHE_STATUS_OK);
    CHECK(readback == 0xCAFEu);

    /* Accessing 0x0240 conflicts with set 0 (0x0200) and forces dirty writeback with writeback_penalty */
    uint16_t val = 0u;
    CHECK(cache_dcache_read16(&cpu.cache, &cpu.memory, 0x0240u, &val, &lat) == CACHE_STATUS_OK);
    CHECK(lat == 8u); /* 1 + 4 miss + 3 dirty writeback */
    CHECK(cpu.cache.dcache.stats.writebacks == 1u);
    CHECK(memory_data_read16(&cpu.memory, 0x0200u, &raw) == MEMORY_STATUS_OK);
    CHECK(raw == 0xFEABu || raw == 0xABCDu); /* low byte at 0x20F overwrote 0x20F, not 0x200..0x201 */
    CHECK((raw & 0x00FFu) == 0xCDu);
    CHECK((raw >> 8u) == 0xABu);

    uint64_t flushed_wb = 0u;
    CHECK(cpu_flush_caches(&cpu, &flushed_wb) == CPU_STATUS_OK);
    CHECK(flushed_wb == 1u); /* line 0x0210 was dirty from cross-line write */
    CHECK(memory_data_read16(&cpu.memory, 0x020Fu, &raw) == MEMORY_STATUS_OK);
    CHECK(raw == 0xCAFEu);

    /* Write-Through + Write-No-Allocate updates backing memory directly */
    dcfg.write_policy = CACHE_WRITE_THROUGH;
    CHECK(cpu_configure_cache(&cpu, NULL, &dcfg) == CPU_STATUS_OK);
    CHECK(cache_dcache_write16(&cpu.cache, &cpu.memory, 0x0300u, 0x1234u, &lat) == CACHE_STATUS_OK);
    CHECK(memory_data_read16(&cpu.memory, 0x0300u, &raw) == MEMORY_STATUS_OK);
    CHECK(raw == 0x1234u);
    CHECK(cpu.cache.dcache.stats.writebacks == 0u);
    CHECK(cpu.cache.dcache.stats.evictions == 0u);

    return 1;
}

static int test_cache_cycle_accurate_and_pipeline_fibonacci_parity(void)
{
    static DioCPU cpu_uncached;
    static DioCPU cpu_cycle_cached;
    static DioCPU cpu_pipe_cached;

    const char *program =
        "    JMP MAIN\n"
        "ACCUM_PAIR:\n"
        "    PUSH R2\n"
        "    ADD R2, R1\n"
        "    STORER R2, [R5 + 0]\n"
        "    ADDI R5, 2\n"
        "    POP R2\n"
        "    RET\n"
        "MAIN:\n"
        "    LOADI R1, 1\n"   /* a = 1 */
        "    LOADI R2, 1\n"   /* b = 1 */
        "    LOADI R3, 6\n"   /* loop 6 times */
        "    LOADI R5, 0x0100\n"
        "LOOP:\n"
        "    MOV R4, R1\n"
        "    ADD R4, R2\n"
        "    MOV R1, R2\n"
        "    MOV R2, R4\n"
        "    CALL ACCUM_PAIR\n"
        "    DEC R3\n"
        "    JNZ LOOP\n"
        "    LOADR R6, [R5 - 2]\n" /* wait: test negative? Use LEA / R7 fixed addr instead */
        "    STORE R2, [0x0140]\n"
        "    LOAD R7, [0x0140]\n"
        "    HLT\n";

    /* Replace R5 - 2 check with safe LOADI/LOAD at 0x0100 */
    (void)program;
    const char *safe_program =
        "    JMP MAIN\n"
        "ACCUM_PAIR:\n"
        "    PUSH R2\n"
        "    ADD R2, R1\n"
        "    STORER R2, [R5 + 0]\n"
        "    ADDI R5, 2\n"
        "    POP R2\n"
        "    RET\n"
        "MAIN:\n"
        "    LOADI R1, 1\n"
        "    LOADI R2, 1\n"
        "    LOADI R3, 6\n"
        "    LOADI R5, 0x0100\n"
        "LOOP:\n"
        "    MOV R4, R1\n"
        "    ADD R4, R2\n"
        "    MOV R1, R2\n"
        "    MOV R2, R4\n"
        "    CALL ACCUM_PAIR\n"
        "    DEC R3\n"
        "    JNZ LOOP\n"
        "    LOAD R6, [0x0100]\n"
        "    LOAD R7, [0x010A]\n"
        "    STORE R2, [0x0140]\n"
        "    LOAD R8, [0x0140]\n"
        "    HLT\n";

    AssemblerResult res;
    CHECK(cpu_init(&cpu_uncached) == CPU_STATUS_OK);
    CHECK(assembler_assemble(safe_program, &cpu_uncached.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu_uncached) == CPU_STATUS_OK);

    CHECK(cpu_init(&cpu_cycle_cached) == CPU_STATUS_OK);
    CHECK(assembler_assemble(safe_program, &cpu_cycle_cached.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu_cycle_cached) == CPU_STATUS_OK);
    CHECK(cpu_set_cache_enabled(&cpu_cycle_cached, true) == CPU_STATUS_OK);

    CHECK(cpu_init(&cpu_pipe_cached) == CPU_STATUS_OK);
    CHECK(assembler_assemble(safe_program, &cpu_pipe_cached.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu_pipe_cached) == CPU_STATUS_OK);
    CHECK(cpu_set_cache_enabled(&cpu_pipe_cached, true) == CPU_STATUS_OK);
    CHECK(cpu_set_pipeline_forwarding(&cpu_pipe_cached, true) == CPU_STATUS_OK);

    uint64_t uncached_steps = 0u;
    uint64_t cyc_steps = 0u;
    uint64_t pipe_cycles = 0u;
    CHECK(cpu_run(&cpu_uncached, 10000, &uncached_steps) == CPU_STATUS_HALTED);
    CHECK(cpu_run(&cpu_cycle_cached, 10000, &cyc_steps) == CPU_STATUS_HALTED);
    CHECK(cpu_pipeline_run(&cpu_pipe_cached, 10000, &pipe_cycles) == CPU_STATUS_HALTED);

    /* Flush dirty Write-Back lines to verify backing memory coherence */
    CHECK(cpu_flush_caches(&cpu_cycle_cached, NULL) == CPU_STATUS_OK);
    CHECK(cpu_flush_caches(&cpu_pipe_cached, NULL) == CPU_STATUS_OK);

    /* Exact architectural parity across uncached, cycle-accurate cached, and pipelined cached */
    for (unsigned int i = 0; i < CPU_GPR_COUNT; i++) {
        CHECK(cpu_cycle_cached.gpr[i] == cpu_uncached.gpr[i]);
        CHECK(cpu_pipe_cached.gpr[i] == cpu_uncached.gpr[i]);
    }
    CHECK(cpu_cycle_cached.pc == cpu_uncached.pc);
    CHECK(cpu_pipe_cached.pc == cpu_uncached.pc);
    CHECK(cpu_cycle_cached.sp == DIOCPU_STACK_INITIAL_SP);
    CHECK(cpu_pipe_cached.sp == DIOCPU_STACK_INITIAL_SP);
    CHECK(cpu_cycle_cached.flags == cpu_uncached.flags);
    CHECK(cpu_pipe_cached.flags == cpu_uncached.flags);
    CHECK(cpu_cycle_cached.instructions_executed == cpu_uncached.instructions_executed);
    CHECK(cpu_pipe_cached.instructions_executed == cpu_uncached.instructions_executed);

    /* Verify Fibonacci numbers: 1,1 -> 2,3,5,8,13,21 => R2=21, [0x100]=1+2=3, [0x10A]=13+21=34 */
    CHECK(cpu_pipe_cached.gpr[2] == 21u);
    CHECK(cpu_pipe_cached.gpr[6] == 3u);
    CHECK(cpu_pipe_cached.gpr[7] == 34u);
    CHECK(cpu_pipe_cached.gpr[8] == 21u);

    for (uint32_t addr = 0x0100u; addr <= 0x0140u; addr += 2u) {
        uint16_t a = 0u, b = 0u, c = 0u;
        CHECK(memory_data_read16(&cpu_uncached.memory, addr, &a) == MEMORY_STATUS_OK);
        CHECK(memory_data_read16(&cpu_cycle_cached.memory, addr, &b) == MEMORY_STATUS_OK);
        CHECK(memory_data_read16(&cpu_pipe_cached.memory, addr, &c) == MEMORY_STATUS_OK);
        CHECK(a == b && b == c);
    }

    /* Verify both I-Cache and D-Cache hit rates, misses, AMAT, and pipeline stalls */
    CHECK(cpu_cycle_cached.cache.icache.stats.misses > 0u);
    CHECK(cpu_cycle_cached.cache.icache.stats.hits > 0u);
    CHECK(cpu_cycle_cached.cache.dcache.stats.misses > 0u);
    CHECK(cpu_cycle_cached.cache.dcache.stats.hits > 0u);

    CHECK(cpu_pipe_cached.cache.icache.stats.misses > 0u);
    CHECK(cpu_pipe_cached.cache.icache.stats.hits > 0u);
    CHECK(cpu_pipe_cached.cache.dcache.stats.misses > 0u);
    CHECK(cpu_pipe_cached.cache.dcache.stats.hits > 0u);
    CHECK(cpu_pipe_cached.pipeline.stats.icache_stall_cycles > 0u);
    CHECK(cpu_pipe_cached.pipeline.stats.dcache_stall_cycles > 0u);
    CHECK(cpu_pipe_cached.pipeline.stats.control_hazard_flushes > 0u);

    double i_hr = cache_hit_rate(&cpu_pipe_cached.cache.icache.stats);
    double d_hr = cache_hit_rate(&cpu_pipe_cached.cache.dcache.stats);
    double i_amat = cache_amat(&cpu_pipe_cached.cache.icache);
    CHECK(i_hr > 0.50 && i_hr <= 1.0);
    CHECK(d_hr > 0.50 && d_hr <= 1.0);
    CHECK(i_amat >= 1.0);

    char fmt_buf[192];
    CHECK(cache_format_stats(&cpu_pipe_cached.cache.icache, "I-Cache", fmt_buf, sizeof(fmt_buf)) > 0u);
    CHECK(strstr(fmt_buf, "I-Cache:") != NULL);

    return 1;
}

static int test_cache_interrupt_vector_coherence(void)
{
    static DioCPU cpu_seq;
    static DioCPU cpu_cached_pipe;

    const char *program =
        "    JMP MAIN\n"
        "ISR_HANDLER:\n"
        "    ADDI R1, 7\n"
        "    IRET\n"
        "MAIN:\n"
        "    LOADI R1, 10\n"
        "    INT 5\n"
        "    STORE R1, [0x0180]\n"
        "    HLT\n";

    AssemblerResult res;
    CHECK(cpu_init(&cpu_seq) == CPU_STATUS_OK);
    CHECK(assembler_assemble(program, &cpu_seq.memory, &res) == ASSEMBLER_STATUS_OK);
    DecodedInstruction first_ins;
    CHECK(decoder_decode(&cpu_seq.memory, 0u, &first_ins) == DECODER_STATUS_OK);
    uint32_t isr_pc = first_ins.next_pc;
    CHECK(cpu_reset(&cpu_seq) == CPU_STATUS_OK);
    CHECK(cpu_register_interrupt_handler(&cpu_seq, 5u, isr_pc) == CPU_STATUS_OK);

    CHECK(cpu_init(&cpu_cached_pipe) == CPU_STATUS_OK);
    CHECK(assembler_assemble(program, &cpu_cached_pipe.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu_cached_pipe) == CPU_STATUS_OK);
    CHECK(cpu_set_cache_enabled(&cpu_cached_pipe, true) == CPU_STATUS_OK);
    /* Warm stale value in D-Cache before re-registering IVT handler */
    uint16_t dummy = 0u;
    uint8_t lat = 0u;
    CHECK(cache_dcache_read16(&cpu_cached_pipe.cache, &cpu_cached_pipe.memory,
                              DIOCPU_DEFAULT_IVT_BASE + (5u * DIOCPU_IVT_ENTRY_SIZE),
                              &dummy, &lat) == CACHE_STATUS_OK);
    CHECK(dummy == 0u);
    /* Registering IVT handler must invalidate D-Cache so INT 5 sees new handler */
    CHECK(cpu_register_interrupt_handler(&cpu_cached_pipe, 5u, isr_pc) == CPU_STATUS_OK);

    uint64_t seq_steps = 0u;
    uint64_t pipe_cycles = 0u;
    CHECK(cpu_run(&cpu_seq, 2000, &seq_steps) == CPU_STATUS_HALTED);
    CHECK(cpu_pipeline_run(&cpu_cached_pipe, 2000, &pipe_cycles) == CPU_STATUS_HALTED);
    CHECK(cpu_flush_caches(&cpu_cached_pipe, NULL) == CPU_STATUS_OK);

    CHECK(cpu_cached_pipe.gpr[1] == 17u);
    CHECK(cpu_cached_pipe.gpr[1] == cpu_seq.gpr[1]);
    CHECK(cpu_cached_pipe.interrupt_count == 1u);
    CHECK(cpu_cached_pipe.interrupt_nesting == 0u);
    uint16_t mem_val = 0u;
    CHECK(memory_data_read16(&cpu_cached_pipe.memory, 0x0180u, &mem_val) == MEMORY_STATUS_OK);
    CHECK(mem_val == 17u);

    return 1;
}

int main(void)
{
    int (*tests[])(void) = {
        test_cache_config_and_geometry_validation,
        test_cache_conflict_vs_associative_lru_and_fifo,
        test_cache_write_policies_flush_and_crossline,
        test_cache_cycle_accurate_and_pipeline_fibonacci_parity,
        test_cache_interrupt_vector_coherence
    };

    size_t count = sizeof(tests) / sizeof(tests[0]);
    for (size_t i = 0; i < count; i++) {
        if (!tests[i]()) {
            return 1;
        }
    }

    printf("All %u cache tests passed!\n", (unsigned int)count);
    return 0;
}

