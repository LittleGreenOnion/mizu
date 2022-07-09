#pragma once

#include <utility>
#include <limits>

// @returns intersection point between two lines
std::pair<double, double> getLineIntersection(double x1, double y1, double x2, double y2,
                                              double x3, double y3, double x4, double y4);
