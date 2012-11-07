//
// auto-generated by op2.m on 07-Nov-2012 09:39:44
//

// user function

#include "gatherLocations.h"


// x86 kernel function

void op_x86_gatherLocations(
  int    blockIdx,
  float *ind_arg0,
  int   *ind_map,
  short *arg_map,
  float *arg1,
  int   *ind_arg_sizes,
  int   *ind_arg_offs,
  int    block_offset,
  int   *blkmap,
  int   *offset,
  int   *nelems,
  int   *ncolors,
  int   *colors,
  int   set_size) {


  int   *ind_arg0_map, ind_arg0_size;
  float *ind_arg0_s;
  int    nelem, offset_b;

  char shared[128000];

  if (0==0) {

    // get sizes and shift pointers and direct-mapped data

    int blockId = blkmap[blockIdx + block_offset];
    nelem    = nelems[blockId];
    offset_b = offset[blockId];

    ind_arg0_size = ind_arg_sizes[0+blockId*1];

    ind_arg0_map = &ind_map[0*set_size] + ind_arg_offs[0+blockId*1];

    // set shared memory pointers

    int nbytes = 0;
    ind_arg0_s = (float *) &shared[nbytes];
  }

  // copy indirect datasets into shared memory or zero increment

  for (int n=0; n<ind_arg0_size; n++)
    for (int d=0; d<4; d++)
      ind_arg0_s[d+n*4] = ind_arg0[d+ind_arg0_map[n]*4];


  // process set elements

  for (int n=0; n<nelem; n++) {

    // user-supplied kernel call


    gatherLocations(  ind_arg0_s+arg_map[0*set_size+n+offset_b]*4,
                      arg1+(n+offset_b)*1 );
  }

}


// host stub function

void op_par_loop_gatherLocations(char const *name, op_set set,
  op_arg arg0,
  op_arg arg1 ){


  int    nargs   = 2;
  op_arg args[2];

  args[0] = arg0;
  args[1] = arg1;

  int    ninds   = 1;
  int    inds[2] = {0,-1};

  if (OP_diags>2) {
    printf(" kernel routine with indirection: gatherLocations\n");
  }

  // get plan

  #ifdef OP_PART_SIZE_14
    int part_size = OP_PART_SIZE_14;
  #else
    int part_size = OP_part_size;
  #endif

  int set_size = op_mpi_halo_exchanges(set, nargs, args);

  // initialise timers

  double cpu_t1, cpu_t2, wall_t1=0, wall_t2=0;
  op_timing_realloc(14);
  OP_kernels[14].name      = name;
  OP_kernels[14].count    += 1;

  if (set->size >0) {

    op_plan *Plan = op_plan_get(name,set,part_size,nargs,args,ninds,inds);

    op_timers_core(&cpu_t1, &wall_t1);

    // execute plan

    int block_offset = 0;

    for (int col=0; col < Plan->ncolors; col++) {
      if (col==Plan->ncolors_core) op_mpi_wait_all(nargs, args);

      int nblocks = Plan->ncolblk[col];

#pragma omp parallel for
      for (int blockIdx=0; blockIdx<nblocks; blockIdx++)
      op_x86_gatherLocations( blockIdx,
         (float *)arg0.data,
         Plan->ind_map,
         Plan->loc_map,
         (float *)arg1.data,
         Plan->ind_sizes,
         Plan->ind_offs,
         block_offset,
         Plan->blkmap,
         Plan->offset,
         Plan->nelems,
         Plan->nthrcol,
         Plan->thrcol,
         set_size);

      block_offset += nblocks;
    }

  op_timing_realloc(14);
  OP_kernels[14].transfer  += Plan->transfer;
  OP_kernels[14].transfer2 += Plan->transfer2;

  }


  // combine reduction data

  op_mpi_set_dirtybit(nargs, args);

  // update kernel record

  op_timers_core(&cpu_t2, &wall_t2);
  OP_kernels[14].time     += wall_t2 - wall_t1;
}

