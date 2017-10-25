/*
 * Copyright (C) 2016 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __NFP_BPF_H__
#define __NFP_BPF_H__ 1

#include <linux/bitfield.h>
#include <linux/bpf.h>
#include <linux/bpf_verifier.h>
#include <linux/list.h>
#include <linux/types.h>

#include "../nfp_asm.h"
#include "../nfp_net.h"

/* For branch fixup logic use up-most byte of branch instruction as scratch
 * area.  Remember to clear this before sending instructions to HW!
 */
#define OP_BR_SPECIAL	0xff00000000000000ULL

enum br_special {
	OP_BR_NORMAL = 0,
	OP_BR_GO_OUT,
	OP_BR_GO_ABORT,
};

enum static_regs {
	STATIC_REG_IMM		= 21, /* Bank AB */
	STATIC_REG_STACK	= 22, /* Bank A */
	STATIC_REG_PKT_LEN	= 22, /* Bank B */
};

enum pkt_vec {
	PKT_VEC_PKT_LEN		= 0,
	PKT_VEC_PKT_PTR		= 2,
};

enum nfp_bpf_action_type {
	NN_ACT_TC_DROP,
	NN_ACT_TC_REDIR,
	NN_ACT_DIRECT,
	NN_ACT_XDP,
};

#define pv_len(np)	reg_lm(1, PKT_VEC_PKT_LEN)
#define pv_ctm_ptr(np)	reg_lm(1, PKT_VEC_PKT_PTR)

#define stack_reg(np)	reg_a(STATIC_REG_STACK)
#define stack_imm(np)	imm_b(np)
#define plen_reg(np)	reg_b(STATIC_REG_PKT_LEN)
#define pptr_reg(np)	pv_ctm_ptr(np)
#define imm_a(np)	reg_a(STATIC_REG_IMM)
#define imm_b(np)	reg_b(STATIC_REG_IMM)
#define imm_both(np)	reg_both(STATIC_REG_IMM)

#define NFP_BPF_ABI_FLAGS	reg_imm(0)
#define   NFP_BPF_ABI_FLAG_MARK	1

struct nfp_prog;
struct nfp_insn_meta;
typedef int (*instr_cb_t)(struct nfp_prog *, struct nfp_insn_meta *);

#define nfp_prog_first_meta(nfp_prog)					\
	list_first_entry(&(nfp_prog)->insns, struct nfp_insn_meta, l)
#define nfp_prog_last_meta(nfp_prog)					\
	list_last_entry(&(nfp_prog)->insns, struct nfp_insn_meta, l)
#define nfp_meta_next(meta)	list_next_entry(meta, l)
#define nfp_meta_prev(meta)	list_prev_entry(meta, l)

/**
 * struct nfp_insn_meta - BPF instruction wrapper
 * @insn: BPF instruction
 * @ptr: pointer type for memory operations
 * @ptr_not_const: pointer is not always constant
 * @off: index of first generated machine instruction (in nfp_prog.prog)
 * @n: eBPF instruction number
 * @skip: skip this instruction (optimized out)
 * @double_cb: callback for second part of the instruction
 * @l: link on nfp_prog->insns list
 */
struct nfp_insn_meta {
	struct bpf_insn insn;
	struct bpf_reg_state ptr;
	bool ptr_not_const;
	unsigned int off;
	unsigned short n;
	bool skip;
	instr_cb_t double_cb;

	struct list_head l;
};

#define BPF_SIZE_MASK	0x18

static inline u8 mbpf_class(const struct nfp_insn_meta *meta)
{
	return BPF_CLASS(meta->insn.code);
}

static inline u8 mbpf_src(const struct nfp_insn_meta *meta)
{
	return BPF_SRC(meta->insn.code);
}

static inline u8 mbpf_op(const struct nfp_insn_meta *meta)
{
	return BPF_OP(meta->insn.code);
}

static inline u8 mbpf_mode(const struct nfp_insn_meta *meta)
{
	return BPF_MODE(meta->insn.code);
}

/**
 * struct nfp_prog - nfp BPF program
 * @prog: machine code
 * @prog_len: number of valid instructions in @prog array
 * @__prog_alloc_len: alloc size of @prog array
 * @act: BPF program/action type (TC DA, TC with action, XDP etc.)
 * @num_regs: number of registers used by this program
 * @regs_per_thread: number of basic registers allocated per thread
 * @start_off: address of the first instruction in the memory
 * @tgt_out: jump target for normal exit
 * @tgt_abort: jump target for abort (e.g. access outside of packet buffer)
 * @tgt_done: jump target to get the next packet
 * @n_translated: number of successfully translated instructions (for errors)
 * @error: error code if something went wrong
 * @stack_depth: max stack depth from the verifier
 * @insns: list of BPF instruction wrappers (struct nfp_insn_meta)
 */
struct nfp_prog {
	u64 *prog;
	unsigned int prog_len;
	unsigned int __prog_alloc_len;

	enum nfp_bpf_action_type act;

	unsigned int num_regs;
	unsigned int regs_per_thread;

	unsigned int start_off;
	unsigned int tgt_out;
	unsigned int tgt_abort;
	unsigned int tgt_done;

	unsigned int n_translated;
	int error;

	unsigned int stack_depth;

	struct list_head insns;
};

struct nfp_bpf_result {
	unsigned int n_instr;
	bool dense_mode;
};

int
nfp_bpf_jit(struct bpf_prog *filter, void *prog, enum nfp_bpf_action_type act,
	    unsigned int prog_start, unsigned int prog_done,
	    unsigned int prog_sz, struct nfp_bpf_result *res);

int nfp_prog_verify(struct nfp_prog *nfp_prog, struct bpf_prog *prog);

struct nfp_net;
struct tc_cls_bpf_offload;

/**
 * struct nfp_net_bpf_priv - per-vNIC BPF private data
 * @rx_filter:		Filter offload statistics - dropped packets/bytes
 * @rx_filter_prev:	Filter offload statistics - values from previous update
 * @rx_filter_change:	Jiffies when statistics last changed
 * @rx_filter_stats_timer:  Timer for polling filter offload statistics
 * @rx_filter_lock:	Lock protecting timer state changes (teardown)
 */
struct nfp_net_bpf_priv {
	struct nfp_stat_pair rx_filter, rx_filter_prev;
	unsigned long rx_filter_change;
	struct timer_list rx_filter_stats_timer;
	struct nfp_net *nn;
	spinlock_t rx_filter_lock;
};

int nfp_net_bpf_offload(struct nfp_net *nn, struct tc_cls_bpf_offload *cls_bpf);
void nfp_net_filter_stats_timer(struct timer_list *t);

#endif
