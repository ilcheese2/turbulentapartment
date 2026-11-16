#include <metal_stdlib> // this will not work without substantial jerryrigging :( but im leaving it here for posteriority
using namespace metal;
float __enzyme_autodiff(float (float3, float3,float));
//float __enzyme_autoiff(...);
float kernal(float3 i, float3 j, float H) {
    float dist = length(i - j);
    if (dist > H) {
        return 0.0;
    }
    float q = dist/H;
    return (1.0/(H*H*H*M_PI_F))*(1.0 - 1.5*q*q + 0.75*q*q*q);
}

float gradient(float3 i, float3 j, float H) {
    __enzyme_autodiff(kernal);
}