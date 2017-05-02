#include <iostream>
#include <numeric> // For accumulate
#include <iostream>

#include <mpi.h>
#include <legion.h>
#include <omp.h>

#include "flecsi/execution/execution.h"
#include "flecsi/data/data_client.h"
#include "flecsi/data/data.h"

#include "mpi_partition.h"
#include "physics.h"
#include "io.h"
#include "sodtube.h"

inline bool
operator==(
  const point_t& p1,
  const point_t& p2
)
{
  for(size_t i=0;i<gdimension;++i)
    if(p1[i]!=p2[i])
      return false;
  return true;
}

namespace flecsi{
namespace execution{

void
mpi_init_task(/*std::string sfilename*/int inputparticles){
  // TODO find a way to use the file name from the specialiszation_driver
  //std::cout<<sfilename<<std::endl;
  const char * filename = "../data/data_test_40.txt";

  int rank; 
  int size; 
  MPI_Comm_size(MPI_COMM_WORLD,&size);
  MPI_Comm_rank(MPI_COMM_WORLD,&rank); 
  
  int nbodies = 0;
  int totaliters = 100;
  int totalnbodies = 0;
  std::vector<std::pair<entity_key_t,body>> rbodies;
  std::array<point_t,2> range;
  mpi_ghosts_t ghosts_data;
  
  //printf("%d/%d, file %s\n",rank,size,filename);   
  // Read data from file, each process read a part of it 
  // For HDF5, no problems because we know the number of particles 
  // For txt format, work on the number of lines yet ... 
  //io::inputDataTxtRange(rbodies,nbodies,totalnbodies,rank,size,filename); 
  totalnbodies = inputparticles;
  sodtube::randomDataSodTube1D(rbodies,nbodies,totalnbodies,rank,size);
  
  // Target number of bodies for every process
  // The last one will takes more 
  //std::vector<int> targetnbodies;
  //for(int i=0;i<size;++i){
  //  if(i!=size-1){
  //    targetnbodies.push_back(totalnbodies/size);
  //  }else{ 
  //    targetnbodies.push_back(totalnbodies-((size-1)*(totalnbodies/size)));
  //  }
  //}

#if 0  
  // Do the research of ghost and shared 
  std::vector<body_holder*> ghosts;
  std::vector<body_holder*> shared;
  
  // In this version consider constant H 
  // TODO handle different H, problem the holder does not contain the H 
  // yet, add it or find a way 
  double smoothinglength = bodies[0]->getBody()->getSmoothinglength(); 

  // 1. Search for ghost
  // Do the tree traversal for my bodies
  // The entities that are non local are ghost 
  if(rank==0)
    std::cout<<"Ghost search";
  for(auto bi: bodies)
  {
    auto neighbors = tree.find_in_radius(bi->coordinates(),
        2*smoothinglength);
    for(auto nb: neighbors)
      if(!nb->is_local()){
        nb->setLocality(GHOST);
        ghosts.push_back(nb);
      }
  }
  if(rank==0)
    std::cout<<".done"<<std::endl;


// 2. Search for shared 
  // Search for the non local entity 
  // The entities found that are LOCAL are shared 
  if(rank==0)
    std::cout<<"Shared search";
  // Get all the entities in the tree
  auto allents = tree.entities();
  for(auto bi: allents)
  {
    // If this entity if not mine, search neighbors
    if(!bi->is_local()){
      auto neighbors = tree.find_in_radius(bi->coordinates(),
        2*smoothinglength);
      // Mark them as shared
    for(auto nb: neighbors)
      if(nb->is_local())
        nb->setLocality(SHARED);
    }
  }
  if(rank==0)
    std::cout<<".done"<<std::endl; 
  // Display current tree, with GHOSTS,SHARED,EXCL,NONLOCAL
  MPI_Barrier(MPI_COMM_WORLD);
  mpi_tree_traversal_graphviz(tree,range);
#endif


  double smoothinglength = 0.0;
  int iter = 0;
  //mpi_output_txt(rbodies,iter); 
  ++iter; 
  do
  {

    // Get the worst smothing length 
    if(rank==0)
      smoothinglength = rbodies[0].second.getSmoothinglength();

    MPI_Bcast(&smoothinglength,1,MPI_DOUBLE,0,MPI_COMM_WORLD);


    MPI_Barrier(MPI_COMM_WORLD);
    if(rank==0)
      std::cout<<std::endl<<"#### Iteration "<<iter<<std::endl;
    MPI_Barrier(MPI_COMM_WORLD);

    mpi_compute_range(rbodies,range);

    // The bodies are loaded
    // Compute the key    
    for(auto& bi: rbodies){
      bi.first = entity_key_t(range,bi.second.coordinates());
    }
    // Check for duplicates keys, particles
    assert(rbodies.end() == 
      std::unique(rbodies.begin(),rbodies.end(),
        [](const auto& left, const auto& right){
          return left.first == right.first;
        }));

    // Apply a distributed sort algorithm 
    mpi_sort_unbalanced(rbodies,totalnbodies);
    //assert(rbodies.size() == (size_t)targetnbodies[rank]); 
    assert(rbodies.end() == std::unique(rbodies.begin(),rbodies.end(),
        [](const auto& left, const auto& right){
          return left.second.coordinates()==right.second.coordinates() &&
            left.first == right.first;
        }));

    // Generate the local tree 
    tree_topology_t tree(range[0],range[1]);

    // create the bodies holders for my local bodies
    // They will be registred as exclusive
    std::vector<body_holder*> bodies;
    for(auto& bi: rbodies){
      auto nbi = tree.make_entity(bi.second.getPosition(),&(bi.second),rank);
      // Add them in the tree
      tree.insert(nbi);
      bodies.push_back(nbi);
    }

    // Search and share the branches 
    mpi_branches_exchange_useful(tree,rbodies,range);
    //{
    //  auto tents = tree.entities();
    //  assert(tents.size() == (size_t)totalnbodies);
    //}
    
    // For test, gather the neighbors and loop over here
    mpi_compute_ghosts(tree,smoothinglength,ghosts_data);
    mpi_refresh_ghosts(tree,ghosts_data);

    //if(iter==1){
      // Display current tree, with GHOSTS,SHARED,EXCL,NONLOCAL
      //MPI_Barrier(MPI_COMM_WORLD);
      //mpi_tree_traversal_graphviz(tree,range);
    //}

    // Do the physics
    if(rank==0)
      std::cout<<"Density"<<std::flush; 
    for(auto& bi: bodies)
    {
      //tree.apply_in_radius();
      bi->getBody()->setDensity(0.0);
      tree.apply_in_radius(bi->coordinates(),2*smoothinglength,
          sodtube::computeDensityApply,bi);
      //auto ents = tree.find_in_radius(bi->coordinates(),2*smoothinglength);
      // Apply physics
      //auto vecents = ents.to_vec(); 
      //sodtube::computeDensity(bi,vecents);
    }
    if(rank==0)
      std::cout<<".done"<<std::endl;

    if(rank==0)
      std::cout<<"PressureSoundSpeed"<<std::flush; 
    for(auto& bi: bodies)
    {
      sodtube::computePressureSoundSpeed(bi);
    }
    if(rank==0)
      std::cout<<".done"<<std::endl;

    MPI_Barrier(MPI_COMM_WORLD);
    // Share data again, need the density and pressure of the ghosts 
    mpi_refresh_ghosts(tree,ghosts_data);    

    if(rank==0)
      std::cout<<"Acceleration"<<std::flush; 
    for(auto& bi: bodies)
    {
      auto ents = tree.find_in_radius(bi->coordinates(),2*smoothinglength);
      // Apply physics
      auto vecents = ents.to_vec(); 
      sodtube::computeAcceleration(bi,vecents);
    }
    if(rank==0)
      std::cout<<".done"<<std::endl;

    if(rank==0)
      std::cout<<"Viscosity"<<std::flush; 
    for(auto& bi: bodies)
    {
      auto ents = tree.find_in_radius(bi->coordinates(),2*smoothinglength);
      // Apply physics
      auto vecents = ents.to_vec(); 
      sodtube::computeViscosity(bi,vecents);
    }
    if(rank==0)
      std::cout<<".done"<<std::endl;

    if(rank==0)
      std::cout<<"MoveParticles"<<std::flush; 
    for(auto& bi: bodies)
    {
      sodtube::moveParticle(bi,range);
    }
    if(rank==0)
      std::cout<<".done"<<std::endl;

    //mpi_output_txt(rbodies,iter);

    ++iter;

  }while(iter<totaliters);
}

flecsi_register_task(mpi_init_task,mpi,index);

void 
specialization_driver(int argc, char * argv[]){
  if (argc!=2) {
    std::cerr << "Error not enough arguments\n"
        "Usage: tree <datafile>\n";
    exit(-1); 
  }

  std::cout << "In user specialization_driver" << std::endl;
  /*const char * filename = argv[1];*/
  /*std::string filename(argv[1]);
  std::cout<<filename<<std::endl;*/
  flecsi_execute_task(mpi_init_task,mpi,index,atoi(argv[1])/*,filename*/); 
} // specialization driver

void 
driver(int argc, char * argv[]){
  std::cout << "In user driver" << std::endl;
} // driver


} // namespace
} // namespace

