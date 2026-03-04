#pragma once

#include <cmath>
#include <iostream>

inline bool NearlyEqual(double a, double b, double eps = 1e-6) {
  return std::fabs(a - b) <= eps;
}

inline bool ReportFailure(const char* test_name, const char* message) {
  std::cerr << "[" << test_name << "] " << message << "\n";
  return false;
}
