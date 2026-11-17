#pragma once
double fabs(double);
float fabsf(float);
double sqrt(double);
float sqrtf(float);
double fma(double, double, double);
float fmaf(float, float, float);
double copysign(double, double);
float copysignf(float, float);
double floor(double);
double ceil(double);
double trunc(double);
double round(double);
double nearbyint(double);
double rint(double);
double fmod(double, double);
double ldexp(double, int);
float ldexpf(float, int);
#define isnan(value) __builtin_isnan(value)
#define isinf(value) __builtin_isinf(value)
#define isfinite(value) __builtin_isfinite(value)
#define signbit(value) __builtin_signbit(value)
#define INFINITY (__builtin_inff())
#define NAN (__builtin_nanf(""))
