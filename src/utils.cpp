#include "utils.h"

#include <utility>
#include <limits>

using namespace std;

pair<double, double> getLineIntersection(double x1, double y1, double x2, double y2,
                                         double x3, double y3, double x4, double y4) {
  // First line, a1x + b1y = c1
  double a1 = y2 - y1;
  double b1 = x1 - x2;
  double c1 = a1*x1 + b1*y1;

  // Second line, a2x + b2y = c2
  double a2 = y4 - y3;
  double b2 = x3 - x4;
  double c2 = a2*x3+ b2*y3;

  double determinant = a1*b2 - a2*b1;

  if (determinant == 0) {
    // The lines are parallel
    return make_pair(numeric_limits<double>::max(), numeric_limits<double>::max());
  }

  double x = (b2*c1 - b1*c2) / determinant;
  double y = (a1*c2 - a2*c1) / determinant;
  return make_pair(x, y);
}
