/**
 *
 * @file irsimpeltype.c
 *
 * Project:     libFIRM
 * File name:   ir/opt/strength_red.c
 * Purpose:     Make strength reduction .
 * Author:      Beyhan Veliev
 * Modified by:
 * Created:     22.8.2003
 * CVS-ID:      $Id$
 * Copyright:   (c) 2003 Universitšt Karlsruhe
 * Licence:     This file protected by GPL -  GNU GENERAL PUBLIC LICENSE.
 */


# include "strength_red.h"

# include "irouts.h"
# include "irnode_t.h"
# include "irgwalk.h"
# include "irloop_t.h"
# include "ircons.h"
# include "irgmod.h"
# include "irdump.h"
# include "firmstat.h"


/* The information needed for an induction variable */
typedef struct _induct_var_info {
  ir_op   *operation_code;
  ir_node *increment, *init, *op;
  int      be_pos;
  int      init_pred_pos;
  int      op_pred_pos;
} induct_var_info;

/** Counter for verbose information about optimization. */
static int n_reduced_expressions;

/** Detect basic iteration variables.
 *
 * The variable ir represented by a subgraph as this:
 *
 *       init
 *       /|\
 *        |
 *   |-- Phi
 *   |   /|\
 *   |    |
 *   |-->op
 *
 * Where op is a Add or Sub, and init is loop invariant.
 * @@@ So far we only accept Phi nodes with two predecessors.
 * We could expand this to Phi nodes where all preds are either
 * op or loop invariant.
 *
 * @param n     A phi node.
 * @param info  After call contains the induction variable information
 */
static induct_var_info *is_induction_variable (ir_node *n, induct_var_info *info) {

  ir_node  *phi_pred_0, *phi_pred_1, *add_r, *add_l, *sub_r, *sub_l ;
  ir_op    *phi_pred_0_op, *phi_pred_1_op;

  info->operation_code = NULL;
  info->increment      = NULL;
  info->init           = NULL;
  info->op             = NULL;
  info->be_pos         = -1;
  info->init_pred_pos  = -1;
  info->op_pred_pos    = -1;

  assert(get_irn_op(n) == op_Phi);

  /* The necessary conditions for the phi node. */
  if (get_irn_arity(n) != 2             ||
      !has_backedges(get_nodes_block(n))  )
    return NULL;

  /* The predecessors  of the phi node. */
  phi_pred_0 = get_Phi_pred(n, 0);
  phi_pred_1 = get_Phi_pred(n, 1);

  /*The operation of the predecessors. */
  phi_pred_0_op = get_irn_op(phi_pred_0);
  phi_pred_1_op = get_irn_op(phi_pred_1);

  /*Compute if the induction variable is added or substracted wiht a constant . */
  if (phi_pred_0_op == op_Add){
    info->operation_code = op_Add;
    add_l = get_Add_left(phi_pred_0);
    add_r = get_Add_right(phi_pred_0);
    info->op_pred_pos = 0;
    info->init_pred_pos = 1;
    if (add_l == n){
      info->increment = add_r;
    } else if (add_r == n){
      info->increment = add_l;
    } else return NULL;
  } else if (phi_pred_1_op == op_Add){
    info->operation_code = op_Add ;
    add_l = get_Add_left(phi_pred_1);
    add_r = get_Add_right(phi_pred_1);
    info->op_pred_pos = 1;
    info->init_pred_pos = 0;
    if (add_l == n){
      info->increment = add_r;
    } else if (add_r == n){
      info->increment = add_l;
    } else return NULL;
  } else if (phi_pred_0_op == op_Sub){
    info->operation_code = op_Sub;
    sub_r = get_Sub_right(phi_pred_0);
    sub_l = get_Sub_left(phi_pred_0);
    info->op_pred_pos = 0;
    info->init_pred_pos = 1;
    if (sub_l == n){
      info->increment = sub_r;
    } else if (sub_r == n){
      info->increment = sub_l;
    } else return NULL;
  } else if (phi_pred_1_op == op_Sub){
    info->operation_code = op_Sub;
    sub_r = get_Sub_right(phi_pred_1);
    sub_l = get_Sub_left(phi_pred_1);
    info->op_pred_pos = 1;
    info->init_pred_pos = 0;
    if (sub_l == n){
      info->increment = sub_r;
    } else return NULL;
  } else
    return NULL;

  /*Compute the position of the backedge. */
  if (is_backedge(get_nodes_block(n), info->op_pred_pos)){
    info->be_pos = info->op_pred_pos;
    info->op = get_Phi_pred(n, info->op_pred_pos);
    info->init = get_Phi_pred(n, info->init_pred_pos);
  }

  if (get_Block_dom_depth(get_nodes_block(info->init))  >=
      get_Block_dom_depth(get_nodes_block(n))) {
    return NULL;
  }

  return info;
}

/* from irdump.c */
const char *get_irg_dump_name(ir_graph *irg);

/**
 * Reduce a node.
 *
 * @param *srong   The node to be reduce.
 * @param *env     Free environment pointer.
 *
 * The node for reduce mus be in a loop whit *phi and *add.The *phi node muss
 * have 2 predecessors a Const and a Add node. The predecessors of Add node muss
 * be *phi and a Const node. The nodes a, b, c  muss be Const with dom_depth <
 * phi.
 */

void reduce_itervar(ir_node *itervar_phi, void *env) {
  int i;
  ir_node *strong = NULL, *cmp = NULL, *c, *cmp_const, *old_add;
  int phi_pred, strong_in_Phi = 0, cmp_in_phi = 0, out_loop_res = 1, Store_in_phi = 0;
  int op_pred, Store_in_op = 0, strong_in_op = 0;

  induct_var_info ivi;

  // This "if" finds the Phi predecessors for the node that must be reduced.
  if ((get_irn_op(itervar_phi) == op_Phi)    &&
      is_induction_variable(itervar_phi, &ivi) != NULL ) {
    phi_pred = get_irn_n_outs(itervar_phi);
    ir_loop *l_itervar_phi = get_irn_loop(get_nodes_block(itervar_phi));
    for (i = 0; i < phi_pred; i++) {
      ir_node *out = get_irn_out(itervar_phi, i);
      ir_op   *out_op = get_irn_op(out);
      if (get_irn_loop(get_nodes_block(out)) != l_itervar_phi)
        out_loop_res = 0;
      if ( out_op == op_Store)
        Store_in_phi++;
      if (out_op == op_Mul) {
        strong = out;
        strong_in_Phi++;
      }else if (out_op == op_Cmp){
        cmp = out;
        cmp_in_phi++;
      }
    }

    if (strong == NULL){
      op_pred = get_irn_n_outs(ivi.op);
      for(i = 0; i < op_pred; i++){
        ir_node  *out = get_irn_out(ivi.op, i);
        ir_op *out_op = get_irn_op(out);
        if(out_op == op_Store)
          Store_in_op++;
        if(out_op == op_Mul){
          strong = out;
          strong_in_op++;
        }
      }
    }
    if (strong == NULL || (strong_in_Phi > 1) || (strong_in_op > 1)) return;

    if (get_irn_loop(get_nodes_block(strong)) != l_itervar_phi) return;

    ir_op *mul_right_op = get_irn_op(get_Mul_right(strong));
    if(mul_right_op == op_Phi || mul_right_op == op_Add || mul_right_op == op_Sub)
      c = get_Mul_left(strong);
    else
      c = get_Mul_right(strong);


    if (get_Block_dom_depth(get_nodes_block(c))  >=
        get_Block_dom_depth(get_nodes_block(itervar_phi))) return;

    if (get_opt_strength_red_verbose() && get_firm_verbosity() > 1) {
      printf("The constant of Reducing node is: "); DDMN(c);
      printf("The Phi node is"); DDMN(itervar_phi);
      printf("Reducing node: "); DDMN(strong);
      printf("  iter var is  "); DDMN(ivi.op);
      printf("  in graph     "); DDMG(current_ir_graph);
    }

    ir_node *inc , *init , *new_phi, *in[2], *new_op = NULL, *block_init;
    ir_node *block_inc, *new_add, *symconst;

    ir_node *init_block      = get_nodes_block(ivi.init);
    ir_node *increment_block = get_nodes_block(ivi.increment);
    ir_node *c_block         = get_nodes_block(c) ;

    if (get_Block_dom_depth(increment_block) >= get_Block_dom_depth(c_block))
      block_inc = increment_block;
    else
      block_inc = c_block;

    if (get_Block_dom_depth(init_block) >= get_Block_dom_depth(c_block))
      block_init = init_block;
    else
      block_init = c_block;

    /* we will do a strength reduction */
    stat_strength_red(current_ir_graph, strong, cmp);

    /* Compute new loop invariant increment and initialization values. */
    inc  = new_r_Mul (current_ir_graph, block_inc,  ivi.increment, c, get_irn_mode(c));
    init = new_r_Mul (current_ir_graph, block_init, ivi.init,      c, get_irn_mode(ivi.init));

    /* Generate a new basic induction variable. Break the data flow loop
       initially by using an Unknown node. */
    in[ivi.op_pred_pos]   = new_Unknown(get_irn_mode(init));
    in[ivi.init_pred_pos] = init;
    new_phi = new_r_Phi(current_ir_graph, get_nodes_block(itervar_phi), 2, in, get_irn_mode(init));
    mark_irn_visited(new_phi);

    if (ivi.operation_code == op_Add)
      new_op = new_r_Add(current_ir_graph, get_nodes_block(ivi.op), inc, new_phi,
                         get_irn_mode(inc));
    else if (ivi.operation_code == op_Sub)
      new_op = new_r_Sub(current_ir_graph, get_nodes_block(ivi.op), new_phi, inc,
                         get_irn_mode(inc));
    set_Phi_pred(new_phi, ivi.op_pred_pos, new_op);

    if(strong_in_op){
      if(get_irn_n_outs(strong) > 1) return;
      else old_add = get_irn_out(strong, 0);
      if(get_irn_op(old_add) != op_Add) return;
      if(get_Add_left(old_add) == strong)
        symconst = get_Add_right(old_add);
      else
        symconst = get_Add_left(old_add);
      new_add = new_r_Add(current_ir_graph, get_nodes_block(old_add),
                          new_op, symconst,  get_irn_mode(symconst));
      exchange(old_add, new_add);
    }

    /* Replace the use of the strength reduced value. */
    exchange(strong, new_phi);

    if (cmp == NULL || cmp_in_phi > 1 || out_loop_res == 0 || Store_in_phi != 0 || Store_in_op != 0) return;

    if (get_irn_op(get_Cmp_left(cmp)) == op_Const)
      cmp_const = get_Cmp_left(cmp);
    else
      cmp_const = get_Cmp_right(cmp);

    if (get_irn_loop(get_nodes_block(cmp)) != l_itervar_phi) return;

    if (get_opt_strength_red_verbose() && get_firm_verbosity() > 1) {
      printf("It is possibale to exchange the Cmp with a new Cmp   \n");
      printf("The constant of Cmp node is: "); DDMN(cmp_const);
      printf("The Phi node is  "); DDMN(itervar_phi);
      printf("Cmp node: "); DDMN(cmp);
      printf("  in graph     "); DDMG(current_ir_graph);
    }

    ir_node *new_cmp_const, *new_cmp, *cmp_const_block = get_nodes_block(cmp_const);

    if (get_Block_dom_depth(init_block) >= get_Block_dom_depth(cmp_const_block))
      block_init = init_block;
    else
      block_init = cmp_const_block;

    new_cmp_const = new_r_Mul (current_ir_graph, block_init, cmp_const,
                               c, get_irn_mode(ivi.init));
    new_cmp = new_r_Cmp (current_ir_graph, get_nodes_block(cmp),
                         new_phi, new_cmp_const);
    exchange(cmp, new_cmp);
  } else return;
}


/* Performs strength reduction for the passed graph. */
void reduce_strength(ir_graph *irg) {
  ir_graph *rem = current_ir_graph;
  current_ir_graph = irg;

  if (!get_optimize() || !get_opt_strength_red()) return;

  n_reduced_expressions = 0;

  /* -- Precompute some information -- */
  /* Call algorithm that computes the backedges */
  construct_cf_backedges(irg);
  /* Call algorithm that computes the dominator trees. */
  compute_doms(irg);
  /* Call algorithm that computes the out edges */
  compute_outs(irg);
  /* -- Search expressions that can be optimized -- */
  irg_walk_graph(irg, NULL, reduce_itervar, NULL);

  if (get_opt_strength_red_verbose())
    printf("Reduced %d iteration variables in graph %s.%s\n.", n_reduced_expressions,
       get_type_name(get_entity_owner(get_irg_entity(irg))),
       get_entity_name(get_irg_entity(irg)));

  current_ir_graph = rem;
}
