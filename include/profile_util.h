/*! \file profile_util.h
 *  \brief this file contains all function prototypes of the code
 */

#ifndef _PROFILE_UTIL
#define _PROFILE_UTIL

#include <cstring>
#include <string>
#include <vector>
#include <tuple>
#include <ostream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <memory>
#include <array>
#include <algorithm>

#include <sched.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/sysinfo.h>

#ifdef _MPI
#include <mpi.h>
#endif 

#ifdef _HIP
#define _GPU 
#define _GPU_API "HIP"
#define _GPU_TO_SECONDS 1.0/1000.0
#include <hip/hip_runtime.h>
#endif

#ifdef _CUDA
#define _GPU
#define _GPU_API "CUDA"
#define _GPU_TO_SECONDS 1.0/1000.0
#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#endif

#ifdef _OPENMP 
#include <omp.h>
#endif
/// \defgroup GPU related define statements 
//@{
#ifdef _HIP
#define pu_gpuMalloc hipMalloc
#define pu_gpuHostMalloc hipHostMalloc
#define pu_gpuFree hipFree
#define pu_gpuMemcpy hipMemcpy
#define pu_gpuMemcpyHostToDevice hipMemcpyHostToDevice
#define pu_gpuMemcpyDeviceToHost hipMemcpyDeviceToHost
#define pu_gpuEvent_t hipEvent_t
#define pu_gpuEventCreate hipEventCreate
#define pu_gpuEventDestroy hipEventDestroy
#define pu_gpuEventRecord hipEventRecord
#define pu_gpuEventSynchronize hipEventSynchronize
#define pu_gpuEventElapsedTime hipEventElapsedTime
#define pu_gpuDeviceSynchronize hipDeviceSynchronize
#define pu_gpuGetErrorString hipGetErrorString
#define pu_gpuError_t hipError_t
#define pu_gpuErr hipErr
#define pu_gpuSuccess hipSuccess
#define pu_gpuGetDeviceCount hipGetDeviceCount
#define pu_gpuGetDevice hipGetDevice
#define pu_gpuDeviceProp_t hipDeviceProp_t
#define pu_gpuSetDevice hipSetDevice
#define pu_gpuGetDeviceProperties hipGetDeviceProperties
#define pu_gpuDeviceGetPCIBusId hipDeviceGetPCIBusId
#define pu_gpuDeviceReset hipDeviceReset
#define pu_gpuLaunchKernel(...) hipLaunchKernelGGL(__VA_ARGS__)

#define pu_gpuVisibleDevices "ROCR_VISIBLE_DEVICES"
#endif

#ifdef _CUDA

#define pu_gpuMalloc cudaMalloc
#define pu_gpuHostMalloc cudaMallocHost
#define pu_gpuFree cudaFree
#define pu_gpuMemcpy cudaMemcpy
#define pu_gpuMemcpyHostToDevice cudaMemcpyHostToDevice
#define pu_gpuMemcpyDeviceToHost cudaMemcpyDeviceToHost
#define pu_gpuEvent_t cudaEvent_t
#define pu_gpuEventCreate cudaEventCreate
#define pu_gpuEventDestroy cudaEventDestroy
#define pu_gpuEventRecord cudaEventRecord
#define pu_gpuEventSynchronize cudaEventSynchronize
#define pu_gpuEventElapsedTime cudaEventElapsedTime
#define pu_gpuDeviceSynchronize cudaDeviceSynchronize
#define pu_gpuError_t cudaError_t
#define pu_gpuErr cudaErr
#define pu_gpuSuccess cudaSuccess
#define pu_gpuGetDeviceCount cudaGetDeviceCount
#define pu_gpuGetDeviceCount cudaGetDevice
#define pu_gpuDeviceProp_t cudaDeviceProp_t
#define pu_gpuSetDevice cudaSetDevice
#define pu_gpuGetDeviceProperties cudaGetDeviceProperties
#define pu_gpuDeviceGetPCIBusId cudaDeviceGetPCIBusId
#define pu_gpuDeviceReset cudaDeviceReset
#define pu_gpuLaunchKernel cudaLaunchKernel

#define pu_gpuVisibleDevices "CUDA_VISIBLE_DEVICES"

#endif

#ifdef _GPU 
// macro for checking errors in HIP API calls
#define pu_gpuErrorCheck(call)                                                                 \
do{                                                                                         \
    pu_gpuError_t pu_gpuErr = call;                                                               \
    if(pu_gpuSuccess != pu_gpuErr){                                                               \
        std::cerr<<_GPU_API<<" error : "<<pu_gpuGetErrorString(pu_gpuErr)<<" - "<<__FILE__<<":"<<__LINE__<<std::endl; \
        exit(0);                                                                            \
    }                                                                                       \
}while(0)

#endif
//@}

namespace profiling_util {
    #ifdef _MPI
    extern MPI_Comm __comm;
    extern int __comm_rank;
    #endif

    /// function that returns a string of the time at when it is called. 
    std::string __when();
    /// function that converts the mask of thread affinity to human readable string 
    void cpuset_to_cstr(cpu_set_t *mask, char *str);
    /// reports the parallelAPI 
    /// @return string of MPI comm size and OpenMP version and max threads for given rank
    /// \todo needs to be generalized to report parallel API of code and not library
    std::string ReportParallelAPI();
    /// reports binding of MPI comm world and each ranks thread affinity 
    /// @return string of MPI comm rank and thread core affinity 
    std::string ReportBinding();
    /// reports thread affinity within a given scope, thus depends if called within OMP region 
    /// @param func function where called in code, useful to provide __func__ and __LINE
    /// @param line code line number where called
    /// @return string of thread core affinity 
    std::string ReportThreadAffinity(std::string func, std::string line);
#ifdef _MPI
    /// reports thread affinity within a given scope, thus depends if called within OMP region, MPI aware
    /// @param func function where called in code, useful to provide __func__ and __LINE
    /// @param line code line number where called
    /// @param comm MPI communicator
    /// @return string of MPI comm rank and thread core affinity 
    std::string MPIReportThreadAffinity(std::string func, std::string line, MPI_Comm &comm);
#endif

    /// run a command
    /// @param cmd string of command to run on system
    /// @return string of MPI comm rank and thread core affinity 
    std::string exec_sys_cmd(std::string cmd);

    namespace detail {

        template <int N, typename T>
        struct _fixed {
            T _val;
        };

        template <typename T, int N, typename VT>
        inline
        std::basic_ostream<T> &operator<<(std::basic_ostream<T> &os, detail::_fixed<N, VT> v)
        {
            os << std::setprecision(N) << std::fixed << v._val;
            return os;
        }

    } // namespace detail

    ///
    /// Sent to a stream object, this manipulator will print the given value with a
    /// precision of N decimal places.
    ///
    /// @param v The value to send to the stream
    ///
    template <int N, typename T>
    inline
    detail::_fixed<N, T> fixed(T v) {
        return {v};
    }

    namespace detail {

        struct _memory_amount {
            std::size_t _val;
        };

        struct _nanoseconds_amount {
            std::chrono::nanoseconds::rep _val;
        };

        template <typename T>
        inline
        std::basic_ostream<T> &operator<<(std::basic_ostream<T> &os, const detail::_memory_amount &m)
        {

            if (m._val < 1024) {
                os << m._val << " [B]";
                return os;
            }

            float v = m._val / 1024.;
            const char *suffix = " [KiB]";

            if (v > 1024) {
                v /= 1024;
                suffix = " [MiB]";
            }
            if (v > 1024) {
                v /= 1024;
                suffix = " [GiB]";
            }
            if (v > 1024) {
                v /= 1024;
                suffix = " [TiB]";
            }
            if (v > 1024) {
                v /= 1024;
                suffix = " [PiB]";
            }
            if (v > 1024) {
                v /= 1024;
                suffix = " [EiB]";
            }
            // that should be enough...

            os << fixed<3>(v) << suffix;
            return os;
        }

        template <typename T>
        inline
        std::basic_ostream<T> &operator<<(std::basic_ostream<T> &os, const detail::_nanoseconds_amount &t)
        {
            auto time = t._val;
	    float ftime = time;
            if (time < 1000) {
                os << time << " [ns]";
                return os;
            }    
	    
	    ftime = time/1000.f;
	    time /= 1000;
	    if (time < 1000) {
                os << time << " [us]";
                return os;
            }

            ftime = time / 1000.f;
            time /= 1000;
            if (time < 1000) {
                os << time << " [ms]";
                return os;
            }

            ftime = time / 1000.f;
            const char *prefix = " [s]";
            if (ftime > 60) {
                ftime /= 60;
                prefix = " [min]";
                if (ftime > 60) {
                    ftime /= 60;
                    prefix = " [h]";
                    if (ftime > 24) {
                        ftime /= 24;
                        prefix = " [d]";
                    }
                }
            }
            // that should be enough...

            os << fixed<3>(ftime) << prefix;
            return os;
        }

    } // namespace detail

    ///
    /// Sent to a stream object, this manipulator will print the given amount of
    /// memory using the correct suffix and 3 decimal places.
    ///
    /// @param v The value to send to the stream
    ///
    inline
    detail::_memory_amount memory_amount(std::size_t amount) {
        return {amount};
    }

    ///
    /// Sent to a stream object, this manipulator will print the given amount of
    /// nanoseconds using the correct suffix and 3 decimal places.
    ///
    /// @param v The value to send to the stream
    ///
    inline
    detail::_nanoseconds_amount ns_time(std::chrono::nanoseconds::rep amount) {
        return {amount};
    }

    struct memory_stats {
        std::size_t current = 0;
        std::size_t peak = 0;
        std::size_t change = 0;
    };

    struct memory_usage {
        memory_stats vm;
        memory_stats rss;
        memory_usage operator+=(const memory_usage& rhs)
        {
            this->vm.current += rhs.vm.current;
            if (this->vm.peak < rhs.vm.peak) this->vm.peak = rhs.vm.peak;
            this->vm.change += rhs.vm.change;

            this->rss.current += rhs.rss.current;
            if (this->rss.peak < rhs.rss.peak) this->vm.peak = rhs.rss.peak;
            this->rss.change += rhs.rss.change;
            return *this;
        };
    };

    struct sys_memory_stats
    {
        std::size_t total;
        std::size_t used;
        std::size_t free;
        std::size_t shared;
        std::size_t cache;
        std::size_t avail;
    };

    ///get memory usage
    memory_usage get_memory_usage();
    ///report memory usage from within a specific function/scope
    ///usage would be from within a function use 
    ///auto l=std::to_string(__LINE__); auto f = __func__; GetMemUsage(f,l);
    std::string ReportMemUsage(const std::string &f, const std::string &l);
    /// like above but also reports change relative to another sampling of memory 
    std::string ReportMemUsage(const memory_usage &prior_mem_use, const std::string &f, const std::string &l);
    /// like ReportMemUsage but also returns the mem usage 
    std::tuple<std::string, memory_usage> GetMemUsage(const std::string &f, const std::string &l);
    std::tuple<std::string, memory_usage> GetMemUsage(const memory_usage &prior_mem_use, const std::string &f, const std::string &l);
    /// Get memory usage on all hosts 
    #ifdef _MPI
    std::string MPIReportNodeMemUsage(MPI_Comm &comm, 
    const std::string &function, 
    const std::string &line_num
    );
    std::tuple<std::string, std::vector<std::string>, std::vector<memory_usage>> MPIGetNodeMemUsage(MPI_Comm &comm, 
    const std::string &function, 
    const std::string &line_num
    );
    #endif


    /// get the memory of the system using free
    sys_memory_stats get_system_memory();
    ///report memory state of the system from within a specific function/scope
    ///usage would be from within a function use 
    ///auto l=std::to_string(__LINE__); auto f = __func__; GetMemUsage(f,l);
    std::string ReportSystemMem(const std::string &f, const std::string &l);
    /// like above but also reports change relative to another sampling of memory 
    std::string ReportSystemMem(const sys_memory_stats &prior_mem_use, const std::string &f, const std::string &l);
    /// like ReportSystemMem but also returns the system memory
    std::tuple<std::string, sys_memory_stats> GetSystemMem(const std::string &f, const std::string &l);
    std::tuple<std::string, sys_memory_stats> GetSystemMem(const sys_memory_stats &prior_mem_use, const std::string &f, const std::string &l);
    #ifdef _MPI
    std::string MPIReportNodeSystemMem(MPI_Comm &comm, const std::string &function, const std::string &line_num);
    std::tuple<std::string, std::vector<std::string>, std::vector<sys_memory_stats>> MPIGetNodeSystemMem(MPI_Comm &comm, const std::string &function, const std::string &line_num);
    #endif

    /// Timer class. 
    /// In code create an instance of time and then just a mantter of 
    /// creating an instance and then reporting it. 
    class Timer {

    public:

        using clock = std::chrono::high_resolution_clock;
        using duration = typename std::chrono::nanoseconds::rep;
        

        /*!
         * Returns whether timer has timer on device and not just host
         *
         * @return boolean on whether timer on device [us]
         */
        inline
        bool get_use_device() const {return use_device;}
        /*!
         * Returns the number of milliseconds elapsed since the reference time
         * of the timer
         *
         * @return The time elapsed since the creation of the timer, in [us]
         */
        inline
        duration get() const {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - tref).count();
        }

        /*!
         * Returns the number of milliseconds elapsed since the creation
         * of the timer
         *
         * @return The time elapsed since the creation of the timer, in [us]
         */
        inline
        duration get_creation() const {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - t0).count();
        }

        /*!
         * Returns the elapsed time on device since the reference time
         * of the device event
         *
         * @return The time elapsed since the creation of the timer, in [ns]
         */
#if defined(_GPU)
        inline void get_ref_device() {
            pu_gpuErrorCheck(pu_gpuGetDevice(&other_device_id));
            swap_device = (other_device_id != device_id);
            if (swap_device) {
                pu_gpuErrorCheck(pu_gpuSetDevice(device_id));
            }
        }
        inline void set_cur_device()  {
            if (swap_device) {
                pu_gpuErrorCheck(pu_gpuSetDevice(other_device_id));
            }
        }
        inline
        float get_on_device()  
        {
            if (!use_device) return 0;
            float telapsed = 0;
            get_ref_device();
            pu_gpuEvent_t t1_event;
            // create event 
            pu_gpuErrorCheck(pu_gpuEventCreate(&t1_event));
            pu_gpuErrorCheck(pu_gpuEventRecord(t1_event)); 
            pu_gpuErrorCheck(pu_gpuEventSynchronize(t1_event));
            pu_gpuErrorCheck(pu_gpuEventElapsedTime(&telapsed,t0_event,t1_event));
            telapsed *= _GPU_TO_SECONDS * 1e9; // to convert to nano seconds 
            pu_gpuErrorCheck(pu_gpuEventDestroy(t1_event));
            set_cur_device();
            return telapsed;
        }
#endif

        void set_ref(const std::string &new_ref)
        {
            ref = new_ref;
            t0 = clock::now();
#if defined(_GPU)
            if (use_device) {
                // clean up current event 
                pu_gpuErrorCheck(pu_gpuSetDevice(device_id));
                pu_gpuErrorCheck(pu_gpuEventDestroy(t0_event));
                // make new event on current device
                pu_gpuErrorCheck(pu_gpuGetDevice(&device_id));
                pu_gpuErrorCheck(pu_gpuEventCreate(&t0_event));
                pu_gpuErrorCheck(pu_gpuEventRecord(t0_event)); 
                pu_gpuErrorCheck(pu_gpuEventSynchronize(t0_event));
                other_device_id = device_id;
                swap_device = false;
            }
#endif
        };
        std::string get_ref() const 
        {
            return ref;
        };
#if defined(_GPU)
        std::string get_device_swap_info()
        {
            if (swap_device) {
                return "WARNING: Device swapped during timing: currently on "+ std::to_string(other_device_id) + " but measuring on " + std::to_string(device_id) + " : ";
            }
            else return "";
        };
#endif

        Timer(const std::string &f, const std::string &l, bool _use_device=true) {
            ref="@"+f+" L"+l;
            t0 = clock::now();
            tref = t0;
            use_device = _use_device;
#if defined(_GPU)
            int ndevices;
            pu_gpuErrorCheck(pu_gpuGetDeviceCount(&ndevices));
            if (ndevices == 0) use_device = false;
            if (use_device) {
                pu_gpuErrorCheck(pu_gpuGetDevice(&device_id));
                pu_gpuErrorCheck(pu_gpuEventCreate(&t0_event));
                pu_gpuErrorCheck(pu_gpuEventRecord(t0_event)); 
                pu_gpuErrorCheck(pu_gpuEventSynchronize(t0_event));
                other_device_id = device_id;
            }
#endif
        }
#if defined(_GPU)
        ~Timer()
        {
            if (use_device) {
                if (swap_device) {
                    pu_gpuErrorCheck(pu_gpuSetDevice(device_id));
                    pu_gpuErrorCheck(pu_gpuEventDestroy(t0_event));
                    pu_gpuErrorCheck(pu_gpuSetDevice(other_device_id));
                }
                else {
                    pu_gpuErrorCheck(pu_gpuEventDestroy(t0_event));
                }
            }
        }
#endif

    private:
        clock::time_point t0;
        clock::time_point tref;
        std::string ref;
        bool use_device = true;
#if defined(_GPU)
        pu_gpuEvent_t t0_event;
        // store the device on which event is recorded
        // and whether timer called on another device
        int device_id, other_device_id;
        bool swap_device = false;
#endif
    };

    /// get the time taken between some reference time (which defaults to creation of timer )
    /// and current call
    std::string ReportTimeTaken(Timer &t, const std::string &f, const std::string &l);
    float GetTimeTaken(Timer &t, const std::string &f, const std::string &l);

#if defined(_GPU)
    std::string ReportTimeTakenOnDevice(Timer &t, const std::string &f, const std::string &l);
    float GetTimeTakenOnDevice(Timer &t, const std::string &f, const std::string &l);
#endif
}

/// \def logger utility definitions 
//@{
#define _where_calling_from "@"<<__func__<<" L"<<std::to_string(__LINE__)<<" "
#define _when_calling_from "("<<profiling_util::__when()<<") : "
#ifdef _MPI 
#define _MPI_calling_rank "["<<std::setw(5) << std::setfill('0')<<profiling_util::__comm_rank<<"] "<<std::setw(0)
#define _log_header _MPI_calling_rank<<_where_calling_from<<_when_calling_from
#else 
#define _log_header _where_calling_from<<_when_calling_from
#endif

//@}

/// \def gerenal logging  
//@{
#ifdef _MPI
#define MPISetLoggingComm(comm) {profiling_util::__comm = comm; MPI_Comm_rank(profiling_util::__comm, &profiling_util::__comm_rank);}
#endif
#define Logger(logger) logger<<_log_header
#define Log() std::cout<<_log_header
#define LogErr() std::cerr<<_log_header
#ifdef _OPENMP
#define LOGGING() shared(profiling_util::__comm, profiling_util::__comm_rank, std::cout)
#endif
//@}

/// \defgroup LogAffinity
/// Log thread affinity and parallelism either to std or an ostream
//@{
#define LogParallelAPI() Log()<<"\n"<<profiling_util::ReportParallelAPI()<<std::endl;
#define LogBinding() Log()<<"\n"<<profiling_util::ReportBinding()<<std::endl;
#define LogThreadAffinity() {auto __s = profiling_util::ReportThreadAffinity(__func__, std::to_string(__LINE__)); Log()<<__s;}
#define LoggerThreadAffinity(logger) {auto __s = <<profiling_util::ReportThreadAffinity(__func__, std::to_string(__LINE__)); Logger(logger)<<__s;}
#ifdef _MPI
#define MPILog0ThreadAffinity() if(profiling_util::__comm_rank == 0) LogThreadAffinity();
#define MPILogger0ThreadAffinity(logger) if(profiling_util::__comm_rank == 0) LogThreadAffinity(logger);
#define MPILogThreadAffinity() {auto __s = profiling_util::MPIReportThreadAffinity(__func__, std::to_string(__LINE__),  profiling_util::__comm); Log()<<__s;}
#define MPILoggerThreadAffinity(logger) {auto __s = profiling_util::MPIReportThreadAffinity(__func__, std::to_string(__LINE__), profiling_util::__comm); Logger(logger)<<__s;}
#define MPILog0ParallelAPI() if(profiling_util::__comm_rank == 0) Log()<<"\n"<<profiling_util::ReportParallelAPI()<<std::endl;
#define MPILog0Binding() {auto s = profiling_util::ReportBinding(); if (profiling_util::__comm_rank == 0)Log()<<"\n"<<s<<std::endl;}
#endif
//@}

/// \defgroup LogMem
/// Log memory usage either to std or an ostream
//@{
#define LogMemUsage() Log()<<profiling_util::ReportMemUsage(__func__, std::to_string(__LINE__))<<std::endl;
#define LoggerMemUsage(logger) Logger(logger)<<profiling_util::ReportMemUsage(__func__, std::to_string(__LINE__))<<std::endl;

#ifdef _MPI
#define MPILogMemUsage() Log()<<profiling_util::ReportMemUsage(__func__, std::to_string(__LINE__))<<std::endl;
#define MPILoggerMemUsage(logger) Logger(logger)<<profiling_util::ReportMemUsage(__func__, std::to_string(__LINE__))<<std::endl;
#define MPILog0NodeMemUsage() {auto __s=profiling_util::MPIReportNodeMemUsage(profiling_util::__comm, __func__, std::to_string(__LINE__)); if (profiling_util::__comm_rank == 0) {Log()<<__s<<std::endl;}}
#define MPILogger0NodeMemUsage(logger) {auto __s=profiling_util::MPIReportNodeMemUsage(profiling_util::__comm, __func__, std::to_string(__LINE__)); int __comm_rank; if (profiling_util::__comm_rank == 0) {Logger(logger)<<__s<<std::endl;}}
#endif

#define LogSystemMem() std::cout<<profiling_util::ReportSystemMem(__func__, std::to_string(__LINE__))<<std::endl;
#define LoggerSystemMem(logger) logger<<profiling_util::ReportSystemMem(__func__, std::to_string(__LINE__))<<std::endl;

#ifdef _MPI
#define MPILogSystemMem() Log()<<profiling_util::ReportSystemMem(__func__, std::to_string(__LINE__))<<std::endl;
#define MPILoggerSystemMem(logger) Logger(logger)<<profiling_util::ReportSystemMem(__func__, std::to_string(__LINE__))<<std::endl;
#define MPILog0NodeSystemMem() {auto __s=profiling_util::MPIReportNodeSystemMem(profiling_util::__comm, __func__, std::to_string(__LINE__));if (profiling_util::__comm_rank == 0){Log()<<__s<<std::endl;}}
#define MPILogger0NodeSystemMem(logger) {auto __s = profiling_util::MPIReportNodeSystemMem(profiling_util::__comm, __func__, std::to_string(__LINE__));if (profiling_util::__comm_rank == 0) {Logger(logger)<<__s<<std::endl;}}
#endif
//@}


/// \defgroup LogTime
/// Log time taken either to std or an ostream
//@{
#define LogTimeTaken(timer) Log()<<profiling_util::ReportTimeTaken(timer, __func__, std::to_string(__LINE__))<<std::endl;
#define LoggerTimeTaken(logger,timer) Logger(logger)<<profiling_util::ReportTimeTaken(timer,__func__, std::to_string(__LINE__))<<std::endl;
#define LogTimeTakenOnDevice(timer) Log()<<profiling_util::ReportTimeTakenOnDevice(timer, __func__, std::to_string(__LINE__))<<std::endl;
#define LoggerTimeTakenOnDevice(logger,timer) Logger(logger)<<profiling_util::ReportTimeTakenOnDevice(timer,__func__, std::to_string(__LINE__))<<std::endl;
#ifdef _MPI
#define MPILogTimeTaken(timer) Log()<<profiling_util::ReportTimeTaken(timer, __func__, std::to_string(__LINE__))<<std::endl;
#define MPILoggerTimeTaken(logger,timer) Logger(logger)<<profiling_util::ReportTimeTaken(timer,__func__, std::to_string(__LINE__))<<std::endl;
#define MPILogTimeTakenOnDevice(timer) Log()<<profiling_util::ReportTimeTakenOnDevice(timer, __func__, std::to_string(__LINE__))<<std::endl;
#define MPILoggerTimeTakenOnDevice(logger,timer) Logger(logger)<<profiling_util::ReportTimeTakenOnDevice(timer,__func__, std::to_string(__LINE__))<<std::endl;
#endif 
#define NewTimer() profiling_util::Timer(__func__, std::to_string(__LINE__));
#define NewTimerHostOnly() profiling_util::Timer(__func__, std::to_string(__LINE__), false);
//@}

/// \defgroup C_naming
/// Extern C interface
//@{
extern "C" {
    /// \defgropu LogAffinity_C
    //@{
    int report_parallel_api(char *str);
    #define log_parallel_api() printf("@%s L%d %s\n", __func__, __LINE__, profiling_util::ReportParallelAPI().c_str());
    int report_binding(char *str);
    #define log_binding() printf("@%s L%d %s\n", __func__, __LINE__, profiling_util::ReportBinding().c_str());
    int report_thread_affinity(char *str, char *f, int l);
    #define log_thread_affinity() printf("%s\n", profiling_util::ReportThreadAffinity(__func__, std::to_string(__LINE__)).c_str());
    #ifdef _MPI
        #define mpi_log0_thread_affinity() if(ThisTask == 0) printf("%s\n", ReportThreadAffinity(__func__, std::to_string(__LINE__)).c_str());
        #define mpi_log_thread_affinity() printf("%s\n", profiling_util::MPIReportThreadAffinity(__func__, std::to_string(__LINE__)).c_str());
        #define mpi_log0_parallel_api() if(ThisTask==0) printf("@%s L%d %s\n", __func__, __LINE__, profiling_util::ReportParallelAPI().c_str());
        #define mpi_log0_binding() if (ThisTask == 0) printf("@%s L%d %s\n", __func__, __LINE__, profiling_util::ReportBinding().c_str());
    #endif
    //@}

    /// \defgroup LogMem_C
    /// Log memory usage either to std or an ostream
    //@{
    #define log_mem_usage() printf("%s \n", profiling_util::ReportMemUsage(__func__, std::to_string(__LINE__)).c_str());
    #ifdef _MPI
    #define mpi_log_mem_usage() printf("%s %s \n", _MPI_calling_rank.c_str(), profiling_util::ReportMemUsage(__func__, std::to_string(__LINE__)).c_str());
    #endif
    //@}

    /// \defgroup LogTime_C
    /// \todo Still implementing a C friendly structure to record timing. 
    //@{
    struct timer_c {
        double t0, tref;
        char ref[2000], where[2000];
        timer_c(){
            t0 = tref = 0;
            memset(ref, 0, sizeof(ref));
            memset(where, 0, sizeof(where));
        }
        void set_timer_ref(double _t0, char _ref[2000]) {
            t0 = _t0;
            strcpy(ref,_ref);
        }
    };
    void report_time_taken(char *str, timer_c &t);

    //@}
}
//@}

#endif
