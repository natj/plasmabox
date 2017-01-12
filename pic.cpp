// default libraries
#include "algorithm"
#include "iomanip"
#include "iostream"
#include "fstream"
#include "string"

// boost
#include "boost/array.hpp"
#include "boost/foreach.hpp"
#include "boost/lexical_cast.hpp"
#include "boost/mpi.hpp"
#include "boost/unordered_set.hpp"

// load balancing
#include "zoltan.h"

// adaptive grid (loaded with pragmas to avoid error spam)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wmissing-braces"
#include "dccrg.hpp"
#include "dccrg_cartesian_geometry.hpp"
#pragma clang diagnostic pop


// vector calculus
#include <Eigen/Dense>

// own header files
#include "cell.hpp" // Cell class for individual grid cells
#include "comm.hpp" // Domain decomposition communications
#include "inject.hpp" // Particle initializer & injector
#include "mesh.hpp" // Mesh and field related functions
#include "field.hpp" // actual field propagators
#include "parameters.hpp" // simulation variables
#include "particles.hpp" // particle spatial & momentum pushers
#include "io.hpp" // Simulation saver


// name spaces
using namespace std;
using namespace boost;
using namespace boost::mpi;
using namespace dccrg;
using namespace Eigen;


/// rank-specific switch for MPI data transfers
int Cell::transfer_mode = Cell::INIT;


// Vector and Matrix calculus type definitions
typedef Array<double, 4, 1> Vectord4;
typedef Array<double, 4, 4> Matrixd44;
typedef Array<double, 3, 2> Matrixd32; ///< old compatibility type





typedef Parameters P;

int main(int argc, char* argv[])
{


    cout << "Starting..." << endl;

    // Start up MPI
	if (MPI_Init(&argc, &argv) != MPI_SUCCESS) {
		cerr << "Couldn't initialize MPI." << endl;
		abort();
	}
	MPI_Comm communicator = MPI_COMM_WORLD;

	int rank = 0, comm_size = 0;
	MPI_Comm_rank(communicator, &rank);
	MPI_Comm_size(communicator, &comm_size);

    // create also communicator object
    Comm comm(rank, comm_size, communicator);
    cout << rank << ": Initialized MPI..." << endl;


    if(rank == 0) {
        cout << "--------------------------------------------------" << endl;
        cout << " Simulation parameters "<< endl;
        cout << "    c: " << c << endl;
        cout << "    q: " << q << endl;
        cout << "    e: " << e << endl;
        cout << "   dt: " << P::dt << endl;
        cout << "   me: " << me << endl;
        cout << "   mp: " << mp << endl;

        cout << " ----  " << endl;

        cout << "   Nx: " << P::Nx << endl;
        cout << "   Ny: " << P::Ny << endl;
        cout << "   Nz: " << P::Nz << endl;
        cout << " xmin: " << P::grid_xmin << endl;
        cout << " xmax: " << P::grid_xmax << endl;
        cout << " ymin: " << P::grid_ymin << endl;
        cout << " ymax: " << P::grid_ymax << endl;
        cout << " zmin: " << P::grid_zmin << endl;
        cout << " zmax: " << P::grid_zmax << endl;
        cout << "--------------------------------------------------" << endl;
    } 


	// initialize Zoltan
	float zoltan_version;
	if (Zoltan_Initialize(argc, argv, &zoltan_version) != ZOLTAN_OK) {
		cerr << "Zoltan_Initialize failed" << endl;
		abort();
	}
    cout << rank << ": Initialized Zoltan..." << endl;


    // initialize grid spatial scales
	Dccrg<Cell, dccrg::Cartesian_Geometry> grid;
    dccrg::Cartesian_Geometry::Parameters geom_params;

	const std::array<uint64_t, 3> grid_length = {{P::Nx, P::Ny, P::Nz}};
	grid.initialize(grid_length, 
                    communicator, 
                    "RCB", 
                    P::N_neighb, 
                    P::max_ref_lvl, 
                    P::Nx_wrap, P::Ny_wrap, P::Nz_wrap);

    geom_params.start[0] = P::grid_xmin;
    geom_params.start[1] = P::grid_ymin;
    geom_params.start[2] = P::grid_zmin;

    geom_params.level_0_cell_length[0] = abs(P::grid_xmax - P::grid_xmin)/double(P::Nx);
    geom_params.level_0_cell_length[1] = abs(P::grid_ymax - P::grid_ymin)/double(P::Ny);
    geom_params.level_0_cell_length[2] = abs(P::grid_zmax - P::grid_zmin)/double(P::Nz);

    if (!grid.set_geometry(geom_params)) {
		cerr << __FILE__ << ":" << __LINE__ << ": Couldn't set grid geometry" << endl;
		abort();
	}


    // Neighborhoods
    //-------------------------------------------------- 
    typedef dccrg::Types<3>::neighborhood_item_t neigh_t;

    // shift with +1/+1/+1 elements
    std::vector<neigh_t> neighborhood;
    neighborhood.clear(); 
    neighborhood.push_back({{1,0,0}});
    neighborhood.push_back({{0,1,0}});
    neighborhood.push_back({{0,0,1}});

    if (!grid.add_neighborhood(CP1_SHIFT, neighborhood)) {
        std::cerr << __FILE__ << ":" << __LINE__
            << " add_neighborhood failed"
            << std::endl;
        abort();
    }

    // shift with -1/-1/-1 elements
    neighborhood.clear(); 
    neighborhood.push_back({{-1,0,0}});
    neighborhood.push_back({{0,-1,0}});
    neighborhood.push_back({{0,0,-1}});
    
    if (!grid.add_neighborhood(CM1_SHIFT, neighborhood)) {
        std::cerr << __FILE__ << ":" << __LINE__
            << " add_neighborhood failed"
            << std::endl;
        abort();
    }




    // Full negative cube (in z-ordering for efficiency)
    // TODO: currently this is done via default -1/0/+1 neighborhood;
    //       use this instead; so we can skip +1 cells
    // std::vector<neigh_t> neighborhood2 = {{ {{1,0,0}},
    //                                        {{0,1,0}},
    //                                        {{0,0,1}}
    //                                      }};




    cout << rank << ": Initialized grid..." << endl;


    //--------------------------------------------------  
    cout << rank << ": Injecting particles..." << endl;
    /* First we transfer everything to rank 0 for easy initialization. 
       Then inject particles, then load balance back using Zoltan
    */
    Injector inject(P::grid_xmin, P::grid_xmax,
                    P::grid_ymin, P::grid_ymax,
                    P::grid_zmin, P::grid_zmax
                    );


    comm.move_all_to_master(grid);

    // Maxwellian temperature
    const double vb = 2.0;

    // inject into cylindrical shape
    inject.cylinder(grid, P::Np, vb);

    comm.load_balance(grid);
    cout << rank << ": load balanced..." << endl;

    // Initialize mesh and create fields
    //-------------------------------------------------- 
    Mesh mesh;
    mesh.rank = rank;

    mesh.deposit_currents(grid);
    comm.update_ghost_zone_currents(grid);

    mesh.yee_currents(grid);
    comm.update_ghost_zone_yee_currents(grid);

    // initialize field solver 
    Field_Solver field;

    field.push_half_B(grid);
    comm.update_ghost_zone_B(grid);

    field.push_E(grid);
    comm.update_ghost_zone_E(grid);

    field.push_half_B(grid);
    comm.update_ghost_zone_B(grid);

    // TODO
    // mesh.nodal_fields(grid);

    Particle_Mover particles;

    particles.update_velocities(grid); 
    comm.update_ghost_zone_particles(grid); 

    particles.propagate(grid); 

    mesh.sort_particles_into_cells(grid); 


    // Simulation save
    //--------------------------------------------------
    Save io;
    io.rank = rank;
    io.comm_size = comm_size;
    io.save_dir = "out/";
    io.filename = "pic";
    io.init();

    io.save_grid(grid);
    io.save_particles(grid);
    io.save_fields(grid);
    io.update_master_list();
    io.step++;


    cout << "Initialized save file" << endl;

    //-------------------------------------------------- 
    // Initialized everything; now starting main loop


    #ifdef DEBUG
	const unsigned int max_steps = 2;
    #else
	const unsigned int max_steps = 50;
    #endif

    cout << "Starting particle propagation" << endl;
	for (unsigned int step = 1; step < max_steps; step++) 
    {
        cout << " step: " << step << endl;

        
        field.push_half_B(grid);
        comm.update_ghost_zone_B(grid);
    
        // update particle velocities and locations
        particles.update_velocities(grid); 
        particles.propagate(grid); 
        comm.update_ghost_zone_particles(grid); 

        mesh.sort_particles_into_cells(grid); 
    
        field.push_half_B(grid);
        comm.update_ghost_zone_B(grid);

        field.push_E(grid);
        comm.update_ghost_zone_E(grid);

        mesh.deposit_currents(grid);
        comm.update_ghost_zone_currents(grid);

        mesh.yee_currents(grid);
        comm.update_ghost_zone_yee_currents(grid);


        // apply filters



        // save step
        io.save_grid(grid);
        io.save_particles(grid);
        io.save_fields(grid);
        io.update_master_list();
        io.step++;

    }


    io.finalize();

	MPI_Finalize();

    cout << "Finalized..." << endl;

	return EXIT_SUCCESS;
}

