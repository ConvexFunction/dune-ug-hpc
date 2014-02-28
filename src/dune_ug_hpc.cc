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

using namespace Dune;


// Helper function for converting numbers to strings
template<typename T>
std::string toString(const T& t) {
  return static_cast<std::ostringstream*>( &(std::ostringstream() << t) )->str();
}

// Helper macro to execute code just on rank zero process
#define RANK0_OPERATION(CODE) do { if (0 == mpihelper.rank()) { CODE } } while(0)

// Define some units, constants and types
const int dim = 3;

const double mm = 1e-3;

typedef FieldVector<double, dim> GlobalVector;

typedef UGGrid<dim> GridType;
typedef GridType::LeafGridView GV;
typedef GV::Codim<0>::Iterator ElementIterator;


int main(int argc, char** argv) try
{
  // Create MPIHelper instance
  Dune::MPIHelper& mpihelper = Dune::MPIHelper::instance(argc, argv);

  RANK0_OPERATION(std::cout << "Using " << mpihelper.size() << " Processes." << std::endl;);

  // Parse parameter file
  const std::string parameterFileName = "param.ini";

  Dune::ParameterTree parameterSet;
  Dune::ParameterTreeParser::readINITree(parameterFileName, parameterSet);

  // Create ug grid from structured grid
  const std::array<unsigned, dim> n = parameterSet.get<std::array<unsigned, dim> >("n");

  const GlobalVector
    lower = parameterSet.get<GlobalVector>("lower"),
    upper = parameterSet.get<GlobalVector>("upper");

  shared_ptr<GridType> grid = StructuredGridFactory<GridType>::createSimplexGrid(lower, upper, n);
  const GV gv = grid->leafView();


  // Create ball
  const GlobalVector center = parameterSet.get<GlobalVector>("center");
  const double r = parameterSet.get<double>("r");

  Ball<dim> ball(center, r);

  // Refine
  const size_t steps = parameterSet.get<size_t>("steps");
  const GlobalVector stepDisplacement = parameterSet.get<GlobalVector>("stepDisplacement");

  const double epsilon = parameterSet.get<double>("epsilon");
  const int levels = parameterSet.get<int>("levels");

  for (size_t s = 0; s < steps; ++s) {
    RANK0_OPERATION(std::cout << "Step " << s << " ..." << std::endl;);

    for (int k = 0; k < levels; ++k) {
      RANK0_OPERATION(std::cout << "   Refining level " << k << " ..." << std::endl;);

      // select elements that are close to the sphere for grid refinement
      for (ElementIterator eIt = gv.begin<0>(); eIt != gv.end<0>(); ++eIt) {
	if (ball.distanceTo(eIt->geometry().center()) < epsilon)
	  grid->mark(1, *eIt);
      }

      // adapt grid
      grid->adapt();

      // clean up markers
      grid->postAdapt();
    }

    // Output grid
    mpihelper.getCollectiveCommunication().barrier();

    RANK0_OPERATION(
    const std::string baseOutName = "RefinedGrid_";
    VTKWriter<GV> vtkWriter(gv);

    vtkWriter.write(baseOutName+toString(s));
    );

    mpihelper.getCollectiveCommunication().barrier();

    // If this is not the last step, move sphere and coarsen grid
    if (s+1 < steps) {
      // Move sphere a little
      ball.center += stepDisplacement;

      // Coarsen everything
      for (ElementIterator eIt = gv.begin<0>(); eIt != gv.end<0>(); ++eIt)
	grid->mark(-levels, *eIt);

      // adapt grid
      grid->adapt();

      // clean up markers
      grid->postAdapt();
    }
  }


  return 0;
}
catch (Exception &e){
  std::cerr << "Exception: " << e << std::endl;
}
