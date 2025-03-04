// SPDX-License-Identifier: GPL-2.0
/*
 * This file contains common KASAN code.
 *
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Andrey Ryabinin <ryabinin.a.a@gmail.com>
 *
 * Some code borrowed from https://github.com/xairy/kasan-prototype by
 *        Andrey Konovalov <andreyknvl@gmail.com>
 */

#include <linux/export.h>
#include <linux/init.h>
#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/linkage.h>
#include <linux/memblock.h>
#include <linux/memory.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/slab.h>
#include <linux/stacktrace.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/bug.h>

#include "kasan.h"
#include "../slab.h"

depot_stack_handle_t kasan_save_stack(gfp_t flags)
{
	unsigned long entries[KASAN_STACK_DEPTH];
	unsigned int nr_entries;

	nr_entries = stack_trace_save(entries, ARRAY_SIZE(entries), 0);
	nr_entries = filter_irq_stacks(entries, nr_entries);
	return stack_depot_save(entries, nr_entries, flags);
}

void kasan_set_track(struct kasan_track *track, gfp_t flags)
{
	track->pid = current->pid;
	track->stack = kasan_save_stack(flags);
}

#if defined(CONFIG_KASAN_GENERIC) || defined(CONFIG_KASAN_SW_TAGS)
void kasan_enable_current(void)
{
	current->kasan_depth++;
}

void kasan_disable_current(void)
{
	current->kasan_depth--;
}
#endif /* CONFIG_KASAN_GENERIC || CONFIG_KASAN_SW_TAGS */

void __kasan_unpoison_range(const void *address, size_t size)
{
	kasan_unpoison(address, size);
}

#ifdef CONFIG_KASAN_STACK
/* Unpoison the entire stack for a task. */
void kasan_unpoison_task_stack(struct task_struct *task)
{
	void *base = task_stack_page(task);

	kasan_unpoison(base, THREAD_SIZE);
}

/* Unpoison the stack for the current task beyond a watermark sp value. */
asmlinkage void kasan_unpoison_task_stack_below(const void *watermark)
{
	/*
	 * Calculate the task stack base address.  Avoid using 'current'
	 * because this function is called by early resume code which hasn't
	 * yet set up the percpu register (%gs).
	 */
	void *base = (void *)((unsigned long)watermark & ~(THREAD_SIZE - 1));

	kasan_unpoison(base, watermark - base);
}
#endif /* CONFIG_KASAN_STACK */

/*
 * Only allow cache merging when stack collection is disabled and no metadata
 * is present.
 */
slab_flags_t __kasan_never_merge(void)
{
	if (kasan_stack_collection_enabled())
		return SLAB_KASAN;
	return 0;
}

void __kasan_alloc_pages(struct page *page, unsigned int order)
{
	u8 tag;
	unsigned long i;

	if (unlikely(PageHighMem(page)))
		return;

	tag = kasan_random_tag();
	for (i = 0; i < (1 << order); i++)
		page_kasan_tag_set(page + i, tag);
	kasan_unpoison(page_address(page), PAGE_SIZE << order);
}

void __kasan_free_pages(struct page *page, unsigned int order)
{
	if (likely(!PageHighMem(page)))
		kasan_poison(page_address(page), PAGE_SIZE << order,
			     KASAN_FREE_PAGE);
}

/*
 * Adaptive redzone policy taken from the userspace AddressSanitizer runtime.
 * For larger allocations larger redzones are used.
 */
static inline unsigned int optimal_redzone(unsigned int object_size)
{
	return
		object_size <= 64        - 16   ? 16 :
		object_size <= 128       - 32   ? 32 :
		object_size <= 512       - 64   ? 64 :
		object_size <= 4096      - 128  ? 128 :
		object_size <= (1 << 14) - 256  ? 256 :
		object_size <= (1 << 15) - 512  ? 512 :
		object_size <= (1 << 16) - 1024 ? 1024 : 2048;
}

void __kasan_cache_create(struct kmem_cache *cache, unsigned int *size,
			  slab_flags_t *flags)
{
	unsigned int ok_size;
	unsigned int optimal_size;

	/*
	 * SLAB_KASAN is used to mark caches as ones that are sanitized by
	 * KASAN. Currently this flag is used in two places:
	 * 1. In slab_ksize() when calculating the size of the accessible
	 *    memory within the object.
	 * 2. In slab_common.c to prevent merging of sanitized caches.
	 */
	*flags |= SLAB_KASAN;

	if (!kasan_stack_collection_enabled())
		return;

	ok_size = *size;

	/* Add alloc meta into redzone. */
	cache->kasan_info.alloc_meta_offset = *size;
	*size += sizeof(struct kasan_alloc_meta);

	/*
	 * If alloc meta doesn't fit, don't add it.
	 * This can only happen with SLAB, as it has KMALLOC_MAX_SIZE equal
	 * to KMALLOC_MAX_CACHE_SIZE and doesn't fall back to page_alloc for
	 * larger sizes.
	 */
	if (*size > KMALLOC_MAX_SIZE) {
		cache->kasan_info.alloc_meta_offset = 0;
		*size = ok_size;
		/* Continue, since free meta might still fit. */
	}

	/* Only the generic mode uses free meta or flexible redzones. */
	if (!IS_ENABLED(CONFIG_KASAN_GENERIC)) {
		cache->kasan_info.free_meta_offset = KASAN_NO_FREE_META;
		return;
	}

	/*
	 * Add free meta into redzone when it's not possible to store
	 * it in the object. This is the case when:
	 * 1. Object is SLAB_TYPESAFE_BY_RCU, which means that it can
	 *    be touched after it was freed, or
	 * 2. Object has a constructor, which means it's expected to
	 *    retain its content until the next allocation, or
	 * 3. Object is too small.
	 * Otherwise cache->kasan_info.free_meta_offset = 0 is implied.
	 */
	if ((cache->flags & SLAB_TYPESAFE_BY_RCU) || cache->ctor ||
	    cache->object_size < sizeof(struct kasan_free_meta)) {
		ok_size = *size;

		cache->kasan_info.free_meta_offset = *size;
		*size += sizeof(struct kasan_free_meta);

		/* If free meta doesn't fit, don't add it. */
		if (*size > KMALLOC_MAX_SIZE) {
			cache->kasan_info.free_meta_offset = KASAN_NO_FREE_META;
			*size = ok_size;
		}
	}

	/* Calculate size with optimal redzone. */
	optimal_size = cache->object_size + optimal_redzone(cache->object_size);
	/* Limit it with KMALLOC_MAX_SIZE (relevant for SLAB only). */
	if (optimal_size > KMALLOC_MAX_SIZE)
		optimal_size = KMALLOC_MAX_SIZE;
	/* Use optimal size if the size with added metas is not large enough. */
	if (*size < optimal_size)
		*size = optimal_size;
}

size_t __kasan_metadata_size(struct kmem_cache *cache)
{
	if (!kasan_stack_collection_enabled())
		return 0;
	return (cache->kasan_info.alloc_meta_offset ?
		sizeof(struct kasan_alloc_meta) : 0) +
		(cache->kasan_info.free_meta_offset ?
		sizeof(struct kasan_free_meta) : 0);
}

struct kasan_alloc_meta *kasan_get_alloc_meta(struct kmem_cache *cache,
					      const void *object)
{
	if (!cache->kasan_info.alloc_meta_offset)
		return NULL;
	return kasan_reset_tag(object) + cache->kasan_info.alloc_meta_offset;
}

#ifdef CONFIG_KASAN_GENERIC
struct kasan_free_meta *kasan_get_free_meta(struct kmem_cache *cache,
					    const void *object)
{
	BUILD_BUG_ON(sizeof(struct kasan_free_meta) > 32);
	if (cache->kasan_info.free_meta_offset == KASAN_NO_FREE_META)
		return NULL;
	return kasan_reset_tag(object) + cache->kasan_info.free_meta_offset;
}
#endif

void __kasan_poison_slab(struct page *page)
{
	unsigned long i;

	for (i = 0; i < compound_nr(page); i++)
		page_kasan_tag_reset(page + i);
	kasan_poison(page_address(page), page_size(page),
		     KASAN_KMALLOC_REDZONE);
}

void __kasan_unpoison_object_data(struct kmem_cache *cache, void *object)
{
	kasan_unpoison(object, cache->object_size);
}

void __kasan_poison_object_data(struct kmem_cache *cache, void *object)
{
	kasan_poison(object, cache->object_size, KASAN_KMALLOC_REDZONE);
}

/*
 * This function assigns a tag to an object considering the following:
 * 1. A cache might have a constructor, which might save a pointer to a slab
 *    object somewhere (e.g. in the object itself). We preassign a tag for
 *    each object in caches with constructors during slab creation and reuse
 *    the same tag each time a particular object is allocated.
 * 2. A cache might be SLAB_TYPESAFE_BY_RCU, which means objects can be
 *    accessed after being freed. We preassign tags for objects in these
 *    caches as well.
 * 3. For SLAB allocator we can't preassign tags randomly since the freelist
 *    is stored as an array of indexes instead of a linked list. Assign tags
 *    based on objects indexes, so that objects that are next to each other
 *    get different tags.
 */
static u8 assign_tag(struct kmem_cache *cache, const void *object,
			bool init, bool keep_tag)
{
	if (IS_ENABLED(CONFIG_KASAN_GENERIC))
		return 0xff;

	/*
	 * 1. When an object is kmalloc()'ed, two hooks are called:
	 *    kasan_slab_alloc() and kasan_kmalloc(). We assign the
	 *    tag only in the first one.
	 * 2. We reuse the same tag for krealloc'ed objects.
	 */
	if (keep_tag)
		return get_tag(object);

	/*
	 * If the cache neither has a constructor nor has SLAB_TYPESAFE_BY_RCU
	 * set, assign a tag when the object is being allocated (init == false).
	 */
	if (!cache->ctor && !(cache->flags & SLAB_TYPESAFE_BY_RCU))
		return init ? KASAN_TAG_KERNEL : kasan_random_tag();

	/* For caches that either have a constructor or SLAB_TYPESAFE_BY_RCU: */
#ifdef CONFIG_SLAB
	/* For SLAB assign tags based on the object index in the freelist. */
	return (u8)obj_to_index(cache, virt_to_page(object), (void *)object);
#else
	/*
	 * For SLUB assign a random tag during slab creation, otherwise reuse
	 * the already assigned tag.
	 */
	return init ? kasan_random_tag() : get_tag(object);
#endif
}

void * __must_check __kasan_init_slab_obj(struct kmem_cache *cache,
						const void *object)
{
	struct kasan_alloc_meta *alloc_meta;

	if (kasan_stack_collection_enabled()) {
		alloc_meta = kasan_get_alloc_meta(cache, object);
		if (alloc_meta)
			__memset(alloc_meta, 0, sizeof(*alloc_meta));
	}

	/* Tag is ignored in set_tag() without CONFIG_KASAN_SW/HW_TAGS */
	object = set_tag(object, assign_tag(cache, object, true, false));

	return (void *)object;
}

static bool ____kasan_slab_free(struct kmem_cache *cache, void *object,
			      unsigned long ip, bool quarantine)
{
	u8 tag;
	void *tagged_object;

	tag = get_tag(object);
	tagged_object = object;
	object = kasan_reset_tag(object);

	if (is_kfence_address(object))
		return false;

	if (unlikely(nearest_obj(cache, virt_to_head_page(object), object) !=
	    object)) {
		kasan_report_invalid_free(tagged_object, ip);
		return true;
	}

	/* RCU slabs could be legally used after free within the RCU period */
	if (unlikely(cache->flags & SLAB_TYPESAFE_BY_RCU))
		return false;

	if (!kasan_check(tagged_object)) {
		kasan_report_invalid_free(tagged_object, ip);
		return true;
	}

	kasan_poison(object, cache->object_size, KASAN_KMALLOC_FREE);

	if (!kasan_stack_collection_enabled())
		return false;

	if ((IS_ENABLED(CONFIG_KASAN_GENERIC) && !quarantine))
		return false;

	kasan_set_free_info(cache, object, tag);

	return kasan_quarantine_put(cache, object);
}

bool __kasan_slab_free(struct kmem_cache *cache, void *object, unsigned long ip)
{
	return ____kasan_slab_free(cache, object, ip, true);
}

void __kasan_slab_free_mempool(void *ptr, unsigned long ip)
{
	struct page *page;

	page = virt_to_head_page(ptr);

	/*
	 * Even though this function is only called for kmem_cache_alloc and
	 * kmalloc backed mempool allocations, those allocations can still be
	 * !PageSlab() when the size provided to kmalloc is larger than
	 * KMALLOC_MAX_SIZE, and kmalloc falls back onto page_alloc.
	 */
	if (unlikely(!PageSlab(page))) {
		if (ptr != page_address(page)) {
			kasan_report_invalid_free(ptr, ip);
			return;
		}
		kasan_poison(ptr, page_size(page), KASAN_FREE_PAGE);
	} else {
		____kasan_slab_free(page->slab_cache, ptr, ip, false);
	}
}

static void set_alloc_info(struct kmem_cache *cache, void *object, gfp_t flags)
{
	struct kasan_alloc_meta *alloc_meta;

	alloc_meta = kasan_get_alloc_meta(cache, object);
	if (alloc_meta)
		kasan_set_track(&alloc_meta->alloc_track, flags);
}

static void *____kasan_kmalloc(struct kmem_cache *cache, const void *object,
				size_t size, gfp_t flags, bool keep_tag)
{
	unsigned long redzone_start;
	unsigned long redzone_end;
	u8 tag;

	if (gfpflags_allow_blocking(flags))
		kasan_quarantine_reduce();

	if (unlikely(object == NULL))
		return NULL;

	if (is_kfence_address(object))
		return (void *)object;

	redzone_start = round_up((unsigned long)(object + size),
				KASAN_GRANULE_SIZE);
	redzone_end = round_up((unsigned long)object + cache->object_size,
				KASAN_GRANULE_SIZE);
	tag = assign_tag(cache, object, false, keep_tag);

	/* Tag is ignored in set_tag without CONFIG_KASAN_SW/HW_TAGS */
	kasan_unpoison(set_tag(object, tag), size);
	kasan_poison((void *)redzone_start, redzone_end - redzone_start,
			   KASAN_KMALLOC_REDZONE);

	if (kasan_stack_collection_enabled())
		set_alloc_info(cache, (void *)object, flags);

	return set_tag(object, tag);
}

void * __must_check __kasan_slab_alloc(struct kmem_cache *cache,
					void *object, gfp_t flags)
{
	return ____kasan_kmalloc(cache, object, cache->object_size, flags, false);
}

void * __must_check __kasan_kmalloc(struct kmem_cache *cache, const void *object,
					size_t size, gfp_t flags)
{
	return ____kasan_kmalloc(cache, object, size, flags, true);
}
EXPORT_SYMBOL(__kasan_kmalloc);

void * __must_check __kasan_kmalloc_large(const void *ptr, size_t size,
						gfp_t flags)
{
	struct page *page;
	unsigned long redzone_start;
	unsigned long redzone_end;

	if (gfpflags_allow_blocking(flags))
		kasan_quarantine_reduce();

	if (unlikely(ptr == NULL))
		return NULL;

	page = virt_to_page(ptr);
	redzone_start = round_up((unsigned long)(ptr + size),
				KASAN_GRANULE_SIZE);
	redzone_end = (unsigned long)ptr + page_size(page);

	kasan_unpoison(ptr, size);
	kasan_poison((void *)redzone_start, redzone_end - redzone_start,
		     KASAN_PAGE_REDZONE);

	return (void *)ptr;
}

void * __must_check __kasan_krealloc(const void *object, size_t size, gfp_t flags)
{
	struct page *page;

	if (unlikely(object == ZERO_SIZE_PTR))
		return (void *)object;

	page = virt_to_head_page(object);

	if (unlikely(!PageSlab(page)))
		return __kasan_kmalloc_large(object, size, flags);
	else
		return ____kasan_kmalloc(page->slab_cache, object, size,
						flags, true);
}

void __kasan_kfree_large(void *ptr, unsigned long ip)
{
	if (ptr != page_address(virt_to_head_page(ptr)))
		kasan_report_invalid_free(ptr, ip);
	/* The object will be poisoned by kasan_free_pages(). */
}

bool __kasan_check_byte(const void *address, unsigned long ip)
{
	if (!kasan_check(address)) {
		kasan_report_invalid_free((void *)address, ip);
		return false;
	}
	return true;
}
