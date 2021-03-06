#include<stdlib.h>
#include<stdio.h>
#include<math.h>
#include<string.h>
#include<fstream>
#include<iostream>
#include<string>
#include<map>
#include<limits.h>

#include<hdf5.h>
#include<hdf5_hl.h>

//
// Sequential OP2 function declarations
//
#include "op_seq.h"

//
// VOLNA function declarations
//
#include "simulation.hpp"
#include "paramFileParser.hpp"
#include "event.hpp"

//
// Define meta data
//
#define N_STATEVAR 4
#define MESH_DIM 2
#define N_NODESPERCELL 3
#define N_CELLSPEREDGE 2

//
//helper functions
//
#define check_hdf5_error(err) __check_hdf5_error(err, __FILE__, __LINE__)
void __check_hdf5_error(herr_t err, const char *file, const int line) {
  if (err < 0) {
    op_printf("%s(%i) : OP2_HDF5_error() Runtime API error %d.\n", file,
        line, (int) err);
    exit(-1);
  }
}

// Read event data from file. Each data is in a new line.
void read_event_data(const char *streamname, float* event_data, int ncell) {
  FILE* fp;
  fp = fopen(streamname, "r");
  if(fp == NULL) {
    op_printf("can't open file %s. Check if the file exists.\n",streamname);
    exit(-1);
  }
  float a;
  for(int i=0; i<ncell; i++) {
    if(fscanf(fp, "%e \n", &a)) {
      event_data[i] = a;
    }
  }
  if(fclose(fp) != 0) {
    op_printf("can't close file %s\n",streamname);
    exit(-1);
  }

}

void triangleIndex(float *val, const float* x, const float* y, float* nodeCoordsA, float* nodeCoordsB, float* nodeCoordsC, float* values) {
  // Return value on cell if the given point is inside the cell
  bool isInside = false;

  // First, check if the point is in the bounding box of the triangle
  // vertices (else, the algorithm is not nearly robust enough)
  float xmin = MIN(MIN(nodeCoordsA[0], nodeCoordsB[0]), nodeCoordsC[0]);
  float xmax = MAX(MAX(nodeCoordsA[0], nodeCoordsB[0]), nodeCoordsC[0]);
  float ymin = MIN(MIN(nodeCoordsA[1], nodeCoordsB[1]), nodeCoordsC[1]);
  float ymax = MAX(MAX(nodeCoordsA[1], nodeCoordsB[1]), nodeCoordsC[1]);

  if ( ( *x < xmin ) || ( *x > xmax ) ||
      ( *y < ymin ) || ( *y > ymax ) ) {
    isInside = false;
  }else{
    // Case where the point is in the bounding box. Here, if abc is not
    // Check if the Triangle vertices are clockwise or
    // counter-clockwise
    float insider = 1.0f;
    float p[2] = {*x, *y};

#define ORIENT2D(pA, pB, pC) (pA[0] - pC[0]) * (pB[1] - pC[1]) - (pA[1] - pC[1]) * (pB[0] - pC[0])
    if ( ORIENT2D(nodeCoordsA, nodeCoordsB, nodeCoordsC) > 0 ) {  // counter clockwise
      insider =  ORIENT2D( nodeCoordsA, p, nodeCoordsC);
      insider *= ORIENT2D( nodeCoordsA, nodeCoordsB, p);
      insider *= ORIENT2D( nodeCoordsB, nodeCoordsC, p);
    }
    else {      // clockwise
      insider =  ORIENT2D( nodeCoordsA, p, nodeCoordsB);
      insider *= ORIENT2D( nodeCoordsA, nodeCoordsC, p);
      insider *= ORIENT2D( nodeCoordsC, nodeCoordsB, p);
    }
    isInside = insider > 0.0f;
  }

  if ( isInside )
    *val = values[0] + values[3]; // H + Zb
}


int main(int argc, char **argv) {
  if (argc != 2) {
    printf("Wrong parameters! Please specify the VOLNA configuration "
        "script filename with the *.vln extension, "
        "e.g. ./volna2hdf5 bump.vln \n");
    exit(-1);
  }

  //
  ////////////// INIT VOLNA TO GAIN DATA IMPORT //////////////
  //

  op_printf("Importing data from VOLNA Framework ...\n");
  //  op_mesh_io_import(filename_msh, 2, 3, &x, &cell, &edge, &ecell,
  //      &bedge, &becell, &bound, &nnode, &ncell, &nedge, &nbedge);

  char const * const file = argv[1];
  spirit::file_iterator<> file_it(file);

  op_printf("Initializing original Volna code... \n");
  Simulation sim;
  ParamFileController fileParser;

  std::cerr << file << "\n";
  std::cerr << "EPSILON value: " << EPS << " \n";
  std::cerr << "Parsing parameter file... ";

  fileParser.parse(file_it, file_it.make_end(), sim);
  //sim.MeshFileName = "stlaurent_35k.msh";

  std::cerr << "Final time is : " << sim.FinalTime << std::endl;
  std::cerr << "Mesh fileName is : " << sim.MeshFileName << std::endl;
  std::cerr << "Initial value formula is : '" << sim.InitFormulas.eta
      << "'" << std::endl;
  std::cerr << "Bathymetry formula is : '"
      << sim.InitFormulas.bathymetry << "'" << std::endl;
  std::cerr << "Horizontal velocity formula is : '"
      << sim.InitFormulas.U << "'" << std::endl;
  std::cerr << "Vertical velocity formula is : '" << sim.InitFormulas.V
      << "'" << std::endl;

  int num_events = sim.events.size();
  int const MAXINT = std::numeric_limits<int>::max();

  std::vector<float> timer_start(num_events);
  std::vector<float> timer_end(num_events);
  std::vector<float> timer_step(num_events);
  std::vector<int> timer_istart(num_events);
  std::vector<int> timer_iend(num_events);
  std::vector<int> timer_istep(num_events);

  std::vector<float> event_location_x(num_events);
  std::vector<float> event_location_y(num_events);
  std::vector<int> event_post_update(num_events);
  std::vector < std::string > event_className(num_events);
  std::vector < std::string > event_formula(num_events);
  std::vector < std::string > event_streamName(num_events);
  int num_outputLocation = 0;
  std::string numbers("0123456789.");
  for (int i = 0; i < num_events; i++) {
    TimerParams t_p;
    EventParams e_p;
    (*sim.events[i]).dump(e_p);
    (*sim.events[i]).timer.dump(t_p);
    timer_start[i] = t_p.start;
    timer_step[i] = t_p.step;
    timer_end[i] = t_p.end;
    timer_start[i] = t_p.istart;
    timer_istep[i] = t_p.istep;
    timer_iend[i] = t_p.iend == MAXINT ? INT_MAX : t_p.iend;
    event_location_x[i] = e_p.location_x;
    event_location_y[i] = e_p.location_y;
    event_post_update[i] = e_p.post_update;
    event_className[i] = e_p.className;
    event_streamName[i] = e_p.streamName;
    int prev = 0;
    int fl = 0;
    std::string temp;
    int str_i = e_p.formula.find("return");
    if (str_i >= 0 && str_i < e_p.formula.length()) {
      str_i += 6;
      for (; str_i < e_p.formula.length(); str_i++) {
        temp = e_p.formula.substr(str_i, 1);
        if (numbers.find(temp) < 11 && numbers.find(temp) >= 0) {
          if (temp == ".") fl = 1;
          prev = 1;
          event_formula[i] += temp;
        } else {
          if (prev && sizeof(RealType)==4 && temp != "f") {
            if (fl)
              event_formula[i] += "f";
            else
              event_formula[i] += ".0f";
          } else if (prev && !fl) {
            event_formula[i] += ".0";
          }
          event_formula[i] += temp;
          fl = 0;
          prev = 0;
        }
      }
      FILE* fp;
      if (strcmp(e_p.className.c_str(), "InitBathymetry") == 0) {
          fp = fopen("../initBathymetry_formula.h", "w");
      } else if (strcmp(e_p.className.c_str(), "InitU") == 0) {
        fp = fopen("../initU_formula.h", "w");
      } else if (strcmp(e_p.className.c_str(), "InitV") == 0) {
        fp = fopen("../initV_formula.h", "w");
      } else if (strcmp(e_p.className.c_str(), "InitEta") == 0) {
        fp = fopen("../initEta_formula.h", "w");
      } else {
        printf("Error: Unrecognized Init event with a formula %s\n", e_p.className.c_str());
        exit(-1);
      }
      fprintf(fp, "inline void init");
      if (strcmp(e_p.className.c_str(), "InitBathymetry") == 0) {
        fprintf(fp, "Bathymetry");
      } else if (strcmp(e_p.className.c_str(), "InitU") == 0) {
        fprintf(fp, "U");
      } else if (strcmp(e_p.className.c_str(), "InitV") == 0) {
        fprintf(fp, "V");
      } else if (strcmp(e_p.className.c_str(), "InitEta") == 0) {
        fprintf(fp, "Eta");
      }
      fprintf(fp, "_formula(float *coords, float *values, const float *time) {\n  float x = coords[0];\n  float y = coords[1];\n  float t = *time;\n  float val =");
      fprintf(fp,"%s;\n", event_formula[i].c_str());
      if (strcmp(e_p.className.c_str(), "InitBathymetry") == 0) {
        fprintf(fp, "  values[3] = val;\n}");
      } else if (strcmp(e_p.className.c_str(), "InitU") == 0) {
        fprintf(fp, "  values[1] += val;\n}");
      } else if (strcmp(e_p.className.c_str(), "InitV") == 0) {
        fprintf(fp, "  values[2] += val;\n}");
      } else if (strcmp(e_p.className.c_str(), "InitEta") == 0) {
        fprintf(fp, "  values[0] += val;\n}");
      }
      if(fclose(fp)) {
        printf("can't close %s formula header file\n",e_p.className.c_str());
        exit(-1);
      } else {
        printf("Written expression to formula file (%s): %s\n", e_p.className.c_str(), event_formula[i].c_str());
      }
    } else {
      event_formula[i] = "";
    }
        //event_formula[i] = e_p.formula;
		if (strcmp(e_p.className.c_str(), "OutputLocation") == 0) num_outputLocation++;
  }
  // Initialize simulation: load mesh, calculate geometry data
  sim.init();
  op_printf("Initializing original volna code... done\n");

  //
  ////////////// INITIALIZE OP2 DATA /////////////////////
  //

  op_printf("Initializing OP2...\n");
  op_init(argc, argv, 2);

  op_printf("Importing data to OP2...\n");

  int *cell = NULL; // Node IDs of cells
  int *ecell = NULL; // Cell IDs of edge
  int *ccell = NULL; // Cell IDs of neighbouring cells
  int *cedge = NULL; // Edge IDs of cells
  float *ccent = NULL; // Cell centre vectors
  float *carea = NULL; // Cell area
  float *enorm = NULL; // Edge normal vectors. Pointing from left cell to the right???
  float *ecent = NULL; // Edge center vectors
  float *eleng = NULL; // Edge length
  int   *isBoundary = NULL;
  float *initEta = NULL;
  float **initBathymetry = NULL;
  float *x = NULL; // Node coordinates in 2D
  float *w = NULL; // Conservative variables

  // Number of nodes, cells, edges and iterations
  int nnode = 0, ncell = 0, nedge = 0;

  // Use this variable to obtain the no. nodes.
  // E.g. sim.mesh.Nodes.size() would return invalid data
  nnode = sim.mesh.NPoints;
  ncell = sim.mesh.NVolumes;
  nedge = sim.mesh.NFaces;

  printf("GMSH file data statistics: \n");
  printf("  No. nodes    = %d\n", nnode);
  printf("  No. cells    = %d\n", ncell);
  printf("Connectivity data statistics: \n");
  printf("  No. of edges = %d\n", nedge);

  // Arrays for mapping data
  cell = (int*) malloc(N_NODESPERCELL * ncell * sizeof(int));
  ecell = (int*) malloc(N_CELLSPEREDGE * nedge * sizeof(int));
  ccell = (int*) malloc(N_NODESPERCELL * ncell * sizeof(int));
  cedge = (int*) malloc(N_NODESPERCELL * ncell * sizeof(int));
  ccent = (float*) malloc(MESH_DIM * ncell * sizeof(float));
  carea = (float*) malloc(ncell * sizeof(float));
  enorm = (float*) malloc(MESH_DIM * nedge * sizeof(float));
  ecent = (float*) malloc(MESH_DIM * nedge * sizeof(float));
  eleng = (float*) malloc(nedge * sizeof(float));
  isBoundary = (int*) malloc(nedge * sizeof(int));
  initEta = (float*) malloc(ncell * sizeof(float));
//  initBathymetry = (float*) malloc(ncell * sizeof(float));
  initBathymetry = (float**) malloc(sizeof(float*));
  initBathymetry[0] = (float*) malloc(ncell*sizeof(float));
  x = (float*) malloc(MESH_DIM * nnode * sizeof(float));
  w = (float*) malloc(N_STATEVAR * ncell * sizeof(float));
  float *event_data;
  event_data = (float*) malloc(ncell*sizeof(float));
  int n_initBathymetry = 0; // Number of initBathymetry input files


  //
  ////////////// USE VOLNA FOR DATA IMPORT //////////////
  //
  int i = 0;
  // Import node coordinates
  for (i = 0; i < sim.mesh.NPoints; i++) {
    x[i * MESH_DIM] = sim.mesh.Nodes[i+1].x();
    x[i * MESH_DIM + 1] = sim.mesh.Nodes[i+1].y();
    //    std::cout << i << "  x,y,z = " << sim.mesh.Nodes[i].x() << " "
    //        << sim.mesh.Nodes[i].y() << " " << sim.mesh.Nodes[i].z()
    //        << endl;
  }

  // Boost arrays for temporarly storing mesh data
  boost::array<int, N_NODESPERCELL> vertices;
  boost::array<int, N_NODESPERCELL> neighbors;
  boost::array<int, N_NODESPERCELL> facet_ids;
  if (sim.CellValues.H.size() < sim.mesh.NVolumes) printf("SMALLER %d %d\n", sim.CellValues.H.size(), sim.mesh.NVolumes);
  for (i = 0; i < sim.mesh.NVolumes; i++) {

    vertices = sim.mesh.Cells[i].vertices();
    neighbors = sim.mesh.Cells[i].neighbors();
    facet_ids = sim.mesh.Cells[i].facets();

    cell[i * N_NODESPERCELL] = vertices[0]    -1;
    cell[i * N_NODESPERCELL + 1] = vertices[1]-1;
    cell[i * N_NODESPERCELL + 2] = vertices[2]-1;

    ccell[i * N_NODESPERCELL] = neighbors[0];
    ccell[i * N_NODESPERCELL + 1] = neighbors[1];
    ccell[i * N_NODESPERCELL + 2] = neighbors[2];

    cedge[i * N_NODESPERCELL] = facet_ids[0];
    cedge[i * N_NODESPERCELL + 1] = facet_ids[1];
    cedge[i * N_NODESPERCELL + 2] = facet_ids[2];

    ccent[i * MESH_DIM] = sim.mesh.CellCenters.x(i);
    ccent[i * MESH_DIM + 1] = sim.mesh.CellCenters.y(i);

    carea[i] = sim.mesh.CellVolumes(i);

    w[i * N_STATEVAR] = sim.CellValues.H(i);
    w[i * N_STATEVAR + 1] = sim.CellValues.U(i);
    w[i * N_STATEVAR + 2] = sim.CellValues.V(i);
    w[i * N_STATEVAR + 3] = sim.CellValues.Zb(i);

    //    std::cout << "Cell " << i << " nodes = " << vertices[0] << " "
    //        << vertices[1] << " " << vertices[2] << std::endl;
    //    std::cout << "Cell " << i << " neighbours = " << neighbors[0]
    //        << " " << neighbors[1] << " " << neighbors[2] << std::endl;
    //    std::cout << "Cell " << i << " facets  = " << facet_ids[0] << " "
    //        << facet_ids[1] << " " << facet_ids[2] << std::endl;
    //    std::cout << "Cell " << i << " center  = [ "
    //        << ccent[i * N_NODESPERCELL] << " , "
    //        << ccent[i * N_NODESPERCELL + 1] << " ]" << std::endl;
    //    std::cout << "Cell " << i << " area  = " << carea[i] << std::endl;
    //    std::cout << "Cell " << i << " w = [H u v Zb] = [ "
    //        << w[i * N_STATEVAR] << " " << w[i * N_STATEVAR + 1] << " "
    //        << w[i * N_STATEVAR + 2] << " " << w[i * N_STATEVAR + 3]
    //        << " ] " << std::endl;
  }

  // Store edge data: edge-cell map, edge normal vectors
  int leftCellId  = 0;
  int rightCellId = 0;
  for (i = 0; i < sim.mesh.NFaces; i++) {
    leftCellId  = sim.mesh.Facets[i].LeftCell();
    rightCellId = sim.mesh.Facets[i].RightCell();
    ecell[i * N_CELLSPEREDGE]     = leftCellId;
    /* If the right cell ID is -1, then the edge is a boundary edge.
     * In this case make the right cell ID identical to the left, to
     * avoid conflicts when using op_map.
     */
    if(rightCellId == -1) {
      ecell[i * N_CELLSPEREDGE + 1] = leftCellId;
      isBoundary[i] = 1;
    } else {
      ecell[i * N_CELLSPEREDGE + 1] = rightCellId;
      isBoundary[i] = 0;
    }


    enorm[i * N_CELLSPEREDGE] = sim.mesh.FacetNormals.x(i);
    enorm[i * N_CELLSPEREDGE + 1] = sim.mesh.FacetNormals.y(i);

    ecent[i * N_CELLSPEREDGE] = sim.mesh.FacetCenters.x(i);
    ecent[i * N_CELLSPEREDGE + 1] = sim.mesh.FacetCenters.y(i);

    eleng[i] = sim.mesh.FacetVolumes(i);

    //    std::cout << "Edge " << i << "   left cell = "
    //        << sim.mesh.Facets[i].LeftCell() << "   right cell = "
    //        << sim.mesh.Facets[i].RightCell() << std::endl;
    //    std::cout << "Edge " << i << "   normal vector = [ "
    //        << enorm[i * N_CELLSPEREDGE] << " , "
    //        << enorm[i * N_CELLSPEREDGE + 1] << " ]" << std::endl;
    //    std::cout << "Edge " << i << "   center vector = [ "
    //        << ecent[i * N_CELLSPEREDGE] << " , "
    //        << ecent[i * N_CELLSPEREDGE + 1] << " ]" << std::endl;
    //    std::cout << "Edge " << i << "   length =  " << eleng[i]
    //        << std::endl;
  }

  /*
   * If event data is stored in a file, import it and put in HDF5
   */
  for (unsigned int i = 0; i < event_className.size(); i++) {
    // If the file exists, read its data
    if (event_streamName[i] == "" || event_post_update[i] == 1) {
      op_printf("Event has no stream file defined to read (although it might have one to write!).\n");
    } else {
      if(strncmp(event_className[i].c_str(), "InitEta",7) == 0) {
        read_event_data(event_streamName[i].c_str(), initEta, ncell);
      }
      if(strncmp(event_className[i].c_str(), "InitBathymetry",14) == 0) {
        if(strcmp(event_streamName[i].c_str(), "") != 0) {
          free(initBathymetry[0]);
          free(initBathymetry);
          char filename[255];
          strcpy(filename, event_streamName[i].c_str());
          const char* substituteIndexPattern = "%i";
          char* pos;
          pos = strstr(filename, substituteIndexPattern);
          if(pos == NULL) {
            n_initBathymetry = 1;
            initBathymetry = (float**) malloc(sizeof(float*));
            initBathymetry[0] = (float*) malloc(ncell*sizeof(float));
            op_printf("Reading InitBathymetry from file: %s \n", filename);
            read_event_data(event_streamName[i].c_str(), initBathymetry[0], ncell);
          }
          else {

            if(timer_iend[i] != MAXINT) {
              n_initBathymetry = (timer_iend[i]-timer_istart[i])/timer_istep[i] + 1;
              op_printf("timer_iend[i] = %d \n",timer_iend[i]);
              op_printf("timer_istart[i] = %d \n",timer_istart[i]);
            } else {
              int tmp_iend = sim.FinalTime/sim.Dtmax;
              n_initBathymetry = (tmp_iend-timer_istart[i])/timer_istep[i] + 1;
              op_printf("tmp_iend = %d \n",tmp_iend);
              op_printf("timer_istart[i] = %d \n",timer_istart[i]);
            }
            op_printf("Reading InitBathymetry from %d files: \n", n_initBathymetry);


            initBathymetry = (float**) malloc(n_initBathymetry*sizeof(float*));
            for(int k=0; k < n_initBathymetry; k++) {
              initBathymetry[k] = (float*) malloc( ncell * sizeof(float));
              char substituteIndex[255];
              char tmp_filename[255];
              //          strcpy(tmp_filename, event->streamName[i].c_str());
              sprintf(substituteIndex, "%04d.txt", timer_istart[i]+k*timer_istep[i]);
              //          pos = strstr(tmp_filename, substituteIndexPattern);
              strcpy(pos, substituteIndex);
              op_printf("  %s\n", filename);
              read_event_data(filename, initBathymetry[k], ncell);
            }
          }
        }
      }
    }
  }

  //
  // Define OP2 sets
  //
  op_set nodes = op_decl_set(nnode, "nodes");
  op_set edges = op_decl_set(nedge, "edges");
  op_set cells = op_decl_set(ncell, "cells");


	op_set outputLocation = NULL;
	op_map outputLocation_map = NULL;
	op_dat outputLocation_dat = NULL;
	if (num_outputLocation) {
		float def = -1.0f*INFINITY;
		int *output_map = (int *)malloc(num_outputLocation*sizeof(int));
		float *output_dat = (float *)malloc(num_outputLocation*sizeof(float));
		outputLocation = op_decl_set(num_outputLocation, "outputLocation");
		for (int e = 0; e < ncell; e++) {
			int j = 0;
			for (int i = 0; i < event_className.size(); i++) {
				if (strcmp(event_className[i].c_str(), "OutputLocation")) continue;
				triangleIndex(&def, &event_location_x[i], &event_location_y[i], &x[2*cell[3*e]]
							, &x[2*cell[3*e+1]], &x[2*cell[3*e+2]],	&w[4*e]);
				if (def != -1.0f*INFINITY) {
					output_map[j] = e;
					def = -1.0f*INFINITY;
					printf("Location %d found in cell %d\n", j, e);
				}
				j++;
			}
		}
		outputLocation_map = op_decl_map(outputLocation, cells, 1, output_map, "outputLocation_map");
		outputLocation_dat = op_decl_dat(outputLocation, 1, "float", output_dat, "outputLocation_dat");
	}


  //
  // Define OP2 set maps
  //
  op_decl_map(cells, nodes, N_NODESPERCELL, cell,
                      "cellsToNodes");
  op_decl_map(edges, cells, N_CELLSPEREDGE, ecell,
              "edgesToCells");
  op_decl_map(cells, cells, N_NODESPERCELL, ccell,
              "cellsToCells");
  op_decl_map(cells, edges, N_NODESPERCELL, cedge,
              "cellsToEdges");
  
  //
  // Define OP2 datasets
  //
  op_decl_dat(cells, MESH_DIM, "float", ccent,
              "cellCenters");
  op_decl_dat(cells, 1, "float", carea, "cellVolumes");
  op_decl_dat(edges, MESH_DIM, "float", enorm,
              "edgeNormals");
  op_decl_dat(edges, MESH_DIM, "float", ecent,
              "edgeCenters");
  op_decl_dat(edges, 1, "float", eleng, "edgeLength");
  op_decl_dat(nodes, MESH_DIM, "float", x, "nodeCoords");
  op_decl_dat(cells, N_STATEVAR, "float", w, "values");
  op_decl_dat(edges, 1, "int", isBoundary, "isBoundary");
  op_decl_dat(cells, 1, "float", initEta, "initEta");
  if(n_initBathymetry == 0) {
    op_decl_dat(cells, 1, "float", initBathymetry[0], "initBathymetry");
  } else if(n_initBathymetry == 1) {
    op_decl_dat(cells, 1, "float", initBathymetry[0], "initBathymetry");
  } else if (n_initBathymetry > 1){
    for(int k=0; k<n_initBathymetry; k++) {
      char dat_name[255];
      // Store iniBathymetry data with sequential numbering instead of iteration step numbering
      sprintf(dat_name,"initBathymetry%d",k);
      op_decl_dat(cells, 1, "float", initBathymetry[k], dat_name);
    }
  }

  //
  // Define HDF5 filename
  //
  char *filename_h5 = (char*)malloc(strlen(file)); // gaussian_landslide.vln -->  gaussian_landslide.h5
  strcpy(filename_h5, file);
  const char* substituteIndexPattern = ".vln";
  char* pos;
  pos = strstr(filename_h5, substituteIndexPattern);
  char substituteIndex[255];
  sprintf(substituteIndex, ".h5");
  strcpy(pos, substituteIndex);
  op_printf("Writing data to HDF5 file: %s \n", filename_h5);

  //
  // Write mesh and geometry data to HDF5
  //
  op_write_hdf5(filename_h5);

  //
  // Read constants and write to HDF5
  //
  float cfl = sim.CFL; // CFL condition
  op_write_const_hdf5("CFL", 1, "float", (char *) &cfl, filename_h5);
  // Final time: as defined by Volna the end of real-time simulation
  float ftime = sim.FinalTime;
  op_write_const_hdf5("ftime", 1, "float", (char *) &ftime,
      filename_h5);
  float dtmax = sim.Dtmax; // Maximum timestep
  op_write_const_hdf5("dtmax", 1, "float", (char *) &dtmax,
      filename_h5);
  float g = 9.81; // Gravity constant
  op_write_const_hdf5("g", 1, "float", (char *) &g, filename_h5);

  //WRITING VALUES MANUALLY
  hid_t h5file;
  h5file = H5Fopen(filename_h5, H5F_ACC_RDWR, H5P_DEFAULT);
  
  const hsize_t dims = 1;

  check_hdf5_error(
      H5LTmake_dataset_float(h5file, "BoreParamsx0", 1, &dims, (float*)&sim.bore_params.x0));
  check_hdf5_error(
      H5LTmake_dataset_float(h5file, "BoreParamsHl", 1, &dims, (float*)&sim.bore_params.Hl));
  check_hdf5_error(
      H5LTmake_dataset_float(h5file, "BoreParamsul", 1, &dims, (float*)&sim.bore_params.ul));
  check_hdf5_error(
      H5LTmake_dataset_float(h5file, "BoreParamsvl", 1, &dims, (float*)&sim.bore_params.vl));
  check_hdf5_error(
      H5LTmake_dataset_float(h5file, "BoreParamsS", 1, &dims, (float*)&sim.bore_params.S));
  check_hdf5_error(
      H5LTmake_dataset_float(h5file, "GaussianLandslideParamsA", 1, &dims, (float*)&sim.gaussian_landslide_params.A));
  check_hdf5_error(
      H5LTmake_dataset_float(h5file, "GaussianLandslideParamsv", 1, &dims, (float*)&sim.gaussian_landslide_params.v));
  check_hdf5_error(
      H5LTmake_dataset_float(h5file, "GaussianLandslideParamslx", 1, &dims, (float*)&sim.gaussian_landslide_params.lx));
  check_hdf5_error(
      H5LTmake_dataset_float(h5file, "GaussianLandslideParamsly", 1, &dims, (float*)&sim.gaussian_landslide_params.ly));

  /*
   * Rectangle mesh geometry and step data (originally defined in the
   * *.vln config file)
   */
  check_hdf5_error(
      H5LTmake_dataset_int(h5file, "nx", 1, &dims, (int*)&sim.mesh.nx));
  check_hdf5_error(
      H5LTmake_dataset_int(h5file, "ny", 1, &dims, (int*)&sim.mesh.ny));
  check_hdf5_error(
      H5LTmake_dataset_float(h5file, "xmin", 1, &dims, (float*)&sim.mesh.xmin));
  check_hdf5_error(
      H5LTmake_dataset_float(h5file, "xmax", 1, &dims, (float*)&sim.mesh.xmax));
  check_hdf5_error(
      H5LTmake_dataset_float(h5file, "ymin", 1, &dims, (float*)&sim.mesh.ymin));
  check_hdf5_error(
      H5LTmake_dataset_float(h5file, "ymax", 1, &dims, (float*)&sim.mesh.ymax));

  /*
   * Put event (and init) data to HDF5
   */
  check_hdf5_error(
      H5LTmake_dataset_int(h5file, "numEvents", 1, &dims, &num_events));

  // Timer data (contained in every Event struct)
  const hsize_t num_events_hsize = num_events;
  check_hdf5_error(
      H5LTmake_dataset(h5file, "timer_start", 1, &num_events_hsize, H5T_NATIVE_FLOAT, &timer_start[0]));
  check_hdf5_error(
      H5LTmake_dataset(h5file, "timer_end", 1, &num_events_hsize, H5T_NATIVE_FLOAT, &timer_end[0]));
  check_hdf5_error(
      H5LTmake_dataset(h5file, "timer_step", 1, &num_events_hsize, H5T_NATIVE_FLOAT, &timer_step[0]));
  check_hdf5_error(
      H5LTmake_dataset(h5file, "timer_istart", 1, &num_events_hsize, H5T_NATIVE_INT, &timer_istart[0]));
  check_hdf5_error(
      H5LTmake_dataset(h5file, "timer_iend", 1, &num_events_hsize, H5T_NATIVE_INT, &timer_iend[0]));
  check_hdf5_error(
      H5LTmake_dataset(h5file, "timer_istep", 1, &num_events_hsize, H5T_NATIVE_INT, &timer_istep[0]));

  check_hdf5_error(
      H5LTmake_dataset(h5file, "event_location_x", 1, &num_events_hsize, H5T_NATIVE_FLOAT, &event_location_x[0]));
  check_hdf5_error(
      H5LTmake_dataset(h5file, "event_location_y", 1, &num_events_hsize, H5T_NATIVE_FLOAT, &event_location_y[0]));
  check_hdf5_error(
      H5LTmake_dataset(h5file, "event_post_update", 1, &num_events_hsize, H5T_NATIVE_INT, &event_post_update[0]));

  // Store event names and their value sources (formula or filename)
  char buffer[22];
  int length = 0;
  for (unsigned int i = 0; i < event_className.size(); i++) {
    memset(buffer, 0, 22);
    sprintf(buffer, "event_className%d", i);
    check_hdf5_error(
        H5LTmake_dataset_string(h5file, buffer, event_className[i].c_str()));
    length = strlen(event_className[i].c_str())+1;
    check_hdf5_error(
        H5LTset_attribute_int(h5file, buffer, "length", &length, 1));
    memset(buffer, 0, 22);
    sprintf(buffer, "event_formula%d", i);
    check_hdf5_error(
        H5LTmake_dataset_string(h5file, buffer, event_formula[i].c_str()));
    length = strlen(event_formula[i].c_str())+1;
    check_hdf5_error(
        H5LTset_attribute_int(h5file, buffer, "length", &length, 1));
    memset(buffer, 0, 22);
    
    sprintf(buffer, "event_streamName%d", i);
    check_hdf5_error(
        H5LTmake_dataset_string(h5file, buffer, event_streamName[i].c_str()));
    length = strlen(event_streamName[i].c_str())+1;
    check_hdf5_error(
        H5LTset_attribute_int(h5file, buffer, "length", &length, 1));
  }

  check_hdf5_error(H5Fclose(h5file));
  op_printf("HDF5 file written and closed successfully.\n");

  free(cell);
  free(ecell);
  free(ccell);
  free(cedge);
//  free(ccent); // Don't free ccent, it result in run-time error. WHY?
  free(carea);
//  free(enorm); // Don't free enorm, it result in run-time error. WHY?
  free(ecent);
  free(eleng);
  free(isBoundary);
  free(initEta);
  free(initBathymetry);
  free(x);
  free(w);
  free(event_data);

  op_exit();
}
