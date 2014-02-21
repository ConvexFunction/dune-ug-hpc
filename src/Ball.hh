#ifndef BALL_H
#define BALL_H

#include <dune/common/fvector.hh>

template<int dim>
struct Ball {
  const double radius;
  const Dune::FieldVector<double, dim> center;

  Ball(const Dune::FieldVector<double, dim>& c, const double& r) : center(c), radius(r) {}

  double distanceTo(const Dune::FieldVector<double, dim>& other) const {
    return std::abs((center - other).two_norm() - radius);
  }
};

#endif
