/*
 * Synthetic micro-benchmarking of slab bulk
 *
 * This test provoke the worse-case behavior for kmem_cache_free_bulk(),
 * which is when adjacent objects in the array belongs to difference pages.
 *
 * This is worse-case for free_bulk, because it cannot exploit the
 * oppotunity to coalesce object belonging to the same page.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/time.h>
#include <linux/time_bench.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/skbuff.h>
#include <linux/jhash.h>

/* GLOBAL */
#define MAX_BULK 32768
void *GLOBAL_OBJS[MAX_BULK];

#define HASHSZ 16
static struct hlist_head objhash[HASHSZ];
static int objhash_cnt = 0;

static int verbose=1;

static unsigned int bulksz = 32 * 2;
module_param(bulksz, uint, 0);
MODULE_PARM_DESC(bulksz, "Parameter for setting bulk size to bench");

static uint32_t loops = 100000;
module_param(loops, uint, 0);
MODULE_PARM_DESC(bulksz, "Parameter for loops in bench");

static uint32_t prefill = HASHSZ * 32 * 8;
module_param(prefill, uint, 0);
MODULE_PARM_DESC(prefill, "Prefill object hash, for picking no-match pages");

static uint32_t nmatch = 0;
module_param(nmatch, uint, 0);
MODULE_PARM_DESC(nmatch, "Parameter only running one N-page-match test");

static uint32_t try_crash = 0;
module_param(try_crash, uint, 0);
MODULE_PARM_DESC(try_crash, "Enable error cases, like freeing NULL ptrs");

struct kmem_cache *my_slab;

/* element used for benchmark testing */
struct my_obj {
	struct hlist_node node; /* for linking into hash-table */
	void *page; /* Save object page address */

	//struct sk_buff skb;
	char pad[200];
};

static int objhash_add_one(struct my_obj *obj)
{
	u32 hash_idx;

	if (obj == NULL) {
		pr_err("%s(): Failed, NULL object\n", __func__);
		return 0;
	}

	objhash_cnt++;
	INIT_HLIST_NODE(&obj->node);
	obj->page = virt_to_head_page(obj);

	/* Hash on the page address of the object */
	hash_idx = jhash(&obj->page, 8, 13);
	//pr_info("DEBUG: hash_idx=0x%x [%u] page=0x%p\n",
	//	hash_idx, hash_idx % HASHSZ, obj->page);
	hash_idx = hash_idx % HASHSZ;

	hlist_add_head(&obj->node, &objhash[hash_idx]);

	return 1;
}

/* If printing "Bad invarians" then the test cannot be considered
 * valid, as something caused the loop to use more time.  Thus,
 * comparing it against another run could vary too much when counting
 * cycles.
 */
static struct my_obj* objhash_extract(struct my_obj *last_obj, bool no_match)
{
	/* Idea: get an object that does NOT match the prev page */
	struct my_obj *obj;
	u32 hash_idx;
	int skip_bucket = 0;

	if (last_obj == NULL) {
		hash_idx = 0;
	} else {
		hash_idx = jhash(&last_obj->page, 8, 13) % HASHSZ;
	}

	if (objhash_cnt < 2) {
		pr_warn("Bad invarians: request too many objects\n");
		return NULL;
	}

	/* With no_match, start looking in/from the next hash bucket */
	if (no_match) {
		hash_idx = (hash_idx + 1) % HASHSZ;
	}

	while(1) {
		struct hlist_head *hhead;
		struct hlist_node *tmp;

		hhead = &objhash[hash_idx];

		if (hlist_empty(hhead)) {
			skip_bucket++;
			hash_idx = (hash_idx + 1) % HASHSZ;
			//pr_info("Skip to next hash bucket[%d]\n", hash_idx);
			continue; /* Skip to next hash bucket */
		}

		hlist_for_each_entry_safe(obj, tmp, hhead, node) {
			if (no_match && last_obj && obj->page == last_obj->page)
				pr_warn("Bad invarians: return same page\n");

			/* When requesting a match, then there might
			 * not be any matching pages left in objhash.
			 * Thus don't try to match, just return obj.
			 */
			hlist_del(&obj->node);
			objhash_cnt--;

			/* Catch too much time on bucket search objects.
			 * Likely need better/more prefill
			 */
			if (skip_bucket >= (HASHSZ/2)) {
				pr_info("Bad invarians: "
					"search skipped many buckets: %d\n",
					skip_bucket);
				skip_bucket = 0;
			}
			return obj;
		}
	}
	pr_warn("Bad invarians: no object found/extracted\n");
	return NULL;
}

/* Debug function for listing object count in each hash bucket.  Used
 * for inspecting if the hash distribution is good.
 */
static void objhash_list_len(void) {
	int i, cnt = 0;
	struct my_obj *obj;

	for (i = 0; i < HASHSZ; i++) {
		struct hlist_head *hhead = &objhash[i];
		struct hlist_node *tmp;
		int list_len = 0;

		hlist_for_each_entry_safe(obj, tmp, hhead, node) {
			cnt++;
			list_len++;
		}
		pr_info("objhash[%d] list length: %d\n", i, list_len);
	}
	pr_info("objhash total: %d\n", cnt);
}

/* Fallback versions copy-pasted here, as they are defined in
 * slab_common that we cannot link with.
 *
 * Force them to be "noinlined" as current patch for slab_common cause
 * them to be a function call.  To keep comparison the same.
 */
noinline
void my__kmem_cache_free_bulk(struct kmem_cache *s, size_t nr, void **p)
{
	size_t i;

	for (i = 0; i < nr; i++)
		kmem_cache_free(s, p[i]);
}
noinline
bool my__kmem_cache_alloc_bulk(struct kmem_cache *s, gfp_t flags, size_t nr,
								void **p)
{
	size_t i;

	for (i = 0; i < nr; i++) {
		void *x = p[i] = kmem_cache_alloc(s, flags);
		if (!x) {
			my__kmem_cache_free_bulk(s, i, p);
			return false;
		}
	}
	return true;
}

enum test_type {
	FALLBACK_BULK = 1,
	BULK
};

static __always_inline int __benchmark_slab_bulk(
	struct time_bench_record *rec, void *data,
	enum test_type type)
{
	uint64_t loops_cnt = 0;
	int i, j;
	bool success;
	size_t bulk = rec->step;
	struct my_obj *last_obj = NULL;
	long int modulo_match = (long int)data;

	if (modulo_match <= 0) {
		/* Sort of disabling N-match case but keeping overhead
		 * of calling modulo to allow easier comparison
		 */
		modulo_match = 32768 * 2;
	} else {
		if (verbose)
			pr_info("Every N:%ld page will be a match\n",
				modulo_match);
	}

	if (bulk > MAX_BULK) {
		pr_warn("%s() bulk(%lu) request too big cap at %d\n",
			__func__, bulk, MAX_BULK);
		bulk = MAX_BULK;
	}
	/* loop count is limited to 32-bit due to div_u64_rem() use */
	if (((uint64_t)rec->loops * bulk *2) >= ((1ULL<<32)-1)) {
		pr_err("Loop cnt too big will overflow 32-bit\n");
		return 0;
	}

	time_bench_start(rec);
	/** Loop to measure **/
	for (i = 0; i < rec->loops; i++) {

		/* request bulk elems */
		if (type & BULK) { /* Compiler will optimize branch out */
			success = kmem_cache_alloc_bulk(my_slab, GFP_ATOMIC,
							bulk, GLOBAL_OBJS);
		} else if (type & FALLBACK_BULK) {
			success = my__kmem_cache_alloc_bulk(my_slab, GFP_ATOMIC,
							    bulk, GLOBAL_OBJS);
		} else {
			BUILD_BUG();
		}
		if (!success)
			goto out;

		/* Place objects to the objhash */
		for (j = 0; j < bulk; j++) {
			objhash_add_one(GLOBAL_OBJS[j]);
		}

		barrier(); /* compiler barrier */

		/* Extract objects to free from objhash */
		for (j = 0; j < bulk; j++) {
			if ((j % modulo_match) == 0) {
				/* Find matching page based on last_obj,
				 * this cause two object on same page
				 * next to each-other.
				 */
				//last_obj = objhash_extract(last_obj, false);

				/* Find matching page based on index[0]
				 * this cause objects related to first
				 * object page to be added throughout array
				 */
				last_obj = objhash_extract(GLOBAL_OBJS[0],
							   false);
			} else {
				/* Find none-matching page based on last_obj */
				last_obj = objhash_extract(last_obj, true);
			}
			if (last_obj)
				GLOBAL_OBJS[j] = last_obj;
			else
				goto out;
		}

		/* bulk return elems */
		if (type & BULK) {
			kmem_cache_free_bulk(my_slab, bulk, GLOBAL_OBJS);
		} else if (type & FALLBACK_BULK) {
			my__kmem_cache_free_bulk(my_slab, bulk, GLOBAL_OBJS);
		} else {
			BUILD_BUG();
		}

		/* NOTICE THIS COUNTS (bulk) alloc+free together*/
		loops_cnt+= bulk;
	}
out:
	time_bench_stop(rec, loops_cnt);
	/* cleanup */
	return loops_cnt;
}
/* Compiler should inline optimize other function calls out */
static int benchmark_slab_bulk(
	struct time_bench_record *rec, void *data)
{
	return __benchmark_slab_bulk(rec, data, BULK);
}
static int benchmark_slab_bulk_fallback(
	struct time_bench_record *rec, void *data)
{
	return __benchmark_slab_bulk(rec, data, FALLBACK_BULK);
}


void bulk_test(int bulk)
{
	time_bench_loop(loops/bulk, bulk, "worse-case-bulk", NULL,
			benchmark_slab_bulk);
	time_bench_loop(loops/bulk, bulk, "worse-case-fallback", NULL,
			benchmark_slab_bulk_fallback);
}


void bulk_N_same_page(int bulk, long int modulo)
{
	void *data = (void *) modulo;
	time_bench_loop(loops/bulk, bulk, "N-page-match-bulk", data,
			benchmark_slab_bulk);
	time_bench_loop(loops/bulk, bulk, "N-page-match-fallback", data,
			benchmark_slab_bulk_fallback);
}


int run_timing_tests(void)
{
	pr_info("Bench bulk size:%d\n", bulksz);
	bulk_test(bulksz);

	bulk_N_same_page(bulksz, 1); /* Map every page same, optimal case */
	bulk_N_same_page(bulksz, 2);
	bulk_N_same_page(bulksz, 3);
	bulk_N_same_page(bulksz, 4);
	bulk_N_same_page(bulksz, 5);
	bulk_N_same_page(bulksz, 6);
	bulk_N_same_page(bulksz, 10);
	bulk_N_same_page(bulksz, bulksz+1);

	return 0;
}

void run_try_crash_tests(void)
{
#define ARRAY_SZ 64
	int i;
	void *objs[ARRAY_SZ];

	pr_info("Run cases that try to crash the API\n");

	for (i=0; i < ARRAY_SZ; i++) {
		objs[i] = (void *)0xdeadbeef;
	}

	/* Test if it crash when freeing NULL objects */
	pr_info("- Misuse API: free array with NULL objects\n");
	for (i=0; i < 42; i++) {
		objs[i] = NULL;
	}
	kmem_cache_free_bulk(my_slab, 42, objs);

	/* Test if it crash when freeing NULL objects and one real obj*/
	pr_info("- Misuse API: free array with 1 object and rest NULL\n");
	objs[3] = kmem_cache_alloc(my_slab, GFP_ATOMIC);
	kmem_cache_free_bulk(my_slab, 42, objs);

	pr_info("- Misuse API: free array with some objects and rest NULL\n");
	objs[0] = kmem_cache_alloc(my_slab, GFP_ATOMIC);
	objs[1] = kmem_cache_alloc(my_slab, GFP_ATOMIC);
	objs[2] = NULL;
	objs[3] = kmem_cache_alloc(my_slab, GFP_ATOMIC);
	objs[4] = kmem_cache_alloc(my_slab, GFP_ATOMIC);
	objs[5] = NULL;
	objs[6] = kmem_cache_alloc(my_slab, GFP_ATOMIC);
	kmem_cache_free_bulk(my_slab, 42, objs);
	/* NOTICE: this test/verification is only valid if the bulk
	 * free call, implements invariance of putting NULLs into
	 * array... this "feature" is not even accepted upstream
	 */
	for (i=0; i < 9; i++) {
		if (objs[i] != NULL)
			pr_err("- ERROR: object[%d] were not free'ed!\n", i);
	}

	pr_info("Run manual cases exercising API\n");
	for (i=0; i < 42; i++) {
		objs[i] = NULL;
	}

	pr_info("- API: case hitting look-ahead\n");
	objs[0] = objhash_extract(NULL, false);    // page0
	objs[1] = objhash_extract(objs[0], false); // same-as-page0
	objs[2] = objhash_extract(objs[0], true);  // diff from page0
	objs[3] = objhash_extract(objs[0], false); // same-as-page0
	objs[4] = objhash_extract(objs[0], true);  // diff from page0
	objs[5] = kmem_cache_alloc(my_slab, GFP_ATOMIC);
	objs[6] = objhash_extract(objs[0], false); // same-as-page0
	objs[7] = (void *)0xbeefdead;
	kmem_cache_free_bulk(my_slab, 7, objs);

	pr_info("- API: case hitting every second elem\n");
	objs[0] = objhash_extract(NULL, false);    // page0
	objs[1] = objhash_extract(objs[0], true);  // page1
	objs[2] = objhash_extract(objs[0], false); // same-as-page0
	objs[3] = objhash_extract(objs[1], false); // same-as-page1
	objs[4] = objhash_extract(objs[0], false); // same-as-page0
	objs[5] = objhash_extract(objs[1], false); // same-as-page1
	objs[6] = objhash_extract(objs[0], false); // same-as-page0
	objs[7] = objhash_extract(objs[1], false); // same-as-page1
	objs[8] = (void *)0xdeaddead;
	kmem_cache_free_bulk(my_slab, 8, objs);

#undef ARRAY_SZ
}

static int __init slab_bulk_test03_module_init(void)
{
	int i;
	struct my_obj *obj = NULL;

	if (verbose)
		pr_info("Loaded\n");

	/* Init object hash */
	for (i = 0; i < HASHSZ; i++)
		INIT_HLIST_HEAD(&objhash[i]);

	/* Create the kmem_cache slab */
	my_slab = kmem_cache_create("slab_bulk_test03", sizeof(struct my_obj),
				    0, SLAB_HWCACHE_ALIGN, NULL);

	if (verbose)
		pr_info("Prefill with %d objects\n", prefill);
	for (i = 0; i < prefill; i++) {
		obj = kmem_cache_alloc(my_slab, GFP_ATOMIC);
		objhash_add_one(obj);
	}

	objhash_list_len();

/*
	pr_info("TEST %d\n", prefill / 4);
	for (i = 0; i < prefill / 4; i++)
		obj = objhash_extract(obj, true);
*/

	if (verbose) {
		preempt_disable();
		pr_info("DEBUG: cpu:%d\n", smp_processor_id());
		preempt_enable();
		pr_info("NOTICE: Measurements include calls to jhash()\n");
	}

#ifdef CONFIG_DEBUG_PREEMPT
	pr_warn("WARN: CONFIG_DEBUG_PREEMPT is enabled: this affect results\n");
#endif
#ifdef CONFIG_PREEMPT
	pr_warn("INFO: CONFIG_PREEMPT is enabled\n");
#endif
#ifdef CONFIG_PREEMPT_COUNT
	pr_warn("INFO: CONFIG_PREEMPT_COUNT is enabled\n");
#endif

	if (!nmatch) {
		if (run_timing_tests() < 0) {
			return -ECANCELED;
		}
	} else {
		bulk_N_same_page(bulksz, nmatch);
	}

	if (try_crash) {
		run_try_crash_tests();
	}

	return 0;
}
module_init(slab_bulk_test03_module_init);

static void __exit slab_bulk_test03_module_exit(void)
{
	int i, cnt = 0;
	struct my_obj *obj = NULL;

	/* Free rest of objhash */
	for (i = 0; i < HASHSZ; i++) {
		struct hlist_head *hhead = &objhash[i];
		struct hlist_node *tmp;
		int list_len = 0;

		hlist_for_each_entry_safe(obj, tmp, hhead, node) {
			hlist_del(&obj->node);
			kmem_cache_free(my_slab, obj);
			objhash_cnt--;
			cnt++;
			list_len++;
		}
		pr_info("objhash[%d] list length: %d\n", i, list_len);
	}
	WARN_ON(objhash_cnt != 0);

	if (verbose)
		pr_info("Unloaded (freed %d objects from objhash)\n", cnt);
}
module_exit(slab_bulk_test03_module_exit);

MODULE_DESCRIPTION("Synthetic worse-case benchmarking of slab bulk");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");
