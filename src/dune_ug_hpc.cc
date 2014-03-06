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

#include <parmetis.h>

#include "Ball.hh"

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

  if (0 == mpihelper.rank()) {
    // Setup parameters for ParMETIS
    MPI_Comm comm = Dune::MPIHelper::getCommunicator();

    const unsigned num_elems = gv.size(0);

    std::vector<unsigned> part(num_elems);

    idx_t wgtflag = 0;                       // we don't use weights
    idx_t numflag = 0;                       // we are using C-style arrays
    idx_t ncon = 1;                          // number of balance constraints
    idx_t ncommonnodes = 2;                  // number of nodes elements must have in common in order to be adjacent to each other
    idx_t options[4] = {0, 0, 0, 0};         // use default values for random seed, output and coupling
    idx_t edgecut;                           // will store number of edges cut by partition
    idx_t nparts = mpihelper.size();         // number of parts equals number of processes
    std::vector<real_t> tpwgts(ncon*nparts); // load per subdomain and weight
    std::vector<real_t> ubvec(ncon);         // weight tolerance

    std::fill(tpwgts.begin(), tpwgts.end(), 1./nparts);
    std::fill(ubvec.begin(), ubvec.end(), 1.05);

    std::vector<idx_t> elmdist(2);
    elmdist[0] = 0; elmdist[1] = gv.size(dim);

    std::vector<idx_t> eptr, eind;
    int numVertices = 0;
    eind.push_back(numVertices);

    for (ElementIterator eIt = gv.begin<0, Interior_Partition>(); eIt != gv.end<0, Interior_Partition>(); ++eIt) {
      const int curNumVertices = ReferenceElements<double, dim>::general(eIt->type()).size(dim);

      numVertices += curNumVertices;
      eind.push_back(numVertices);

      for (size_t k = 0; k < curNumVertices; ++k)
	eptr.push_back(gv.indexSet().subIndex(*eIt, k, dim));
    }

    // Partition mesh usinig ParMETIS
    const int OK = ParMETIS_V3_PartMeshKway(elmdist.data(), eptr.data(), eind.data(), NULL, &wgtflag, &numflag,
					    &ncon, &ncommonnodes, &nparts, tpwgts.data(), ubvec.data(),
					    options, &edgecut, reinterpret_cast<idx_t*>(part.data()), &comm);

    if (OK != METIS_OK)
      DUNE_THROW(Dune::Exception, "ParMETIS is not happy.");

    // Transfer partitioning from ParMETIS to our grid
    grid->loadBalance(part, 0);
  }

  // Output grid
  VTKWriter<GV> vtkWriter(gv);
  vtkWriter.write("PartitionedGrid");


  return 0;
}
catch (Exception &e){
  std::cerr << "Exception: " << e << std::endl;
}
