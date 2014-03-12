#ifdef HAVE_CONFIG_H
# include "config.h"     
#endif
#include <iostream>
#include <algorithm>

#include <dune/grid/io/file/vtk/vtkwriter.hh>

#include <dune/grid/uggrid.hh>
#include <dune/grid/utility/structuredgridfactory.hh>

#include <dune/common/parallel/mpihelper.hh> // An initializer of MPI
#include <dune/common/exceptions.hh>
#include <dune/common/parametertree.hh>
#include <dune/common/parametertreeparser.hh>

#include "Ball.hh"
#include "GlobalUniqueIndex.hh"
#include "Parmetisgridpartitioner.hh"


using namespace Dune;

// Define some constants and types
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

  // Get the parameters of the initial partitioning
  const GV gv = grid->leafView();

  // Create initial partitioning using ParMETIS
  std::vector<unsigned> part(ParMetisGridPartitioner<GV>::initialPartition(gv, mpihelper));

  // Transfer partitioning from ParMETIS to our grid
  grid->loadBalance(part, 0);

  // Create global index map
  GlobalUniqueIndex<GV> globalIndex(gv);

  // Output global indices
  for (size_t k = 0; k < mpihelper.size(); ++k) {
    if (mpihelper.rank() == k)
      for (ElementIterator eIt = gv.begin<0, Interior_Partition>(); eIt != gv.end<0, Interior_Partition>(); ++eIt)
	std::cout << mpihelper.rank() << ": " << globalIndex.globalIndex(*eIt) << std::endl;

    mpihelper.getCollectiveCommunication().barrier();
  }

  // Output grid
  VTKWriter<GV> vtkWriter(gv);
  vtkWriter.write("PartitionedGrid");


  return 0;
}
catch (Exception &e){
  std::cerr << "Exception: " << e << std::endl;
}
