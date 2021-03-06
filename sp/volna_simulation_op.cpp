//
// auto-generated by op2.m on 12-Nov-2012 12:05:08
//

#include "volna_common.h"
#include "computeFluxes.h"
#include "NumericalFluxes.h"
#include "SpaceDiscretization.h"

#include "op_lib_cpp.h"
//int op2_stride = 1;
//#define OP2_STRIDE(arr, idx) arr[op2_stride*(idx)]

//
// op_par_loop declarations
//

void op_par_loop_computeFluxes(char const *, op_set,
  op_arg,
  op_arg,
  op_arg,
  op_arg,
  op_arg,
  op_arg,
  op_arg,
  op_arg );

void op_par_loop_NumericalFluxes(char const *, op_set,
  op_arg,
  op_arg,
  op_arg,
  op_arg,
  op_arg );

void op_par_loop_SpaceDiscretization(char const *, op_set,
  op_arg,
  op_arg,
  op_arg,
  op_arg,
  op_arg,
  op_arg,
  op_arg );

void spaceDiscretization(op_dat data_in, op_dat data_out, float *minTimestep,
                         op_dat bathySource, op_dat edgeFluxes, op_dat maxEdgeEigenvalues,
                         op_dat edgeNormals, op_dat edgeLength, op_dat cellVolumes, op_dat isBoundary,
                         op_set cells, op_set edges, op_map edgesToCells, op_map cellsToEdges, int most) {
  {
    *minTimestep = INFINITY;
    { //Following loops merged:
      //FacetsValuesFromCellValues
      //FacetsValuesFromCellValues
      //spaceDiscretisation_1
      //NumericalFluxes_1
      //SpaceDiscretization
      op_par_loop_computeFluxes("computeFluxes",edges,
                 op_arg_dat(data_in,0,edgesToCells,4,"float",OP_READ),
                 op_arg_dat(data_in,1,edgesToCells,4,"float",OP_READ),
                 op_arg_dat(edgeLength,-1,OP_ID,1,"float",OP_READ),
                 op_arg_dat(edgeNormals,-1,OP_ID,2,"float",OP_READ),
                 op_arg_dat(isBoundary,-1,OP_ID,1,"int",OP_READ),
                 op_arg_dat(bathySource,-1,OP_ID,2,"float",OP_WRITE),
                 op_arg_dat(edgeFluxes,-1,OP_ID,3,"float",OP_WRITE),
                 op_arg_dat(maxEdgeEigenvalues,-1,OP_ID,1,"float",OP_WRITE));

    }
#ifdef DEBUG
    printf("maxFacetEigenvalues %g edgeLen %g cellVol %g\n", normcomp(maxEdgeEigenvalues, 0), normcomp(edgeLength, 0), normcomp(cellVolumes, 0));
#endif
    op_par_loop_NumericalFluxes("NumericalFluxes",cells,
               op_arg_dat(maxEdgeEigenvalues,-3,cellsToEdges,1,"float",OP_READ),
               op_arg_dat(edgeLength,-3,cellsToEdges,1,"float",OP_READ),
               op_arg_dat(cellVolumes,-1,OP_ID,1,"float",OP_READ),
               op_arg_dat(data_out,-1,OP_ID,4,"float",OP_WRITE),
               op_arg_gbl(minTimestep,1,"float",OP_MIN));

    //end NumericalFluxes
    op_par_loop_SpaceDiscretization("SpaceDiscretization",edges,
               op_arg_dat(data_out,0,edgesToCells,4,"float",OP_INC),
               op_arg_dat(data_out,1,edgesToCells,4,"float",OP_INC),
               op_arg_dat(edgeFluxes,-1,OP_ID,3,"float",OP_READ),
               op_arg_dat(bathySource,-1,OP_ID,2,"float",OP_READ),
               op_arg_dat(edgeNormals,-1,OP_ID,2,"float",OP_READ),
               op_arg_dat(isBoundary,-1,OP_ID,1,"int",OP_READ),
               op_arg_dat(cellVolumes,-2,edgesToCells,1,"float",OP_READ));
  } //end SpaceDiscretization
}
