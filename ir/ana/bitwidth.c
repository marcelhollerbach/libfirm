/*
 * This file is part of libFirm.
 * Copyright (C) 2012 University of Karlsruhe.
 */

/**
 * @file
 * @brief   Bitwidth analyses of a graph
 * @author  Marcel Hollerbach
 */
#include "bitwidth.h"

#include "debug.h"
#include "iredges_t.h"
#include "irgwalk.h"
#include "irnode_t.h"
#include "irnodemap.h"
#include "iropt.h"
#include "irhooks.h"
#include "nodes.h"
#include "pqueue.h"
#include "util.h"
#include "irouts.h"
#include "scalar_replace.h"
#include "math.h"
#include "irconsconfirm.h"
#include "iroptimize.h"
#include "irgraph_t.h"
#include "pset_new.h"

#include <assert.h>

unsigned int
bitwidth_used_bits(const ir_node *const node)
{
	bitwidth *b = bitwidth_fetch_bitwidth(node);
	ir_mode *mode = get_irn_mode(node);

	if (b) {
		return get_mode_size_bits(mode) - b->stable_digits;
	} else {
		return get_mode_size_bits(mode);
	}
}


bitwidth*
bitwidth_fetch_bitwidth(const ir_node *const node)
{
	ir_graph *g = get_irn_irg(node);

	if (!g->bitwidth.infos.data) {
		return NULL;
	} else {
		return ir_nodemap_get(bitwidth, &g->bitwidth.infos, node);
	}
}

static bool
is_meaningfull(ir_node *n)
{
	ir_mode *mode = get_irn_mode(n);

	if (!mode_is_int(mode))
		return false;

	return true;
}

static void
create_node(ir_node *node, void *data)
{
	ir_graph *g = get_irn_irg(node);
	bitwidth *info = XMALLOC(bitwidth);
	ir_mode *mode = get_irn_mode(node);

	info->valid = is_meaningfull(node);
	info->stable_digits = 0;
	//if the mode is valid, try to compute the best stable digit number you can think of
	if (info->valid){
		switch (get_irn_opcode(node)) {
			case iro_Const: {
				//const can be calculated directly
				long value = get_Const_long(node);
				unsigned word_length = get_mode_size_bits(mode);
				unsigned required_bits;

				if (value > 0)
				  required_bits = log2_floor(value) + 1;
				else if (value < 0)
					required_bits = log2_floor(-value) + 1;
				else
					required_bits = 1;

				//how many bits are required to represent the value
				assert(required_bits <= word_length);
				info->stable_digits = word_length - required_bits;

				//setting the not negative bit
				if (value < 0)
					info->is_positive = false;
				else
					info->is_positive = true;

				break;
			}
			case iro_Builtin: {
				info->stable_digits = 0;
				info->is_positive = false;
				//FIXME this can be made better depending on the buildin type.
				//however, for now this is not implemented
			}
			case iro_Member:
			case iro_Sel:
			case iro_Proj:
			case iro_Address: {
				info->stable_digits = 0;
				info->is_positive = false;
				break;
			}
			case iro_Size: {
				ir_type *t = get_Size_type(node);
				info->stable_digits = get_type_size(t);
				info->is_positive = false;
				break;
			}
			default: {
				info->stable_digits = get_mode_size_bits(mode);
				info->is_positive = true;
			}
		}
	}

	ir_nodemap_insert(&g->bitwidth.infos, node, info);
}

static void
add_node(ir_node *node, void *data)
{
	pqueue_t *queue = data;

	pqueue_put(queue, node, 0);
}

static void
refit_children(ir_node *node, pqueue_t *queue)
{
	for (int n = 0; n < get_irn_n_outs(node); ++n) {
		ir_node *successor = get_irn_out(node, n);
		pqueue_put(queue, successor, 0);
	}
}

static long
generate_max_value(ir_node *n)
{
	if (get_irn_opcode_(n) == iro_Const) {
		return get_Const_long(n);
	} else {
		ir_mode *mode = get_irn_mode(n);
		unsigned bits = get_mode_size_bits(mode);

		if (mode_is_signed(mode))
			return pow(2, bits - 1) - 1;
		else
			return pow(2, bits) - 1;
	}
}

static long
generate_min_abs_value(ir_node *n)
{
	if (get_irn_opcode_(n) == iro_Const) {
		return get_Const_long(n);
	} else {
		return 0;
	}
}

static unsigned
compute_bitwidth_relation(bitwidth *value, bitwidth *bound, ir_relation relation)
{
	unsigned result;

	switch(relation) {
		case ir_relation_less_equal:
		case ir_relation_less:
			return MAX(value->stable_digits, bound->stable_digits);
		case ir_relation_equal:
			return bound->stable_digits;
		case ir_relation_greater:
		case ir_relation_greater_equal:
		case ir_relation_false:
			return 0;
		default:
			printf("FIXME missing things\n");
			return 0;
	}
}

typedef enum {
   SMALLER = 0,
   EQUAL = 1,
   BIGGER = 2
} Compare_result;

static Compare_result
cmp_bitwidth(bitwidth *a, bitwidth *b)
{
	if (a->stable_digits < b->stable_digits)
		return SMALLER;
	if (a->stable_digits > b->stable_digits)
		return BIGGER;

	if (a->is_positive == b->is_positive) {
		return EQUAL;
	} else if (a->is_positive) {
		return BIGGER;
	} else { //if (!a->not_negative)
		return SMALLER;
	}

}

static void
evalulate_node(ir_node *node, pqueue_t *queue)
{
	bitwidth *info = bitwidth_fetch_bitwidth(node);
	ir_mode *mode = get_irn_mode(node);
	ir_node *obj_a, *obj_b;

	if (!info->valid) return;

	bitwidth new = *info;
	switch(get_irn_opcode(node)) {
		case iro_Add: {
			// stable digits are defininig some max value of this data word,
			// both max values added and transformed back to stable digits
			obj_a = get_Add_left(node);
			obj_b = get_Add_right(node);
			bitwidth *a = bitwidth_fetch_bitwidth(obj_a), *b = bitwidth_fetch_bitwidth(obj_b);
			int bigger_bitwidth = MIN(a->stable_digits, b->stable_digits);
			new.stable_digits = MAX(bigger_bitwidth - 1, 0);
			if (a->is_positive && b->is_positive && new.stable_digits != 0)
				new.is_positive = true;
			else
				new.is_positive = false;
			break;
		}
		case iro_Sub: {
			//we invert the right node - bitwidth stays the same
			//we do + 1 - bitwidth gets one werse
			//we do a + *from-before* - bitwidth gets one worse
			obj_a = get_Sub_left(node);
			obj_b = get_Sub_right(node);
			bitwidth *a = bitwidth_fetch_bitwidth(obj_a), *b = bitwidth_fetch_bitwidth(obj_b);
			int bigger_bitwidth = MIN(a->stable_digits, b->stable_digits);
			new.stable_digits = MAX(bigger_bitwidth - 1, 0);
			new.is_positive = false;
			break;
		}
		case iro_Minus: {
			//we invert the right node - bitwidth stays the same
			//we do + 1 - bitwidth gets one werse
			obj_a = get_Minus_op(node);
			bitwidth *a = bitwidth_fetch_bitwidth(obj_a);
			new.stable_digits = MAX(a->stable_digits - 1, 0);
			new.is_positive = false;
			break;
		}
		// after those nodes the number of stable digits is the smallest number of stable digits from the input nodes
		case iro_Mux:
		case iro_Phi:
		case iro_And:
		case iro_Eor:
		case iro_Or: {
			ir_node **ins = get_irn_in(node);
			unsigned min_of_nodes = INT_MAX;
			bool is_positive = true;
			for (int i = 0; i < get_irn_arity(node); ++i) {
				ir_node *c = ins[i];
				bitwidth *bc = bitwidth_fetch_bitwidth(c);

				min_of_nodes = MIN(min_of_nodes, bc->stable_digits);
				is_positive &= bc->is_positive;
			}
			new.stable_digits = min_of_nodes;
			new.is_positive = is_positive;
			break;
		}
		case iro_Conv: {
			ir_node *op = get_Conv_op(node);
			bitwidth *op_bitwidth = bitwidth_fetch_bitwidth(op);
			ir_mode *old_mode = get_irn_mode(op);

			int delta = (int)get_mode_size_bits(mode) - (int)get_mode_size_bits(old_mode);

			new.stable_digits = MAX(op_bitwidth->stable_digits + delta, 0);
			if (op_bitwidth->is_positive && new.stable_digits > 0)
			  new.is_positive = true;
			break;
		}
		case iro_Mod: {
			obj_a = get_Mod_right(node);
			obj_b = get_Mod_right(node);
			bitwidth *a = bitwidth_fetch_bitwidth(obj_a), *b = bitwidth_fetch_bitwidth(obj_b);
			unsigned max_val = generate_max_value(obj_b);

			new.stable_digits = log2_floor(max_val) + 1;
			new.is_positive = a->is_positive;
			break;
		}
		case iro_Shl: {
			//shift left makes the number of stable digits lower by the amount of min of the right node
			ir_node *obj_a = get_Shl_left(node) , *obj_b = get_Shl_right(node);
			bitwidth *a = bitwidth_fetch_bitwidth(obj_a);
			long max_val = generate_max_value(obj_b);

			if (get_irn_opcode(obj_b) == iro_Const) {
				new.stable_digits = a->stable_digits - max_val;
			} else {
				//if we dont know how much we shift, we should take the worst case, which is 0
				new.stable_digits = 0;
			}

			if (new.stable_digits < 0)
				new.is_positive = a->is_positive;
			break;
		}
		case iro_Not: {
			obj_a = get_Not_op(node);
			bitwidth *a = bitwidth_fetch_bitwidth(obj_a);
			new.stable_digits = info->stable_digits;
			new.is_positive = !a->is_positive;
			break;
		}
		case iro_Div: { //WC: X / -1
			ir_mode *mode_right;
			obj_a = get_Div_left(node);
			obj_b = get_Div_right(node);
			mode_right = get_irn_mode(obj_b);
			bitwidth *a = bitwidth_fetch_bitwidth(obj_a), *b = bitwidth_fetch_bitwidth(obj_b);
			if (!mode_is_signed(mode_right))
				new.stable_digits = a->stable_digits;
			else
				new.stable_digits = a->stable_digits - 1;

			if (a->is_positive && b->is_positive && new.stable_digits > 0)
				new.is_positive = true;
			else
				new.is_positive = false;
			break;
		}
		case iro_Shr: { // the worst case is the maximum number
			obj_a = get_Shr_left(node);
			obj_b = get_Shr_right(node);
			ir_mode *mode_a = get_irn_mode(obj_a);

			if (mode_is_signed(mode_a)) {
				//Worst case, we shift a value < 0 one value to the right, then we are at 1.
				new.stable_digits = 1;
			} else {
				bitwidth *a = bitwidth_fetch_bitwidth(obj_a);
				long min_val = generate_min_abs_value(obj_b);
				//Worst case, we shift right by the amount from b
				new.stable_digits = MAX((int)a->stable_digits + min_val, 0);
			}
			new.is_positive = true;
			break;
		}
		case iro_Shrs: {
			obj_a = get_Shrs_left(node);
			obj_b = get_Shrs_right(node);
			bitwidth *a = bitwidth_fetch_bitwidth(obj_a);
			long min_val = generate_min_abs_value(obj_b);

			new.stable_digits = MAX((int)a->stable_digits + min_val, 0);
			new.is_positive = a->is_positive;
			break;
		}
		case iro_Mul: {
			ir_mode *mode = get_irn_mode(node);
			obj_a = get_Mul_left(node);
			obj_b = get_Mul_right(node);
			long used_a = bitwidth_used_bits(obj_a);
			long used_b = bitwidth_used_bits(obj_b);
			bitwidth *a = bitwidth_fetch_bitwidth(obj_a), *b = bitwidth_fetch_bitwidth(obj_b);

			new.stable_digits = MAX((int)get_mode_size_bits(mode) - (used_b + used_a), 0);
			if (a->is_positive && b->is_positive && new.stable_digits > 0)
				new.is_positive = true;
			else
				new.is_positive = false;
			break;
		}
		case iro_Mulh: {
			ir_mode *mode = get_irn_mode(node);
			obj_a = get_Mulh_left(node);
			obj_b = get_Mulh_right(node);
			long used_a = bitwidth_used_bits(obj_a);
			long used_b = bitwidth_used_bits(obj_b);
			bitwidth *a = bitwidth_fetch_bitwidth(obj_a), *b = bitwidth_fetch_bitwidth(obj_b);

			new.stable_digits = MAX((used_b + used_a) - (int)get_mode_size_bits(mode), 0);
			/* FIXME THIS COULD BE WEIRD */
			if (a->is_positive && b->is_positive && new.stable_digits > 0)
				new.is_positive = true;
			else
				new.is_positive = false;
			break;
		}
		case iro_Confirm: {
			ir_node *obj_value = get_Confirm_value(node);
			ir_node *obj_bound = get_Confirm_bound(node);
			bitwidth *value = bitwidth_fetch_bitwidth(obj_value);
			bitwidth *bound = bitwidth_fetch_bitwidth(obj_bound);

			ir_relation relation = get_Confirm_relation(node);

			new.stable_digits = compute_bitwidth_relation(value, bound, relation);
			new.is_positive = value->is_positive;
			break;
		}
		case iro_Bitcast: {
			obj_a = get_Bitcast_op(node);
			bitwidth *a = bitwidth_fetch_bitwidth(obj_a);
			new.stable_digits = a->stable_digits;
			new.is_positive = true;
			break;
		}
		case iro_Offset: {
			//FIXME try to fetch the real offset in this entity
			break;
		}
		//nodes that stay the same
		case iro_Address:
		case iro_Const:
		case iro_Align:
		case iro_Alloc:
		case iro_Anchor:
		case iro_Bad:
		case iro_Block:
		case iro_Call:
		case iro_Cmp:
		case iro_Cond:
		case iro_CopyB:
		case iro_Deleted:
		case iro_Dummy:
		case iro_End:
		case iro_Free:
		case iro_IJmp:
		case iro_Id:
		case iro_Jmp:
		case iro_Load:
		case iro_NoMem:
		case iro_Pin:
		case iro_Proj:
		case iro_Raise:
		case iro_Return:
		case iro_Start:
		case iro_Store:
		case iro_Switch:
		case iro_Sync:
		case iro_Tuple:
		case iro_Unknown:
		case iro_Size:
		case iro_Member:
		case iro_Sel:
		case iro_Builtin:
		break;
	}

	if (cmp_bitwidth(&new, info) == SMALLER) {
		refit_children(node, queue);
		memcpy(info, &new, sizeof(bitwidth));
	}
}

static hook_entry_t bitwidth_dump_hook;

static void
dump_bitwidth_info(void *ctx, FILE *F, const ir_node *node)
{
	bitwidth *b = bitwidth_fetch_bitwidth(node);
	(void)ctx;

	if (b) {
		if (b->valid) {
			fprintf(F, "bitwidth-stable-digits %d\n", b->stable_digits);
			fprintf(F, "bitwidth-is-positive %d\n", b->is_positive);
		} else {
			fprintf(F, "bitwidth-stable-digits 'invalid'\n");
		}
	}
}

void
compute_bitwidth_info(ir_graph *irg)
{
	pqueue_t *queue;

	if (bitwidth_dump_hook.hook._hook_node_info == NULL) {
		bitwidth_dump_hook.hook._hook_node_info = dump_bitwidth_info;
		register_hook(hook_node_info, &bitwidth_dump_hook);
	}

	//init inital state
	remove_confirms(irg);
	construct_confirms(irg);
	assure_irg_outs(irg);
	ir_nodemap_init(&irg->bitwidth.infos, irg);
	queue = new_pqueue();

	//phase 1 init all nodes that are meaningfull
	irg_walk_graph(irg, create_node, add_node, queue);

	//phase 2 walk down the queue reevalulating each child if things are changing
	while (!pqueue_empty(queue)) {
		ir_node *node = pqueue_pop_front(queue);
		evalulate_node(node, queue);
	}
	optimize_cf(irg);
	remove_confirms(irg);
	del_pqueue(queue);

	add_irg_properties(irg, IR_GRAPH_PROPERTY_CONSISTENT_BITWIDTH_INFO);
}

void
free_bitwidth_info(ir_graph *irg)
{
	size_t len = ARR_LEN(irg->bitwidth.infos.data);
	for (int i = 0; i < len; ++i) {
		bitwidth *info = irg->bitwidth.infos.data[i];

		free(info);
	}
	ir_nodemap_destroy(&irg->bitwidth.infos);

	clear_irg_properties(irg, IR_GRAPH_PROPERTY_CONSISTENT_BITWIDTH_INFO);
}

void
assure_bitwidth_info(ir_graph *irg)
{
	if (!irg_has_properties(irg, IR_GRAPH_PROPERTY_CONSISTENT_BITWIDTH_INFO)) {
		compute_bitwidth_info(irg);
	}
}
