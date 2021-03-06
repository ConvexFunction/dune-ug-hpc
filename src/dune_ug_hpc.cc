#ifdef HAVE_CONFIG_H
# include "config.h"     
#endif
#include <iostream>

#include <dune/grid/io/file/vtk/vtkwriter.hh>

#include <dune/grid/uggrid.hh>
#include <dune/grid/utility/structuredgridfactory.hh>

#include <dune/common/parallel/mpihelper.hh> // An initializer of MPI
#include <dune/common/exceptions.hh>
#include <dune/common/parametertree.hh>
#include <dune/common/parametertreeparser.hh>

#include "Ball.hh"
#include "Parmetisgridpartitioner.hh"

using namespace Dune;


// Helper function for converting numbers to strings
template<typename T>
std::string toString(const T& t) {
  return static_cast<std::ostringstream*>( &(std::ostringstream() << t) )->str();
}

// Define some units, constants and types
const int dim = 2;

typedef FieldVector<double, dim> GlobalVector;

typedef UGGrid<dim> GridType;
typedef GridType::LeafGridView GV;

typedef GV::Codim<0>::Partition<Interior_Partition>::Iterator ElementIterator;


int main(int argc, char** argv) try
{
  // Create MPIHelper instance
  MPIHelper& mpihelper = MPIHelper::instance(argc, argv);

  if (0 == mpihelper.rank()) {
    std::cout << "Using " << mpihelper.size() << " Processes." << std::endl;
  }

  // Parse parameter file
  const std::string parameterFileName = "param.ini";

  ParameterTree parameterSet;
  ParameterTreeParser::readINITree(parameterFileName, parameterSet);

  // Create ug grid from structured grid
  const std::array<unsigned, dim> n = parameterSet.get<std::array<unsigned, dim> >("n");

  const GlobalVector
    lower = parameterSet.get<GlobalVector>("lower"),
    upper = parameterSet.get<GlobalVector>("upper");

  shared_ptr<GridType> grid = StructuredGridFactory<GridType>::createSimplexGrid(lower, upper, n);

  const GV gv = grid->leafGridView();

  // Create ball
  const GlobalVector center = parameterSet.get<GlobalVector>("center");
  const double r = parameterSet.get<double>("r");

  Ball<dim> ball(center, r);

  // Refine
  const size_t steps = parameterSet.get<size_t>("steps");
  const GlobalVector stepDisplacement = parameterSet.get<GlobalVector>("stepDisplacement");

  const double epsilon = parameterSet.get<double>("epsilon");
  const int levels = parameterSet.get<int>("levels");


  // Create initial partitioning using ParMETIS
  std::vector<unsigned> part(ParMetisGridPartitioner<GV>::initialPartition(gv, mpihelper));

  // Transfer partitioning from ParMETIS to our grid
  grid->loadBalance(part, 0);

  /*
  std::vector<unsigned> part;
  grid->loadBalance();
  */

  for (size_t s = 0; s < steps; ++s) {
    std::cout << "Step " << s << " on " << mpihelper.rank() << " ..." << std::endl;

    for (int k = 0; k < levels; ++k) {
      std::cout << "   Refining level " << k << " on " << mpihelper.rank() << " ..." << std::endl;

      // select elements that are close to the sphere for grid refinement
      for (ElementIterator eIt = gv.begin<0, Interior_Partition>(); eIt != gv.end<0, Interior_Partition>(); ++eIt) {
	if (ball.distanceTo(eIt->geometry().center()) < epsilon)
	  grid->mark(1, *eIt);
      }

      // adapt grid
      grid->adapt();

      // clean up markers
      grid->postAdapt();
    }

mpihelper.getCollectiveCommunication().barrier();

    // Repartition
    real_t itr = 1000; // ratio of inter-processor communication time compared to data redistribution time
                       // high ~> minimize edge-cut and have smaller communication time during calculations
                       // low  ~> do not move elements around between processes too much and thous reduce communication time during redistribution

    part = ParMetisGridPartitioner<GV>::repartition(gv, mpihelper, itr);

    // Transfer partitioning from ParMETIS to our grid
    grid->loadBalance(part, 0);

    // Output grid
    const std::string baseOutName = "RefinedGrid_";

    VTKWriter<GV> vtkWriter(gv);
    std::vector<int> rankField(gv.size(0));
    std::fill(rankField.begin(), rankField.end(), grid->comm().rank());
    vtkWriter.addCellData(rankField,"rank");
    vtkWriter.write(baseOutName+toString(s));

    // If this is not the last step, move sphere and coarsen grid
    if (s+1 < steps) {
      // Move sphere a little
      ball.center += stepDisplacement;

      // Coarsen everything
      for (int k = 0; k < levels; ++k) {
	for (ElementIterator eIt = gv.begin<0, Interior_Partition>(); eIt != gv.end<0, Interior_Partition>(); ++eIt)
	  grid->mark(-1, *eIt);

	// adapt grid
	grid->adapt();

	// clean up markers
	grid->postAdapt();
      }
    }
  }


  return 0;
}
catch (Exception &e){
  std::cerr << "Exception: " << e << std::endl;
}
