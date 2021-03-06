#include "volna_common.h"
#include "EvolveValuesRK2_1.h"
#include "EvolveValuesRK2_2.h"
#include "simulation_1.h"
#include "limits.h"

#include "op_seq.h"

//these are not const, we just don't want to pass them around
float timestamp = 0.0;
int itercount = 0;

// Constants
float CFL, g, EPS;

// Store maximum elevation in global variable, for the sake of max search
op_dat currentMaxElevation;

void __check_hdf5_error(herr_t err, const char *file, const int line){
  if (err < 0) {
    printf("%s(%i) : OP2_HDF5_error() Runtime API error %d.\n", file,
        line, (int) err);
    exit(-1);
  }
}

int main(int argc, char **argv) {
  if (argc < 2) {
    printf("Wrong parameters! Please specify the OP2 HDF5 data file "
        "name, that was created with volna2hdf5 tool, e.g. volna2hdf5 script_filename.vln "
        "Use Volna configuration script filename with the *.h5 extension, "
        "e.g. ./volna script_filename.h5 \n");
    exit(-1);
  }

  op_init(argc, argv, 2);

  EPS = 1e-6; //machine epsilon, for doubles 1e-11
	
	//Some simulation parameters when using InitGaussianLandslide and InitBore
  GaussianLandslideParams gaussian_landslide_params;
  BoreParams bore_params;

	//Parameters for the rectangualr domain special case
  RectangleDomainParams rect_params;

  hid_t file;
  const char *filename_h5 = argv[1];
  file = H5Fopen(filename_h5, H5F_ACC_RDONLY, H5P_DEFAULT);

	//Read the above parameters
  check_hdf5_error(H5LTread_dataset_float(file, "BoreParamsx0", &bore_params.x0));
  check_hdf5_error(H5LTread_dataset_float(file, "BoreParamsHl", &bore_params.Hl));
  check_hdf5_error(H5LTread_dataset_float(file, "BoreParamsul", &bore_params.ul));
  check_hdf5_error(H5LTread_dataset_float(file, "BoreParamsvl", &bore_params.vl));
  check_hdf5_error(H5LTread_dataset_float(file, "BoreParamsS", &bore_params.S));
  check_hdf5_error(H5LTread_dataset_float(file, "GaussianLandslideParamsA", &gaussian_landslide_params.A));
  check_hdf5_error(H5LTread_dataset_float(file, "GaussianLandslideParamsv", &gaussian_landslide_params.v));
  check_hdf5_error(H5LTread_dataset_float(file, "GaussianLandslideParamslx", &gaussian_landslide_params.lx));
  check_hdf5_error(H5LTread_dataset_float(file, "GaussianLandslideParamsly", &gaussian_landslide_params.ly));
  check_hdf5_error(H5LTread_dataset_int(file, "nx", &rect_params.nx));
  check_hdf5_error(H5LTread_dataset_int(file, "ny", &rect_params.ny));
  check_hdf5_error(H5LTread_dataset_float(file, "xmin", &rect_params.xmin));
  check_hdf5_error(H5LTread_dataset_float(file, "xmax", &rect_params.xmax));
  check_hdf5_error(H5LTread_dataset_float(file, "ymin", &rect_params.ymin));
  check_hdf5_error(H5LTread_dataset_float(file, "ymax", &rect_params.ymax));

  int num_events = 0;
  int num_outputLocation = 0;

  check_hdf5_error(H5LTread_dataset_int(file, "numEvents", &num_events));
  std::vector<TimerParams> timers(num_events);
  std::vector<EventParams> events(num_events);
	
	//Read Event "objects" (Init and Output events) into timers and events
  read_events_hdf5(file, num_events, &timers, &events, &num_outputLocation);

  check_hdf5_error(H5Fclose(file));

  /*
   * Define OP2 sets - Read mesh and geometry data from HDF5
   */
  op_set nodes = op_decl_set_hdf5(filename_h5, "nodes");
  op_set edges = op_decl_set_hdf5(filename_h5, "edges");
  op_set cells = op_decl_set_hdf5(filename_h5, "cells");

	
  /*
   * Define OP2 set maps
   */
  op_map cellsToNodes = op_decl_map_hdf5(cells, nodes, N_NODESPERCELL,
                                  filename_h5,
                                  "cellsToNodes");
  op_map edgesToCells = op_decl_map_hdf5(edges, cells, N_CELLSPEREDGE,
                                  filename_h5,
                                  "edgesToCells");
  op_map cellsToEdges = op_decl_map_hdf5(cells, edges, N_NODESPERCELL,
                                  filename_h5,
                                  "cellsToEdges");

  //When using OutputLocation events we have already computed the cell index of the points
  //so we don't have to locate the cell every time
	op_set outputLocation = NULL;
	op_map outputLocation_map = NULL;
	op_dat outputLocation_dat = NULL;
  if (num_outputLocation) {
		outputLocation = op_decl_set_hdf5(filename_h5, "outputLocation");
		outputLocation_map = op_decl_map_hdf5(outputLocation, cells, 1,
	                                  filename_h5,
	                                  "outputLocation_map");
		outputLocation_dat = op_decl_dat_hdf5(outputLocation, 1, "float",
																					filename_h5,
																          "outputLocation_dat");
	}
	
  /*
   * Define OP2 datasets
   */
  op_dat cellCenters = op_decl_dat_hdf5(cells, MESH_DIM, "float",
                                    filename_h5,
                                    "cellCenters");

  op_dat cellVolumes = op_decl_dat_hdf5(cells, 1, "float",
                                    filename_h5,
                                    "cellVolumes");

  op_dat edgeNormals = op_decl_dat_hdf5(edges, MESH_DIM, "float",
                                    filename_h5,
                                    "edgeNormals");

  op_dat edgeLength = op_decl_dat_hdf5(edges, 1, "float",
                                    filename_h5,
                                    "edgeLength");

  op_dat nodeCoords = op_decl_dat_hdf5(nodes, MESH_DIM, "float",
                                      filename_h5,
                                      "nodeCoords");

  op_dat values = op_decl_dat_hdf5(cells, N_STATEVAR, "float",
                                    filename_h5,
                                    "values");
  op_dat isBoundary = op_decl_dat_hdf5(edges, 1, "int",
                                    filename_h5,
                                    "isBoundary");


  /*
   * Read constants from HDF5
   */
  float ftime, dtmax;
  op_get_const_hdf5("CFL", 1, "float", (char *) &CFL, filename_h5);

  // Final time: as defined by Volna the end of real-time simulation
  op_get_const_hdf5("ftime", 1, "float", (char *) &ftime, filename_h5);
  op_get_const_hdf5("dtmax", 1, "float", (char *) &dtmax, filename_h5);
  op_get_const_hdf5("g", 1, "float", (char *) &g, filename_h5);

  op_decl_const(1, "float", &CFL);
  op_decl_const(1, "float", &EPS);
  op_decl_const(1, "float", &g);

	//op_dats storing InitBathymetry and InitEta event files
  op_dat temp_initEta         = NULL;
  op_dat* temp_initBathymetry = NULL;  // Store initBathymtery in an array: there might be more input files for different timesteps
  int n_initBathymetry = 0; // Number of initBathymetry files
	
	//Read InitBathymetry and InitEta event data when they come from files
  for (unsigned int i = 0; i < events.size(); i++) {
      if (!strcmp(events[i].className.c_str(), "InitEta")) {
        if (strcmp(events[i].streamName.c_str(), ""))
          temp_initEta = op_decl_dat_hdf5(cells, 1, "float",
              filename_h5,
              "initEta");
      } else if (!strcmp(events[i].className.c_str(), "InitBathymetry")) {
        if (strcmp(events[i].streamName.c_str(), "")){
          // If one initBathymetry file is used
          if (strstr(events[i].streamName.c_str(), "%i") == NULL){
            n_initBathymetry = 1;
            temp_initBathymetry = (op_dat*) malloc(sizeof(op_dat));
            temp_initBathymetry[0] = op_decl_dat_hdf5(cells, 1, "float",
                          filename_h5,
                          "initBathymetry");
          // If multiple initBathymetry files are used
          } else{
            if(timers[i].iend != INT_MAX) {
              n_initBathymetry = (timers[i].iend - timers[i].istart) / timers[i].istep + 1;
            } else {
              int tmp_iend = ftime/dtmax;
              n_initBathymetry = (tmp_iend-timers[i].istart)/timers[i].istep + 1;
            }
            op_printf("Reading %d consecutive InitBathymetry data array... ", n_initBathymetry);
            temp_initBathymetry = (op_dat*) malloc(n_initBathymetry * sizeof(op_dat));
            for(int k=0; k<n_initBathymetry; k++) {
                char dat_name[255];
                // iniBathymetry data is stored with sequential numbering instead of iteration step numbering!
                sprintf(dat_name,"initBathymetry%d",k);
                temp_initBathymetry[k] = op_decl_dat_hdf5(cells, 1, "float",
                                filename_h5,
                                dat_name);
            }
            op_printf("done.\n");
          }
        }
      }
  }

  op_diagnostic_output();

  op_partition("PARMETIS", "GEOM", NULL, NULL, cellCenters);

  double cpu_t1, cpu_t2, wall_t1, wall_t2;
  op_timers(&cpu_t1, &wall_t1);

  //Very first Init loop
  processEvents(&timers, &events, 1/*firstTime*/, 1/*update timers*/, 0.0/*=dt*/, 1/*remove finished events*/, 2/*init loop, not pre/post*/,
                     cells, values, cellVolumes, cellCenters, nodeCoords, cellsToNodes, temp_initEta, temp_initBathymetry, n_initBathymetry, bore_params, gaussian_landslide_params, outputLocation_map, outputLocation_dat);


  //Corresponding to CellValues and tmp in Simulation::run() (simulation.hpp)
  //and in and out in EvolveValuesRK2() (timeStepper.hpp)

  float *tmp_elem = NULL;

	/*
	*  Declaring temporary dats
	*/
  op_dat values_new = op_decl_dat_temp(cells, 4, "float",tmp_elem,"values_new"); //tmp - cells - dim 4

  //temporary dats
  //EvolveValuesRK2
  op_dat midPointConservative = op_decl_dat_temp(cells, 4, "float", tmp_elem, "midPointConservative"); //temp - cells - dim 4
  op_dat inConservative = op_decl_dat_temp(cells, 4, "float", tmp_elem, "inConservative"); //temp - cells - dim 4
  op_dat outConservative = op_decl_dat_temp(cells, 4, "float", tmp_elem, "outConservative"); //temp - cells - dim 4
  op_dat midPoint = op_decl_dat_temp(cells, 4, "float", tmp_elem, "midPoint"); //temp - cells - dim 4
  //SpaceDiscretization
  op_dat bathySource = op_decl_dat_temp(edges, 2, "float", tmp_elem, "bathySource"); //temp - edges - dim 2 (left & right)
  op_dat edgeFluxes = op_decl_dat_temp(edges, 3, "float", tmp_elem, "edgeFluxes"); //temp - edges - dim 4
  //NumericalFluxes
  op_dat maxEdgeEigenvalues = op_decl_dat_temp(edges, 1, "float", tmp_elem, "maxEdgeEigenvalues"); //temp - edges - dim 1

  double timestep;

  while (timestamp < ftime) {
		//process post_update==false events (usually Init events)
    processEvents(&timers, &events, 0, 0, 0.0, 0, 0,
                  cells, values, cellVolumes, cellCenters, nodeCoords, cellsToNodes,
 									temp_initEta, temp_initBathymetry, n_initBathymetry, bore_params,
									gaussian_landslide_params, outputLocation_map, outputLocation_dat);
    
#ifdef DEBUG
    printf("Call to EvolveValuesRK2 CellValues H %g U %g V %g Zb %g\n", normcomp(values, 0), normcomp(values, 1),normcomp(values, 2),normcomp(values, 3));
#endif

    { //begin EvolveValuesRK2
      float minTimestep = 0.0;
      spaceDiscretization(values, midPointConservative, &minTimestep,
          bathySource, edgeFluxes, maxEdgeEigenvalues,
          edgeNormals, edgeLength, cellVolumes, isBoundary,
          cells, edges, edgesToCells, cellsToEdges, 0);
#ifdef DEBUG
      printf("Return of SpaceDiscretization #1 midPointConservative H %g U %g V %g Zb %g\n", normcomp(midPointConservative, 0), normcomp(midPointConservative, 1),normcomp(midPointConservative, 2),normcomp(midPointConservative, 3));
#endif
      float dT = CFL * minTimestep;

      op_par_loop(EvolveValuesRK2_1, "EvolveValuesRK2_2", cells,
          op_arg_gbl(&dT,1,"float", OP_READ),
          op_arg_dat(midPointConservative, -1, OP_ID, 4, "float", OP_RW),
          op_arg_dat(values, -1, OP_ID, 4, "float", OP_READ),
          op_arg_dat(inConservative, -1, OP_ID, 4, "float", OP_WRITE),
          op_arg_dat(midPoint, -1, OP_ID, 4, "float", OP_WRITE));

      float dummy = 0.0;

      //call to SpaceDiscretization( midPoint, outConservative, m, params, dummy_time, t );
      spaceDiscretization(midPoint, outConservative, &dummy,
          bathySource, edgeFluxes, maxEdgeEigenvalues,
          edgeNormals, edgeLength, cellVolumes, isBoundary,
          cells, edges, edgesToCells, cellsToEdges, 1);

      op_par_loop(EvolveValuesRK2_2, "EvolveValuesRK2_2", cells,
          op_arg_gbl(&dT,1,"float", OP_READ),
          op_arg_dat(outConservative, -1, OP_ID, 4, "float", OP_RW),
          op_arg_dat(inConservative, -1, OP_ID, 4, "float", OP_READ),
          op_arg_dat(midPointConservative, -1, OP_ID, 4, "float", OP_READ),
          op_arg_dat(values_new, -1, OP_ID, 4, "float", OP_WRITE));

      timestep = dT;
    } //end EvolveValuesRK2

    op_par_loop(simulation_1, "simulation_1", cells,
        op_arg_dat(values, -1, OP_ID, 4, "float", OP_WRITE),
        op_arg_dat(values_new, -1, OP_ID, 4, "float", OP_READ));

    timestep = timestep < dtmax ? timestep : dtmax;

#ifdef DEBUG
//    if (itercount%50 == 0) {
//      printf("itercount %d\n", itercount);
//      dumpme(values,0);
//      dumpme(values,1);
//      dumpme(values,2);
//      dumpme(values,3);
//      if (itercount==300) exit(-1);
//    }
    printf("New cell values %g %g %g %g\n", normcomp(values, 0), normcomp(values, 1),normcomp(values, 2),normcomp(values, 3));
    op_printf("timestep = %g\n", timestep);
    {
      int dim = values->dim;
      float *data = (float *)(values->data);
      float norm = 0.0;
      for (int i = 0; i < values->set->size; i++) {
        norm += (data[dim*i]+data[dim*i+3])*(data[dim*i]+data[dim*i+3]);
      }
      printf("H+Zb: %g\n", sqrt(norm));
    }
#endif

    itercount++;
    timestamp += timestep;

		//process post_update==true events (usually Output events)
    processEvents(&timers, &events, 0, 1, timestep, 1, 1,
                  cells, values, cellVolumes, cellCenters, nodeCoords, cellsToNodes,
									temp_initEta, temp_initBathymetry, n_initBathymetry, bore_params,
									gaussian_landslide_params, outputLocation_map, outputLocation_dat);
  }

	/*
	*	 Free temporary dats
	*/
  //simulation
  if (op_free_dat_temp(values_new) < 0)
        op_printf("Error: temporary op_dat %s cannot be removed\n",values_new->name);
  //EvolveValuesRK2
  if (op_free_dat_temp(midPointConservative) < 0)
          op_printf("Error: temporary op_dat %s cannot be removed\n",midPointConservative->name);
  if (op_free_dat_temp(inConservative) < 0)
          op_printf("Error: temporary op_dat %s cannot be removed\n",inConservative->name);
  if (op_free_dat_temp(outConservative) < 0)
          op_printf("Error: temporary op_dat %s cannot be removed\n",outConservative->name);
  if (op_free_dat_temp(midPoint) < 0)
          op_printf("Error: temporary op_dat %s cannot be removed\n",midPoint->name);
  //SpaceDiscretization
  if (op_free_dat_temp(bathySource) < 0)
          op_printf("Error: temporary op_dat %s cannot be removed\n",bathySource->name);
  if (op_free_dat_temp(edgeFluxes) < 0)
          op_printf("Error: temporary op_dat %s cannot be removed\n",edgeFluxes->name);
  //NumericalFluxes
  if (op_free_dat_temp(maxEdgeEigenvalues) < 0)
          op_printf("Error: temporary op_dat %s cannot be removed\n",maxEdgeEigenvalues->name);

  op_timers(&cpu_t2, &wall_t2);
  op_timing_output();
  op_printf("Max total runtime = \n%lf\n",wall_t2-wall_t1);

  op_exit();

  return 0;
}
