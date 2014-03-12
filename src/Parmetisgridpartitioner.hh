#ifndef PARMETISGRIDPARTITIONER_H
#define PARMETISGRIDPARTITIONER_H

#include <dune/common/parallel/mpihelper.hh>
#include <dune/common/exceptions.hh>

#include <algorithm>
#include <vector>

#include <parmetis.h>

template<class GridView>
struct ParMetisGridPartitioner {
#if PARMETIS_MAJOR_VERSION < 4
  typedef idxtype idx_t;
  typedef float real_t;
#endif

  typedef typename GridView::template Codim<0>::template Partition<Dune::Interior_Partition>::Iterator ElementIterator;

  enum {
    dimension = GridView::dimension
  };


  static std::vector<unsigned> initialPartition(const GridView& gv, const Dune::MPIHelper& mpihelper) {
    const unsigned num_elems = gv.size(0);

    std::vector<unsigned> part(num_elems);

    // Setup parameters for ParMETIS
    idx_t wgtflag = 0;                                  // we don't use weights
    idx_t numflag = 0;                                  // we are using C-style arrays
    idx_t ncon = 1;                                     // number of balance constraints
    idx_t ncommonnodes = 2;                             // number of nodes elements must have in common in order to be adjacent to each other
    idx_t options[4] = {0, 0, 0, 0};                    // use default values for random seed, output and coupling
    idx_t edgecut;                                      // will store number of edges cut by partition
    idx_t nparts = mpihelper.size();                    // number of parts equals number of processes
    std::vector<real_t> tpwgts(ncon*nparts, 1./nparts); // load per subdomain and weight (same load on every process)
    std::vector<real_t> ubvec(ncon, 1.05);              // weight tolerance (same weight tolerance for every weight there is)

    // The difference elmdist[i+1] - elmdist[i] is the number of nodes that are on process i
    std::vector<idx_t> elmdist(nparts+1);
    elmdist[0] = 0;
    std::fill(elmdist.begin()+1, elmdist.end(), gv.size(0)); // all elements are on process zero

    // Create and fill arrays "eptr", where eptr[i] is the number of vertices that belong to the i-th element, and
    // "eind" contains the vertex-numbers of the i-the element in eind[eptr[i]] to eind[eptr[i+1]-1]
    std::vector<idx_t> eptr, eind;
    int numVertices = 0;
    eptr.push_back(numVertices);

    for (ElementIterator eIt = gv.template begin<0, Dune::Interior_Partition>(); eIt != gv.template end<0, Dune::Interior_Partition>(); ++eIt) {
      const int curNumVertices = Dune::ReferenceElements<double, dimension>::general(eIt->type()).size(dimension);

      numVertices += curNumVertices;
      eptr.push_back(numVertices);

      for (size_t k = 0; k < curNumVertices; ++k)
	eind.push_back(gv.indexSet().subIndex(*eIt, k, dimension));
    }

    // Partition mesh using ParMETIS
    if (0 == mpihelper.rank()) {
      MPI_Comm comm = Dune::MPIHelper::getLocalCommunicator();

#if PARMETIS_MAJOR_VERSION >= 4
      const int OK =
#endif
	ParMETIS_V3_PartMeshKway(elmdist.data(), eptr.data(), eind.data(), NULL, &wgtflag, &numflag,
				 &ncon, &ncommonnodes, &nparts, tpwgts.data(), ubvec.data(),
				 options, &edgecut, reinterpret_cast<idx_t*>(part.data()), &comm);

#if PARMETIS_MAJOR_VERSION >= 4
      if (OK != METIS_OK)
	DUNE_THROW(Dune::Exception, "ParMETIS is not happy.");
#endif
    }

    return part;
  }
};

#endif
