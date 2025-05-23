// SPDX-License-Identifier: GPL-2.0-or-later
#include "basic_api.h"
#include <string.h>
#include <linux/memblock.h>

#define EXPECTED_MEMBLOCK_REGIONS			128
#define FUNC_ADD					"memblock_add"
#define FUNC_RESERVE					"memblock_reserve"
#define FUNC_REMOVE					"memblock_remove"
#define FUNC_FREE					"memblock_free"
#define FUNC_TRIM					"memblock_trim_memory"

static int memblock_initialization_check(void)
{
	PREFIX_PUSH();

	ASSERT_NE(memblock.memory.regions, NULL);
	ASSERT_EQ(memblock.memory.cnt, 0);
	ASSERT_EQ(memblock.memory.max, EXPECTED_MEMBLOCK_REGIONS);
	ASSERT_EQ(strcmp(memblock.memory.name, "memory"), 0);

	ASSERT_NE(memblock.reserved.regions, NULL);
	ASSERT_EQ(memblock.reserved.cnt, 0);
	ASSERT_EQ(memblock.memory.max, EXPECTED_MEMBLOCK_REGIONS);
	ASSERT_EQ(strcmp(memblock.reserved.name, "reserved"), 0);

	ASSERT_EQ(memblock.bottom_up, false);
	ASSERT_EQ(memblock.current_limit, MEMBLOCK_ALLOC_ANYWHERE);

	test_pass_pop();

	return 0;
}

/*
 * A simple test that adds a memory block of a specified base address
 * and size to the collection of available memory regions (memblock.memory).
 * Expect to create a new entry. The region counter and total memory get
 * updated.
 */
static int memblock_add_simple_check(void)
{
	struct memblock_region *rgn;

	rgn = &memblock.memory.regions[0];

	struct region r = {
		.base = SZ_1G,
		.size = SZ_4M
	};

	PREFIX_PUSH();

	reset_memblock_regions();
	memblock_add(r.base, r.size);

	ASSERT_EQ(rgn->base, r.base);
	ASSERT_EQ(rgn->size, r.size);

	ASSERT_EQ(memblock.memory.cnt, 1);
	ASSERT_EQ(memblock.memory.total_size, r.size);

	test_pass_pop();

	return 0;
}

/*
 * A simple test that adds a memory block of a specified base address, size,
 * NUMA node and memory flags to the collection of available memory regions.
 * Expect to create a new entry. The region counter and total memory get
 * updated.
 */
static int memblock_add_node_simple_check(void)
{
	struct memblock_region *rgn;

	rgn = &memblock.memory.regions[0];

	struct region r = {
		.base = SZ_1M,
		.size = SZ_16M
	};

	PREFIX_PUSH();

	reset_memblock_regions();
	memblock_add_node(r.base, r.size, 1, MEMBLOCK_HOTPLUG);

	ASSERT_EQ(rgn->base, r.base);
	ASSERT_EQ(rgn->size, r.size);
#ifdef CONFIG_NUMA
	ASSERT_EQ(rgn->nid, 1);
#endif
	ASSERT_EQ(rgn->flags, MEMBLOCK_HOTPLUG);

	ASSERT_EQ(memblock.memory.cnt, 1);
	ASSERT_EQ(memblock.memory.total_size, r.size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to add two memory blocks that don't overlap with one
 * another:
 *
 *  |        +--------+        +--------+  |
 *  |        |   r1   |        |   r2   |  |
 *  +--------+--------+--------+--------+--+
 *
 * Expect to add two correctly initialized entries to the collection of
 * available memory regions (memblock.memory). The total size and
 * region counter fields get updated.
 */
static int memblock_add_disjoint_check(void)
{
	struct memblock_region *rgn1, *rgn2;

	rgn1 = &memblock.memory.regions[0];
	rgn2 = &memblock.memory.regions[1];

	struct region r1 = {
		.base = SZ_1G,
		.size = SZ_8K
	};
	struct region r2 = {
		.base = SZ_1G + SZ_16K,
		.size = SZ_8K
	};

	PREFIX_PUSH();

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_add(r2.base, r2.size);

	ASSERT_EQ(rgn1->base, r1.base);
	ASSERT_EQ(rgn1->size, r1.size);

	ASSERT_EQ(rgn2->base, r2.base);
	ASSERT_EQ(rgn2->size, r2.size);

	ASSERT_EQ(memblock.memory.cnt, 2);
	ASSERT_EQ(memblock.memory.total_size, r1.size + r2.size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to add two memory blocks r1 and r2, where r2 overlaps
 * with the beginning of r1 (that is r1.base < r2.base + r2.size):
 *
 *  |    +----+----+------------+          |
 *  |    |    |r2  |   r1       |          |
 *  +----+----+----+------------+----------+
 *       ^    ^
 *       |    |
 *       |    r1.base
 *       |
 *       r2.base
 *
 * Expect to merge the two entries into one region that starts at r2.base
 * and has size of two regions minus their intersection. The total size of
 * the available memory is updated, and the region counter stays the same.
 */
static int memblock_add_overlap_top_check(void)
{
	struct memblock_region *rgn;
	phys_addr_t total_size;

	rgn = &memblock.memory.regions[0];

	struct region r1 = {
		.base = SZ_512M,
		.size = SZ_1G
	};
	struct region r2 = {
		.base = SZ_256M,
		.size = SZ_512M
	};

	PREFIX_PUSH();

	total_size = (r1.base - r2.base) + r1.size;

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_add(r2.base, r2.size);

	ASSERT_EQ(rgn->base, r2.base);
	ASSERT_EQ(rgn->size, total_size);

	ASSERT_EQ(memblock.memory.cnt, 1);
	ASSERT_EQ(memblock.memory.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to add two memory blocks r1 and r2, where r2 overlaps
 * with the end of r1 (that is r2.base < r1.base + r1.size):
 *
 *  |  +--+------+----------+              |
 *  |  |  | r1   | r2       |              |
 *  +--+--+------+----------+--------------+
 *     ^  ^
 *     |  |
 *     |  r2.base
 *     |
 *     r1.base
 *
 * Expect to merge the two entries into one region that starts at r1.base
 * and has size of two regions minus their intersection. The total size of
 * the available memory is updated, and the region counter stays the same.
 */
static int memblock_add_overlap_bottom_check(void)
{
	struct memblock_region *rgn;
	phys_addr_t total_size;

	rgn = &memblock.memory.regions[0];

	struct region r1 = {
		.base = SZ_128M,
		.size = SZ_512M
	};
	struct region r2 = {
		.base = SZ_256M,
		.size = SZ_1G
	};

	PREFIX_PUSH();

	total_size = (r2.base - r1.base) + r2.size;

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_add(r2.base, r2.size);

	ASSERT_EQ(rgn->base, r1.base);
	ASSERT_EQ(rgn->size, total_size);

	ASSERT_EQ(memblock.memory.cnt, 1);
	ASSERT_EQ(memblock.memory.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to add two memory blocks r1 and r2, where r2 is
 * within the range of r1 (that is r1.base < r2.base &&
 * r2.base + r2.size < r1.base + r1.size):
 *
 *  |   +-------+--+-----------------------+
 *  |   |       |r2|      r1               |
 *  +---+-------+--+-----------------------+
 *      ^
 *      |
 *      r1.base
 *
 * Expect to merge two entries into one region that stays the same.
 * The counter and total size of available memory are not updated.
 */
static int memblock_add_within_check(void)
{
	struct memblock_region *rgn;

	rgn = &memblock.memory.regions[0];

	struct region r1 = {
		.base = SZ_8M,
		.size = SZ_32M
	};
	struct region r2 = {
		.base = SZ_16M,
		.size = SZ_1M
	};

	PREFIX_PUSH();

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_add(r2.base, r2.size);

	ASSERT_EQ(rgn->base, r1.base);
	ASSERT_EQ(rgn->size, r1.size);

	ASSERT_EQ(memblock.memory.cnt, 1);
	ASSERT_EQ(memblock.memory.total_size, r1.size);

	test_pass_pop();

	return 0;
}

/*
 * A simple test that tries to add the same memory block twice. Expect
 * the counter and total size of available memory to not be updated.
 */
static int memblock_add_twice_check(void)
{
	struct region r = {
		.base = SZ_16K,
		.size = SZ_2M
	};

	PREFIX_PUSH();

	reset_memblock_regions();

	memblock_add(r.base, r.size);
	memblock_add(r.base, r.size);

	ASSERT_EQ(memblock.memory.cnt, 1);
	ASSERT_EQ(memblock.memory.total_size, r.size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to add two memory blocks that don't overlap with one
 * another and then add a third memory block in the space between the first two:
 *
 *  |        +--------+--------+--------+  |
 *  |        |   r1   |   r3   |   r2   |  |
 *  +--------+--------+--------+--------+--+
 *
 * Expect to merge the three entries into one region that starts at r1.base
 * and has size of r1.size + r2.size + r3.size. The region counter and total
 * size of the available memory are updated.
 */
static int memblock_add_between_check(void)
{
	struct memblock_region *rgn;
	phys_addr_t total_size;

	rgn = &memblock.memory.regions[0];

	struct region r1 = {
		.base = SZ_1G,
		.size = SZ_8K
	};
	struct region r2 = {
		.base = SZ_1G + SZ_16K,
		.size = SZ_8K
	};
	struct region r3 = {
		.base = SZ_1G + SZ_8K,
		.size = SZ_8K
	};

	PREFIX_PUSH();

	total_size = r1.size + r2.size + r3.size;

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_add(r2.base, r2.size);
	memblock_add(r3.base, r3.size);

	ASSERT_EQ(rgn->base, r1.base);
	ASSERT_EQ(rgn->size, total_size);

	ASSERT_EQ(memblock.memory.cnt, 1);
	ASSERT_EQ(memblock.memory.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A simple test that tries to add a memory block r when r extends past
 * PHYS_ADDR_MAX:
 *
 *                               +--------+
 *                               |    r   |
 *                               +--------+
 *  |                            +----+
 *  |                            | rgn|
 *  +----------------------------+----+
 *
 * Expect to add a memory block of size PHYS_ADDR_MAX - r.base. Expect the
 * total size of available memory and the counter to be updated.
 */
static int memblock_add_near_max_check(void)
{
	struct memblock_region *rgn;
	phys_addr_t total_size;

	rgn = &memblock.memory.regions[0];

	struct region r = {
		.base = PHYS_ADDR_MAX - SZ_1M,
		.size = SZ_2M
	};

	PREFIX_PUSH();

	total_size = PHYS_ADDR_MAX - r.base;

	reset_memblock_regions();
	memblock_add(r.base, r.size);

	ASSERT_EQ(rgn->base, r.base);
	ASSERT_EQ(rgn->size, total_size);

	ASSERT_EQ(memblock.memory.cnt, 1);
	ASSERT_EQ(memblock.memory.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that trying to add the 129th memory block.
 * Expect to trigger memblock_double_array() to double the
 * memblock.memory.max, find a new valid memory as
 * memory.regions.
 */
static int memblock_add_many_check(void)
{
	int i;
	void *orig_region;
	struct region r = {
		.base = SZ_16K,
		.size = SZ_16K,
	};
	phys_addr_t new_memory_regions_size;
	phys_addr_t base, size = SZ_64;
	phys_addr_t gap_size = SZ_64;

	PREFIX_PUSH();

	reset_memblock_regions();
	memblock_allow_resize();

	dummy_physical_memory_init();
	/*
	 * We allocated enough memory by using dummy_physical_memory_init(), and
	 * split it into small block. First we split a large enough memory block
	 * as the memory region which will be choosed by memblock_double_array().
	 */
	base = PAGE_ALIGN(dummy_physical_memory_base());
	new_memory_regions_size = PAGE_ALIGN(INIT_MEMBLOCK_REGIONS * 2 *
					     sizeof(struct memblock_region));
	memblock_add(base, new_memory_regions_size);

	/* This is the base of small memory block. */
	base += new_memory_regions_size + gap_size;

	orig_region = memblock.memory.regions;

	for (i = 0; i < INIT_MEMBLOCK_REGIONS; i++) {
		/*
		 * Add these small block to fulfill the memblock. We keep a
		 * gap between the nearby memory to avoid being merged.
		 */
		memblock_add(base, size);
		base += size + gap_size;

		ASSERT_EQ(memblock.memory.cnt, i + 2);
		ASSERT_EQ(memblock.memory.total_size, new_memory_regions_size +
						      (i + 1) * size);
	}

	/*
	 * At there, memblock_double_array() has been succeed, check if it
	 * update the memory.max.
	 */
	ASSERT_EQ(memblock.memory.max, INIT_MEMBLOCK_REGIONS * 2);

	/* memblock_double_array() will reserve the memory it used. Check it. */
	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, new_memory_regions_size);

	/*
	 * Now memblock_double_array() works fine. Let's check after the
	 * double_array(), the memblock_add() still works as normal.
	 */
	memblock_add(r.base, r.size);
	ASSERT_EQ(memblock.memory.regions[0].base, r.base);
	ASSERT_EQ(memblock.memory.regions[0].size, r.size);

	ASSERT_EQ(memblock.memory.cnt, INIT_MEMBLOCK_REGIONS + 2);
	ASSERT_EQ(memblock.memory.total_size, INIT_MEMBLOCK_REGIONS * size +
					      new_memory_regions_size +
					      r.size);
	ASSERT_EQ(memblock.memory.max, INIT_MEMBLOCK_REGIONS * 2);

	dummy_physical_memory_cleanup();

	/*
	 * The current memory.regions is occupying a range of memory that
	 * allocated from dummy_physical_memory_init(). After free the memory,
	 * we must not use it. So restore the origin memory region to make sure
	 * the tests can run as normal and not affected by the double array.
	 */
	memblock.memory.regions = orig_region;
	memblock.memory.cnt = INIT_MEMBLOCK_REGIONS;

	test_pass_pop();

	return 0;
}

static int memblock_add_checks(void)
{
	prefix_reset();
	prefix_push(FUNC_ADD);
	test_print("Running %s tests...\n", FUNC_ADD);

	memblock_add_simple_check();
	memblock_add_node_simple_check();
	memblock_add_disjoint_check();
	memblock_add_overlap_top_check();
	memblock_add_overlap_bottom_check();
	memblock_add_within_check();
	memblock_add_twice_check();
	memblock_add_between_check();
	memblock_add_near_max_check();
	memblock_add_many_check();

	prefix_pop();

	return 0;
}

/*
 * A simple test that marks a memory block of a specified base address
 * and size as reserved and to the collection of reserved memory regions
 * (memblock.reserved). Expect to create a new entry. The region counter
 * and total memory size are updated.
 */
static int memblock_reserve_simple_check(void)
{
	struct memblock_region *rgn;

	rgn =  &memblock.reserved.regions[0];

	struct region r = {
		.base = SZ_2G,
		.size = SZ_128M
	};

	PREFIX_PUSH();

	reset_memblock_regions();
	memblock_reserve(r.base, r.size);

	ASSERT_EQ(rgn->base, r.base);
	ASSERT_EQ(rgn->size, r.size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to mark two memory blocks that don't overlap as reserved:
 *
 *  |        +--+      +----------------+  |
 *  |        |r1|      |       r2       |  |
 *  +--------+--+------+----------------+--+
 *
 * Expect to add two entries to the collection of reserved memory regions
 * (memblock.reserved). The total size and region counter for
 * memblock.reserved are updated.
 */
static int memblock_reserve_disjoint_check(void)
{
	struct memblock_region *rgn1, *rgn2;

	rgn1 = &memblock.reserved.regions[0];
	rgn2 = &memblock.reserved.regions[1];

	struct region r1 = {
		.base = SZ_256M,
		.size = SZ_16M
	};
	struct region r2 = {
		.base = SZ_512M,
		.size = SZ_512M
	};

	PREFIX_PUSH();

	reset_memblock_regions();
	memblock_reserve(r1.base, r1.size);
	memblock_reserve(r2.base, r2.size);

	ASSERT_EQ(rgn1->base, r1.base);
	ASSERT_EQ(rgn1->size, r1.size);

	ASSERT_EQ(rgn2->base, r2.base);
	ASSERT_EQ(rgn2->size, r2.size);

	ASSERT_EQ(memblock.reserved.cnt, 2);
	ASSERT_EQ(memblock.reserved.total_size, r1.size + r2.size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to mark two memory blocks r1 and r2 as reserved,
 * where r2 overlaps with the beginning of r1 (that is
 * r1.base < r2.base + r2.size):
 *
 *  |  +--------------+--+--------------+  |
 *  |  |       r2     |  |     r1       |  |
 *  +--+--------------+--+--------------+--+
 *     ^              ^
 *     |              |
 *     |              r1.base
 *     |
 *     r2.base
 *
 * Expect to merge two entries into one region that starts at r2.base and
 * has size of two regions minus their intersection. The total size of the
 * reserved memory is updated, and the region counter is not updated.
 */
static int memblock_reserve_overlap_top_check(void)
{
	struct memblock_region *rgn;
	phys_addr_t total_size;

	rgn = &memblock.reserved.regions[0];

	struct region r1 = {
		.base = SZ_1G,
		.size = SZ_1G
	};
	struct region r2 = {
		.base = SZ_128M,
		.size = SZ_1G
	};

	PREFIX_PUSH();

	total_size = (r1.base - r2.base) + r1.size;

	reset_memblock_regions();
	memblock_reserve(r1.base, r1.size);
	memblock_reserve(r2.base, r2.size);

	ASSERT_EQ(rgn->base, r2.base);
	ASSERT_EQ(rgn->size, total_size);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to mark two memory blocks r1 and r2 as reserved,
 * where r2 overlaps with the end of r1 (that is
 * r2.base < r1.base + r1.size):
 *
 *  |  +--------------+--+--------------+  |
 *  |  |       r1     |  |     r2       |  |
 *  +--+--------------+--+--------------+--+
 *     ^              ^
 *     |              |
 *     |              r2.base
 *     |
 *     r1.base
 *
 * Expect to merge two entries into one region that starts at r1.base and
 * has size of two regions minus their intersection. The total size of the
 * reserved memory is updated, and the region counter is not updated.
 */
static int memblock_reserve_overlap_bottom_check(void)
{
	struct memblock_region *rgn;
	phys_addr_t total_size;

	rgn = &memblock.reserved.regions[0];

	struct region r1 = {
		.base = SZ_2K,
		.size = SZ_128K
	};
	struct region r2 = {
		.base = SZ_128K,
		.size = SZ_128K
	};

	PREFIX_PUSH();

	total_size = (r2.base - r1.base) + r2.size;

	reset_memblock_regions();
	memblock_reserve(r1.base, r1.size);
	memblock_reserve(r2.base, r2.size);

	ASSERT_EQ(rgn->base, r1.base);
	ASSERT_EQ(rgn->size, total_size);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to mark two memory blocks r1 and r2 as reserved,
 * where r2 is within the range of r1 (that is
 * (r1.base < r2.base) && (r2.base + r2.size < r1.base + r1.size)):
 *
 *  | +-----+--+---------------------------|
 *  | |     |r2|          r1               |
 *  +-+-----+--+---------------------------+
 *    ^     ^
 *    |     |
 *    |     r2.base
 *    |
 *    r1.base
 *
 * Expect to merge two entries into one region that stays the same. The
 * counter and total size of available memory are not updated.
 */
static int memblock_reserve_within_check(void)
{
	struct memblock_region *rgn;

	rgn = &memblock.reserved.regions[0];

	struct region r1 = {
		.base = SZ_1M,
		.size = SZ_8M
	};
	struct region r2 = {
		.base = SZ_2M,
		.size = SZ_64K
	};

	PREFIX_PUSH();

	reset_memblock_regions();
	memblock_reserve(r1.base, r1.size);
	memblock_reserve(r2.base, r2.size);

	ASSERT_EQ(rgn->base, r1.base);
	ASSERT_EQ(rgn->size, r1.size);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, r1.size);

	test_pass_pop();

	return 0;
}

/*
 * A simple test that tries to reserve the same memory block twice.
 * Expect the region counter and total size of reserved memory to not
 * be updated.
 */
static int memblock_reserve_twice_check(void)
{
	struct region r = {
		.base = SZ_16K,
		.size = SZ_2M
	};

	PREFIX_PUSH();

	reset_memblock_regions();

	memblock_reserve(r.base, r.size);
	memblock_reserve(r.base, r.size);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, r.size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to mark two memory blocks that don't overlap as reserved
 * and then reserve a third memory block in the space between the first two:
 *
 *  |        +--------+--------+--------+  |
 *  |        |   r1   |   r3   |   r2   |  |
 *  +--------+--------+--------+--------+--+
 *
 * Expect to merge the three entries into one reserved region that starts at
 * r1.base and has size of r1.size + r2.size + r3.size. The region counter and
 * total for memblock.reserved are updated.
 */
static int memblock_reserve_between_check(void)
{
	struct memblock_region *rgn;
	phys_addr_t total_size;

	rgn = &memblock.reserved.regions[0];

	struct region r1 = {
		.base = SZ_1G,
		.size = SZ_8K
	};
	struct region r2 = {
		.base = SZ_1G + SZ_16K,
		.size = SZ_8K
	};
	struct region r3 = {
		.base = SZ_1G + SZ_8K,
		.size = SZ_8K
	};

	PREFIX_PUSH();

	total_size = r1.size + r2.size + r3.size;

	reset_memblock_regions();
	memblock_reserve(r1.base, r1.size);
	memblock_reserve(r2.base, r2.size);
	memblock_reserve(r3.base, r3.size);

	ASSERT_EQ(rgn->base, r1.base);
	ASSERT_EQ(rgn->size, total_size);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A simple test that tries to reserve a memory block r when r extends past
 * PHYS_ADDR_MAX:
 *
 *                               +--------+
 *                               |    r   |
 *                               +--------+
 *  |                            +----+
 *  |                            | rgn|
 *  +----------------------------+----+
 *
 * Expect to reserve a memory block of size PHYS_ADDR_MAX - r.base. Expect the
 * total size of reserved memory and the counter to be updated.
 */
static int memblock_reserve_near_max_check(void)
{
	struct memblock_region *rgn;
	phys_addr_t total_size;

	rgn = &memblock.reserved.regions[0];

	struct region r = {
		.base = PHYS_ADDR_MAX - SZ_1M,
		.size = SZ_2M
	};

	PREFIX_PUSH();

	total_size = PHYS_ADDR_MAX - r.base;

	reset_memblock_regions();
	memblock_reserve(r.base, r.size);

	ASSERT_EQ(rgn->base, r.base);
	ASSERT_EQ(rgn->size, total_size);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that trying to reserve the 129th memory block.
 * Expect to trigger memblock_double_array() to double the
 * memblock.memory.max, find a new valid memory as
 * reserved.regions.
 */
static int memblock_reserve_many_check(void)
{
	int i;
	void *orig_region;
	struct region r = {
		.base = SZ_16K,
		.size = SZ_16K,
	};
	phys_addr_t memory_base = SZ_128K;
	phys_addr_t new_reserved_regions_size;

	PREFIX_PUSH();

	reset_memblock_regions();
	memblock_allow_resize();

	/* Add a valid memory region used by double_array(). */
	dummy_physical_memory_init();
	memblock_add(dummy_physical_memory_base(), MEM_SIZE);

	for (i = 0; i < INIT_MEMBLOCK_REGIONS; i++) {
		/* Reserve some fakes memory region to fulfill the memblock. */
		memblock_reserve(memory_base, MEM_SIZE);

		ASSERT_EQ(memblock.reserved.cnt, i + 1);
		ASSERT_EQ(memblock.reserved.total_size, (i + 1) * MEM_SIZE);

		/* Keep the gap so these memory region will not be merged. */
		memory_base += MEM_SIZE * 2;
	}

	orig_region = memblock.reserved.regions;

	/* This reserve the 129 memory_region, and makes it double array. */
	memblock_reserve(memory_base, MEM_SIZE);

	/*
	 * This is the memory region size used by the doubled reserved.regions,
	 * and it has been reserved due to it has been used. The size is used to
	 * calculate the total_size that the memblock.reserved have now.
	 */
	new_reserved_regions_size = PAGE_ALIGN((INIT_MEMBLOCK_REGIONS * 2) *
					sizeof(struct memblock_region));
	/*
	 * The double_array() will find a free memory region as the new
	 * reserved.regions, and the used memory region will be reserved, so
	 * there will be one more region exist in the reserved memblock. And the
	 * one more reserved region's size is new_reserved_regions_size.
	 */
	ASSERT_EQ(memblock.reserved.cnt, INIT_MEMBLOCK_REGIONS + 2);
	ASSERT_EQ(memblock.reserved.total_size, (INIT_MEMBLOCK_REGIONS + 1) * MEM_SIZE +
						new_reserved_regions_size);
	ASSERT_EQ(memblock.reserved.max, INIT_MEMBLOCK_REGIONS * 2);

	/*
	 * Now memblock_double_array() works fine. Let's check after the
	 * double_array(), the memblock_reserve() still works as normal.
	 */
	memblock_reserve(r.base, r.size);
	ASSERT_EQ(memblock.reserved.regions[0].base, r.base);
	ASSERT_EQ(memblock.reserved.regions[0].size, r.size);

	ASSERT_EQ(memblock.reserved.cnt, INIT_MEMBLOCK_REGIONS + 3);
	ASSERT_EQ(memblock.reserved.total_size, (INIT_MEMBLOCK_REGIONS + 1) * MEM_SIZE +
						new_reserved_regions_size +
						r.size);
	ASSERT_EQ(memblock.reserved.max, INIT_MEMBLOCK_REGIONS * 2);

	dummy_physical_memory_cleanup();

	/*
	 * The current reserved.regions is occupying a range of memory that
	 * allocated from dummy_physical_memory_init(). After free the memory,
	 * we must not use it. So restore the origin memory region to make sure
	 * the tests can run as normal and not affected by the double array.
	 */
	memblock.reserved.regions = orig_region;
	memblock.reserved.cnt = INIT_MEMBLOCK_RESERVED_REGIONS;

	test_pass_pop();

	return 0;
}


/*
 * A test that trying to reserve the 129th memory block at all locations.
 * Expect to trigger memblock_double_array() to double the
 * memblock.memory.max, find a new valid memory as reserved.regions.
 *
 *  0               1               2                 128
 *  +-------+       +-------+       +-------+         +-------+
 *  |  32K  |       |  32K  |       |  32K  |   ...   |  32K  |
 *  +-------+-------+-------+-------+-------+         +-------+
 *          |<-32K->|       |<-32K->|
 *
 */
/* Keep the gap so these memory region will not be merged. */
#define MEMORY_BASE(idx) (SZ_128K + (MEM_SIZE * 2) * (idx))
static int memblock_reserve_all_locations_check(void)
{
	int i, skip;
	void *orig_region;
	struct region r = {
		.base = SZ_16K,
		.size = SZ_16K,
	};
	phys_addr_t new_reserved_regions_size;

	PREFIX_PUSH();

	/* Reserve the 129th memory block for all possible positions*/
	for (skip = 0; skip < INIT_MEMBLOCK_REGIONS + 1; skip++) {
		reset_memblock_regions();
		memblock_allow_resize();

		/* Add a valid memory region used by double_array(). */
		dummy_physical_memory_init();
		memblock_add(dummy_physical_memory_base(), MEM_SIZE);

		for (i = 0; i < INIT_MEMBLOCK_REGIONS + 1; i++) {
			if (i == skip)
				continue;

			/* Reserve some fakes memory region to fulfill the memblock. */
			memblock_reserve(MEMORY_BASE(i), MEM_SIZE);

			if (i < skip) {
				ASSERT_EQ(memblock.reserved.cnt, i + 1);
				ASSERT_EQ(memblock.reserved.total_size, (i + 1) * MEM_SIZE);
			} else {
				ASSERT_EQ(memblock.reserved.cnt, i);
				ASSERT_EQ(memblock.reserved.total_size, i * MEM_SIZE);
			}
		}

		orig_region = memblock.reserved.regions;

		/* This reserve the 129 memory_region, and makes it double array. */
		memblock_reserve(MEMORY_BASE(skip), MEM_SIZE);

		/*
		 * This is the memory region size used by the doubled reserved.regions,
		 * and it has been reserved due to it has been used. The size is used to
		 * calculate the total_size that the memblock.reserved have now.
		 */
		new_reserved_regions_size = PAGE_ALIGN((INIT_MEMBLOCK_REGIONS * 2) *
						sizeof(struct memblock_region));
		/*
		 * The double_array() will find a free memory region as the new
		 * reserved.regions, and the used memory region will be reserved, so
		 * there will be one more region exist in the reserved memblock. And the
		 * one more reserved region's size is new_reserved_regions_size.
		 */
		ASSERT_EQ(memblock.reserved.cnt, INIT_MEMBLOCK_REGIONS + 2);
		ASSERT_EQ(memblock.reserved.total_size, (INIT_MEMBLOCK_REGIONS + 1) * MEM_SIZE +
							new_reserved_regions_size);
		ASSERT_EQ(memblock.reserved.max, INIT_MEMBLOCK_REGIONS * 2);

		/*
		 * Now memblock_double_array() works fine. Let's check after the
		 * double_array(), the memblock_reserve() still works as normal.
		 */
		memblock_reserve(r.base, r.size);
		ASSERT_EQ(memblock.reserved.regions[0].base, r.base);
		ASSERT_EQ(memblock.reserved.regions[0].size, r.size);

		ASSERT_EQ(memblock.reserved.cnt, INIT_MEMBLOCK_REGIONS + 3);
		ASSERT_EQ(memblock.reserved.total_size, (INIT_MEMBLOCK_REGIONS + 1) * MEM_SIZE +
							new_reserved_regions_size +
							r.size);
		ASSERT_EQ(memblock.reserved.max, INIT_MEMBLOCK_REGIONS * 2);

		dummy_physical_memory_cleanup();

		/*
		 * The current reserved.regions is occupying a range of memory that
		 * allocated from dummy_physical_memory_init(). After free the memory,
		 * we must not use it. So restore the origin memory region to make sure
		 * the tests can run as normal and not affected by the double array.
		 */
		memblock.reserved.regions = orig_region;
		memblock.reserved.cnt = INIT_MEMBLOCK_RESERVED_REGIONS;
	}

	test_pass_pop();

	return 0;
}

/*
 * A test that trying to reserve the 129th memory block at all locations.
 * Expect to trigger memblock_double_array() to double the
 * memblock.memory.max, find a new valid memory as reserved.regions. And make
 * sure it doesn't conflict with the range we want to reserve.
 *
 * For example, we have 128 regions in reserved and now want to reserve
 * the skipped one. Since reserved is full, memblock_double_array() would find
 * an available range in memory for the new array. We intended to put two
 * ranges in memory with one is the exact range of the skipped one. Before
 * commit 48c3b583bbdd ("mm/memblock: fix overlapping allocation when doubling
 * reserved array"), the new array would sits in the skipped range which is a
 * conflict. The expected new array should be allocated from memory.regions[0].
 *
 *           0                               1
 * memory    +-------+                       +-------+
 *           |  32K  |                       |  32K  |
 *           +-------+ ------+-------+-------+-------+
 *                   |<-32K->|<-32K->|<-32K->|
 *
 *                           0               skipped           127
 * reserved                  +-------+       .........         +-------+
 *                           |  32K  |       .  32K  .   ...   |  32K  |
 *                           +-------+-------+-------+         +-------+
 *                                   |<-32K->|
 *                                           ^
 *                                           |
 *                                           |
 *                                           skipped one
 */
/* Keep the gap so these memory region will not be merged. */
#define MEMORY_BASE_OFFSET(idx, offset) ((offset) + (MEM_SIZE * 2) * (idx))
static int memblock_reserve_many_may_conflict_check(void)
{
	int i, skip;
	void *orig_region;
	struct region r = {
		.base = SZ_16K,
		.size = SZ_16K,
	};
	phys_addr_t new_reserved_regions_size;

	/*
	 *  0        1          129
	 *  +---+    +---+      +---+
	 *  |32K|    |32K|  ..  |32K|
	 *  +---+    +---+      +---+
	 *
	 * Pre-allocate the range for 129 memory block + one range for double
	 * memblock.reserved.regions at idx 0.
	 */
	dummy_physical_memory_init();
	phys_addr_t memory_base = dummy_physical_memory_base();
	phys_addr_t offset = PAGE_ALIGN(memory_base);

	PREFIX_PUSH();

	/* Reserve the 129th memory block for all possible positions*/
	for (skip = 1; skip <= INIT_MEMBLOCK_REGIONS + 1; skip++) {
		reset_memblock_regions();
		memblock_allow_resize();

		reset_memblock_attributes();
		/* Add a valid memory region used by double_array(). */
		memblock_add(MEMORY_BASE_OFFSET(0, offset), MEM_SIZE);
		/*
		 * Add a memory region which will be reserved as 129th memory
		 * region. This is not expected to be used by double_array().
		 */
		memblock_add(MEMORY_BASE_OFFSET(skip, offset), MEM_SIZE);

		for (i = 1; i <= INIT_MEMBLOCK_REGIONS + 1; i++) {
			if (i == skip)
				continue;

			/* Reserve some fakes memory region to fulfill the memblock. */
			memblock_reserve(MEMORY_BASE_OFFSET(i, offset), MEM_SIZE);

			if (i < skip) {
				ASSERT_EQ(memblock.reserved.cnt, i);
				ASSERT_EQ(memblock.reserved.total_size, i * MEM_SIZE);
			} else {
				ASSERT_EQ(memblock.reserved.cnt, i - 1);
				ASSERT_EQ(memblock.reserved.total_size, (i - 1) * MEM_SIZE);
			}
		}

		orig_region = memblock.reserved.regions;

		/* This reserve the 129 memory_region, and makes it double array. */
		memblock_reserve(MEMORY_BASE_OFFSET(skip, offset), MEM_SIZE);

		/*
		 * This is the memory region size used by the doubled reserved.regions,
		 * and it has been reserved due to it has been used. The size is used to
		 * calculate the total_size that the memblock.reserved have now.
		 */
		new_reserved_regions_size = PAGE_ALIGN((INIT_MEMBLOCK_REGIONS * 2) *
						sizeof(struct memblock_region));
		/*
		 * The double_array() will find a free memory region as the new
		 * reserved.regions, and the used memory region will be reserved, so
		 * there will be one more region exist in the reserved memblock. And the
		 * one more reserved region's size is new_reserved_regions_size.
		 */
		ASSERT_EQ(memblock.reserved.cnt, INIT_MEMBLOCK_REGIONS + 2);
		ASSERT_EQ(memblock.reserved.total_size, (INIT_MEMBLOCK_REGIONS + 1) * MEM_SIZE +
							new_reserved_regions_size);
		ASSERT_EQ(memblock.reserved.max, INIT_MEMBLOCK_REGIONS * 2);

		/*
		 * The first reserved region is allocated for double array
		 * with the size of new_reserved_regions_size and the base to be
		 * MEMORY_BASE_OFFSET(0, offset) + SZ_32K - new_reserved_regions_size
		 */
		ASSERT_EQ(memblock.reserved.regions[0].base + memblock.reserved.regions[0].size,
			  MEMORY_BASE_OFFSET(0, offset) + SZ_32K);
		ASSERT_EQ(memblock.reserved.regions[0].size, new_reserved_regions_size);

		/*
		 * Now memblock_double_array() works fine. Let's check after the
		 * double_array(), the memblock_reserve() still works as normal.
		 */
		memblock_reserve(r.base, r.size);
		ASSERT_EQ(memblock.reserved.regions[0].base, r.base);
		ASSERT_EQ(memblock.reserved.regions[0].size, r.size);

		ASSERT_EQ(memblock.reserved.cnt, INIT_MEMBLOCK_REGIONS + 3);
		ASSERT_EQ(memblock.reserved.total_size, (INIT_MEMBLOCK_REGIONS + 1) * MEM_SIZE +
							new_reserved_regions_size +
							r.size);
		ASSERT_EQ(memblock.reserved.max, INIT_MEMBLOCK_REGIONS * 2);

		/*
		 * The current reserved.regions is occupying a range of memory that
		 * allocated from dummy_physical_memory_init(). After free the memory,
		 * we must not use it. So restore the origin memory region to make sure
		 * the tests can run as normal and not affected by the double array.
		 */
		memblock.reserved.regions = orig_region;
		memblock.reserved.cnt = INIT_MEMBLOCK_RESERVED_REGIONS;
	}

	dummy_physical_memory_cleanup();

	test_pass_pop();

	return 0;
}

static int memblock_reserve_checks(void)
{
	prefix_reset();
	prefix_push(FUNC_RESERVE);
	test_print("Running %s tests...\n", FUNC_RESERVE);

	memblock_reserve_simple_check();
	memblock_reserve_disjoint_check();
	memblock_reserve_overlap_top_check();
	memblock_reserve_overlap_bottom_check();
	memblock_reserve_within_check();
	memblock_reserve_twice_check();
	memblock_reserve_between_check();
	memblock_reserve_near_max_check();
	memblock_reserve_many_check();
	memblock_reserve_all_locations_check();
	memblock_reserve_many_may_conflict_check();

	prefix_pop();

	return 0;
}

/*
 * A simple test that tries to remove a region r1 from the array of
 * available memory regions. By "removing" a region we mean overwriting it
 * with the next region r2 in memblock.memory:
 *
 *  |  ......          +----------------+  |
 *  |  : r1 :          |       r2       |  |
 *  +--+----+----------+----------------+--+
 *                     ^
 *                     |
 *                     rgn.base
 *
 * Expect to add two memory blocks r1 and r2 and then remove r1 so that
 * r2 is the first available region. The region counter and total size
 * are updated.
 */
static int memblock_remove_simple_check(void)
{
	struct memblock_region *rgn;

	rgn = &memblock.memory.regions[0];

	struct region r1 = {
		.base = SZ_2K,
		.size = SZ_4K
	};
	struct region r2 = {
		.base = SZ_128K,
		.size = SZ_4M
	};

	PREFIX_PUSH();

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_add(r2.base, r2.size);
	memblock_remove(r1.base, r1.size);

	ASSERT_EQ(rgn->base, r2.base);
	ASSERT_EQ(rgn->size, r2.size);

	ASSERT_EQ(memblock.memory.cnt, 1);
	ASSERT_EQ(memblock.memory.total_size, r2.size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to remove a region r2 that was not registered as
 * available memory (i.e. has no corresponding entry in memblock.memory):
 *
 *                     +----------------+
 *                     |       r2       |
 *                     +----------------+
 *  |  +----+                              |
 *  |  | r1 |                              |
 *  +--+----+------------------------------+
 *     ^
 *     |
 *     rgn.base
 *
 * Expect the array, regions counter and total size to not be modified.
 */
static int memblock_remove_absent_check(void)
{
	struct memblock_region *rgn;

	rgn = &memblock.memory.regions[0];

	struct region r1 = {
		.base = SZ_512K,
		.size = SZ_4M
	};
	struct region r2 = {
		.base = SZ_64M,
		.size = SZ_1G
	};

	PREFIX_PUSH();

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_remove(r2.base, r2.size);

	ASSERT_EQ(rgn->base, r1.base);
	ASSERT_EQ(rgn->size, r1.size);

	ASSERT_EQ(memblock.memory.cnt, 1);
	ASSERT_EQ(memblock.memory.total_size, r1.size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to remove a region r2 that overlaps with the
 * beginning of the already existing entry r1
 * (that is r1.base < r2.base + r2.size):
 *
 *           +-----------------+
 *           |       r2        |
 *           +-----------------+
 *  |                 .........+--------+  |
 *  |                 :     r1 |  rgn   |  |
 *  +-----------------+--------+--------+--+
 *                    ^        ^
 *                    |        |
 *                    |        rgn.base
 *                    r1.base
 *
 * Expect that only the intersection of both regions is removed from the
 * available memory pool. The regions counter and total size are updated.
 */
static int memblock_remove_overlap_top_check(void)
{
	struct memblock_region *rgn;
	phys_addr_t r1_end, r2_end, total_size;

	rgn = &memblock.memory.regions[0];

	struct region r1 = {
		.base = SZ_32M,
		.size = SZ_32M
	};
	struct region r2 = {
		.base = SZ_16M,
		.size = SZ_32M
	};

	PREFIX_PUSH();

	r1_end = r1.base + r1.size;
	r2_end = r2.base + r2.size;
	total_size = r1_end - r2_end;

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_remove(r2.base, r2.size);

	ASSERT_EQ(rgn->base, r1.base + r2.base);
	ASSERT_EQ(rgn->size, total_size);

	ASSERT_EQ(memblock.memory.cnt, 1);
	ASSERT_EQ(memblock.memory.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to remove a region r2 that overlaps with the end of
 * the already existing region r1 (that is r2.base < r1.base + r1.size):
 *
 *        +--------------------------------+
 *        |               r2               |
 *        +--------------------------------+
 *  | +---+.....                           |
 *  | |rgn| r1 :                           |
 *  +-+---+----+---------------------------+
 *    ^
 *    |
 *    r1.base
 *
 * Expect that only the intersection of both regions is removed from the
 * available memory pool. The regions counter and total size are updated.
 */
static int memblock_remove_overlap_bottom_check(void)
{
	struct memblock_region *rgn;
	phys_addr_t total_size;

	rgn = &memblock.memory.regions[0];

	struct region r1 = {
		.base = SZ_2M,
		.size = SZ_64M
	};
	struct region r2 = {
		.base = SZ_32M,
		.size = SZ_256M
	};

	PREFIX_PUSH();

	total_size = r2.base - r1.base;

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_remove(r2.base, r2.size);

	ASSERT_EQ(rgn->base, r1.base);
	ASSERT_EQ(rgn->size, total_size);

	ASSERT_EQ(memblock.memory.cnt, 1);
	ASSERT_EQ(memblock.memory.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to remove a region r2 that is within the range of
 * the already existing entry r1 (that is
 * (r1.base < r2.base) && (r2.base + r2.size < r1.base + r1.size)):
 *
 *                  +----+
 *                  | r2 |
 *                  +----+
 *  | +-------------+....+---------------+ |
 *  | |     rgn1    | r1 |     rgn2      | |
 *  +-+-------------+----+---------------+-+
 *    ^
 *    |
 *    r1.base
 *
 * Expect that the region is split into two - one that ends at r2.base and
 * another that starts at r2.base + r2.size, with appropriate sizes. The
 * region counter and total size are updated.
 */
static int memblock_remove_within_check(void)
{
	struct memblock_region *rgn1, *rgn2;
	phys_addr_t r1_size, r2_size, total_size;

	rgn1 = &memblock.memory.regions[0];
	rgn2 = &memblock.memory.regions[1];

	struct region r1 = {
		.base = SZ_1M,
		.size = SZ_32M
	};
	struct region r2 = {
		.base = SZ_16M,
		.size = SZ_1M
	};

	PREFIX_PUSH();

	r1_size = r2.base - r1.base;
	r2_size = (r1.base + r1.size) - (r2.base + r2.size);
	total_size = r1_size + r2_size;

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_remove(r2.base, r2.size);

	ASSERT_EQ(rgn1->base, r1.base);
	ASSERT_EQ(rgn1->size, r1_size);

	ASSERT_EQ(rgn2->base, r2.base + r2.size);
	ASSERT_EQ(rgn2->size, r2_size);

	ASSERT_EQ(memblock.memory.cnt, 2);
	ASSERT_EQ(memblock.memory.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A simple test that tries to remove a region r1 from the array of
 * available memory regions when r1 is the only available region.
 * Expect to add a memory block r1 and then remove r1 so that a dummy
 * region is added. The region counter stays the same, and the total size
 * is updated.
 */
static int memblock_remove_only_region_check(void)
{
	struct memblock_region *rgn;

	rgn = &memblock.memory.regions[0];

	struct region r1 = {
		.base = SZ_2K,
		.size = SZ_4K
	};

	PREFIX_PUSH();

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_remove(r1.base, r1.size);

	ASSERT_EQ(rgn->base, 0);
	ASSERT_EQ(rgn->size, 0);

	ASSERT_EQ(memblock.memory.cnt, 0);
	ASSERT_EQ(memblock.memory.total_size, 0);

	test_pass_pop();

	return 0;
}

/*
 * A simple test that tries remove a region r2 from the array of available
 * memory regions when r2 extends past PHYS_ADDR_MAX:
 *
 *                               +--------+
 *                               |   r2   |
 *                               +--------+
 *  |                        +---+....+
 *  |                        |rgn|    |
 *  +------------------------+---+----+
 *
 * Expect that only the portion between PHYS_ADDR_MAX and r2.base is removed.
 * Expect the total size of available memory to be updated and the counter to
 * not be updated.
 */
static int memblock_remove_near_max_check(void)
{
	struct memblock_region *rgn;
	phys_addr_t total_size;

	rgn = &memblock.memory.regions[0];

	struct region r1 = {
		.base = PHYS_ADDR_MAX - SZ_2M,
		.size = SZ_2M
	};

	struct region r2 = {
		.base = PHYS_ADDR_MAX - SZ_1M,
		.size = SZ_2M
	};

	PREFIX_PUSH();

	total_size = r1.size - (PHYS_ADDR_MAX - r2.base);

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_remove(r2.base, r2.size);

	ASSERT_EQ(rgn->base, r1.base);
	ASSERT_EQ(rgn->size, total_size);

	ASSERT_EQ(memblock.memory.cnt, 1);
	ASSERT_EQ(memblock.memory.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to remove a region r3 that overlaps with two existing
 * regions r1 and r2:
 *
 *            +----------------+
 *            |       r3       |
 *            +----------------+
 *  |    +----+.....   ........+--------+
 *  |    |    |r1  :   :       |r2      |     |
 *  +----+----+----+---+-------+--------+-----+
 *
 * Expect that only the intersections of r1 with r3 and r2 with r3 are removed
 * from the available memory pool. Expect the total size of available memory to
 * be updated and the counter to not be updated.
 */
static int memblock_remove_overlap_two_check(void)
{
	struct memblock_region *rgn1, *rgn2;
	phys_addr_t new_r1_size, new_r2_size, r2_end, r3_end, total_size;

	rgn1 = &memblock.memory.regions[0];
	rgn2 = &memblock.memory.regions[1];

	struct region r1 = {
		.base = SZ_16M,
		.size = SZ_32M
	};
	struct region r2 = {
		.base = SZ_64M,
		.size = SZ_64M
	};
	struct region r3 = {
		.base = SZ_32M,
		.size = SZ_64M
	};

	PREFIX_PUSH();

	r2_end = r2.base + r2.size;
	r3_end = r3.base + r3.size;
	new_r1_size = r3.base - r1.base;
	new_r2_size = r2_end - r3_end;
	total_size = new_r1_size + new_r2_size;

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_add(r2.base, r2.size);
	memblock_remove(r3.base, r3.size);

	ASSERT_EQ(rgn1->base, r1.base);
	ASSERT_EQ(rgn1->size, new_r1_size);

	ASSERT_EQ(rgn2->base, r3_end);
	ASSERT_EQ(rgn2->size, new_r2_size);

	ASSERT_EQ(memblock.memory.cnt, 2);
	ASSERT_EQ(memblock.memory.total_size, total_size);

	test_pass_pop();

	return 0;
}

static int memblock_remove_checks(void)
{
	prefix_reset();
	prefix_push(FUNC_REMOVE);
	test_print("Running %s tests...\n", FUNC_REMOVE);

	memblock_remove_simple_check();
	memblock_remove_absent_check();
	memblock_remove_overlap_top_check();
	memblock_remove_overlap_bottom_check();
	memblock_remove_within_check();
	memblock_remove_only_region_check();
	memblock_remove_near_max_check();
	memblock_remove_overlap_two_check();

	prefix_pop();

	return 0;
}

/*
 * A simple test that tries to free a memory block r1 that was marked
 * earlier as reserved. By "freeing" a region we mean overwriting it with
 * the next entry r2 in memblock.reserved:
 *
 *  |              ......           +----+ |
 *  |              : r1 :           | r2 | |
 *  +--------------+----+-----------+----+-+
 *                                  ^
 *                                  |
 *                                  rgn.base
 *
 * Expect to reserve two memory regions and then erase r1 region with the
 * value of r2. The region counter and total size are updated.
 */
static int memblock_free_simple_check(void)
{
	struct memblock_region *rgn;

	rgn = &memblock.reserved.regions[0];

	struct region r1 = {
		.base = SZ_4M,
		.size = SZ_1M
	};
	struct region r2 = {
		.base = SZ_8M,
		.size = SZ_1M
	};

	PREFIX_PUSH();

	reset_memblock_regions();
	memblock_reserve(r1.base, r1.size);
	memblock_reserve(r2.base, r2.size);
	memblock_free((void *)r1.base, r1.size);

	ASSERT_EQ(rgn->base, r2.base);
	ASSERT_EQ(rgn->size, r2.size);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, r2.size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to free a region r2 that was not marked as reserved
 * (i.e. has no corresponding entry in memblock.reserved):
 *
 *                     +----------------+
 *                     |       r2       |
 *                     +----------------+
 *  |  +----+                              |
 *  |  | r1 |                              |
 *  +--+----+------------------------------+
 *     ^
 *     |
 *     rgn.base
 *
 * The array, regions counter and total size are not modified.
 */
static int memblock_free_absent_check(void)
{
	struct memblock_region *rgn;

	rgn = &memblock.reserved.regions[0];

	struct region r1 = {
		.base = SZ_2M,
		.size = SZ_8K
	};
	struct region r2 = {
		.base = SZ_16M,
		.size = SZ_128M
	};

	PREFIX_PUSH();

	reset_memblock_regions();
	memblock_reserve(r1.base, r1.size);
	memblock_free((void *)r2.base, r2.size);

	ASSERT_EQ(rgn->base, r1.base);
	ASSERT_EQ(rgn->size, r1.size);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, r1.size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to free a region r2 that overlaps with the beginning
 * of the already existing entry r1 (that is r1.base < r2.base + r2.size):
 *
 *     +----+
 *     | r2 |
 *     +----+
 *  |    ...+--------------+               |
 *  |    :  |    r1        |               |
 *  +----+--+--------------+---------------+
 *       ^  ^
 *       |  |
 *       |  rgn.base
 *       |
 *       r1.base
 *
 * Expect that only the intersection of both regions is freed. The
 * regions counter and total size are updated.
 */
static int memblock_free_overlap_top_check(void)
{
	struct memblock_region *rgn;
	phys_addr_t total_size;

	rgn = &memblock.reserved.regions[0];

	struct region r1 = {
		.base = SZ_8M,
		.size = SZ_32M
	};
	struct region r2 = {
		.base = SZ_1M,
		.size = SZ_8M
	};

	PREFIX_PUSH();

	total_size = (r1.size + r1.base) - (r2.base + r2.size);

	reset_memblock_regions();
	memblock_reserve(r1.base, r1.size);
	memblock_free((void *)r2.base, r2.size);

	ASSERT_EQ(rgn->base, r2.base + r2.size);
	ASSERT_EQ(rgn->size, total_size);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to free a region r2 that overlaps with the end of
 * the already existing entry r1 (that is r2.base < r1.base + r1.size):
 *
 *                   +----------------+
 *                   |       r2       |
 *                   +----------------+
 *  |    +-----------+.....                |
 *  |    |       r1  |    :                |
 *  +----+-----------+----+----------------+
 *
 * Expect that only the intersection of both regions is freed. The
 * regions counter and total size are updated.
 */
static int memblock_free_overlap_bottom_check(void)
{
	struct memblock_region *rgn;
	phys_addr_t total_size;

	rgn = &memblock.reserved.regions[0];

	struct region r1 = {
		.base = SZ_8M,
		.size = SZ_32M
	};
	struct region r2 = {
		.base = SZ_32M,
		.size = SZ_32M
	};

	PREFIX_PUSH();

	total_size = r2.base - r1.base;

	reset_memblock_regions();
	memblock_reserve(r1.base, r1.size);
	memblock_free((void *)r2.base, r2.size);

	ASSERT_EQ(rgn->base, r1.base);
	ASSERT_EQ(rgn->size, total_size);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to free a region r2 that is within the range of the
 * already existing entry r1 (that is
 * (r1.base < r2.base) && (r2.base + r2.size < r1.base + r1.size)):
 *
 *                    +----+
 *                    | r2 |
 *                    +----+
 *  |    +------------+....+---------------+
 *  |    |    rgn1    | r1 |     rgn2      |
 *  +----+------------+----+---------------+
 *       ^
 *       |
 *       r1.base
 *
 * Expect that the region is split into two - one that ends at r2.base and
 * another that starts at r2.base + r2.size, with appropriate sizes. The
 * region counter and total size fields are updated.
 */
static int memblock_free_within_check(void)
{
	struct memblock_region *rgn1, *rgn2;
	phys_addr_t r1_size, r2_size, total_size;

	rgn1 = &memblock.reserved.regions[0];
	rgn2 = &memblock.reserved.regions[1];

	struct region r1 = {
		.base = SZ_1M,
		.size = SZ_8M
	};
	struct region r2 = {
		.base = SZ_4M,
		.size = SZ_1M
	};

	PREFIX_PUSH();

	r1_size = r2.base - r1.base;
	r2_size = (r1.base + r1.size) - (r2.base + r2.size);
	total_size = r1_size + r2_size;

	reset_memblock_regions();
	memblock_reserve(r1.base, r1.size);
	memblock_free((void *)r2.base, r2.size);

	ASSERT_EQ(rgn1->base, r1.base);
	ASSERT_EQ(rgn1->size, r1_size);

	ASSERT_EQ(rgn2->base, r2.base + r2.size);
	ASSERT_EQ(rgn2->size, r2_size);

	ASSERT_EQ(memblock.reserved.cnt, 2);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A simple test that tries to free a memory block r1 that was marked
 * earlier as reserved when r1 is the only available region.
 * Expect to reserve a memory block r1 and then free r1 so that r1 is
 * overwritten with a dummy region. The region counter stays the same,
 * and the total size is updated.
 */
static int memblock_free_only_region_check(void)
{
	struct memblock_region *rgn;

	rgn = &memblock.reserved.regions[0];

	struct region r1 = {
		.base = SZ_2K,
		.size = SZ_4K
	};

	PREFIX_PUSH();

	reset_memblock_regions();
	memblock_reserve(r1.base, r1.size);
	memblock_free((void *)r1.base, r1.size);

	ASSERT_EQ(rgn->base, 0);
	ASSERT_EQ(rgn->size, 0);

	ASSERT_EQ(memblock.reserved.cnt, 0);
	ASSERT_EQ(memblock.reserved.total_size, 0);

	test_pass_pop();

	return 0;
}

/*
 * A simple test that tries free a region r2 when r2 extends past PHYS_ADDR_MAX:
 *
 *                               +--------+
 *                               |   r2   |
 *                               +--------+
 *  |                        +---+....+
 *  |                        |rgn|    |
 *  +------------------------+---+----+
 *
 * Expect that only the portion between PHYS_ADDR_MAX and r2.base is freed.
 * Expect the total size of reserved memory to be updated and the counter to
 * not be updated.
 */
static int memblock_free_near_max_check(void)
{
	struct memblock_region *rgn;
	phys_addr_t total_size;

	rgn = &memblock.reserved.regions[0];

	struct region r1 = {
		.base = PHYS_ADDR_MAX - SZ_2M,
		.size = SZ_2M
	};

	struct region r2 = {
		.base = PHYS_ADDR_MAX - SZ_1M,
		.size = SZ_2M
	};

	PREFIX_PUSH();

	total_size = r1.size - (PHYS_ADDR_MAX - r2.base);

	reset_memblock_regions();
	memblock_reserve(r1.base, r1.size);
	memblock_free((void *)r2.base, r2.size);

	ASSERT_EQ(rgn->base, r1.base);
	ASSERT_EQ(rgn->size, total_size);

	ASSERT_EQ(memblock.reserved.cnt, 1);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to free a reserved region r3 that overlaps with two
 * existing reserved regions r1 and r2:
 *
 *            +----------------+
 *            |       r3       |
 *            +----------------+
 *  |    +----+.....   ........+--------+
 *  |    |    |r1  :   :       |r2      |     |
 *  +----+----+----+---+-------+--------+-----+
 *
 * Expect that only the intersections of r1 with r3 and r2 with r3 are freed
 * from the collection of reserved memory. Expect the total size of reserved
 * memory to be updated and the counter to not be updated.
 */
static int memblock_free_overlap_two_check(void)
{
	struct memblock_region *rgn1, *rgn2;
	phys_addr_t new_r1_size, new_r2_size, r2_end, r3_end, total_size;

	rgn1 = &memblock.reserved.regions[0];
	rgn2 = &memblock.reserved.regions[1];

	struct region r1 = {
		.base = SZ_16M,
		.size = SZ_32M
	};
	struct region r2 = {
		.base = SZ_64M,
		.size = SZ_64M
	};
	struct region r3 = {
		.base = SZ_32M,
		.size = SZ_64M
	};

	PREFIX_PUSH();

	r2_end = r2.base + r2.size;
	r3_end = r3.base + r3.size;
	new_r1_size = r3.base - r1.base;
	new_r2_size = r2_end - r3_end;
	total_size = new_r1_size + new_r2_size;

	reset_memblock_regions();
	memblock_reserve(r1.base, r1.size);
	memblock_reserve(r2.base, r2.size);
	memblock_free((void *)r3.base, r3.size);

	ASSERT_EQ(rgn1->base, r1.base);
	ASSERT_EQ(rgn1->size, new_r1_size);

	ASSERT_EQ(rgn2->base, r3_end);
	ASSERT_EQ(rgn2->size, new_r2_size);

	ASSERT_EQ(memblock.reserved.cnt, 2);
	ASSERT_EQ(memblock.reserved.total_size, total_size);

	test_pass_pop();

	return 0;
}

static int memblock_free_checks(void)
{
	prefix_reset();
	prefix_push(FUNC_FREE);
	test_print("Running %s tests...\n", FUNC_FREE);

	memblock_free_simple_check();
	memblock_free_absent_check();
	memblock_free_overlap_top_check();
	memblock_free_overlap_bottom_check();
	memblock_free_within_check();
	memblock_free_only_region_check();
	memblock_free_near_max_check();
	memblock_free_overlap_two_check();

	prefix_pop();

	return 0;
}

static int memblock_set_bottom_up_check(void)
{
	prefix_push("memblock_set_bottom_up");

	memblock_set_bottom_up(false);
	ASSERT_EQ(memblock.bottom_up, false);
	memblock_set_bottom_up(true);
	ASSERT_EQ(memblock.bottom_up, true);

	reset_memblock_attributes();
	test_pass_pop();

	return 0;
}

static int memblock_bottom_up_check(void)
{
	prefix_push("memblock_bottom_up");

	memblock_set_bottom_up(false);
	ASSERT_EQ(memblock_bottom_up(), memblock.bottom_up);
	ASSERT_EQ(memblock_bottom_up(), false);
	memblock_set_bottom_up(true);
	ASSERT_EQ(memblock_bottom_up(), memblock.bottom_up);
	ASSERT_EQ(memblock_bottom_up(), true);

	reset_memblock_attributes();
	test_pass_pop();

	return 0;
}

static int memblock_bottom_up_checks(void)
{
	test_print("Running memblock_*bottom_up tests...\n");

	prefix_reset();
	memblock_set_bottom_up_check();
	prefix_reset();
	memblock_bottom_up_check();

	return 0;
}

/*
 * A test that tries to trim memory when both ends of the memory region are
 * aligned. Expect that the memory will not be trimmed. Expect the counter to
 * not be updated.
 */
static int memblock_trim_memory_aligned_check(void)
{
	struct memblock_region *rgn;
	const phys_addr_t alignment = SMP_CACHE_BYTES;

	rgn = &memblock.memory.regions[0];

	struct region r = {
		.base = alignment,
		.size = alignment * 4
	};

	PREFIX_PUSH();

	reset_memblock_regions();
	memblock_add(r.base, r.size);
	memblock_trim_memory(alignment);

	ASSERT_EQ(rgn->base, r.base);
	ASSERT_EQ(rgn->size, r.size);

	ASSERT_EQ(memblock.memory.cnt, 1);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to trim memory when there are two available regions, r1 and
 * r2. Region r1 is aligned on both ends and region r2 is unaligned on one end
 * and smaller than the alignment:
 *
 *                                     alignment
 *                                     |--------|
 * |        +-----------------+        +------+   |
 * |        |        r1       |        |  r2  |   |
 * +--------+-----------------+--------+------+---+
 *          ^        ^        ^        ^      ^
 *          |________|________|________|      |
 *                            |               Unaligned address
 *                Aligned addresses
 *
 * Expect that r1 will not be trimmed and r2 will be removed. Expect the
 * counter to be updated.
 */
static int memblock_trim_memory_too_small_check(void)
{
	struct memblock_region *rgn;
	const phys_addr_t alignment = SMP_CACHE_BYTES;

	rgn = &memblock.memory.regions[0];

	struct region r1 = {
		.base = alignment,
		.size = alignment * 2
	};
	struct region r2 = {
		.base = alignment * 4,
		.size = alignment - SZ_2
	};

	PREFIX_PUSH();

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_add(r2.base, r2.size);
	memblock_trim_memory(alignment);

	ASSERT_EQ(rgn->base, r1.base);
	ASSERT_EQ(rgn->size, r1.size);

	ASSERT_EQ(memblock.memory.cnt, 1);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to trim memory when there are two available regions, r1 and
 * r2. Region r1 is aligned on both ends and region r2 is unaligned at the base
 * and aligned at the end:
 *
 *                               Unaligned address
 *                                       |
 *                                       v
 * |        +-----------------+          +---------------+   |
 * |        |        r1       |          |      r2       |   |
 * +--------+-----------------+----------+---------------+---+
 *          ^        ^        ^        ^        ^        ^
 *          |________|________|________|________|________|
 *                            |
 *                    Aligned addresses
 *
 * Expect that r1 will not be trimmed and r2 will be trimmed at the base.
 * Expect the counter to not be updated.
 */
static int memblock_trim_memory_unaligned_base_check(void)
{
	struct memblock_region *rgn1, *rgn2;
	const phys_addr_t alignment = SMP_CACHE_BYTES;
	phys_addr_t offset = SZ_2;
	phys_addr_t new_r2_base, new_r2_size;

	rgn1 = &memblock.memory.regions[0];
	rgn2 = &memblock.memory.regions[1];

	struct region r1 = {
		.base = alignment,
		.size = alignment * 2
	};
	struct region r2 = {
		.base = alignment * 4 + offset,
		.size = alignment * 2 - offset
	};

	PREFIX_PUSH();

	new_r2_base = r2.base + (alignment - offset);
	new_r2_size = r2.size - (alignment - offset);

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_add(r2.base, r2.size);
	memblock_trim_memory(alignment);

	ASSERT_EQ(rgn1->base, r1.base);
	ASSERT_EQ(rgn1->size, r1.size);

	ASSERT_EQ(rgn2->base, new_r2_base);
	ASSERT_EQ(rgn2->size, new_r2_size);

	ASSERT_EQ(memblock.memory.cnt, 2);

	test_pass_pop();

	return 0;
}

/*
 * A test that tries to trim memory when there are two available regions, r1 and
 * r2. Region r1 is aligned on both ends and region r2 is aligned at the base
 * and unaligned at the end:
 *
 *                                             Unaligned address
 *                                                     |
 *                                                     v
 * |        +-----------------+        +---------------+   |
 * |        |        r1       |        |      r2       |   |
 * +--------+-----------------+--------+---------------+---+
 *          ^        ^        ^        ^        ^        ^
 *          |________|________|________|________|________|
 *                            |
 *                    Aligned addresses
 *
 * Expect that r1 will not be trimmed and r2 will be trimmed at the end.
 * Expect the counter to not be updated.
 */
static int memblock_trim_memory_unaligned_end_check(void)
{
	struct memblock_region *rgn1, *rgn2;
	const phys_addr_t alignment = SMP_CACHE_BYTES;
	phys_addr_t offset = SZ_2;
	phys_addr_t new_r2_size;

	rgn1 = &memblock.memory.regions[0];
	rgn2 = &memblock.memory.regions[1];

	struct region r1 = {
		.base = alignment,
		.size = alignment * 2
	};
	struct region r2 = {
		.base = alignment * 4,
		.size = alignment * 2 - offset
	};

	PREFIX_PUSH();

	new_r2_size = r2.size - (alignment - offset);

	reset_memblock_regions();
	memblock_add(r1.base, r1.size);
	memblock_add(r2.base, r2.size);
	memblock_trim_memory(alignment);

	ASSERT_EQ(rgn1->base, r1.base);
	ASSERT_EQ(rgn1->size, r1.size);

	ASSERT_EQ(rgn2->base, r2.base);
	ASSERT_EQ(rgn2->size, new_r2_size);

	ASSERT_EQ(memblock.memory.cnt, 2);

	test_pass_pop();

	return 0;
}

static int memblock_trim_memory_checks(void)
{
	prefix_reset();
	prefix_push(FUNC_TRIM);
	test_print("Running %s tests...\n", FUNC_TRIM);

	memblock_trim_memory_aligned_check();
	memblock_trim_memory_too_small_check();
	memblock_trim_memory_unaligned_base_check();
	memblock_trim_memory_unaligned_end_check();

	prefix_pop();

	return 0;
}

static int memblock_overlaps_region_check(void)
{
	struct region r = {
		.base = SZ_1G,
		.size = SZ_4M
	};

	PREFIX_PUSH();

	reset_memblock_regions();
	memblock_add(r.base, r.size);

	/* Far Away */
	ASSERT_FALSE(memblock_overlaps_region(&memblock.memory, SZ_1M, SZ_1M));
	ASSERT_FALSE(memblock_overlaps_region(&memblock.memory, SZ_2G, SZ_1M));

	/* Neighbor */
	ASSERT_FALSE(memblock_overlaps_region(&memblock.memory, SZ_1G - SZ_1M, SZ_1M));
	ASSERT_FALSE(memblock_overlaps_region(&memblock.memory, SZ_1G + SZ_4M, SZ_1M));

	/* Partial Overlap */
	ASSERT_TRUE(memblock_overlaps_region(&memblock.memory, SZ_1G - SZ_1M, SZ_2M));
	ASSERT_TRUE(memblock_overlaps_region(&memblock.memory, SZ_1G + SZ_2M, SZ_2M));

	/* Totally Overlap */
	ASSERT_TRUE(memblock_overlaps_region(&memblock.memory, SZ_1G, SZ_4M));
	ASSERT_TRUE(memblock_overlaps_region(&memblock.memory, SZ_1G - SZ_2M, SZ_8M));
	ASSERT_TRUE(memblock_overlaps_region(&memblock.memory, SZ_1G + SZ_1M, SZ_1M));

	test_pass_pop();

	return 0;
}

static int memblock_overlaps_region_checks(void)
{
	prefix_reset();
	prefix_push("memblock_overlaps_region");
	test_print("Running memblock_overlaps_region tests...\n");

	memblock_overlaps_region_check();

	prefix_pop();

	return 0;
}

#ifdef CONFIG_NUMA
static int memblock_set_node_check(void)
{
	unsigned long i, max_reserved;
	struct memblock_region *rgn;
	void *orig_region;

	PREFIX_PUSH();

	reset_memblock_regions();
	memblock_allow_resize();

	dummy_physical_memory_init();
	memblock_add(dummy_physical_memory_base(), MEM_SIZE);
	orig_region = memblock.reserved.regions;

	/* Equally Split range to node 0 and 1*/
	memblock_set_node(memblock_start_of_DRAM(),
			memblock_phys_mem_size() / 2, &memblock.memory, 0);
	memblock_set_node(memblock_start_of_DRAM() + memblock_phys_mem_size() / 2,
			memblock_phys_mem_size() / 2, &memblock.memory, 1);

	ASSERT_EQ(memblock.memory.cnt, 2);
	rgn = &memblock.memory.regions[0];
	ASSERT_EQ(rgn->base, memblock_start_of_DRAM());
	ASSERT_EQ(rgn->size, memblock_phys_mem_size() / 2);
	ASSERT_EQ(memblock_get_region_node(rgn), 0);
	rgn = &memblock.memory.regions[1];
	ASSERT_EQ(rgn->base, memblock_start_of_DRAM() + memblock_phys_mem_size() / 2);
	ASSERT_EQ(rgn->size, memblock_phys_mem_size() / 2);
	ASSERT_EQ(memblock_get_region_node(rgn), 1);

	/* Reserve 126 regions with the last one across node boundary */
	for (i = 0; i < 125; i++)
		memblock_reserve(memblock_start_of_DRAM() + SZ_16 * i, SZ_8);

	memblock_reserve(memblock_start_of_DRAM() + memblock_phys_mem_size() / 2 - SZ_8,
			SZ_16);

	/*
	 * Commit 61167ad5fecd ("mm: pass nid to reserve_bootmem_region()")
	 * do following process to set nid to each memblock.reserved region.
	 * But it may miss some region if memblock_set_node() double the
	 * array.
	 *
	 * By checking 'max', we make sure all region nid is set properly.
	 */
repeat:
	max_reserved = memblock.reserved.max;
	for_each_mem_region(rgn) {
		int nid = memblock_get_region_node(rgn);

		memblock_set_node(rgn->base, rgn->size, &memblock.reserved, nid);
	}
	if (max_reserved != memblock.reserved.max)
		goto repeat;

	/* Confirm each region has valid node set */
	for_each_reserved_mem_region(rgn) {
		ASSERT_TRUE(numa_valid_node(memblock_get_region_node(rgn)));
		if (rgn == (memblock.reserved.regions + memblock.reserved.cnt - 1))
			ASSERT_EQ(1, memblock_get_region_node(rgn));
		else
			ASSERT_EQ(0, memblock_get_region_node(rgn));
	}

	dummy_physical_memory_cleanup();

	/*
	 * The current reserved.regions is occupying a range of memory that
	 * allocated from dummy_physical_memory_init(). After free the memory,
	 * we must not use it. So restore the origin memory region to make sure
	 * the tests can run as normal and not affected by the double array.
	 */
	memblock.reserved.regions = orig_region;
	memblock.reserved.cnt = INIT_MEMBLOCK_RESERVED_REGIONS;

	test_pass_pop();

	return 0;
}

static int memblock_set_node_checks(void)
{
	prefix_reset();
	prefix_push("memblock_set_node");
	test_print("Running memblock_set_node tests...\n");

	memblock_set_node_check();

	prefix_pop();

	return 0;
}
#else
static int memblock_set_node_checks(void)
{
	return 0;
}
#endif

int memblock_basic_checks(void)
{
	memblock_initialization_check();
	memblock_add_checks();
	memblock_reserve_checks();
	memblock_remove_checks();
	memblock_free_checks();
	memblock_bottom_up_checks();
	memblock_trim_memory_checks();
	memblock_overlaps_region_checks();
	memblock_set_node_checks();

	return 0;
}
