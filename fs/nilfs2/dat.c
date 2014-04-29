/*
 * dat.c - NILFS disk address translation.
 *
 * Copyright (C) 2006-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Koji Sato <koji@osrg.net>.
 */

#include <linux/types.h>
#include <linux/buffer_head.h>
#include <linux/string.h>
#include <linux/errno.h>
#include "nilfs.h"
#include "mdt.h"
#include "alloc.h"
#include "dat.h"
#include "sufile.h"


#define NILFS_CNO_MIN	((__u64)1)
#define NILFS_CNO_MAX	(~(__u64)0)

/*
 * special snapshot value used as a flag to indicate
 * that the segment usage information to which this
 * entry belongs was incremented
 */
#define NILFS_ENTRY_INC		((__u64)0)
/*
 * special snapshot value used as a flag to indicate
 * that the segment usage information to which this
 * entry belongs was decremented
 */
#define NILFS_ENTRY_DEC		(NILFS_CNO_MAX)

/**
 * nilfs_dat_entry_belongs_to_cp - check if @entry belongs to @cno
 * @entry: DAT-Entry
 * @cno: checkpoint number
 *
 * Description: Returns 1 if @entry belongs to @cno. An entry belongs to a
 * checkpoint if the value of the checkpoint number falls into the range of
 * de_start inclusive to de_end exclusive.
 */
static inline int nilfs_dat_entry_belongs_to_cp(struct nilfs_dat_entry *entry,
						__u64 cno)
{
	return cno >= le64_to_cpu(entry->de_start) &&
			cno < le64_to_cpu(entry->de_end);
}

/**
 * nilfs_dat_entry_is_alive - check if @entry is alive
 * @entry: DAT-Entry
 *
 * Description: Simple check if @entry is alive in the current checkpoint.
 */
static inline int nilfs_dat_entry_is_live(struct nilfs_dat_entry *entry)
{
	return entry->de_end == cpu_to_le64(NILFS_CNO_MAX);
}

/**
 * nilfs_dat_entry_is_inc - check if @entry has been incremented
 * @entry: DAT-Entry
 *
 * Description: Simple check if the segment usage information of the
 * segment to which de_block belongs has been incremented by one. The special
 * snapshot value NILFS_ENTRY_INC is used as a flag.
 */
static inline int nilfs_dat_entry_is_inc(struct nilfs_dat_entry *entry)
{
	return entry->de_ss == cpu_to_le64(NILFS_ENTRY_INC);
}

/**
 * nilfs_dat_entry_is_dec - check if @entry has an unknown snapshot
 * @entry: DAT-Entry
 *
 * Description: Simple check if the segment usage information of the
 * segment to which de_block belongs has been decremented by one. The special
 * snapshot value NILFS_ENTRY_DEC is used as a flag.
 */
static inline int nilfs_dat_entry_is_dec(struct nilfs_dat_entry *entry)
{
	return entry->de_ss == cpu_to_le64(NILFS_ENTRY_DEC);
}

/**
 * nilfs_dat_entry_has_ss - check if @entry has some concrete snapshot
 * @entry: DAT-Entry
 *
 * Description: Returns true if the value in de_ss is a real snapshot value
 * and none of the special flags NILFS_ENTRY_INC and NILFS_ENTRY_DEC.
 */
static inline int nilfs_dat_entry_has_ss(struct nilfs_dat_entry *entry)
{
	return !nilfs_dat_entry_is_inc(entry) &&
	       !nilfs_dat_entry_is_dec(entry);
}

/**
 * struct nilfs_dat_info - on-memory private data of DAT file
 * @mi: on-memory private data of metadata file
 * @palloc_cache: persistent object allocator cache of DAT file
 * @shadow: shadow map of DAT file
 */
struct nilfs_dat_info {
	struct nilfs_mdt_info mi;
	struct nilfs_palloc_cache palloc_cache;
	struct nilfs_shadow_map shadow;
};

static inline struct nilfs_dat_info *NILFS_DAT_I(struct inode *dat)
{
	return (struct nilfs_dat_info *)NILFS_MDT(dat);
}

static int nilfs_dat_prepare_entry(struct inode *dat,
				   struct nilfs_palloc_req *req, int create)
{
	return nilfs_palloc_get_entry_block(dat, req->pr_entry_nr,
					    create, &req->pr_entry_bh);
}

static void nilfs_dat_commit_entry(struct inode *dat,
				   struct nilfs_palloc_req *req)
{
	mark_buffer_dirty(req->pr_entry_bh);
	nilfs_mdt_mark_dirty(dat);
	brelse(req->pr_entry_bh);
}

static void nilfs_dat_abort_entry(struct inode *dat,
				  struct nilfs_palloc_req *req)
{
	brelse(req->pr_entry_bh);
}

int nilfs_dat_prepare_alloc(struct inode *dat, struct nilfs_palloc_req *req)
{
	int ret;

	ret = nilfs_palloc_prepare_alloc_entry(dat, req);
	if (ret < 0)
		return ret;

	ret = nilfs_dat_prepare_entry(dat, req, 1);
	if (ret < 0)
		nilfs_palloc_abort_alloc_entry(dat, req);

	return ret;
}

void nilfs_dat_commit_alloc(struct inode *dat, struct nilfs_palloc_req *req)
{
	struct nilfs_dat_entry *entry;
	void *kaddr;

	kaddr = kmap_atomic(req->pr_entry_bh->b_page);
	entry = nilfs_palloc_block_get_entry(dat, req->pr_entry_nr,
					     req->pr_entry_bh, kaddr);
	entry->de_start = cpu_to_le64(NILFS_CNO_MIN);
	entry->de_end = cpu_to_le64(NILFS_CNO_MAX);
	entry->de_blocknr = cpu_to_le64(0);
	entry->de_ss = cpu_to_le64(NILFS_ENTRY_INC);
	kunmap_atomic(kaddr);

	nilfs_palloc_commit_alloc_entry(dat, req);
	nilfs_dat_commit_entry(dat, req);
}

void nilfs_dat_abort_alloc(struct inode *dat, struct nilfs_palloc_req *req)
{
	nilfs_dat_abort_entry(dat, req);
	nilfs_palloc_abort_alloc_entry(dat, req);
}

static void nilfs_dat_commit_free(struct inode *dat,
				  struct nilfs_palloc_req *req)
{
	struct nilfs_dat_entry *entry;
	void *kaddr;

	kaddr = kmap_atomic(req->pr_entry_bh->b_page);
	entry = nilfs_palloc_block_get_entry(dat, req->pr_entry_nr,
					     req->pr_entry_bh, kaddr);
	entry->de_start = cpu_to_le64(NILFS_CNO_MIN);
	entry->de_end = cpu_to_le64(NILFS_CNO_MIN);
	entry->de_blocknr = cpu_to_le64(0);
	entry->de_ss = cpu_to_le64(NILFS_ENTRY_INC);
	kunmap_atomic(kaddr);

	nilfs_dat_commit_entry(dat, req);
	nilfs_palloc_commit_free_entry(dat, req);
}

int nilfs_dat_prepare_start(struct inode *dat, struct nilfs_palloc_req *req)
{
	int ret;

	ret = nilfs_dat_prepare_entry(dat, req, 0);
	WARN_ON(ret == -ENOENT);
	return ret;
}

void nilfs_dat_commit_start(struct inode *dat, struct nilfs_palloc_req *req,
			    sector_t blocknr)
{
	struct nilfs_dat_entry *entry;
	void *kaddr;

	kaddr = kmap_atomic(req->pr_entry_bh->b_page);
	entry = nilfs_palloc_block_get_entry(dat, req->pr_entry_nr,
					     req->pr_entry_bh, kaddr);
	entry->de_start = cpu_to_le64(nilfs_mdt_cno(dat));
	entry->de_blocknr = cpu_to_le64(blocknr);
	kunmap_atomic(kaddr);

	nilfs_dat_commit_entry(dat, req);
}

int nilfs_dat_prepare_end(struct inode *dat, struct nilfs_palloc_req *req)
{
	struct nilfs_dat_entry *entry;
	__u64 start;
	sector_t blocknr;
	void *kaddr;
	int ret;

	ret = nilfs_dat_prepare_entry(dat, req, 0);
	if (ret < 0) {
		WARN_ON(ret == -ENOENT);
		return ret;
	}

	kaddr = kmap_atomic(req->pr_entry_bh->b_page);
	entry = nilfs_palloc_block_get_entry(dat, req->pr_entry_nr,
					     req->pr_entry_bh, kaddr);
	start = le64_to_cpu(entry->de_start);
	blocknr = le64_to_cpu(entry->de_blocknr);
	kunmap_atomic(kaddr);

	if (blocknr == 0) {
		ret = nilfs_palloc_prepare_free_entry(dat, req);
		if (ret < 0) {
			nilfs_dat_abort_entry(dat, req);
			return ret;
		}
	}

	return 0;
}

void nilfs_dat_commit_end(struct inode *dat, struct nilfs_palloc_req *req,
			  struct nilfs_sufile_accu_state *state,
			  int dead, int count_blocks)
{
	struct nilfs_dat_entry *entry;
	__u64 start, end;
	sector_t blocknr;
	void *kaddr;
	struct the_nilfs *nilfs = dat->i_sb->s_fs_info;
	int decremented;

	kaddr = kmap_atomic(req->pr_entry_bh->b_page);
	entry = nilfs_palloc_block_get_entry(dat, req->pr_entry_nr,
					     req->pr_entry_bh, kaddr);
	decremented = nilfs_dat_entry_is_dec(entry) &&
			le64_to_cpu(entry->de_end) == nilfs_mdt_cno(dat);
	end = start = le64_to_cpu(entry->de_start);
	if (!dead) {
		end = nilfs_mdt_cno(dat);
		WARN_ON(start > end);
	}
	entry->de_end = cpu_to_le64(end);
	entry->de_ss = cpu_to_le64(NILFS_ENTRY_DEC);
	blocknr = le64_to_cpu(entry->de_blocknr);
	kunmap_atomic(kaddr);

	if (blocknr == 0)
		nilfs_dat_commit_free(dat, req);
	else {
		nilfs_dat_commit_entry(dat, req);

		if (!decremented && count_blocks &&
		    nilfs_feature_track_live_blks(nilfs)) {
			nilfs_sufile_accu_nlive_blks(nilfs, state, blocknr, -1);
		}
	}
}

void nilfs_dat_abort_end(struct inode *dat, struct nilfs_palloc_req *req)
{
	struct nilfs_dat_entry *entry;
	__u64 start;
	sector_t blocknr;
	void *kaddr;

	kaddr = kmap_atomic(req->pr_entry_bh->b_page);
	entry = nilfs_palloc_block_get_entry(dat, req->pr_entry_nr,
					     req->pr_entry_bh, kaddr);
	start = le64_to_cpu(entry->de_start);
	blocknr = le64_to_cpu(entry->de_blocknr);
	kunmap_atomic(kaddr);

	if (start == nilfs_mdt_cno(dat) && blocknr == 0)
		nilfs_palloc_abort_free_entry(dat, req);
	nilfs_dat_abort_entry(dat, req);
}

int nilfs_dat_prepare_update(struct inode *dat,
			     struct nilfs_palloc_req *oldreq,
			     struct nilfs_palloc_req *newreq)
{
	int ret;

	ret = nilfs_dat_prepare_end(dat, oldreq);
	if (!ret) {
		ret = nilfs_dat_prepare_alloc(dat, newreq);
		if (ret < 0)
			nilfs_dat_abort_end(dat, oldreq);
	}
	return ret;
}

void nilfs_dat_commit_update(struct inode *dat,
			     struct nilfs_palloc_req *oldreq,
			     struct nilfs_palloc_req *newreq,
			     int dead, int count_blocks)
{
	nilfs_dat_commit_end(dat, oldreq, NULL, dead, count_blocks);
	nilfs_dat_commit_alloc(dat, newreq);
}

void nilfs_dat_abort_update(struct inode *dat,
			    struct nilfs_palloc_req *oldreq,
			    struct nilfs_palloc_req *newreq)
{
	nilfs_dat_abort_end(dat, oldreq);
	nilfs_dat_abort_alloc(dat, newreq);
}

/**
 * nilfs_dat_mark_dirty -
 * @dat: DAT file inode
 * @vblocknr: virtual block number
 *
 * Description:
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 */
int nilfs_dat_mark_dirty(struct inode *dat, __u64 vblocknr)
{
	struct nilfs_palloc_req req;
	int ret;

	req.pr_entry_nr = vblocknr;
	ret = nilfs_dat_prepare_entry(dat, &req, 0);
	if (ret == 0)
		nilfs_dat_commit_entry(dat, &req);
	return ret;
}

/**
 * nilfs_dat_freev - free virtual block numbers
 * @dat: DAT file inode
 * @vblocknrs: array of virtual block numbers
 * @nitems: number of virtual block numbers
 *
 * Description: nilfs_dat_freev() frees the virtual block numbers specified by
 * @vblocknrs and @nitems.
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-ENOENT - The virtual block number have not been allocated.
 */
int nilfs_dat_freev(struct inode *dat, __u64 *vblocknrs, size_t nitems)
{
	return nilfs_palloc_freev(dat, vblocknrs, nitems);
}

/**
 * nilfs_dat_move - change a block number
 * @dat: DAT file inode
 * @vblocknr: virtual block number
 * @blocknr: block number
 *
 * Description: nilfs_dat_move() changes the block number associated with
 * @vblocknr to @blocknr.
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 */
int nilfs_dat_move(struct inode *dat, __u64 vblocknr, sector_t blocknr,
		   int is_snapshot)
{
	struct buffer_head *entry_bh;
	struct nilfs_dat_entry *entry;
	void *kaddr;
	int ret;

	ret = nilfs_palloc_get_entry_block(dat, vblocknr, 0, &entry_bh);
	if (ret < 0)
		return ret;

	/*
	 * The given disk block number (blocknr) is not yet written to
	 * the device at this point.
	 *
	 * To prevent nilfs_dat_translate() from returning the
	 * uncommitted block number, this makes a copy of the entry
	 * buffer and redirects nilfs_dat_translate() to the copy.
	 */
	if (!buffer_nilfs_redirected(entry_bh)) {
		ret = nilfs_mdt_freeze_buffer(dat, entry_bh);
		if (ret) {
			brelse(entry_bh);
			return ret;
		}
	}

	kaddr = kmap_atomic(entry_bh->b_page);
	entry = nilfs_palloc_block_get_entry(dat, vblocknr, entry_bh, kaddr);
	if (unlikely(entry->de_blocknr == cpu_to_le64(0))) {
		printk(KERN_CRIT "%s: vbn = %llu, [%llu, %llu)\n", __func__,
		       (unsigned long long)vblocknr,
		       (unsigned long long)le64_to_cpu(entry->de_start),
		       (unsigned long long)le64_to_cpu(entry->de_end));
		kunmap_atomic(kaddr);
		brelse(entry_bh);
		return -EINVAL;
	}
	WARN_ON(blocknr == 0);
	entry->de_blocknr = cpu_to_le64(blocknr);
	if (is_snapshot && nilfs_dat_entry_is_dec(entry))
		entry->de_ss = cpu_to_le64(NILFS_ENTRY_INC);
	kunmap_atomic(kaddr);

	mark_buffer_dirty(entry_bh);
	nilfs_mdt_mark_dirty(dat);

	brelse(entry_bh);

	return 0;
}

/**
 * nilfs_dat_set_inc - set flag to indicate that @vblocknr was incremented
 * @dat: DAT file inode
 * @vblocknr: virtual block number
 *
 * Description: nilfs_dat_set_inc() sets the flag NILFS_ENTRY_INC, if
 * necessary, to indicate that the segment usage information of the segment
 * to which the DAT-Entry of @vblocknr belongs was incremented. This flag is
 * important to assure accurate counting of live blocks.
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 */
int nilfs_dat_set_inc(struct inode *dat, __u64 vblocknr)
{
	struct buffer_head *entry_bh;
	struct nilfs_dat_entry *entry;
	void *kaddr;
	int ret;

	ret = nilfs_palloc_get_entry_block(dat, vblocknr, 0, &entry_bh);
	if (ret < 0)
		return ret;

	kaddr = kmap_atomic(entry_bh->b_page);
	entry = nilfs_palloc_block_get_entry(dat, vblocknr, entry_bh, kaddr);
	if (nilfs_dat_entry_is_dec(entry)) {
		entry->de_ss = cpu_to_le64(NILFS_ENTRY_INC);
		kunmap_atomic(kaddr);
		mark_buffer_dirty(entry_bh);
		nilfs_mdt_mark_dirty(dat);
	} else
		kunmap_atomic(kaddr);

	put_bh(entry_bh);
	return 0;
}

/**
 * nilfs_dat_is_live - checks if the virtual block number is alive
 * @dat: DAT file inode
 * @vblocknr: virtual block number
 * @errp: pointer to return code if error occurred
 *
 * Description: nilfs_dat_is_live() looks up the DAT-Entry for
 * @vblocknr and determines if the corresponding block is alive in the current
 * checkpoint or not. This check ignores snapshots and protection periods.
 *
 * Return Value: 1 if vblocknr is alive and 0 otherwise. On error, 0 is
 * returned and @errp is set to one of the following negative error codes.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-ENOENT - A block number associated with @vblocknr does not exist.
 */
int nilfs_dat_is_live(struct inode *dat, __u64 vblocknr, int *errp)
{
	struct buffer_head *entry_bh, *bh;
	struct nilfs_dat_entry *entry;
	sector_t blocknr;
	void *kaddr;
	int ret = 0, err;

	err = nilfs_palloc_get_entry_block(dat, vblocknr, 0, &entry_bh);
	if (err < 0)
		goto out;

	if (!nilfs_doing_gc() && buffer_nilfs_redirected(entry_bh)) {
		bh = nilfs_mdt_get_frozen_buffer(dat, entry_bh);
		if (bh) {
			WARN_ON(!buffer_uptodate(bh));
			put_bh(entry_bh);
			entry_bh = bh;
		}
	}

	kaddr = kmap_atomic(entry_bh->b_page);
	entry = nilfs_palloc_block_get_entry(dat, vblocknr, entry_bh, kaddr);
	blocknr = le64_to_cpu(entry->de_blocknr);
	if (blocknr == 0) {
		err = -ENOENT;
		goto out_unmap;
	}

	ret = nilfs_dat_entry_is_live(entry);

out_unmap:
	kunmap_atomic(kaddr);
	put_bh(entry_bh);
out:
	if (errp)
		*errp = err;
	return ret;
}

/**
 * nilfs_dat_translate - translate a virtual block number to a block number
 * @dat: DAT file inode
 * @vblocknr: virtual block number
 * @blocknrp: pointer to a block number
 *
 * Description: nilfs_dat_translate() maps the virtual block number @vblocknr
 * to the corresponding block number.
 *
 * Return Value: On success, 0 is returned and the block number associated
 * with @vblocknr is stored in the place pointed by @blocknrp. On error, one
 * of the following negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-ENOENT - A block number associated with @vblocknr does not exist.
 */
int nilfs_dat_translate(struct inode *dat, __u64 vblocknr, sector_t *blocknrp)
{
	struct buffer_head *entry_bh, *bh;
	struct nilfs_dat_entry *entry;
	sector_t blocknr;
	void *kaddr;
	int ret;

	ret = nilfs_palloc_get_entry_block(dat, vblocknr, 0, &entry_bh);
	if (ret < 0)
		return ret;

	if (!nilfs_doing_gc() && buffer_nilfs_redirected(entry_bh)) {
		bh = nilfs_mdt_get_frozen_buffer(dat, entry_bh);
		if (bh) {
			WARN_ON(!buffer_uptodate(bh));
			brelse(entry_bh);
			entry_bh = bh;
		}
	}

	kaddr = kmap_atomic(entry_bh->b_page);
	entry = nilfs_palloc_block_get_entry(dat, vblocknr, entry_bh, kaddr);
	blocknr = le64_to_cpu(entry->de_blocknr);
	if (blocknr == 0) {
		ret = -ENOENT;
		goto out;
	}
	*blocknrp = blocknr;

 out:
	kunmap_atomic(kaddr);
	brelse(entry_bh);
	return ret;
}

/**
 * nilfs_dat_replace_snapshot - replaces snapshot with prev or next snapshot
 * @entry: DAT-Entry
 * @prev: previous snapshot of the current snapshot
 * @next: next snapshot of the current snapshot
 *
 * Description: nilfs_dat_replace_snapshot() replaces the current snapshot,
 * which is about to be deleted, with either the previous or the next
 * snapshot. Since all snapshots are stored in a SORTED linked list and the
 * previous and next snapshots are known, it is possible to reliably determine
 * that the block doesn't belong to any other snapshot if it belongs to neither
 * one of them.
 */
static __u64 nilfs_dat_replace_snapshot(struct nilfs_dat_entry *entry,
				       __u64 prev,
				       __u64 next)
{
	if (nilfs_dat_entry_belongs_to_cp(entry, prev)) {
		entry->de_ss = cpu_to_le64(prev);
		return prev;
	} else if (nilfs_dat_entry_belongs_to_cp(entry, next)) {
		entry->de_ss = cpu_to_le64(next);
		return next;
	} else if (!nilfs_dat_entry_is_dec(entry))
		entry->de_ss = cpu_to_le64(NILFS_ENTRY_DEC);

	return NILFS_ENTRY_DEC;
}

struct nilfs_dat_scan_data {
	__u64 ss;
	__u64 prev_ss;
	__u64 next_ss;
	struct nilfs_sufile_accu_state accu_state;
};

static void nilfs_dat_do_scan_dec(struct inode *dat,
				  struct nilfs_palloc_req *req,
				  void *data)
{
	struct the_nilfs *nilfs;
	struct nilfs_dat_entry *entry;
	void *kaddr;
	__u64 prev_ss;
	struct nilfs_dat_scan_data *sd = data;
	__u64 ss = sd->ss, prev = sd->prev_ss, next = sd->next_ss;
	sector_t blocknr;
	int nblocks;

	kaddr = kmap_atomic(req->pr_entry_bh->b_page);
	entry = nilfs_palloc_block_get_entry(dat, req->pr_entry_nr,
					     req->pr_entry_bh, kaddr);
	blocknr = le64_to_cpu(entry->de_blocknr);
	prev_ss = le64_to_cpu(entry->de_ss);

	if (blocknr != 0 &&
	    !nilfs_dat_entry_is_live(entry) &&
	    (!nilfs_dat_entry_has_ss(entry) || prev_ss == ss) &&
	    nilfs_dat_entry_belongs_to_cp(entry, ss)) {

		ss = nilfs_dat_replace_snapshot(entry, prev, next);
		kunmap_atomic(kaddr);

		/* only mark dirty if the value actually changed */
		if (prev_ss != ss) {
			mark_buffer_dirty(req->pr_entry_bh);
			nilfs_mdt_mark_dirty(dat);

			/*
			 * Decrement segusg if NILFS_ENTRY_DEC was
			 * set by nilfs_dat_replace_snapshot(), but wasn't set
			 * before. Increment segusg if NILFS_ENTRY_DEC was
			 * set before, but was replaced by prev or next.
			 */
			if (ss == NILFS_ENTRY_DEC)
				nblocks = -1;
			else if (prev_ss == NILFS_ENTRY_DEC)
				nblocks = 1;
			else
				return;

			nilfs = dat->i_sb->s_fs_info;

			nilfs_sufile_accu_nlive_blks(nilfs, &sd->accu_state,
						     blocknr, nblocks);
		}
	} else
		kunmap_atomic(kaddr);
}

static void nilfs_dat_do_scan_inc(struct inode *dat,
				  struct nilfs_palloc_req *req,
				  void *data)
{
	struct the_nilfs *nilfs;
	struct nilfs_dat_entry *entry;
	void *kaddr;
	struct nilfs_dat_scan_data *sd = data;
	__u64 prev_ss, ss = sd->ss;
	sector_t blocknr;

	kaddr = kmap_atomic(req->pr_entry_bh->b_page);
	entry = nilfs_palloc_block_get_entry(dat, req->pr_entry_nr,
					     req->pr_entry_bh, kaddr);
	blocknr = le64_to_cpu(entry->de_blocknr);
	prev_ss = le64_to_cpu(entry->de_ss);

	if (blocknr != 0 &&
	    !nilfs_dat_entry_is_live(entry) &&
	    !nilfs_dat_entry_has_ss(entry) &&
	    nilfs_dat_entry_belongs_to_cp(entry, ss)) {

		entry->de_ss = cpu_to_le64(ss);

		kunmap_atomic(kaddr);
		mark_buffer_dirty(req->pr_entry_bh);
		nilfs_mdt_mark_dirty(dat);

		/*
		 * increment segment usage only if NILFS_ENTRY_DEC
		 * was set before the snapshot was created
		 */
		if (prev_ss == NILFS_ENTRY_DEC) {
			nilfs = dat->i_sb->s_fs_info;

			nilfs_sufile_accu_nlive_blks(nilfs, &sd->accu_state,
						     blocknr, 1);
		}
	} else
		kunmap_atomic(kaddr);
}

/**
 * nilfs_dat_scan_dec_ss - scan all dat entries for a checkpoint dec suinfo
 * @dat: inode of dat file
 * @ss: snapshot number
 * @prev: previous snapshot number
 * @next: next snapshot number
 */
int nilfs_dat_scan_dec_ss(struct inode *dat, __u64 ss,
					__u64 prev, __u64 next)
{
	struct nilfs_dat_scan_data data = {.ss = ss, .prev_ss = prev,
					   .next_ss = next};
	struct the_nilfs *nilfs = dat->i_sb->s_fs_info;
	int ret;

	ret = nilfs_palloc_scan_entries(dat, nilfs_dat_do_scan_dec, &data);

	nilfs_sufile_flush_nlive_blks(nilfs, &data.accu_state);

	return ret;
}

/**
 * nilfs_dat_scan_dec_ss - scan all dat entries for a checkpoint inc suinfo
 * @dat: inode of dat file
 * @ss: snapshot number
 */
int nilfs_dat_scan_inc_ss(struct inode *dat, __u64 ss)
{
	struct nilfs_dat_scan_data data = {.ss = ss};
	struct the_nilfs *nilfs = dat->i_sb->s_fs_info;
	int ret;

	ret = nilfs_palloc_scan_entries(dat, nilfs_dat_do_scan_inc, &data);

	nilfs_sufile_flush_nlive_blks(nilfs, &data.accu_state);

	return ret;
}

ssize_t nilfs_dat_get_vinfo(struct inode *dat, void *buf, unsigned visz,
			    size_t nvi)
{
	struct buffer_head *entry_bh;
	struct nilfs_dat_entry *entry;
	struct nilfs_vinfo *vinfo = buf;
	__u64 first, last;
	void *kaddr;
	unsigned long entries_per_block = NILFS_MDT(dat)->mi_entries_per_block;
	int i, j, n, ret;

	for (i = 0; i < nvi; i += n) {
		ret = nilfs_palloc_get_entry_block(dat, vinfo->vi_vblocknr,
						   0, &entry_bh);
		if (ret < 0)
			return ret;
		kaddr = kmap_atomic(entry_bh->b_page);
		/* last virtual block number in this block */
		first = vinfo->vi_vblocknr;
		do_div(first, entries_per_block);
		first *= entries_per_block;
		last = first + entries_per_block - 1;
		for (j = i, n = 0;
		     j < nvi && vinfo->vi_vblocknr >= first &&
			     vinfo->vi_vblocknr <= last;
		     j++, n++, vinfo = (void *)vinfo + visz) {
			entry = nilfs_palloc_block_get_entry(
				dat, vinfo->vi_vblocknr, entry_bh, kaddr);
			vinfo->vi_start = le64_to_cpu(entry->de_start);
			vinfo->vi_end = le64_to_cpu(entry->de_end);
			vinfo->vi_blocknr = le64_to_cpu(entry->de_blocknr);
		}
		kunmap_atomic(kaddr);
		brelse(entry_bh);
	}

	return nvi;
}

/**
 * nilfs_dat_read - read or get dat inode
 * @sb: super block instance
 * @entry_size: size of a dat entry
 * @raw_inode: on-disk dat inode
 * @inodep: buffer to store the inode
 */
int nilfs_dat_read(struct super_block *sb, size_t entry_size,
		   struct nilfs_inode *raw_inode, struct inode **inodep)
{
	static struct lock_class_key dat_lock_key;
	struct inode *dat;
	struct nilfs_dat_info *di;
	int err;

	if (entry_size > sb->s_blocksize) {
		printk(KERN_ERR
		       "NILFS: too large DAT entry size: %zu bytes.\n",
		       entry_size);
		return -EINVAL;
	} else if (entry_size < NILFS_MIN_DAT_ENTRY_SIZE) {
		printk(KERN_ERR
		       "NILFS: too small DAT entry size: %zu bytes.\n",
		       entry_size);
		return -EINVAL;
	}

	dat = nilfs_iget_locked(sb, NULL, NILFS_DAT_INO);
	if (unlikely(!dat))
		return -ENOMEM;
	if (!(dat->i_state & I_NEW))
		goto out;

	err = nilfs_mdt_init(dat, NILFS_MDT_GFP, sizeof(*di));
	if (err)
		goto failed;

	err = nilfs_palloc_init_blockgroup(dat, entry_size);
	if (err)
		goto failed;

	di = NILFS_DAT_I(dat);
	lockdep_set_class(&di->mi.mi_sem, &dat_lock_key);
	nilfs_palloc_setup_cache(dat, &di->palloc_cache);
	nilfs_mdt_setup_shadow_map(dat, &di->shadow);

	err = nilfs_read_inode_common(dat, raw_inode);
	if (err)
		goto failed;

	unlock_new_inode(dat);
 out:
	*inodep = dat;
	return 0;
 failed:
	iget_failed(dat);
	return err;
}
