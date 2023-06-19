#include <iostream>
#include <vector>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <random>
#include <tuple>
#include <profile_util.h>
#include "kernels.h"
#include "common.h"

#ifdef USEOPENMP
#include <omp.h>
#endif

std::chrono::system_clock::time_point logtime;
std::time_t log_time;
char wherebuff[1000];
std::string whenbuff;

template<class T> std::vector<T> allocate_and_init_vector(unsigned long long N)
{
    std::vector<T> v(N);
    std::random_device rd; // obtain a random number from hardware
    std::mt19937 gen(rd()); // seed the generator
    std::normal_distribution<> distr(0,1);
    auto t_init = NewTimer();
    #pragma omp parallel \
    default(none) shared(v,N) firstprivate(gen,distr) \
    if (N>10000)
    {
        LogThreadAffinity();
        #pragma omp for 
        for (auto i=0;i<v.size();i++)
        {
            v[i] = distr(gen);
        }
    }
    LogTimeTaken(t_init);
    LogMemUsage();
    return v;
}


template<class T> T vector_sq_and_sum_cpu(std::vector<T> &v)
{
    T sum = 0;
    auto t1 = NewTimer();
    #pragma omp parallel \
    default(none) shared(v,sum) \
    if (v.size()>1000)
    {
        LogThreadAffinity();
        #pragma omp for reduction(+:sum) nowait 
        for (auto i=0;i<v.size();i++)
        {
            auto x = v[i];
            sum += x*x;
        }
    }
    LogTimeTaken(t1);
    t1 = NewTimer();
    T sum_serial = 0;
    for (auto &x:v) 
    {
        sum_serial += x*x;
    }
    LogTimeTaken(t1);
    std::cout<<__func__<<" "<<__LINE__<<" "<<v.size()<<" omp reduction "<<sum<<" serial sum  "<<sum_serial<<std::endl;
    return sum;
}

// allocate mem and logs time taken and memory usage
void allocate_mem_host(unsigned long long Nentries, 
    std::vector<int> &x_int, std::vector<int> &y_int, 
    std::vector<float> &x_float, std::vector<float> &y_float, 
    std::vector<double> &x_double, std::vector<double> &y_double) 
{
    auto time_mem = NewTimerHostOnly();
    std::cout<<"Allocation on host with "<<Nentries<<" requiring "<<Nentries*2*(sizeof(int)+sizeof(float)+sizeof(double))/1024./1024./1024.<<"GB"<<std::endl;
    x_int.resize(Nentries);
    y_int.resize(Nentries);
    x_float.resize(Nentries);
    y_float.resize(Nentries);
    x_double.resize(Nentries);
    y_double.resize(Nentries);
    LogMemUsage();
    LogTimeTaken(time_mem);
}

// allocate mem on gpus logs time taken
void allocate_mem_gpu(unsigned long long Nentries, 
    std::vector<int*> &x_int, 
    std::vector<int*> &y_int, 
    std::vector<float*> &x_float, 
    std::vector<float*> &y_float, 
    std::vector<double*> &x_double, 
    std::vector<double*> &y_double) 
{
    auto time_mem = NewTimerHostOnly();
    LocalLogger()<<"Allocating on GPU running with "<<Nentries<<" requiring "<<Nentries*2*(sizeof(int)+sizeof(float)+sizeof(double))/1024./1024./1024.<<"GB"<<std::endl;
    int nDevices;
    size_t nbytes;
    pu_gpuErrorCheck(pu_gpuGetDeviceCount(&nDevices));
    x_int.resize(nDevices);
    y_int.resize(nDevices);
    x_float.resize(nDevices);
    y_float.resize(nDevices);
    x_double.resize(nDevices);
    y_double.resize(nDevices);
    for (auto idev=0;idev<nDevices;idev++) {
        pu_gpuErrorCheck(pu_gpuSetDevice(idev));
        auto time_local = NewTimer();
        nbytes = Nentries * sizeof(int);
        pu_gpuErrorCheck(pu_gpuMalloc(&x_int[idev], nbytes));
        pu_gpuErrorCheck(pu_gpuMalloc(&y_int[idev], nbytes));
        nbytes = Nentries * sizeof(float);
        pu_gpuErrorCheck(pu_gpuMalloc(&x_float[idev], nbytes));
        pu_gpuErrorCheck(pu_gpuMalloc(&y_float[idev], nbytes));
        nbytes = Nentries * sizeof(double);
        pu_gpuErrorCheck(pu_gpuMalloc(&x_double[idev], nbytes));
        pu_gpuErrorCheck(pu_gpuMalloc(&y_double[idev], nbytes));
        pu_gpuErrorCheck(pu_gpuDeviceSynchronize());
        LogTimeTakenOnDevice(time_local);
    }
    LogTimeTaken(time_mem);
}

void transfer_to_gpu(unsigned long long Nentries, 
    std::vector<int> &x_int, std::vector<int> &y_int, 
    std::vector<float> &x_float, std::vector<float> &y_float, 
    std::vector<double> &x_double, std::vector<double> &y_double, 
    std::vector<int*> &x_int_gpu, 
    std::vector<int*> &y_int_gpu, 
    std::vector<float*> &x_float_gpu, 
    std::vector<float*> &y_float_gpu, 
    std::vector<double*> &x_double_gpu, 
    std::vector<double*> &y_double_gpu
    ) 
{
    LocalLogger()<<" Transfer data to GPU"<<std::endl;
    auto time_mem = NewTimerHostOnly();
    int nDevices;
    size_t nbytes;
    pu_gpuErrorCheck(pu_gpuGetDeviceCount(&nDevices));
    for (auto idev=0;idev<nDevices;idev++) {
        pu_gpuErrorCheck(pu_gpuSetDevice(idev));
        nbytes = Nentries * sizeof(int);
        pu_gpuErrorCheck(pu_gpuMemcpy(x_int_gpu[idev], x_int.data(), nbytes, pu_gpuMemcpyHostToDevice));
        pu_gpuErrorCheck(pu_gpuMemcpy(y_int_gpu[idev], y_int.data(), nbytes, pu_gpuMemcpyHostToDevice));
        nbytes = Nentries * sizeof(float);
        pu_gpuErrorCheck(pu_gpuMemcpy(x_float_gpu[idev], x_float.data(), nbytes, pu_gpuMemcpyHostToDevice));
        pu_gpuErrorCheck(pu_gpuMemcpy(y_float_gpu[idev], y_float.data(), nbytes, pu_gpuMemcpyHostToDevice));
        nbytes = Nentries * sizeof(double);
        pu_gpuErrorCheck(pu_gpuMemcpy(x_double_gpu[idev], x_double.data(), nbytes, pu_gpuMemcpyHostToDevice));
        pu_gpuErrorCheck(pu_gpuMemcpy(y_double_gpu[idev], y_double.data(), nbytes, pu_gpuMemcpyHostToDevice));
    }
    LogTimeTaken(time_mem);
}

void transfer_from_gpu(unsigned long long Nentries, 
    std::vector<int> &x_int, std::vector<int> &y_int, 
    std::vector<float> &x_float, std::vector<float> &y_float, 
    std::vector<double> &x_double, std::vector<double> &y_double, 
    std::vector<int*> &x_int_gpu, 
    std::vector<int*> &y_int_gpu, 
    std::vector<float*> &x_float_gpu, 
    std::vector<float*> &y_float_gpu, 
    std::vector<double*> &x_double_gpu, 
    std::vector<double*> &y_double_gpu
    ) 
{
    LocalLogger()<<" Transfer data from GPU"<<std::endl;
    auto time_mem = NewTimerHostOnly();
    int nDevices;
    pu_gpuErrorCheck(pu_gpuGetDeviceCount(&nDevices));
    for (auto idev=0;idev<nDevices;idev++) {
        pu_gpuErrorCheck(pu_gpuSetDevice(idev));
        auto nbytes = Nentries * sizeof(int);
        pu_gpuErrorCheck(pu_gpuMemcpy(&x_int.data()[0], x_int_gpu[idev], nbytes, pu_gpuMemcpyDeviceToHost));
        pu_gpuErrorCheck(pu_gpuMemcpy(&y_int.data()[0], y_int_gpu[idev], nbytes, pu_gpuMemcpyDeviceToHost));
        nbytes = Nentries * sizeof(float);
        pu_gpuErrorCheck(pu_gpuMemcpy(&x_float.data()[0], x_float_gpu[idev], nbytes, pu_gpuMemcpyDeviceToHost));
        pu_gpuErrorCheck(pu_gpuMemcpy(&y_float.data()[0], y_float_gpu[idev], nbytes, pu_gpuMemcpyDeviceToHost));
        nbytes = Nentries * sizeof(double);
        pu_gpuErrorCheck(pu_gpuMemcpy(&x_double.data()[0], x_double_gpu[idev], nbytes, pu_gpuMemcpyDeviceToHost));
        pu_gpuErrorCheck(pu_gpuMemcpy(&y_double.data()[0], y_double_gpu[idev], nbytes, pu_gpuMemcpyDeviceToHost));
    }
    LogTimeTaken(time_mem);
}


void deallocate_mem_host(std::vector<int> &x_int, std::vector<int> &y_int, 
    std::vector<float> &x_float, std::vector<float> &y_float, 
    std::vector<double> &x_double, std::vector<double> &y_double) 
{
    auto time_mem = NewTimer();
    x_int.clear();
    x_float.clear();
    x_double.clear();
    y_int.clear();
    y_float.clear();
    y_double.clear();
    x_int.shrink_to_fit();
    x_float.shrink_to_fit();
    x_double.shrink_to_fit();
    y_int.shrink_to_fit();
    y_float.shrink_to_fit();
    y_double.shrink_to_fit();
    LogMemUsage();
    LogTimeTaken(time_mem);
}

// allocate mem on gpus logs time taken
void deallocate_mem_gpu( 
    std::vector<int*> &x_int, 
    std::vector<int*> &y_int, 
    std::vector<float*> &x_float, 
    std::vector<float*> &y_float, 
    std::vector<double*> &x_double, 
    std::vector<double*> &y_double) 
{
    auto time_mem = NewTimerHostOnly();
    int nDevices;
    pu_gpuErrorCheck(pu_gpuGetDeviceCount(&nDevices));
    for (auto idev=0;idev<nDevices;idev++) {
        pu_gpuErrorCheck(pu_gpuSetDevice(idev));
        pu_gpuErrorCheck(pu_gpuFree(x_int[idev]));
        pu_gpuErrorCheck(pu_gpuFree(y_int[idev]));
        pu_gpuErrorCheck(pu_gpuFree(x_float[idev]));
        pu_gpuErrorCheck(pu_gpuFree(y_float[idev]));
        pu_gpuErrorCheck(pu_gpuFree(x_double[idev]));
        pu_gpuErrorCheck(pu_gpuFree(y_double[idev]));
    }
    LogTimeTaken(time_mem);
}

void reset_gpu() 
{
    auto time_mem = NewTimerHostOnly();
    int nDevices;
    pu_gpuErrorCheck(pu_gpuGetDeviceCount(&nDevices));
    for (auto idev=0;idev<nDevices;idev++) {
        pu_gpuErrorCheck(pu_gpuSetDevice(idev));
        pu_gpuErrorCheck(pu_gpuDeviceSynchronize());
        pu_gpuErrorCheck(pu_gpuDeviceReset());
    }
    LogTimeTaken(time_mem);
}

int main(int argc, char **argv) {
    LogParallelAPI();
    LogBinding();

    std::vector<int> x_int, y_int;
    std::vector<float> x_float, y_float;
    std::vector<double> x_double, y_double;
    unsigned long long Nentries = 24.0*1024.0*1024.0*1024.0/8.0/6.0;
    std::vector<int*> x_int_gpu, y_int_gpu;
    std::vector<float*> x_float_gpu, y_float_gpu;
    std::vector<double*> x_double_gpu, y_double_gpu;
    if (argc == 2) Nentries = atoi(argv[1]);

    //allocate, test vectorization and deallocate
    //functions showcase use of logging time taken and mem usage
    allocate_mem_host(Nentries, x_int, y_int, x_float, y_float, x_double, y_double);
    allocate_mem_gpu(Nentries, x_int_gpu, y_int_gpu, x_float_gpu, y_float_gpu, x_double_gpu, y_double_gpu);
    transfer_to_gpu(Nentries, 
        x_int, y_int, 
        x_float, y_float, 
        x_double, y_double,
        x_int_gpu, y_int_gpu, 
        x_float_gpu, y_float_gpu, 
        x_double_gpu, y_double_gpu);
    compute_kernel1(Nentries, x_int_gpu, y_int_gpu, x_float_gpu, y_float_gpu, x_double_gpu, y_double_gpu);
    transfer_from_gpu(Nentries, 
        x_int, y_int, 
        x_float, y_float, 
        x_double, y_double,
        x_int_gpu, y_int_gpu, 
        x_float_gpu, y_float_gpu, 
        x_double_gpu, y_double_gpu);
    deallocate_mem_host(x_int, y_int, x_float, y_float, x_double, y_double);
    deallocate_mem_gpu(x_int_gpu, y_int_gpu, x_float_gpu, y_float_gpu, x_double_gpu, y_double_gpu);
    reset_gpu();
}
