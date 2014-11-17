/*
 * Module designed for looking at assembler code of store/load helper
 * from linux/alf_queue_helper.h
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/alf_queue.h>

static int verbose=1;

static int fake_variable=0;
static void *fake_array[10];

/* Defines for creating fake func calls to helpers */
#define create_helper_alf_enqueue_store(NAME)				\
	static noinline void						\
	helper_alf_enqueue_store_##NAME(u32 p_head, struct alf_queue *q,\
					void **ptr, const u32 n)	\
	{								\
		__helper_alf_enqueue_store_##NAME(p_head, q, ptr, n);   \
	}
#define create_helper_alf_dequeue_load(NAME)				\
	static noinline void						\
	helper_alf_dequeue_load_##NAME(u32 c_head, struct alf_queue *q,	\
				       void **ptr, const u32 n)		\
	{								\
		__helper_alf_dequeue_load_##NAME(c_head, q, ptr, n);	\
	}
#define create_helpers(NAME)						\
	create_helper_alf_enqueue_store(NAME)				\
	create_helper_alf_dequeue_load(NAME)
#define call_helper_alf_enqueue_store(NAME)				\
	helper_alf_enqueue_store_##NAME(p_head, q, ptr, n)
#define call_helper_alf_dequeue_load(NAME)				\
	helper_alf_dequeue_load_##NAME (p_head, q, ptr, n)

create_helpers(simple);
create_helpers(mask);
create_helpers(mask_less);
create_helpers(mask_less2);
create_helpers(nomask);
create_helpers(unroll);
create_helpers(unroll_duff);
create_helpers(memcpy);

static noinline
void fake_calls(struct alf_queue *q)
{
	u32 p_head = 1;
	void *ptr[42];
	const u32 n = 1;

	call_helper_alf_enqueue_store(simple);
	call_helper_alf_dequeue_load(simple);

	call_helper_alf_enqueue_store(mask);
	call_helper_alf_dequeue_load(mask);

	call_helper_alf_enqueue_store(mask_less);
	call_helper_alf_dequeue_load(mask_less);

	call_helper_alf_enqueue_store(mask_less2);
	call_helper_alf_dequeue_load(mask_less2);

	call_helper_alf_enqueue_store(nomask);
	call_helper_alf_dequeue_load(nomask);

	call_helper_alf_enqueue_store(unroll);
	call_helper_alf_dequeue_load(unroll);

	call_helper_alf_enqueue_store(unroll_duff);
	call_helper_alf_dequeue_load(unroll_duff);

	call_helper_alf_enqueue_store(memcpy);
	call_helper_alf_dequeue_load(memcpy);
}

/* This demonstrate that compiler will generate more specific/smaller
 * code when number of enqueue elements is a constant.
 */
static noinline void fake_call_enq_elem1(struct alf_queue *q, void *elem)
{
	alf_sp_enqueue(q, &elem, 1);
}
static noinline void fake_call_enq_elem3(struct alf_queue *q, void *elem)
{
	alf_sp_enqueue(q, &elem, 3);
}
static noinline void fake_call_enq_elem4(struct alf_queue *q, void *elem)
{
	alf_sp_enqueue(q, &elem, 4);
}
static noinline void fake_call_enq_elem16(struct alf_queue *q, void *elem)
{
	alf_sp_enqueue(q, &elem, 16);
}
static noinline void fake_call_enq_variable(struct alf_queue *q,  void *elem, u32 n)
{
	alf_sp_enqueue(q, &elem, n);
}


static int __init alf_queue_test_module_init(void)
{
	struct alf_queue *q;
	int ring_size = 512;
	int n = 42;

	if (verbose)
		pr_info("Loaded\n");

	q = alf_queue_alloc(ring_size, GFP_KERNEL);

	if (fake_variable) {
		fake_calls(q);
		fake_call_enq_elem1   (q, &fake_array[0]);
		fake_call_enq_elem3   (q, &fake_array[0]);
		fake_call_enq_elem4   (q, &fake_array[0]);
		fake_call_enq_elem16  (q, &fake_array[0]);
		fake_call_enq_variable(q, &fake_array[0], n);
	}

	alf_queue_free(q);
	return 0;
}
module_init(alf_queue_test_module_init);

static void __exit alf_queue_test_module_exit(void)
{
	if (verbose)
		pr_info("Unloaded\n");
}
module_exit(alf_queue_test_module_exit);

MODULE_DESCRIPTION("Test module for disassemble");
MODULE_AUTHOR("Jesper Dangaard Brouer <netoptimizer@brouer.com>");
MODULE_LICENSE("GPL");
