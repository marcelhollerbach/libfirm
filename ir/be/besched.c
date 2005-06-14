
#include <stdlib.h>

#include "impl.h"
#include "irprintf.h"
#include "irgwalk.h"
#include "irnode.h"
#include "debug.h"

#include "besched_t.h"
#include "besched.h"
#include "belistsched.h"

FIRM_IMPL1(sched_get_time_step, int, const ir_node *)
FIRM_IMPL1(sched_has_next, int, const ir_node *)
FIRM_IMPL1(sched_has_prev, int, const ir_node *)
FIRM_IMPL1(sched_next, ir_node *, const ir_node *)
FIRM_IMPL1(sched_prev, ir_node *, const ir_node *)
FIRM_IMPL1(sched_first, ir_node *, const ir_node *)
FIRM_IMPL1(sched_last, ir_node *, const ir_node *)
FIRM_IMPL2(sched_add_after, ir_node *, ir_node *, ir_node *)
FIRM_IMPL2(sched_add_before, ir_node *, ir_node *, ir_node *)

size_t sched_irn_data_offset = 0;

static void block_sched_dumper(ir_node *block, void *env)
{
	FILE *f = env;
	const ir_node *curr;

	ir_fprintf(f, "%n:\n", block);
	sched_foreach(block, curr) {
		ir_fprintf(f, "\t%n\n", curr);
	}
}

void be_sched_dump(FILE *f, const ir_graph *irg)
{
	irg_block_walk_graph((ir_graph *) irg, block_sched_dumper, NULL, f);
}

void be_sched_init(void)
{
	sched_irn_data_offset = register_additional_node_data(sizeof(sched_info_t));
  firm_dbg_register("be.sched");
}

void be_sched_test(void)
{
	int i, n;
	struct obstack obst;

	obstack_init(&obst);

	for(i = 0, n = get_irp_n_irgs(); i < n; ++i) {
		ir_graph *irg = get_irp_irg(i);

		list_sched(irg, trivial_selector);
		be_sched_dump(stdout, irg);
	}

	obstack_free(&obst, NULL);
}

void sched_renumber(const ir_node *block)
{
  ir_node *irn;
  sched_info_t *inf;
  sched_timestep_t step = 0;

  sched_foreach(block, irn) {
    inf = get_irn_sched_info(irn);
    inf->time_step = step;
    step += SCHED_INITIAL_GRANULARITY;
  }
}


int sched_verify(const ir_node *block)
{
  firm_dbg_module_t *dbg_sched = firm_dbg_register("be.sched");
  int res = 1;
  const ir_node *irn;
  int i, n;
  int *save_time_step;

  /* Count the number of nodes in the schedule. */
  n = 0;
  sched_foreach(block, irn)
    n++;

  save_time_step = malloc(n * sizeof(save_time_step[0]));

  i = 0;
  sched_foreach(block, irn) {
    sched_info_t *info = get_irn_sched_info(irn);
    save_time_step[i] = info->time_step;
    info->time_step = i;

    i += 1;
  }

  /*
   * Check if each relevant operand of a node is scheduled before
   * the node itself.
   */
  sched_foreach(block, irn) {
    int i, n;
    int step = sched_get_time_step(irn);

    for(i = 0, n = get_irn_arity(irn); i < n; i++) {
      ir_node *op = get_irn_n(irn, i);

      if(mode_is_datab(get_irn_mode(op))
          && get_nodes_block(op) == block
          && sched_get_time_step(op) > step) {

          DBG((dbg_sched, LEVEL_DEFAULT,
                "%n is operand of %n but scheduled after", op, irn));
          res = 0;
      }
    }
  }

  /* Check, if the time steps are correct */
  for(i = 1; i < n; ++i) {
    if(save_time_step[i] - save_time_step[i - 1] <= 0) {
      DBG((dbg_sched, LEVEL_DEFAULT,
            "position %d: time step shrinks (from %d to %d)\n",
            i, save_time_step[i - 1], save_time_step[i]));
      res = 0;
    }
  }

  /* Restore the old time steps */
  i = 0;
  sched_foreach(block, irn) {
    sched_info_t *info = get_irn_sched_info(irn);
    info->time_step = save_time_step[i++];
  }

  free(save_time_step);
  return res;
}

static void sched_verify_walker(ir_node *irn, void *data)
{
  int *res = data;
  *res &= sched_verify(irn);
}

int sched_verify_irg(ir_graph *irg)
{
  int res = 1;
  irg_block_walk_graph(irg, sched_verify_walker, NULL, &res);

  return res;
}
