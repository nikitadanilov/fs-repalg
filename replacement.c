/* -*- C -*- */

/* replacement.c */

/*
 * Prominent copyright and license message is at the end of this file, please
 * read it.
 */

/*
 * "replacement" processes logs with file system access traces to test and
 * study various page replacement policies.
 */

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <time.h>

#include <sys/types.h>

#include "list.h"

#define ergo(a, b) (!(a) || (b))
#define equi(a, b) (!!(a) == !!(b))
#define sizeof_array(a) (sizeof(a)/sizeof((a)[0]))

/*
 * min()/max() macros that also do
 * strict type-checking.. See the
 * "unnecessary" pointer comparison.
 */
#define min(x,y) ({ \
	typeof(x) _x = (x);	\
	typeof(y) _y = (y);	\
	(void) (&_x == &_y);		\
	_x < _y ? _x : _y; })

#define max(x,y) ({ \
	typeof(x) _x = (x);	\
	typeof(y) _y = (y);	\
	(void) (&_x == &_y);		\
	_x > _y ? _x : _y; })

/*
 * ..and if you can't take the strict
 * types, you can specify one yourself.
 *
 * Or not use min/max at all, of course.
 */
#define min_t(type,x,y) \
	({ type __x = (x); type __y = (y); __x < __y ? __x: __y; })
#define max_t(type,x,y) \
	({ type __x = (x); type __y = (y); __x > __y ? __x: __y; })

/*
 * Data type declarations.
 */

/*
 * Type of access. Kept in ->a_type field of struct access.
 */
enum fslog_rec_type {
	FSLOG_READ   = 'R', /* read(2) */
	FSLOG_RA     = 'r', /* read-ahead */
	FSLOG_WRITE  = 'W', /* write(2) */
	FSLOG_PFAULT = 'P', /* page fault */
	FSLOG_PUNCH  = 'T'  /* truncate(2) */
};

typedef u_int64_t frame_no_t; /* physical frame number */
typedef u_int64_t vpage_no_t; /* virtual page number */
typedef u_int64_t inode_no_t; /* file object number (inode number) */
typedef u_int64_t pgoff_t;    /* offset of page within file object */


struct vpage;
struct frame;
struct mm;
struct repalg;
struct object;

/*
 * virtual page.
 *
 * One of these exist for every page in every file accessed in the given
 * trace.
 *
 */
struct vpage {
	/*
	 * page number
	 */
	vpage_no_t       v_no;
	/*
	 * logical offset of page within file
	 */
	pgoff_t          v_index;
	/*
	 * flags from enum vpage_flags
	 */
	u_int32_t        v_flags;
	/*
	 * file object this page belongs to
	 */
	struct object   *v_object;
	/*
	 * physical frame this page resides in, or NULL if not resident
	 */
	struct frame    *v_frame;
	/*
	 * linkage into list of pages belonging to the same file object
	 */
	struct list_head v_pages;
	/*
	 * reserved for replacement policy use. Currently used by OPT
	 * algorithm to keep time-sorted list of future access times, and by
	 * 2Q algorithm to keep track of A1out list, and by CAR algorithm.
	 */
	struct list_head v_stuff;
};

/*
 * virtual page flags. Stored in ->v_flags.
 */
enum vpage_flags {
	/*
	 * this page has already been seen in this trace.
	 */
	VP_SEEN  = 1 << 0,
	/*
	 * Policy reserved bits.
	 */
	VP_PSHIFT = 1,
	VP_PBIT0 = 1 << (VP_PSHIFT + 0),
	VP_PBIT1 = 1 << (VP_PSHIFT + 1),
	VP_PBIT2 = 1 << (VP_PSHIFT + 2),
	VP_PBIT3 = 1 << (VP_PSHIFT + 3),
	VP_PMASK = VP_PBIT0|VP_PBIT1|VP_PBIT2|VP_PBIT3
};

/*
 * physical frame.
 *
 * One of these exists for each frame in emulated primary storage.
 */
struct frame {
	/*
	 * frame number.
	 */
	frame_no_t       f_no;
	/*
	 * frame flags, taken from enum frame_flags
	 */
	u_int32_t        f_flags;
	/*
	 * virtual page residing in this frame, or NULL if frame is free.
	 */
	struct vpage    *f_page;
	/*
	 * linkage into policy-specific (usually global) list, or free list.
	 */
	struct list_head f_linkage;
};

/*
 * physical frame flags.
 */
enum frame_flags {
	/*
	 * frame was referenced. This is set by main tracing loop. Currently
	 * only used by fifo2 (FIFO-SECOND-CHANCE) algorithm.
	 */
	FR_REF   = 1 << 0,
	/*
	 * much like FR_REF, but completely controlled by policy.
	 */
	FR_REF1  = 1 << 1,
	/*
	 * frame is dirty. Page in the dirty frame has to be paged out to the
	 * secondary storage before frame can be reclaimed.
	 */
	FR_DIRTY = 1 << 2,
	/*
	 * Frame is in "tail" list. Current used by SFIFO, 2Q and LINUX
	 * algorithms.
	 */
	FR_TAIL  = 1 << 3
};

/*
 * file object.
 */
struct object {
	/*
	 * inode number.
	 */
	inode_no_t       o_no;
	/*
	 * list of pages, linked through ->v_pages.
	 */
	struct list_head o_pages;
};

/*
 * record, representing single access to the file system, recorded in the
 * trace stream.
 */
struct access {
	/*
	 * number of accessed virtual page.
	 */
	vpage_no_t       a_page;
	/*
	 * inode number of accessed object.
	 */
	inode_no_t       a_object;
	/*
	 * offset of accessed page in the accessed object.
	 */
	pgoff_t          a_index;
	/*
	 * access type, must be from enum fslog_rec_type.
	 */
	char             a_type;
	/*
	 * linkage into list. This is used by access look-ahead functions to
	 * allow certain "clairvoyant" replacement policies (OPT and WORST
	 * currently) to look at the future accesses.
	 */
	struct list_head a_linkage;
};

enum car_queue {
	CQ_NONE,
	CQ_T1,
	CQ_T2,
	CQ_B1,
	CQ_B2,
	CQ_NR
};

/*
 * mm.
 *
 * Represents emulated memory subsystem.
 */
struct mm {
	/*
	 * selected replacement algorithm.
	 */
	struct repalg   *m_alg;
	/*
	 * number of physical frames in the primary storage.
	 */
	u_int64_t        m_nr_frames;
	/*
	 * number of virtual pages in the virtual memory.
	 */
	u_int64_t        m_nr_vpages;
	/*
	 * number of file objects.
	 */
	u_int64_t        m_nr_objects;
	/*
	 * number of free pages.
	 */
	u_int64_t        m_nr_free;
	/*
	 * an array of frames (physical memory).
	 */
	struct frame    *m_frames;
	/*
	 * an array of pages (virtual memory).
	 */
	struct vpage    *m_vpages;
	/*
	 * an array of file objects.
	 */
	struct object   *m_objects;
	/*
	 * list of free frames, linked through ->f_linkage.
	 */
	struct list_head m_freelist;
	/*
	 * lru list of frames, used by LRU policy.
	 */
	struct list_head m_lru;
	/*
	 * fifo list of frames, used by FIFO policy.
	 */
	struct list_head m_fifo;
	/*
	 * fifo2 list of frames, used by FIFO-SECOND-CHANCE policy.
	 */
	struct list_head m_fifo2;

	/*
	 * list of looked-ahead accesses.
	 */
	struct list_head m_future;

	/*
	 * number of cache hits (i.e., avoided page faults).
	 */
	u_int64_t        m_hits;
	/*
	 * number of cache misses (i.e., page faults).
	 */
	u_int64_t        m_misses;
	/*
	 * total number of accesses processed so far. Can be larger that sum
	 * of ->m_hits and ->m_misses, because FSLOG_WRITE and FSLOG_PUNCH
	 * accesses do not cause faults (there is no need to read missing page
	 * to be overwritten or discarded), and are counted in neither
	 * ->m_hits nor ->m_misses.
	 */
	u_int64_t        m_total;

	struct {
		/*
		 * Percentage of tail list in SFIFO algorithm. When this is
		 * 100 SFIFO is identical to LRU, when this is 0, SFIFO is
		 * identical to FIFO.
		 */
		u_int16_t        tail;
		u_int64_t        tail_nr;
	} m_sfifo;
	struct {
		/*
		 * Kin parameter (percent of ->m_nr_frames)
		 */
		u_int16_t        kin;
		/*
		 * Kout parameter (percent of ->m_nr_frames)
		 */
		u_int16_t        kout;

		u_int64_t        am_nr;
		u_int64_t        a1in_nr;
		u_int64_t        a1out_nr;

		struct list_head am;
		struct list_head a1in;
		struct list_head a1out;
	} m_q2;
	struct {
		struct {
			struct list_head list;
			u_int64_t        nr;
		} q[CQ_NR];
		/*
		 * Target T1 size
		 */
		u_int64_t        p;
	} m_car, m_arc;
	struct {
		struct list_head active;
		struct list_head inactive;

		u_int64_t        nr_active;
		u_int64_t        nr_inactive;

		u_int64_t        refill_counter;
		u_int64_t        pages_scanned;

		u_int64_t        nr_scan_active;
		u_int64_t        nr_scan_inactive;

		int              temp_priority;
		int              prev_priority;

	} m_linux;
};

/*
 * replacement algorithm.
 */
struct repalg {
	/*
	 * name.
	 */
	const char *r_name;
	/*
	 * initialization method.
	 */
	int       (*r_init)(struct mm *mm);
	/*
	 * finalization method.
	 */
	void      (*r_fini)(struct mm *mm);

	/*
	 * called to process read.
	 */
	void      (*r_read )(struct mm *mm, struct vpage *pg);
	/*
	 * called to process read-ahead.
	 */
	void      (*r_ra   )(struct mm *mm, struct vpage *pg);
	/*
	 * called to process write.
	 */
	void      (*r_write)(struct mm *mm, struct vpage *pg);
	/*
	 * called to process page fault.
	 */
	void      (*r_fault)(struct mm *mm, struct vpage *pg);
	/*
	 * called to process page truncate request.
	 */
	void      (*r_punch)(struct mm *mm, struct vpage *pg);

	/*
	 * called to allocate new frame to place @pg to. This is called
	 * internally from ->r_{read,ra,write,fault}() implementations.
	 */
	void      (*r_alloc)(struct mm *mm, struct vpage *pg);
};

enum {
	VERBOSE_TRACE    = 1 << 0,
	VERBOSE_TABLE    = 1 << 1,
	VERBOSE_LOG      = 1 << 2,
	VERBOSE_PROGRESS = 1 << 3
};

static int verbose = 0;

static void vpage_print(const char *prefix, const struct vpage *pg);

static void frame_fini(struct mm *mm, struct frame *frame)
{
	list_del_init(&frame->f_linkage);
}

static void frame_init(struct mm *mm, struct frame *frame)
{
	list_add_tail(&frame->f_linkage, &mm->m_freelist);
}

static void vpage_fini(struct mm *mm, struct vpage *page)
{
}

static void vpage_init(struct mm *mm, struct vpage *page)
{
	INIT_LIST_HEAD(&page->v_pages);
	INIT_LIST_HEAD(&page->v_stuff);
}

static void object_fini(struct mm *mm, struct object *obj)
{
}

static void object_init(struct mm *mm, struct object *obj)
{
	INIT_LIST_HEAD(&obj->o_pages);
}

static int frame_invariant(const struct mm *mm, const struct frame *frame)
{
	return
		frame->f_no < mm->m_nr_frames &&
		ergo(frame->f_page != NULL, frame->f_page->v_frame == frame);
}

static int vpage_invariant(const struct mm *mm, const struct vpage *page)
{
	return
		page->v_no < mm->m_nr_vpages &&
		ergo(page->v_frame != NULL, page->v_frame->f_page == page);
}

static struct frame *frame_from_list(struct list_head *head)
{
	return container_of(head, struct frame, f_linkage);
}

static struct frame *frame_free_get(struct mm *mm)
{
	struct list_head *head;
	struct frame     *frame;

	assert(!list_empty(&mm->m_freelist));
	assert(mm->m_nr_free > 0);

	head = mm->m_freelist.next;
	list_del_init(head);
	mm->m_nr_free--;
	frame = frame_from_list(head);
	assert(frame->f_page == NULL);
	assert(frame->f_flags == 0);
	return frame;
}

static void frame_free_put(struct mm *mm, struct frame *frame)
{
	assert(mm->m_nr_free < mm->m_nr_frames);
	assert(frame->f_page == NULL);

	frame->f_flags = 0;
	list_move(&frame->f_linkage, &mm->m_freelist);
	++mm->m_nr_free;
}

static void vpage_place(struct mm *mm, struct vpage *pg, struct frame *frame)
{
	assert(pg->v_frame == NULL);
	assert(frame->f_page == NULL);

	pg->v_frame = frame;
	frame->f_page = pg;
	if (verbose & VERBOSE_TRACE)
		vpage_print("P  ", pg);
}

static struct vpage *vpage_from_list(struct list_head *head)
{
	return container_of(head, struct vpage, v_stuff);
}

static void frame_pageout(struct mm *mm, struct frame *frame)
{
	struct vpage *pg;

	assert(frame_invariant(mm, frame));

	pg = frame->f_page;
	assert(pg != NULL);
	assert(vpage_invariant(mm, pg));
	if (verbose & VERBOSE_TRACE)
		vpage_print("O  ", pg);
	frame->f_flags &= ~FR_DIRTY;
}

static void vpage_pagein(struct mm *mm, struct vpage *pg)
{
	struct frame *frame;

	frame = pg->v_frame;
	assert(frame != NULL);
	if (verbose & VERBOSE_TRACE)
		vpage_print("I  ", pg);
}

static void frame_free(struct mm *mm, struct frame *frame)
{
	struct vpage *pg;

	assert(frame_invariant(mm, frame));

	pg = frame->f_page;
	assert(pg != NULL);
	assert(pg->v_frame == frame);
	assert(vpage_invariant(mm, pg));
	if (verbose & VERBOSE_TRACE)
		vpage_print("F  ", pg);
	pg->v_frame = NULL;
	frame->f_page = NULL;
	frame_free_put(mm, frame);
}

static void frame_steal(struct mm *mm, struct frame *frame)
{
	assert(frame_invariant(mm, frame));

	if (frame->f_page != NULL) {
		if (frame->f_flags & FR_DIRTY)
			frame_pageout(mm, frame);
		frame_free(mm, frame);
	}
}

static struct access *access_from_list(struct list_head *head)
{
	return container_of(head, struct access, a_linkage);
}

static int access_read(struct access *access)
{
	int result;
	char line[64];

	if (!fgets(line, sizeof line, stdin))
		result = ENOENT;
	else if (sscanf(line, "%llx %llx %llx %c", &access->a_page,
			&access->a_object,
			&access->a_index, &access->a_type) != 4) {
		fprintf(stderr, "Malformed input: `%s'\n", line);
		result = EINVAL;
	} else
		result = 0;
	return result;
}

static int access_get(struct mm *mm, struct access *access)
{
	int result;

	if (!list_empty(&mm->m_future)) {
		struct access *tomorrow;

		tomorrow = access_from_list(mm->m_future.next);
		list_del_init(&tomorrow->a_linkage);
		*access = *tomorrow;
		free(tomorrow);
		result = 0;
	} else
		result = access_read(access);
	return result;
}

static int access_look_ahead(struct mm *mm, struct access **access)
{
	int result;
	struct access *forecast;

	forecast = *access;
	if (forecast == NULL)
		forecast = access_from_list(&mm->m_future);

	if (forecast->a_linkage.next != &mm->m_future) {
		*access = access_from_list(forecast->a_linkage.next);
		result = 0;
	} else {
		forecast = malloc(sizeof *forecast);
		if (forecast != NULL) {
			result = access_read(forecast);
			if (result == 0) {
				list_add_tail(&forecast->a_linkage,
					      &mm->m_future);
				*access = forecast;
			} else
				free(forecast);
		} else
			result = ENOMEM;
	}
	return result;
}

static int generic_init(struct mm *mm)
{
	return 0;
}

static void generic_fini(struct mm *mm)
{
}

static void generic_read(struct mm *mm, struct vpage *pg)
{
	mm->m_alg->r_alloc(mm, pg);
	vpage_pagein(mm, pg);
}

static void generic_write(struct mm *mm, struct vpage *pg)
{
	mm->m_alg->r_alloc(mm, pg);
}

static void generic_punch(struct mm *mm, struct vpage *pg)
{
	assert(vpage_invariant(mm, pg));
	if (pg->v_frame != NULL) {
		struct frame *frame;

		frame = pg->v_frame;

		assert(frame_invariant(mm, frame));
		frame_free(mm, frame);
	}
	assert(vpage_invariant(mm, pg));
}

static int random_init(struct mm *mm)
{
	srandom(time(NULL));
	return 0;
}

static void random_alloc(struct mm *mm, struct vpage *pg)
{
	struct frame *victim;

	assert(vpage_invariant(mm, pg));

	if (pg->v_frame == NULL) {
		/*
		 * Miss
		 */
		if (mm->m_nr_free == 0) {
			/*
			 * XXX random() is long int.
			 *
			 * XXX yes, % is bad. Who cares?
			 */
			victim = &mm->m_frames[random() % mm->m_nr_frames];
			frame_steal(mm, victim);
		}
		assert(mm->m_nr_free > 0);
		victim = frame_free_get(mm);
		vpage_place(mm, pg, victim);
	}
	assert(vpage_invariant(mm, pg));
}

static void lru_alloc(struct mm *mm, struct vpage *pg)
{
	struct frame *frame;

	assert(vpage_invariant(mm, pg));

	if (pg->v_frame == NULL) {
		/*
		 * Miss
		 */
		if (mm->m_nr_free == 0) {
			assert(!list_empty(&mm->m_lru));
			frame = frame_from_list(mm->m_lru.prev);
			frame_steal(mm, frame);
		}
		assert(mm->m_nr_free > 0);
		frame = frame_free_get(mm);
		vpage_place(mm, pg, frame);
	}
	assert(vpage_invariant(mm, pg));
	frame = pg->v_frame;
	assert(pg->v_frame != NULL);
	list_move(&frame->f_linkage, &mm->m_lru);
}

static void fifo_alloc(struct mm *mm, struct vpage *pg)
{
	struct frame *frame;

	assert(vpage_invariant(mm, pg));

	if (pg->v_frame == NULL) {
		/*
		 * Miss
		 */
		if (mm->m_nr_free == 0) {
			assert(!list_empty(&mm->m_fifo));
			frame = frame_from_list(mm->m_fifo.prev);
			frame_steal(mm, frame);
		}
		assert(mm->m_nr_free > 0);
		frame = frame_free_get(mm);
		vpage_place(mm, pg, frame);
		assert(list_empty(&frame->f_linkage));
		list_add(&frame->f_linkage, &mm->m_fifo);
	}
	assert(vpage_invariant(mm, pg));
	assert(pg->v_frame != NULL);
}

/*
 * FIFO2
 *
 * FIFO Second Chance.
 */
static void fifo2_alloc(struct mm *mm, struct vpage *pg)
{
	struct frame *frame;

	assert(vpage_invariant(mm, pg));

	if (pg->v_frame == NULL) {
		/*
		 * Miss
		 */
		if (mm->m_nr_free == 0) {
			assert(!list_empty(&mm->m_fifo2));
			do {
				frame = frame_from_list(mm->m_fifo2.prev);
				if (frame->f_flags & FR_REF) {
					frame->f_flags &= ~FR_REF;
					list_move(&frame->f_linkage,
						  &mm->m_fifo2);
				} else
					break;
			} while (1);
			frame_steal(mm, frame);
		}
		assert(mm->m_nr_free > 0);
		frame = frame_free_get(mm);
		vpage_place(mm, pg, frame);
		assert(list_empty(&frame->f_linkage));
		list_add(&frame->f_linkage, &mm->m_fifo2);
	}
	assert(vpage_invariant(mm, pg));
	assert(pg->v_frame != NULL);
}

/*
 * SFIFO
 *
 * From "Segmented FIFO page replacement", by Rollins Turner and Henry Levy
 *
 * http://portal.acm.org/ft_gateway.cfm?id=805473&type=pdf&coll=portal&dl=ACM&CFID=15151515&CFTOKEN=6184618
 */
static void sfifo_alloc(struct mm *mm, struct vpage *pg)
{
	struct frame *frame;

	assert(vpage_invariant(mm, pg));

	if (pg->v_frame == NULL) {
		if (mm->m_nr_free == 0) {
			while (mm->m_sfifo.tail_nr <=
			       mm->m_nr_frames * mm->m_sfifo.tail / 100) {
				/*
				 * Tail list is too short, populate it.
				 */
				frame = frame_from_list(mm->m_fifo.prev);
				assert(!(frame->f_flags & FR_TAIL));
				frame->f_flags |= FR_TAIL;
				list_move(&frame->f_linkage, &mm->m_lru);
				mm->m_sfifo.tail_nr++;
			}
			assert(!list_empty(&mm->m_lru));
			frame = frame_from_list(mm->m_lru.prev);
			assert(frame->f_flags & FR_TAIL);
			frame->f_flags &= ~FR_TAIL;
			mm->m_sfifo.tail_nr--;
			frame_steal(mm, frame);
		}
		assert(mm->m_nr_free > 0);
		frame = frame_free_get(mm);
		vpage_place(mm, pg, frame);
		assert(list_empty(&frame->f_linkage));
		list_add(&frame->f_linkage, &mm->m_fifo);
	} else {
		frame = pg->v_frame;
		if (frame->f_flags & FR_TAIL) {
			frame->f_flags &= ~FR_TAIL;
			mm->m_sfifo.tail_nr--;
			list_move(&frame->f_linkage, &mm->m_fifo);
		}
	}
	assert(vpage_invariant(mm, pg));
	assert(pg->v_frame != NULL);
}

/*
 * 2Q
 *
 * From "2Q: A Low Overhead High Performance Buffer Management Replacement
 * Algorithm", by Theodore Johnson and Dennis Shasha.
 *
 * http://www.vldb.org/conf/1994/P439.PDF
 */

static void q2_reclaim_for(struct mm *mm, struct vpage *pg)
{
	struct frame *frame;

	if (mm->m_nr_free == 0) {
		if (mm->m_q2.a1in_nr > mm->m_nr_frames * mm->m_q2.kin / 100) {
			struct vpage *tail;

			frame = frame_from_list(mm->m_q2.a1in.prev);
			list_del_init(&frame->f_linkage);
			--mm->m_q2.a1in_nr;
			assert(frame->f_flags & FR_TAIL);
			frame->f_flags &= ~FR_TAIL;
			tail = frame->f_page;
			assert(tail != NULL);
			assert(list_empty(&tail->v_stuff));
			list_add(&tail->v_stuff, &mm->m_q2.a1out);
			if (mm->m_q2.a1out_nr >=
			    mm->m_nr_frames * mm->m_q2.kout / 100)
				list_del_init(mm->m_q2.a1out.prev);
			else
				++mm->m_q2.a1out_nr;
		} else {
			frame = frame_from_list(mm->m_q2.am.prev);
			assert(!(frame->f_flags & FR_TAIL));
			list_del_init(&frame->f_linkage);
			--mm->m_q2.am_nr;
		}
		frame_steal(mm, frame);
	}
	frame = frame_free_get(mm);
	vpage_place(mm, pg, frame);
}

static void q2_alloc(struct mm *mm, struct vpage *pg)
{
	struct frame *frame;

	assert(vpage_invariant(mm, pg));

	frame = pg->v_frame;
	if (frame != NULL) {
		if (!(frame->f_flags & FR_TAIL))
			/*
			 * Frame is in Am list.
			 */
			list_move(&frame->f_linkage, &mm->m_q2.am);
	} else {
		q2_reclaim_for(mm, pg);
		frame = pg->v_frame;
		assert(frame != NULL);
		assert(list_empty(&frame->f_linkage));
		if (!list_empty(&pg->v_stuff)) {
			/*
			 * Page is in A1out list.
			 */
			list_add(&frame->f_linkage, &mm->m_q2.am);
			++mm->m_q2.am_nr;
			list_del_init(&pg->v_stuff);
			--mm->m_q2.a1out_nr;
		} else {
			list_add(&frame->f_linkage, &mm->m_q2.a1in);
			++mm->m_q2.a1in_nr;
			frame->f_flags |= FR_TAIL;
		}
	}
}

/*
 * CAR
 *
 * From "CAR: Clock with Adaptive Replacement", by Sorav Bansal and Dharmendra
 * S. Modha
 *
 * http://citeseer.ist.psu.edu/bansal04car.html
 */

static int car_init(struct mm *mm)
{
	int i;

	for (i = 0; i < sizeof_array(mm->m_car.q); ++i)
		INIT_LIST_HEAD(&mm->m_car.q[i].list);
	mm->m_car.q[CQ_NONE].nr = mm->m_nr_vpages;
	return 0;
}

static void car_fini(struct mm *mm)
{
}

static enum car_queue car_queue_get(const struct vpage *pg)
{
	return (pg->v_flags & VP_PMASK) >> (VP_PSHIFT + 1);
}

static void car_queue_set(struct vpage *pg, enum car_queue q)
{
	pg->v_flags &= ~VP_PMASK;
	pg->v_flags |= q << (VP_PSHIFT + 1);
}

static int car_ref_get(const struct vpage *pg)
{
	return pg->v_flags & VP_PBIT0;
}

static void car_ref_set(struct vpage *pg, int bit)
{
	pg->v_flags |= !!bit * VP_PBIT0;
}


static void car_move(struct mm *mm, struct vpage *pg,
		     enum car_queue t, int tail)
{
	enum car_queue q;

	q = car_queue_get(pg);

	--mm->m_car.q[q].nr;
	++mm->m_car.q[t].nr;
	if (tail)
		list_move_tail(&pg->v_stuff, &mm->m_car.q[t].list);
	else
		list_move(&pg->v_stuff, &mm->m_car.q[t].list);
	car_queue_set(pg, t);
	assert(equi(pg->v_frame != NULL,
		    car_queue_get(pg) == CQ_T1 || car_queue_get(pg) == CQ_T2));
}

static struct vpage *car_queue(struct mm *mm, enum car_queue q, int tail)
{
	struct vpage *pg;

	assert(mm->m_car.q[q].nr > 0);

	if (tail)
		pg = vpage_from_list(mm->m_car.q[q].list.prev);
	else
		pg = vpage_from_list(mm->m_car.q[q].list.next);
	assert(car_queue_get(pg) == q);
	return pg;
}

static void car_replace(struct mm *mm)
{
	int found;
	assert(mm->m_car.q[CQ_T1].nr + mm->m_car.q[CQ_T2].nr + mm->m_nr_free ==
	       mm->m_nr_frames);

	found = 0;
	do {
		struct vpage *pg;
		enum car_queue target;
		int ref;

		if (mm->m_car.q[CQ_T1].nr >= max(1ULL, mm->m_car.p)) {
			pg = car_queue(mm, CQ_T1, 0);
			target = CQ_B1;
		} else {
			pg = car_queue(mm, CQ_T2, 0);
			target = CQ_B2;
		}
		ref = car_ref_get(pg);
		if (!ref) {
			found = 1;
			assert(pg->v_frame != NULL);
			frame_steal(mm, pg->v_frame);
		} else {
			car_ref_set(pg, 0);
			target = CQ_T2;
		}
		car_move(mm, pg, target, ref);
	} while (!found);

	assert(mm->m_car.q[CQ_T1].nr + mm->m_car.q[CQ_T2].nr + mm->m_nr_free ==
	       mm->m_nr_frames);
}

static void car_dir_replace(struct mm *mm)
{
	enum car_queue chop;

	assert(mm->m_car.q[CQ_T1].nr + mm->m_car.q[CQ_T2].nr + mm->m_nr_free ==
	       mm->m_nr_frames);

	if (mm->m_car.q[CQ_T1].nr + mm->m_car.q[CQ_B1].nr == mm->m_nr_frames)
		chop = CQ_B1;
	else if (mm->m_car.q[CQ_T1].nr + mm->m_car.q[CQ_T2].nr +
		 mm->m_car.q[CQ_B1].nr + mm->m_car.q[CQ_B2].nr ==
		 2 * mm->m_nr_frames)
		chop = CQ_B2;
	else
		return;

	car_move(mm, car_queue(mm, chop, 1), CQ_NONE, 0);

	assert(mm->m_car.q[CQ_T1].nr + mm->m_car.q[CQ_T2].nr + mm->m_nr_free ==
	       mm->m_nr_frames);
}

static void car_alloc(struct mm *mm, struct vpage *pg)
{
	enum car_queue q;
	enum car_queue target;
	int dirmiss;

	q = car_queue_get(pg);

	assert(equi(pg->v_frame != NULL, q == CQ_T1 || q == CQ_T2));
	assert(mm->m_car.q[CQ_T1].nr + mm->m_car.q[CQ_T2].nr + mm->m_nr_free ==
	       mm->m_nr_frames);

	dirmiss = q != CQ_B1 && q != CQ_B2;
	if (pg->v_frame != NULL)
		car_ref_set(pg, 1);
	else {
		struct frame *frame;

		assert(equi(dirmiss, q == CQ_NONE));

		if (mm->m_nr_free == 0) {

			car_replace(mm);
			/*
			 * Cache directory replacement.
			 */
			if (dirmiss)
				car_dir_replace(mm);
		}

		assert(mm->m_nr_free > 0);
		frame = frame_free_get(mm);
		vpage_place(mm, pg, frame);

		if (dirmiss) {
			target = CQ_T1;
		} else {
			u_int64_t delta;

			if (q == CQ_B1) {
				delta = max(1ULL, mm->m_car.q[CQ_B2].nr /
					    mm->m_car.q[CQ_B1].nr);
				mm->m_car.p = min(mm->m_car.p + delta,
						  mm->m_nr_frames);
			} else {
				assert(q == CQ_B2);

				delta = max(1ULL, mm->m_car.q[CQ_B1].nr /
					    mm->m_car.q[CQ_B2].nr);
				mm->m_car.p = min(mm->m_car.p - delta, 0ULL);
			}
			target = CQ_T2;
		}
		car_move(mm, pg, target, 1);
		car_ref_set(pg, 0);

	}
	assert(equi(pg->v_frame != NULL,
		    car_queue_get(pg) == CQ_T1 || car_queue_get(pg) == CQ_T2));
}

static void car_punch(struct mm *mm, struct vpage *pg)
{
	generic_punch(mm, pg);
	car_move(mm, pg, CQ_NONE, 0);
}

/*
 * ARC
 *
 * Shares a lot with CAR.
 *
 * From "ARC: A Self-Tuning, Low Overhead Replacement Cache" by Nimrod
 * Megiddo, Dharmendra Modha
 *
 * http://citeseer.ist.psu.edu/megiddo03arc.html
 */
static void arc_alloc(struct mm *mm, struct vpage *pg)
{
	enum car_queue q;
	enum car_queue target;
	u_int64_t      delta;

	q = car_queue_get(pg);

	assert(equi(pg->v_frame != NULL, q == CQ_T1 || q == CQ_T2));
	assert(mm->m_car.q[CQ_T1].nr + mm->m_car.q[CQ_T2].nr + mm->m_nr_free ==
	       mm->m_nr_frames);

	target = CQ_T2;
	if (pg->v_frame != NULL) {
		;
	} else if (q == CQ_B1) {
		delta = max(1ULL, mm->m_car.q[CQ_B2].nr/mm->m_car.q[CQ_B1].nr);
		mm->m_car.p = min(mm->m_car.p + delta, mm->m_nr_frames);
	} else if (q == CQ_B2) {
		delta = max(1ULL, mm->m_car.q[CQ_B1].nr/mm->m_car.q[CQ_B2].nr);
		mm->m_car.p = min(mm->m_car.p - delta, 0ULL);
	} else {
		struct vpage *tail;

		assert(q == CQ_NONE);

		if (mm->m_car.q[CQ_T1].nr + mm->m_car.q[CQ_B1].nr ==
		    mm->m_nr_frames) {
			if (mm->m_car.q[CQ_B1].nr > 0)
				tail = car_queue(mm, CQ_B1, 1);
			else {
				tail = car_queue(mm, CQ_T1, 1);
				frame_steal(mm, tail->v_frame);
			}
			car_move(mm, tail, CQ_NONE, 0);
		} else {
			u_int64_t total;

			total = mm->m_car.q[CQ_T1].nr + mm->m_car.q[CQ_B1].nr +
				mm->m_car.q[CQ_T2].nr + mm->m_car.q[CQ_B2].nr;
			if (total >= mm->m_nr_frames) {
				if (total == 2 * mm->m_nr_frames) {
					car_move(mm, car_queue(mm, CQ_B2, 1),
						 CQ_NONE, 0);
				}
			}
		}
		target = CQ_T1;
	}
	if (pg->v_frame == NULL) {
		if (mm->m_nr_free == 0) {
			u_int64_t t1;
			enum car_queue shrink;
			enum car_queue expand;
			struct vpage *shuttle;

			t1 = mm->m_car.q[CQ_T1].nr;
			if (t1 > 0 &&
			    (t1 > mm->m_car.p || (q == CQ_B2 &&
						  t1 == mm->m_car.p))) {
				shrink = CQ_T1;
				expand = CQ_B1;
			} else {
				shrink = CQ_T2;
				expand = CQ_B2;
			}
			shuttle = car_queue(mm, shrink, 1);
			frame_steal(mm, shuttle->v_frame);
			car_move(mm, shuttle, expand, 0);
		}
		vpage_place(mm, pg, frame_free_get(mm));
	}
	car_move(mm, pg, target, 0);
	assert(equi(pg->v_frame != NULL,
		    car_queue_get(pg) == CQ_T1 || car_queue_get(pg) == CQ_T2));
}

/*
 * LINUX
 *
 * This emulates replacement algorithm of Linux 2.6. kernel (mm/vmscan.c)
 *
 * Assumptions:
 *
 *  - no mapped or anonymous pages;
 *
 *  - no kswapd, only direct reclaim;
 *
 *  - all allocations are with GFP_FS;
 *
 *  - no non-fs caches;
 *
 *  - no low_page reserves;
 *
 *  - single zone.
 */

static void linux_add_to_active(struct mm *mm, struct frame *frame)
{
	list_add(&frame->f_linkage, &mm->m_linux.active);
	mm->m_linux.nr_active++;
}

static void linux_add_to_inactive(struct mm *mm, struct frame *frame)
{
	list_add(&frame->f_linkage, &mm->m_linux.inactive);
	mm->m_linux.nr_inactive++;
}

#if 0
static void linux_del_from_active(struct mm *mm, struct frame *frame)
{
	assert(mm->m_linux.nr_active > 0);
	list_del_init(&frame->f_linkage);
	mm->m_linux.nr_active--;
}
#endif

static void linux_del_from_inactive(struct mm *mm, struct frame *frame)
{
	assert(mm->m_linux.nr_inactive > 0);
	list_del_init(&frame->f_linkage);
	mm->m_linux.nr_inactive--;
}

static void linux_activate_page(struct mm *mm, struct frame *frame)
{
	if (frame->f_flags & FR_TAIL) {
		linux_del_from_inactive(mm, frame);
		frame->f_flags &= ~FR_TAIL;
		linux_add_to_active(mm, frame);
	}
}

/*
 * Mark a page as having seen activity.
 *
 * inactive,unreferenced	->	inactive,referenced
 * inactive,referenced		->	active,unreferenced
 * active,unreferenced		->	active,referenced
 */
static void linux_mark_page_accessed(struct mm *mm, struct frame *frame)
{
	if ((frame->f_flags & (FR_TAIL|FR_REF1)) == (FR_TAIL|FR_REF1)) {
		linux_activate_page(mm, frame);
		frame->f_flags &= ~FR_REF1;
	} else
		frame->f_flags |= FR_REF1;
}

enum {
	DEF_PRIORITY     = 12,
	SWAP_CLUSTER_MAX = 32
};

struct scan_control {
	struct mm    *mm;
	/* Incremented by the number of inactive pages that were scanned */
	unsigned long nr_scanned;

	int may_writepage;

	/* Can pages be swapped as part of reclaim? */
	int may_swap;

	/* This context's SWAP_CLUSTER_MAX. If freeing memory for
	 * suspend, we effectively ignore SWAP_CLUSTER_MAX.
	 * In this context, it doesn't matter that we scan the
	 * whole list at once. */
	int swap_cluster_max;

	int swappiness;
};

/* possible outcome of pageout() */
typedef enum {
	/* failed to write page out, page is locked */
	PAGE_KEEP,
	/* move page to the active list, page is locked */
	PAGE_ACTIVATE,
	/* page has been sent to the disk successfully, page is unlocked */
	PAGE_SUCCESS,
	/* page is clean and locked */
	PAGE_CLEAN,
} pageout_t;

static pageout_t linux_pageout(struct frame *frame)
{
	return PAGE_SUCCESS;
}

static u_int64_t linux_shrink_page_list(struct list_head *page_list,
					struct scan_control *sc)
{
	LIST_HEAD(ret_pages);
	unsigned long nr_reclaimed = 0;

	while (!list_empty(page_list)) {
		struct frame *frame;
		int referenced;

		frame = frame_from_list(page_list->prev);
		list_del_init(&frame->f_linkage);

		assert(frame->f_flags & FR_TAIL);

		sc->nr_scanned++;

		referenced = frame->f_flags & FR_REF1;
		frame->f_flags &= ~FR_REF1;

		if (frame->f_flags & FR_DIRTY) {
			if (referenced)
				goto keep_locked;
			if (!sc->may_writepage)
				goto keep_locked;

			/* Page is dirty, try to write it out here */
			switch(linux_pageout(frame)) {
			case PAGE_KEEP:
				goto keep_locked;
			case PAGE_ACTIVATE:
				goto activate_locked;
			case PAGE_SUCCESS:
			case PAGE_CLEAN:
				; /* try to free the page below */
			}
		}

		nr_reclaimed++;
		frame_steal(sc->mm, frame);
		continue;

activate_locked:
		frame->f_flags &= ~FR_TAIL;
keep_locked:
		list_add(&frame->f_linkage, &ret_pages);
	}
	list_splice(&ret_pages, page_list);
	return nr_reclaimed;
}

static u_int64_t linux_isolate_lru_pages(unsigned long nr_to_scan,
					 struct list_head *src,
					 struct list_head *dst,
					 unsigned long *scanned)
{
	struct frame *frame;
	unsigned long scan;

	for (scan = 0; scan < nr_to_scan && !list_empty(src); scan++) {
		frame = frame_from_list(src->prev);
		list_move(&frame->f_linkage, dst);
	}

	*scanned = scan;
	return scan;
}

static u_int64_t linux_shrink_inactive(unsigned long max_scan,
					    struct mm *mm,
					    struct scan_control *sc)
{
	LIST_HEAD(page_list);
	unsigned long nr_scanned = 0;
	unsigned long nr_reclaimed = 0;

	do {
		struct frame *frame;
		unsigned long nr_taken;
		unsigned long nr_scan;
		unsigned long nr_freed;

		nr_taken = linux_isolate_lru_pages(sc->swap_cluster_max,
						   &mm->m_linux.inactive,
						   &page_list, &nr_scan);
		mm->m_linux.nr_inactive -= nr_taken;
		mm->m_linux.pages_scanned += nr_scan;

		nr_scanned += nr_scan;
		nr_freed = linux_shrink_page_list(&page_list, sc);
		nr_reclaimed += nr_freed;

		if (nr_taken == 0)
			break;

		/*
		 * Put back any unfreeable pages.
		 */
		while (!list_empty(&page_list)) {
			frame = frame_from_list(page_list.prev);
			list_del_init(&frame->f_linkage);
			if (!(frame->f_flags & FR_TAIL))
				linux_add_to_active(mm, frame);
			else
				linux_add_to_inactive(mm, frame);
		}
  	} while (nr_scanned < max_scan);
	return nr_reclaimed;
}

static void linux_shrink_active(u_int64_t nr_pages, struct mm *mm,
				     struct scan_control *sc)
{
	unsigned long pgscanned;
	unsigned long pgmoved;
	LIST_HEAD(l_hold);	/* The pages which were snipped off */
	struct frame *frame;

	pgmoved = linux_isolate_lru_pages(nr_pages, &mm->m_linux.active,
					  &l_hold, &pgscanned);
	mm->m_linux.pages_scanned += pgscanned;
	assert(mm->m_linux.nr_active >= pgmoved);
	mm->m_linux.nr_active -= pgmoved;

	while (!list_empty(&l_hold)) {
		frame = frame_from_list(l_hold.prev);
		assert(!(frame->f_flags & FR_TAIL));
		frame->f_flags |= FR_TAIL;

		list_move(&frame->f_linkage, &mm->m_linux.inactive);
		mm->m_linux.nr_inactive++;
	}
}

static u_int64_t linux_shrink_zone(int prio, struct mm *mm,
				   struct scan_control *sc)
{
	unsigned long nr_active;
	unsigned long nr_inactive;
	unsigned long nr_to_scan;
	unsigned long nr_reclaimed = 0;

	/*
	 * Add one to `nr_to_scan' just to make sure that the kernel will
	 * slowly sift through the active list.
	 */
	mm->m_linux.nr_scan_active += (mm->m_linux.nr_active >> prio) + 1;
	nr_active = mm->m_linux.nr_scan_active;
	if (nr_active >= sc->swap_cluster_max)
		mm->m_linux.nr_scan_active = 0;
	else
		nr_active = 0;

	mm->m_linux.nr_scan_inactive += (mm->m_linux.nr_inactive >> prio) + 1;
	nr_inactive = mm->m_linux.nr_scan_inactive;
	if (nr_inactive >= sc->swap_cluster_max)
		mm->m_linux.nr_scan_inactive = 0;
	else
		nr_inactive = 0;

	while (nr_active || nr_inactive) {
		if (nr_active) {
			nr_to_scan = min(nr_active,
					(unsigned long)sc->swap_cluster_max);
			nr_active -= nr_to_scan;
			linux_shrink_active(nr_to_scan, mm, sc);
		}

		if (nr_inactive) {
			nr_to_scan = min(nr_inactive,
					(unsigned long)sc->swap_cluster_max);
			nr_inactive -= nr_to_scan;
			nr_reclaimed +=
				linux_shrink_inactive(nr_to_scan, mm, sc);
		}
	}
	return nr_reclaimed;
}

static u_int64_t linux_shrink_zones(int prio, struct mm *mm,
				    struct scan_control *sc)
{
	mm->m_linux.temp_priority = prio;
	if (mm->m_linux.prev_priority > prio)
		mm->m_linux.prev_priority = prio;

	return linux_shrink_zone(prio, mm, sc);
}

static void linux_try_to_free_pages(struct mm *mm)
{
	int priority;
	int total_scanned = 0;
	unsigned long nr_reclaimed = 0;
	unsigned long lru_pages;
	struct scan_control sc = {
		.mm = mm,
		.may_writepage = 1 /* !laptop_mode */,
		.swap_cluster_max = SWAP_CLUSTER_MAX,
	};

	mm->m_linux.temp_priority = DEF_PRIORITY;
	lru_pages = mm->m_linux.nr_active + mm->m_linux.nr_inactive;

	sc.may_writepage = 0;

	for (priority = DEF_PRIORITY; priority >= 0; priority--) {
		sc.nr_scanned = 0;

		nr_reclaimed += linux_shrink_zones(priority, mm, &sc);
		total_scanned += sc.nr_scanned;
		if (nr_reclaimed >= sc.swap_cluster_max)
			break;

		if (total_scanned > sc.swap_cluster_max +
		    sc.swap_cluster_max / 2)
			sc.may_writepage = 1;

	}
	mm->m_linux.prev_priority = mm->m_linux.temp_priority;
}

static void linux_alloc(struct mm *mm, struct vpage *pg)
{
	struct frame *frame;

	assert(vpage_invariant(mm, pg));

	if (pg->v_frame == NULL) {
		if (mm->m_nr_free == 0)
			/*
			 * Enter reclaim.
			 */
			linux_try_to_free_pages(mm);
		assert(mm->m_nr_free > 0);
		frame = frame_free_get(mm);
		vpage_place(mm, pg, frame);
		linux_add_to_inactive(mm, frame);
		frame->f_flags |= FR_TAIL;
	} else
		/*
		 * mark_page_accessed().
		 */
		linux_mark_page_accessed(mm, pg->v_frame);
	assert(vpage_invariant(mm, pg));
}

static void linux_punch(struct mm *mm, struct vpage *pg)
{
	struct frame *frame;

	frame = pg->v_frame;
	if (frame != NULL) {
		if (frame->f_flags & FR_TAIL) {
			frame->f_flags &= ~FR_TAIL;
			assert(mm->m_linux.nr_inactive > 0);
			mm->m_linux.nr_inactive--;
		} else {
			assert(mm->m_linux.nr_active > 0);
			mm->m_linux.nr_active--;
		}
	}
	generic_punch(mm, pg);
}

/*
 * WORST
 *
 * Worst possible algorithm. Causes as many faults as possible.
 */
static void worst_alloc(struct mm *mm, struct vpage *pg)
{
	struct frame *frame;

	assert(vpage_invariant(mm, pg));

	if (pg->v_frame == NULL) {
		if (mm->m_nr_free == 0) {
			struct vpage  *nextfault;
			struct access *peek;
			int result;

			peek = NULL;
			result = access_look_ahead(mm, &peek);
			if (result != 0)
				return random_alloc(mm, pg);
			assert(peek != NULL);
			nextfault = &mm->m_vpages[peek->a_page];
			if (nextfault->v_frame != NULL)
				/*
				 * Next access is to page already in memory:
				 * replace it so that next access faults.
				 */
				frame_steal(mm, nextfault->v_frame);
			else
				/*
				 * Next access will fault anyway.
				 */
				return random_alloc(mm, pg);
		}
		assert(mm->m_nr_free > 0);
		frame = frame_free_get(mm);
		vpage_place(mm, pg, frame);
		assert(list_empty(&frame->f_linkage));
	}
	assert(vpage_invariant(mm, pg));
	assert(pg->v_frame != NULL);
}

/*
 * OPT
 *
 * Optimal clairvoyant algorithm by Belady.
 *
 * Replaces pages that won't be faulted for the longest time.
 */

struct opt_access {
	u_int64_t        oa_turn;
	struct list_head oa_linkage;
};

static struct opt_access *opt_from_list(struct list_head *head)
{
	return container_of(head, struct opt_access, oa_linkage);
}

static int opt_build(struct mm *mm)
{
	int result;
	u_int64_t      epoch;
	struct vpage  *scan;
	struct access *peek;

	peek = NULL;

	for (epoch = 1, result = 0; result == 0; epoch++) {
		result = access_look_ahead(mm, &peek);
		if (result == 0) {
			/*
			 * WRITE and PUNCH do not cause fault, skip them.
			 */
			if (peek->a_type != FSLOG_WRITE &&
			    peek->a_type != FSLOG_PUNCH) {
				struct opt_access *oa;

				oa = malloc(sizeof *oa);
				if (oa != NULL) {
					scan = &mm->m_vpages[peek->a_page];
					oa->oa_turn = epoch;
					list_add_tail(&oa->oa_linkage,
						      &scan->v_stuff);
				} else
					result = ENOMEM;
			}
		} else {
			result = 0;
			break;
		}
	}
	if (verbose & VERBOSE_TABLE) {
		vpage_no_t vno;

		for (vno = 0; vno < mm->m_nr_vpages; ++vno) {
			struct opt_access *oa;

			if (!list_empty(&mm->m_vpages[vno].v_stuff)) {
				printf("%llx: ", vno);
				list_for_each_entry(oa,
						    &mm->m_vpages[vno].v_stuff,
						    oa_linkage)
					printf("%llx ", oa->oa_turn);
				printf("\n");
			}
		}
	}
	return result;
}

static void opt_alloc(struct mm *mm, struct vpage *pg)
{
	struct frame *frame;
	struct frame *scan;

	assert(vpage_invariant(mm, pg));

	if (pg->v_frame == NULL) {
		if (mm->m_nr_free == 0) {
			frame_no_t fno;
			u_int64_t  next_max;

			next_max = 0;
			frame = NULL;
			for (fno = 0, scan = mm->m_frames;
			     fno < mm->m_nr_frames; fno++, scan++) {
				struct vpage *page;
				u_int64_t     next;

				page = scan->f_page;
				assert(page != NULL);
				assert(scan == page->v_frame);
				if (!list_empty(&page->v_stuff)) {
					struct opt_access *oa;

					oa = opt_from_list(page->v_stuff.next);
					next = oa->oa_turn;
					assert(next > mm->m_total);
					if (next > next_max) {
						next_max = next;
						frame    = scan;
					}
				} else {
					frame = scan;
					break;
				}

			}
			if (verbose & VERBOSE_TABLE) {
				printf("%8llx: ", mm->m_total);
				for (fno = 0, scan = mm->m_frames;
				     fno < mm->m_nr_frames; fno++, scan++) {
					struct vpage *page;

					page = scan->f_page;
					if (!list_empty(&page->v_stuff)) {
						struct opt_access *oa;

						oa = opt_from_list(page->v_stuff.next);
						printf("%8llx", oa->oa_turn);
					} else
						printf("   never");
					printf(scan == frame ? "*" : " ");
				}
				printf("\n");
			}
			assert(frame != NULL);
			frame_steal(mm, frame);
		}
		assert(mm->m_nr_free > 0);
		frame = frame_free_get(mm);
		vpage_place(mm, pg, frame);
		assert(list_empty(&frame->f_linkage));
	}
	assert(vpage_invariant(mm, pg));
	assert(pg->v_frame != NULL);
}

static int opt_init(struct mm *mm)
{
	return opt_build(mm);
}

static void opt_fini(struct mm *mm)
{
	/*
	 * XXX add cleanup.
	 */
}

static void opt_read(struct mm *mm, struct vpage *pg)
{
	struct opt_access *oa;

	if (!list_empty(&pg->v_stuff)) {
		oa = opt_from_list(pg->v_stuff.next);
		assert(oa->oa_turn == mm->m_total);
		list_del_init(&oa->oa_linkage);
		free(oa);
	}

	mm->m_alg->r_alloc(mm, pg);
	vpage_pagein(mm, pg);
}

/*
 * TODO:
 *
 * LRFU: http://citeseer.ist.psu.edu/lee97implementation.html
 */

struct repalg algs[] = {
	{
		.r_name = "random",
		.r_init = random_init,
		.r_fini = generic_fini,

		.r_read  = generic_read,
		.r_ra    = generic_read,
		.r_write = generic_write,
		.r_fault = generic_read,
		.r_punch = generic_punch,
		.r_alloc = random_alloc
	},
	{
		.r_name = "lru",
		.r_init = generic_init,
		.r_fini = generic_fini,

		.r_read  = generic_read,
		.r_ra    = generic_read,
		.r_write = generic_write,
		.r_fault = generic_read,
		.r_punch = generic_punch,
		.r_alloc = lru_alloc
	},
	{
		.r_name = "fifo",
		.r_init = generic_init,
		.r_fini = generic_fini,

		.r_read  = generic_read,
		.r_ra    = generic_read,
		.r_write = generic_write,
		.r_fault = generic_read,
		.r_punch = generic_punch,
		.r_alloc = fifo_alloc
	},
	{
		.r_name = "fifo2",
		.r_init = generic_init,
		.r_fini = generic_fini,

		.r_read  = generic_read,
		.r_ra    = generic_read,
		.r_write = generic_write,
		.r_fault = generic_read,
		.r_punch = generic_punch,
		.r_alloc = fifo2_alloc
	},
	{
		.r_name = "sfifo",
		.r_init = generic_init,
		.r_fini = generic_fini,

		.r_read  = generic_read,
		.r_ra    = generic_read,
		.r_write = generic_write,
		.r_fault = generic_read,
		.r_punch = generic_punch,
		.r_alloc = sfifo_alloc
	},
	{
		.r_name = "2q",
		.r_init = generic_init,
		.r_fini = generic_fini,

		.r_read  = generic_read,
		.r_ra    = generic_read,
		.r_write = generic_write,
		.r_fault = generic_read,
		.r_punch = generic_punch,
		.r_alloc = q2_alloc
	},
	{
		.r_name = "car",
		.r_init = car_init,
		.r_fini = car_fini,

		.r_read  = generic_read,
		.r_ra    = generic_read,
		.r_write = generic_write,
		.r_fault = generic_read,
		.r_punch = car_punch,
		.r_alloc = car_alloc
	},
	{
		.r_name = "arc",
		.r_init = car_init,
		.r_fini = car_fini,

		.r_read  = generic_read,
		.r_ra    = generic_read,
		.r_write = generic_write,
		.r_fault = generic_read,
		.r_punch = car_punch,
		.r_alloc = arc_alloc
	},
	{
		.r_name = "linux",
		.r_init = generic_init,
		.r_fini = generic_fini,

		.r_read  = generic_read,
		.r_ra    = generic_read,
		.r_write = generic_write,
		.r_fault = generic_read,
		.r_punch = linux_punch,
		.r_alloc = linux_alloc
	},
	{
		.r_name = "worst",
		.r_init = generic_init,
		.r_fini = generic_fini,

		.r_read  = generic_read,
		.r_ra    = generic_read,
		.r_write = generic_write,
		.r_fault = generic_read,
		.r_punch = generic_punch,
		.r_alloc = worst_alloc
	},
	{
		.r_name = "opt",
		.r_init = opt_init,
		.r_fini = opt_fini,

		.r_read  = opt_read,
		.r_ra    = opt_read,
		.r_write = generic_write,
		.r_fault = opt_read,
		.r_punch = generic_punch,
		.r_alloc = opt_alloc
	},
	{
		.r_name = NULL
	}
};

static void mm_fini(struct mm *mm)
{
	frame_no_t fno;
	vpage_no_t vno;
	inode_no_t ino;

	mm->m_alg->r_fini(mm);

	if (mm->m_objects != NULL) {
		for (ino = 0; ino < mm->m_nr_objects; ++ino)
			object_fini(mm, &mm->m_objects[ino]);
		free(mm->m_objects);
		mm->m_objects = NULL;
	}

	if (mm->m_frames != NULL) {
		for (fno = 0; fno < mm->m_nr_frames; ++fno)
			frame_fini(mm, &mm->m_frames[fno]);
		free(mm->m_frames);
		mm->m_frames = NULL;
	}
	if (mm->m_vpages != NULL) {
		for (vno = 0; vno < mm->m_nr_vpages; ++vno)
			vpage_fini(mm, &mm->m_vpages[vno]);
		free(mm->m_vpages);
		mm->m_vpages = NULL;
	}
}

static int mm_init(struct mm *mm, struct repalg *alg)
{
	int result;

	mm->m_alg = alg;
	mm->m_nr_free = mm->m_nr_frames;
	INIT_LIST_HEAD(&mm->m_freelist);
	INIT_LIST_HEAD(&mm->m_lru);
	INIT_LIST_HEAD(&mm->m_fifo);
	INIT_LIST_HEAD(&mm->m_fifo2);

	INIT_LIST_HEAD(&mm->m_future);

	INIT_LIST_HEAD(&mm->m_q2.am);
	INIT_LIST_HEAD(&mm->m_q2.a1in);
	INIT_LIST_HEAD(&mm->m_q2.a1out);

	INIT_LIST_HEAD(&mm->m_linux.active);
	INIT_LIST_HEAD(&mm->m_linux.inactive);

	mm->m_frames = calloc(mm->m_nr_frames, sizeof(struct frame));
	mm->m_vpages = calloc(mm->m_nr_vpages, sizeof(struct vpage));
	mm->m_objects = calloc(mm->m_nr_objects, sizeof(struct object));

	if (mm->m_frames != NULL &&
	    mm->m_vpages != NULL && mm->m_objects != NULL) {
		frame_no_t fno;
		vpage_no_t vno;
		inode_no_t ino;

		for (fno = 0; fno < mm->m_nr_frames; ++fno) {
			mm->m_frames[fno].f_no = fno;
			frame_init(mm, &mm->m_frames[fno]);
		}
		for (vno = 0; vno < mm->m_nr_vpages; ++vno) {
			mm->m_vpages[vno].v_no = vno;
			vpage_init(mm, &mm->m_vpages[vno]);
		}
		for (ino = 0; ino < mm->m_nr_objects; ++ino) {
			mm->m_objects[ino].o_no = ino;
			object_init(mm, &mm->m_objects[ino]);
		}
		result = alg->r_init(mm);
	} else
		result = ENOMEM;
	if (result != 0)
		mm_fini(mm);
	return result;
}

static void vpage_print(const char *prefix, const struct vpage *pg)
{
	struct frame *frame;

	frame = pg->v_frame;
	printf("%s%8.8llx %8.8x ", prefix, pg->v_no, pg->v_flags);
	if (frame != NULL)
		printf("[%16.16llx %8.8x]\n", frame->f_no, frame->f_flags);
	else
		printf("NR\n");
}

static void usage(void)
{
	struct repalg *alg;

	printf("replacement [ -v <logging flags> | -h | -V <virtual pages> | "
	       "-M <frames> | -f <files> | -r <radix> | -a <algorithm> ]\n\n"
	       "Available algorithms:\n\n");
	for (alg = &algs[0]; alg->r_name != NULL; alg++)
		printf("\t%s\n", alg->r_name);
}

int main(int argc, char **argv)
{
	int result;
	int radix;
	int opt;
	struct repalg *alg;
	struct mm      mm = {0,};
	struct access  access;
	char          *eoc;

	setbuf(stdout, NULL);

	verbose = 0;
	radix   = 0;
	alg     = &algs[0];
	do {
		opt = getopt(argc, argv, "V:v:a:r:M:hf:t:k:K:");
		switch (opt) {
		case -1:
			break;
		case '?':
		default:
			fprintf(stderr, "Unable to parse options.\n");
		case 'h':
			usage();
			return 0;
		case 'v':
			verbose = atoi(optarg);
			break;
		case 'M':
			mm.m_nr_frames = strtoull(optarg, &eoc, radix);
			if (*eoc != 0) {
				fprintf(stderr,
					"Malformed nr_frames: `%s'\n", optarg);
				return 1;
			}
			break;
		case 'V':
			mm.m_nr_vpages = strtoull(optarg, &eoc, radix);
			if (*eoc != 0) {
				fprintf(stderr,
					"Malformed nr_vpages: `%s'\n", optarg);
				return 1;
			}
			break;
		case 'f':
			mm.m_nr_objects = strtoull(optarg, &eoc, radix);
			if (*eoc != 0) {
				fprintf(stderr,
					"Malformed nr_files: `%s'\n", optarg);
				return 1;
			}
			break;
		case 'r':
			radix = atoi(optarg);
			break;
		case 't':
			mm.m_sfifo.tail = strtoull(optarg, &eoc, radix);
			if (*eoc != 0) {
				fprintf(stderr,
					"mm.m_sfifo.tail: `%s'\n", optarg);
				return 1;
			}
			break;
		case 'k':
			mm.m_q2.kin = strtoull(optarg, &eoc, radix);
			if (*eoc != 0) {
				fprintf(stderr,
					"mm.m_q2.kin: `%s'\n", optarg);
				return 1;
			}
			break;
		case 'K':
			mm.m_q2.kout = strtoull(optarg, &eoc, radix);
			if (*eoc != 0) {
				fprintf(stderr,
					"mm.m_q2.kout: `%s'\n", optarg);
				return 1;
			}
			break;
		case 'a':
			for (alg = &algs[0]; alg->r_name != NULL; alg++) {
				if (!strcmp(alg->r_name, optarg))
					break;
			}
			if (alg->r_name == NULL) {
				fprintf(stderr,
					"Unknown algorithm `%s'\n", optarg);
				return 1;
			}
			break;
		}
	} while (opt != -1);

	result = mm_init(&mm, alg);
	if (result != 0)
		return result;

	while (access_get(&mm, &access) == 0) {
		vpage_no_t     vpage;
		inode_no_t     ino;
		pgoff_t        index;
		struct vpage  *pg;
		struct object *object;
		char           type;
		char           prefix[] = "? ";

		vpage = access.a_page;
		ino   = access.a_object;
		index = access.a_index;
		type  = access.a_type;

		if (vpage >= mm.m_nr_vpages) {
			fprintf(stderr, "Invalid page nr.: %llu >= %llu\n",
				vpage, mm.m_nr_vpages);
			return 1;
		}
		if (ino >= mm.m_nr_objects) {
			fprintf(stderr, "Invalid ino: %llu >= %llu\n",
				ino, mm.m_nr_objects);
			return 1;
		}
		pg = &mm.m_vpages[vpage];
		object = &mm.m_objects[ino];
		if (!(pg->v_flags & VP_SEEN)) {
			/*
			 * First time this page is seen.
			 */
			assert(pg->v_object == NULL);
			pg->v_object = object;
			list_add(&pg->v_pages, &object->o_pages);
			pg->v_index = index;
		}
		pg->v_flags |= VP_SEEN;
		if (pg->v_object->o_no != ino) {
			fprintf(stderr, "Invalid ino: %llx != %llx\n",
				pg->v_object->o_no, ino);
			return 1;
		}
		if (pg->v_index != index) {
			fprintf(stderr, "Invalid index: %llx != %llx\n",
				pg->v_index, index);
			return 1;
		}

		prefix[0] = type;
		if (verbose & VERBOSE_LOG)
			vpage_print(prefix, pg);
		if (type != FSLOG_WRITE && type != FSLOG_PUNCH) {
			if (pg->v_frame != NULL)
				mm.m_hits++;
			else
				mm.m_misses++;
		}
		mm.m_total++;
		switch (type) {
		case FSLOG_READ:
			mm.m_alg->r_read(&mm, pg);
			break;
		case FSLOG_RA:
			mm.m_alg->r_ra(&mm, pg);
			break;
		case FSLOG_WRITE:
			mm.m_alg->r_write(&mm, pg);
			pg->v_frame->f_flags |= FR_DIRTY;
			break;
		case FSLOG_PFAULT:
			mm.m_alg->r_fault(&mm, pg);
			break;
		case FSLOG_PUNCH: {
			struct vpage *scan;

			list_for_each_entry(scan, &object->o_pages, v_pages) {
				if (scan->v_index >= index)
					mm.m_alg->r_punch(&mm, pg);
			}
			continue;
		}
		default:
			fprintf(stderr, "Invalid access type `%c'", type);
			return 1;
		}
		if (pg->v_frame == NULL) {
			fprintf(stderr, "Frame wasn't installed\n");
			return 1;
		}
		pg->v_frame->f_flags |= FR_REF;
		if ((verbose & VERBOSE_PROGRESS) && mm.m_total % 1000 == 0)
			printf(".");
	}
	printf("%12llu %12llu %f\n", mm.m_hits, mm.m_misses,
	       mm.m_hits*100.0/(mm.m_hits + mm.m_misses));
	mm_fini(&mm);
	return result;
}

/*
 * Author: Nikita Danilov <Danilov@Gmail.COM>
 * Keywords: VM page replacement simulation tracing
 *
 * Copyright (C) 2006 Nikita Danilov <Danilov@Gmail.COM>
 *
 * This file is a part of itself.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */
