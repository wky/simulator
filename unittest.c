#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "inc/unittest.h"
#include "inc/misc.h"
#include "inc/instruction.h"
#include "inc/mmu.h"
#include "inc/cache.h"
#include "inc/cpu.h"

static void
test_mmu()
{
	uint32_t data[CACHE_LINE_WORDS] = {
		0x12345678, 0x9abcdef0, 0x23456789, 0xabcdef01,
		0x3456789a, 0xbcdef012, 0x456789ab, 0xcdef0123,
	};
	uint32_t buf[CACHE_LINE_WORDS];
	// test mmu init
	mmu_t *mmu = mmu_init(NULL, TLB_EVICT_RAND);
	assert(mmu != NULL);
	printf("mmu allocated: 0x%p\n", mmu);
	uint32_t va = 0x2000000;
	void *pa;
	// test page translation
	assert((pa = mmu_get_page(mmu, va, 1)) != NULL);
	assert(mmu_get_page(mmu, va, 0) == pa);
	printf("mmu adress translation: 0x%08x[va] -> %p[pa]\n", va, pa);
	assert(mmu_get_page(mmu, va + PGSIZE - 1, 0) == pa);
	assert(mmu_get_page(mmu, va + PGSIZE, 0) == NULL);
	// test load/store
	mm_store(mmu, pa, (void*)data);
	mm_load(mmu, pa, (void*)buf);
	int i;
	for (i = 0; i < CACHE_LINE_WORDS; i++)
		assert(data[i] == buf[i]);
	printf("mmu load/store match.\n");
	for (i = 0; i < PGSIZE/WORD_SIZE; i++)
		((uint32_t*)pa)[i] = i;
	// test mmu stats
	assert(mmu->stats.npages == 1);
	assert(mmu->stats.npgtbls == 1);
	assert((pa = mmu_get_page(mmu, 0, 1)) != NULL);
	assert(pa == mmu_get_page(mmu, 0, 0));
	assert(mmu->stats.npages == 2);
	assert(mmu->stats.npgtbls == 2);
	assert((pa = mmu_get_page(mmu, va + PGSIZE, 1)) != NULL);
	assert(pa == mmu_get_page(mmu, va + PGSIZE, 0));
	assert(mmu->stats.npages == 3);
	assert(mmu->stats.npgtbls == 2);
	// test entire address space
	for (va = PGSIZE; va <= ~(PGSIZE-1) && va; va += PGSIZE){
		assert((pa = mmu_get_page(mmu, va, 1)) != NULL);
		assert(pa == mmu_get_page(mmu, va, 0));
		memset(pa, 1, PGSIZE);
		if (va % (1<<29) == 0)
			printf("%dMB allocated\n", va>>20);
	}
	printf("4096MB allocated\n");
	assert(mmu->stats.npgtbls == NPDENTRIES);
	assert(mmu->stats.npages == NPDENTRIES*NPTENTRIES);
	// test tlb
	assert(NDTLB == NITLB);
	int32_t latency1[NDTLB], latency2[NDTLB];
	va = 0x2000000;
	for (i = 0; i < NDTLB; i++)
		pa = mmu_paging(mmu, va + PGSIZE * i, MEM_DATA, latency1+i);
	for (i = 0; i < NDTLB; i++)
		pa = mmu_paging(mmu, va + PGSIZE * i, MEM_DATA, latency2+i);
	for (i = 0; i < NDTLB; i++){
		printf("data paging latency: [%d]->[%d]\n", latency1[i], latency2[i]);
		assert(latency2[i] < latency1[i]);
	}
	pa = mmu_paging(mmu, va + PGSIZE * NDTLB, MEM_DATA, latency1);
	pa = mmu_paging(mmu, va + PGSIZE * NDTLB, MEM_DATA, latency2);
	printf("new dtlb entry: latency [%d]->[%d]\n", *latency1, *latency2);
	for (i = 0; i < NDTLB; i++){
		pa = mmu_paging(mmu, va + PGSIZE * i, MEM_DATA, latency1+i);
		printf("later data paging latency: [%d]\n", latency1[i]);
	}
	va = 0xff000000;
	for (i = 0; i < NDTLB; i++)
		pa = mmu_paging(mmu, va + PGSIZE * i, MEM_INST, latency1+i);
	for (i = 0; i < NDTLB; i++)
		pa = mmu_paging(mmu, va + PGSIZE * i, MEM_INST, latency2+i);
	for (i = 0; i < NDTLB; i++){
		printf("inst paging latency: [%d]->[%d]\n", latency1[i], latency2[i]);
		assert(latency2[i] < latency1[i]);
	}
	pa = mmu_paging(mmu, va + PGSIZE * NDTLB, MEM_INST, latency1);
	pa = mmu_paging(mmu, va + PGSIZE * NDTLB, MEM_INST, latency2);
	printf("new dtlb entry: latency [%d]->[%d]\n", *latency1, *latency2);
	for (i = 0; i < NDTLB; i++){
		pa = mmu_paging(mmu, va + PGSIZE * i, MEM_INST, latency1+i);
		printf("later inst paging latency: [%d]\n", latency1[i]);
	}
	printf("free memory...\n");
	mmu_destroy(mmu);
	printf("test_mmu passed\n");
}

void
test_cache()
{
	mmu_t *mmu = mmu_init(NULL, TLB_EVICT_RAND);
	assert(mmu != NULL);
	cache_t *cache = cache_init(mmu, NULL, CACHE_EVICT_LRU);
	assert(cache != NULL);
	printf("cache initialized: %p\n", cache);
	uint32_t va = 0x2000000;
	int i, j;
	uint32_t *pa;
	for (i = 0; i < 16; i++){
		assert((pa = mmu_get_page(mmu, va + i * PGSIZE, 1)) != NULL);
		for (j = 0; j < PGSIZE/4; j++)
			pa[j] = j;
	}
	pa = mmu_get_page(mmu, va, 0);
	printf("16 pages (64KB, 0x%08x-0x%08x) for the cache to play with.\n",
		va, va + 16 * PGSIZE - 1);
	int32_t latency = 0, tmp, words = 16 * PGSIZE / 4;
	for (i = 0; i < words; i++)
		cache_dstore(cache, va + i*4, i&0x3ff, MM_WORD, NULL);
	printf("mem. init. using cache data store [word].\n");
	// icache read correctness
	for (i = 0; i < words; i++){
		assert((i&0x3ff) == cache_load(cache, va + i * 4, MEM_INST, &tmp));
		latency += tmp;
	}
	printf("icache seq. load correct: avg. latency [%.2lf]\n", (double)latency/words);
	// dcache read correctness
	latency = 0;
	for (i = 0; i < words; i++){
		assert((i&0x3ff) == cache_load(cache, va + i * 4, MEM_DATA, &tmp));
		latency += tmp;
	}
	printf("dcache seq. load correct: avg. latency [%.2lf]\n", (double)latency/words);
	printf("inst paging still intact: 0x2000000->%p [was %p]\n", 
		mmu_paging(mmu, 0x2000000, MEM_INST, NULL), pa);
	printf("data paging still intact: 0x2000000->%p [was %p]\n",
		mmu_paging(mmu, 0x2000000, MEM_DATA, NULL), pa);
	// dcache store
	printf("testing byte store/load...");
	va += PGSIZE/2;
	for (i = 0; i < PGSIZE; i++)
		cache_dstore(cache, va + i, i&0xff, MM_BYTE, NULL);
	for (i = 0; i < PGSIZE; i++){
		uint8_t buf[4];
		*(uint32_t*)buf = cache_load(cache, va+i, MEM_DATA, NULL);
		assert((i&0xff) == buf[i&0x3]);
		*(uint32_t*)buf = cache_load(cache, va+i, MEM_INST, NULL);
		assert((i&0xff) == buf[i&0x3]);
	}
	printf("  passed\n");
	printf("testing halfword store/load...");
	va += 3*PGSIZE;
	for (i = 0; i < PGSIZE/2; i++)
		cache_dstore(cache, va + i*2, i&0xffff, MM_HALFWORD, NULL);
	for (i = 0; i < PGSIZE/2; i++){
		uint16_t buf[2];
		*(uint32_t*)buf = cache_load(cache, va+i*2, MEM_DATA, NULL);
		assert((i&0xffff) == buf[i&0x1]);
		// *(uint32_t*)buf = cache_load(cache, va+i*2, MEM_INST, NULL);
		// assert((i&0xffff) == buf[i&0x1]);
	}
	printf("  passed\n");
	printf("free memory...\n");
	mmu_destroy(mmu);
	cache_destroy(cache);
	printf("test_cache passed\n");
}

#define EXEC_INST_VAL(inst_val) do {\
	*(uint32_t*)(&inst) = inst_val;\
	cpu->reg[PC_NUM] += 4;\
	cycles = inst_dispatch(cpu, inst);\
} while(0)

#define EXEC_INST() do {\
	cpu->reg[PC_NUM] += 4;\
	cycles = inst_dispatch(cpu, inst);\
} while(0)

void
test_instruction(){
	printf("testing individual instruction execution...\n");
	mmu_t *mmu = mmu_init(NULL, TLB_EVICT_RAND);
	assert(mmu != NULL);
	cache_t *cache = cache_init(mmu, NULL, CACHE_EVICT_LRU);
	assert(cache != NULL);
	cpu_t *cpu = cpu_init(cache, mmu);
	assert(cpu != NULL);
	uint32_t pc = 0x2000000, sp = USER_STACK_TOP;
	uint32_t *text = mmu_get_page(mmu, pc, 1);
	assert(text != 0);
	uint32_t *stack = mmu_get_page(mmu, sp-PGSIZE, 1);
	assert(stack != 0);
	uint32_t cycles;
	general_inst inst;
	
	int i;
	for (i = 0; i < 32; i++)
		assert(cpu->reg[i] == 0);
	cpu->reg[PC_NUM] = pc;
	cpu->reg[SP_NUM] = sp;


	{// sub	sp, sp, #4
		EXEC_INST_VAL(0x24ef4004);
		assert(cpu->reg[PC_NUM] == 0x2000004);
		assert(cpu->reg[SP_NUM] == USER_STACK_TOP-4);
		assert(cpu->flags.N == 0 && cpu->flags.Z == 0 && cpu->flags.C == 0 && cpu->flags.V == 0);
		printf("24ef4004 sub sp, sp, #4: %d[cycles]\n", cycles);
	}
	{// add	sp, sp, #4	; 0x4
		EXEC_INST_VAL(0x28ef4004);
		assert(cpu->reg[PC_NUM] == 0x2000008);
		// printf("sp:0x%x\n", cpu->reg[SP_NUM]);
		assert(cpu->reg[SP_NUM] == USER_STACK_TOP);
		assert(cpu->flags.N == 0 && cpu->flags.Z == 0 && cpu->flags.C == 0 && cpu->flags.V == 0);
		printf("28ef4004 add sp, sp, #4: %d[cycles]\n", cycles);
	}
	{//add	r0, sp, #12
		EXEC_INST_VAL(0x28e8000c);
		assert(cpu->reg[PC_NUM] == 0x200000c);
		assert(cpu->reg[0] == USER_STACK_TOP + 12);
		assert(cpu->flags.N == 0 && cpu->flags.Z == 0 && cpu->flags.C == 0 && cpu->flags.V == 0);
		printf("0x28e8000c add r0, sp, #12: %d[cycles]\n", cycles);
	}
	{//stw.w r0, [sp-], #4
		EXEC_INST_VAL(0x72e80004);
		assert(cpu->reg[SP_NUM] == USER_STACK_TOP - 4);
		assert (cache_load(cache, USER_STACK_TOP - 4, MEM_DATA, NULL) == USER_STACK_TOP + 12);
		printf("0x72e80004 stw.w r0, [sp-], #4: %d[cycles]\n", cycles);
	}
	{//0x79eac000 ldw r11, [sp+]
		EXEC_INST_VAL(0x79eac000);
		assert(cpu->reg[SP_NUM] == USER_STACK_TOP - 4);
		assert(cpu->reg[11] == USER_STACK_TOP + 12);
		printf("0x79eac000 ldw r11, [sp+]: %d[cycles]\n", cycles);
	}
	{// count leading zeroes/ones
		*(uint32_t*)(&inst) = 0;
		inst.id = REG_ALU;
		inst.func = FUNC_MUL_BR_CNT;
		inst.opcode = OP_BRANCH_CNT | OP_CNT; // cntlo count leading ones
		inst.rd = 0;
		inst.rm = 0;
		cpu->reg[0] = ~0;
		EXEC_INST();
		assert(cpu->reg[0] == 32);
		cpu->reg[0] = ~0 << 10;
		EXEC_INST();
		assert(cpu->reg[0] == 22);
		cpu->reg[0] = 0;
		EXEC_INST();
		assert(cpu->reg[0] == 0);
		printf("0x%08x cntlo r0, r0: %d[cycles]\n", *(uint32_t*)(&inst), cycles);
		inst.opcode |= OP_CNT_ZERO; // count zeroes
		cpu->reg[0] = ~0;
		EXEC_INST();
		assert(cpu->reg[0] == 0);
		cpu->reg[0] = ~0u >> 10;
		EXEC_INST();
		assert(cpu->reg[0] == 10);
		cpu->reg[0] = 0;
		EXEC_INST();
		assert(cpu->reg[0] == 32);
		printf("0x%08x cntlz r0, r0: %d[cycles]\n", *(uint32_t*)(&inst), cycles);
	}
	{// jump link r24 / jump lr (ret)
		cpu->reg[24] = pc;
		uint32_t cur_pc = cpu->reg[PC_NUM] + 4;
		EXEC_INST_VAL(0x11ffc138);
		assert(cpu->reg[PC_NUM] == pc);
		assert(cpu->reg[LR_NUM] == cur_pc);
		printf("0x10ffc138 jump link r24: %d[cycles]\n", cycles);
		EXEC_INST_VAL(0x10ffc13e);
		assert(cpu->reg[PC_NUM] == cur_pc);
		printf("0x10ffc13e jump  lr: %d[cycles]\n", cycles);
	}
	{
		inst.id = REG_ALU;
		inst.func = FUNC_MUL_BR_CNT;
		inst.opcode = 0;
		inst.flag = 0;
		inst.rd = 0;
		inst.rn = 1;
		inst.rm = 2;
		cpu->reg[1] = 0xff38;
		cpu->reg[2] = 0x12345;
		EXEC_INST();
		assert(cpu->reg[0] == 0x22617218);
		assert(cpu->flags.N == 0 && cpu->flags.Z == 0);
		inst.flag = 1;
		cpu->reg[1] = 0xffff;
		cpu->reg[2] = 0xffff;
		EXEC_INST();
		assert(cpu->reg[0] == 0xfffe0001);
		assert(cpu->flags.N == 1 && cpu->flags.Z == 0);
		cpu->reg[2] = 0x0;
		EXEC_INST();
		assert(cpu->reg[0] == 0);
		assert(cpu->flags.N == 0 && cpu->flags.Z == 1);
		inst.opcode |= OP_MUL_ADD;
		inst.rs = 3;
		cpu->reg[2] = 0x1;
		cpu->reg[3] = 0xabcd;
		EXEC_INST();
		assert(cpu->reg[0] == 0xffff+0xabcd);
		assert(cpu->flags.N == 0 && cpu->flags.Z == 0);
		printf("mul works.\n");
	}
	{
		inst.opcode |= OP_MUL_LONG | OP_MUL_UNSIGN;
		// {r3, r0} += r1 * r2
		cpu->reg[0] = 0x23456789;
		cpu->reg[3] = 0xabcdef01;
		cpu->reg[1] = 0x12345;
		cpu->reg[2] = 0x6789a;
		EXEC_INST();
		assert(cpu->reg[0] == 0x801AF70B && cpu->reg[3] == 0xabcdef08);
		assert(cpu->flags.N == 1 && cpu->flags.Z == 0);
		inst.opcode -= OP_MUL_ADD + OP_MUL_UNSIGN;
		cpu->reg[1] = -1;
		// {r3, r0} = r1 * r2 64bit signed
		EXEC_INST();
		assert(cpu->reg[0] == 0xfff98766 && cpu->reg[3] == 0xffffffff);
		assert(cpu->flags.N == 1 && cpu->flags.Z == 0);
		printf("long mul works.\n");
	}
	{
		*(uint32_t*)(&inst) = 0;
		cpu->flags.C = 1;
		cpu->reg[1] = cpu->reg[0] = 0xffffffff;
		inst.id = REG_ALU;
		inst.opcode = ALU_ADC;
		inst.rd = 0;
		inst.rn = 0;
		inst.rm = 1;
		inst.rs = 0;
		EXEC_INST();
		assert(cpu->reg[0] == 0xffffffff);
		assert(cpu->flags.C == 1);
		printf("ADC works\n");
	}
	printf("test_instruction completed.\n");
}

void
run_unit_tests()
{
	static_assert(sizeof(general_inst) == 4);
	static_assert(sizeof(imm9_inst) == 4);
	static_assert(sizeof(imm14_inst) == 4);
	static_assert(sizeof(reg_ldst_inst) == 4);
	static_assert(sizeof(multi_ldst_inst) == 4);
	static_assert(sizeof(cond_branch_inst) == 4);
	static_assert(sizeof(soft_trap_inst) == 4);
	static_assert(sizeof(flags_reg) == 4);
	if (setjmp(cpu_exec_buf)){
		printf("run_unit_tests failed\n");
		abort();
	}
	printf("skipped test_mmu/test_cache\n");
	if (0){
		test_mmu();
		test_cache();
	}
	test_instruction();
	printf("all tests passed\n");
	exit(0);
}