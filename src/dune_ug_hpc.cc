#ifdef HAVE_CONFIG_H
# include "config.h"     
#endif
#include <iostream>

#include <dune/grid/io/file/vtk/vtkwriter.hh>

#include <dune/grid/uggrid.hh>
#include <dune/grid/utility/structuredgridfactory.hh>

//#include <dune/common/parallel/mpihelper.hh> // An initializer of MPI
#include <dune/common/exceptions.hh>

#include "Ball.hh"

using namespace Dune;


// Helper function for converting numbers to strings
template<typename T>
std::string toString(const T& t) {
  return static_cast<std::ostringstream*>( &(std::ostringstream() << t) )->str();
}

// Define some units, constants and types
const int dim = 3;

const double mm = 1e-3;

typedef UGGrid<dim> GridType;
typedef GridType::LeafGridView GV;
typedef GV::Codim<0>::Iterator ElementIterator;


int main(int argc, char** argv) try
{
  // Create ug grid from structured grid
  std::array<unsigned int, dim> n;
  n[0] = n[2] = 16;
  n[1] = 50;

  FieldVector<double, dim> lower;
  lower[0] = lower [1] = lower[2] = 0.0;

  FieldVector<double, dim> upper;
  upper[0] = upper[2] = 12*mm;
  upper[1] = 30*mm;

  shared_ptr<GridType> grid = StructuredGridFactory<GridType>::createSimplexGrid(lower, upper, n);
  const GV gv = grid->leafView();


  // Create ball
  FieldVector<double, dim> center;
  center[0] = center[2] = 6*mm;
  center[1] = 5*mm;

  Ball<dim> ball(center, 2*mm);


  // Refine
  const size_t steps = 4;
  FieldVector<double, dim> stepDisplacement;
  stepDisplacement[0] = stepDisplacement[2] = 0;
  stepDisplacement[1] = 1*mm;


  const double epsilon = 0.1*mm;
  const int levels = 1;

  for (size_t s = 0; s < steps; ++s) {
    std::cout << "Step " << s << " ..." << std::endl;

    for (int k = 0; k < levels; ++k) {
      std::cout << "   Refining level " << k << " ..." << std::endl;

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
    const std::string baseOutName = "RefinedGrid_";
    VTKWriter<GV> vtkWriter(gv);

    vtkWriter.write(baseOutName+toString(s));

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
