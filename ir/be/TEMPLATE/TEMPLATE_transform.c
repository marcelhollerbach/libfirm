/*
 * Copyright (C) 1995-2008 University of Karlsruhe.  All right reserved.
 *
 * This file is part of libFirm.
 *
 * This file may be distributed and/or modified under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation and appearing in the file LICENSE.GPL included in the
 * packaging of this file.
 *
 * Licensees holding valid libFirm Professional Edition licenses may use
 * this file in accordance with the libFirm Commercial License.
 * Agreement provided with the Software.
 *
 * This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
 * WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE.
 */

/**
 * @file
 * @brief   code selection (transform FIRM into TEMPLATE FIRM)
 * @version $Id$
 */
#include "config.h"

#include "irnode_t.h"
#include "irgraph_t.h"
#include "irmode_t.h"
#include "irgmod.h"
#include "iredges.h"
#include "irvrfy.h"
#include "ircons.h"
#include "iropt_t.h"
#include "debug.h"

#include "../benode.h"
#include "bearch_TEMPLATE_t.h"

#include "TEMPLATE_nodes_attr.h"
#include "TEMPLATE_transform.h"
#include "TEMPLATE_new_nodes.h"

#include "gen_TEMPLATE_regalloc_if.h"

DEBUG_ONLY(static firm_dbg_module_t *dbg = NULL;)

/**
 * Creates an TEMPLATE Add.
 *
 * @param env   The transformation environment
 * @param op1   first operator
 * @param op2   second operator
 * @return the created TEMPLATE Add node
 */
static ir_node *gen_Add(TEMPLATE_transform_env_t *env, ir_node *op1, ir_node *op2)
{
	return new_bd_TEMPLATE_Add(env->dbg, env->block, op1, op2, env->mode);
}



/**
 * Creates an TEMPLATE Mul.
 *
 * @param dbg       firm node dbg
 * @param block     the block the new node should belong to
 * @param op1       first operator
 * @param op2       second operator
 * @param mode      node mode
 * @return the created TEMPLATE Mul node
 */
static ir_node *gen_Mul(TEMPLATE_transform_env_t *env, ir_node *op1, ir_node *op2)
{
	if (mode_is_float(env->mode)) {
		return new_bd_TEMPLATE_fMul(env->dbg, env->block, op1, op2, env->mode);
	} else {
		return new_bd_TEMPLATE_Mul(env->dbg, env->block, op1, op2, env->mode);
	}
}



/**
 * Creates an TEMPLATE And.
 *
 * @param dbg       firm node dbg
 * @param block     the block the new node should belong to
 * @param op1       first operator
 * @param op2       second operator
 * @param mode      node mode
 * @return the created TEMPLATE And node
 */
static ir_node *gen_And(TEMPLATE_transform_env_t *env, ir_node *op1, ir_node *op2)
{
	return new_bd_TEMPLATE_And(env->dbg, env->block, op1, op2, env->mode);
}



/**
 * Creates an TEMPLATE Or.
 *
 * @param dbg       firm node dbg
 * @param block     the block the new node should belong to
 * @param op1       first operator
 * @param op2       second operator
 * @param mode      node mode
 * @return the created TEMPLATE Or node
 */
static ir_node *gen_Or(TEMPLATE_transform_env_t *env, ir_node *op1, ir_node *op2)
{
	return new_bd_TEMPLATE_Or(env->dbg, env->block, op1, op2, env->mode);
}



/**
 * Creates an TEMPLATE Eor.
 *
 * @param dbg       firm node dbg
 * @param block     the block the new node should belong to
 * @param op1       first operator
 * @param op2       second operator
 * @param mode      node mode
 * @return the created TEMPLATE Eor node
 */
static ir_node *gen_Eor(TEMPLATE_transform_env_t *env, ir_node *op1, ir_node *op2)
{
	return new_bd_TEMPLATE_Eor(env->dbg, env->block, op1, op2, env->mode);
}



/**
 * Creates an TEMPLATE Sub.
 *
 * @param dbg       firm node dbg
 * @param block     the block the new node should belong to
 * @param op1       first operator
 * @param op2       second operator
 * @param mode      node mode
 * @return the created TEMPLATE Sub node
 */
static ir_node *gen_Sub(TEMPLATE_transform_env_t *env, ir_node *op1, ir_node *op2)
{
	if (mode_is_float(env->mode)) {
		return new_bd_TEMPLATE_fSub(env->dbg, env->block, op1, op2, env->mode);
	} else {
		return new_bd_TEMPLATE_Sub(env->dbg, env->block, op1, op2, env->mode);
	}
}



/**
 * Creates an TEMPLATE floating Div.
 *
 * @param dbg       firm node dbg
 * @param block     the block the new node should belong to
 * @param op1       first operator
 * @param op2       second operator
 * @param mode      node mode
 * @return the created TEMPLATE fDiv node
 */
static ir_node *gen_Quot(TEMPLATE_transform_env_t *env, ir_node *op1, ir_node *op2)
{
	return new_bd_TEMPLATE_fDiv(env->dbg, env->block, op1, op2, env->mode);
}



/**
 * Creates an TEMPLATE Shl.
 *
 * @param dbg       firm node dbg
 * @param block     the block the new node should belong to
 * @param op1       first operator
 * @param op2       second operator
 * @param mode      node mode
 * @return the created TEMPLATE Shl node
 */
static ir_node *gen_Shl(TEMPLATE_transform_env_t *env, ir_node *op1, ir_node *op2)
{
	return new_bd_TEMPLATE_Shl(env->dbg, env->block, op1, op2, env->mode);
}



/**
 * Creates an TEMPLATE Shr.
 *
 * @param dbg       firm node dbg
 * @param block     the block the new node should belong to
 * @param op1       first operator
 * @param op2       second operator
 * @param mode      node mode
 * @return the created TEMPLATE Shr node
 */
static ir_node *gen_Shr(TEMPLATE_transform_env_t *env, ir_node *op1, ir_node *op2)
{
	return new_bd_TEMPLATE_Shr(env->dbg, env->block, op1, op2, env->mode);
}



/**
 * Transforms a Minus node.
 *
 * @param mod     the debug module
 * @param block   the block the new node should belong to
 * @param node    the ir Minus node
 * @param op      operator
 * @param mode    node mode
 * @return the created TEMPLATE Minus node
 */
static ir_node *gen_Minus(TEMPLATE_transform_env_t *env, ir_node *op)
{
	if (mode_is_float(env->mode)) {
		return new_bd_TEMPLATE_fMinus(env->dbg, env->block, op, env->mode);
	}
	return new_bd_TEMPLATE_Minus(env->dbg, env->block, op, env->mode);
}



/**
 * Transforms a Not node.
 *
 * @param mod     the debug module
 * @param block   the block the new node should belong to
 * @param node    the ir Not node
 * @param op      operator
 * @param mode    node mode
 * @return the created TEMPLATE Not node
 */
static ir_node *gen_Not(TEMPLATE_transform_env_t *env, ir_node *op)
{
	return new_bd_TEMPLATE_Not(env->dbg, env->block, op, env->mode);
}



/**
 * Transforms a Load.
 *
 * @param mod     the debug module
 * @param block   the block the new node should belong to
 * @param node    the ir Load node
 * @param mode    node mode
 * @return the created TEMPLATE Load node
 */
static ir_node *gen_Load(TEMPLATE_transform_env_t *env)
{
	ir_node *node = env->irn;

	if (mode_is_float(env->mode)) {
		return new_bd_TEMPLATE_fLoad(env->dbg, env->block, get_Load_ptr(node), get_Load_mem(node), env->mode);
	}
	return new_bd_TEMPLATE_Load(env->dbg, env->block, get_Load_ptr(node), get_Load_mem(node), env->mode);
}



/**
 * Transforms a Store.
 *
 * @param mod     the debug module
 * @param block   the block the new node should belong to
 * @param node    the ir Store node
 * @param mode    node mode
 * @return the created TEMPLATE Store node
 */
static ir_node *gen_Store(TEMPLATE_transform_env_t *env)
{
	ir_node *node = env->irn;

	if (mode_is_float(env->mode)) {
		return new_bd_TEMPLATE_fStore(env->dbg, env->block, get_Store_ptr(node), get_Store_value(node), get_Store_mem(node), env->mode);
	}
	return new_bd_TEMPLATE_Store(env->dbg, env->block, get_Store_ptr(node), get_Store_value(node), get_Store_mem(node), env->mode);
}

static ir_node *gen_Jmp(TEMPLATE_transform_env_t *env)
{
	return new_bd_TEMPLATE_Jmp(env->dbg, env->block);
}




/**
 * Transforms the given firm node (and maybe some other related nodes)
 * into one or more assembler nodes.
 *
 * @param node    the firm node
 * @param env     the debug module
 */
void TEMPLATE_transform_node(ir_node *node, void *env)
{
	ir_opcode code             = get_irn_opcode(node);
	ir_node *asm_node          = NULL;
	TEMPLATE_transform_env_t tenv;
	(void) env;

	if (is_Block(node))
		return;

	tenv.block    = get_nodes_block(node);
	tenv.dbg      = get_irn_dbg_info(node);
	tenv.irg      = current_ir_graph;
	tenv.irn      = node;
	tenv.mode     = get_irn_mode(node);

#define UNOP(a)        case iro_##a: asm_node = gen_##a(&tenv, get_##a##_op(node)); break
#define BINOP(a)       case iro_##a: asm_node = gen_##a(&tenv, get_##a##_left(node), get_##a##_right(node)); break
#define GEN(a)         case iro_##a: asm_node = gen_##a(&tenv); break
#define IGN(a)         case iro_##a: break
#define BAD(a)         case iro_##a: goto bad

	DBG((dbg, LEVEL_1, "check %+F ... ", node));

	switch (code) {
		BINOP(Add);
		BINOP(Mul);
		BINOP(And);
		BINOP(Or);
		BINOP(Eor);

		BINOP(Sub);
		BINOP(Shl);
		BINOP(Shr);
		BINOP(Quot);


		UNOP(Minus);
		UNOP(Not);

		GEN(Load);
		GEN(Store);
		GEN(Jmp);

		/* TODO: implement these nodes */
		IGN(Shrs);
		IGN(Div);
		IGN(Mod);
		IGN(DivMod);
		IGN(Const);
		IGN(SymConst);
		IGN(Conv);
		IGN(Abs);
		IGN(Cond);
		IGN(Mux);
		IGN(Mulh);
		IGN(CopyB);
		IGN(Unknown);
		IGN(Cmp);

		/* You probably don't need to handle the following nodes */

		IGN(Call);
		IGN(Proj);
		IGN(Alloc);

		IGN(Block);
		IGN(Start);
		IGN(End);
		IGN(NoMem);
		IGN(Phi);
		IGN(IJmp);
		IGN(Break);
		IGN(Sync);

		BAD(Raise);
		BAD(Sel);
		BAD(InstOf);
		BAD(Cast);
		BAD(Free);
		BAD(Tuple);
		BAD(Id);
		BAD(Bad);
		BAD(Confirm);
		BAD(Filter);
		BAD(CallBegin);
		BAD(EndReg);
		BAD(EndExcept);

		default:
			break;
bad:
		fprintf(stderr, "Not implemented: %s\n", get_irn_opname(node));
		assert(0);
	}

	if (asm_node) {
		exchange(node, asm_node);
		DB((dbg, LEVEL_1, "created node %+F[%p]\n", asm_node, asm_node));
	} else {
		DB((dbg, LEVEL_1, "ignored\n"));
	}
}

void TEMPLATE_init_transform(void)
{
	FIRM_DBG_REGISTER(dbg, "firm.be.TEMPLATE.transform");
}
