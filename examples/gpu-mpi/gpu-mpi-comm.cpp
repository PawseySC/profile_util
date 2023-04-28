#include <iostream>
#include <vector>
#include <chrono>
#include <cmath>
#include <algorithm>
#include <random>
#include <tuple>
#include <thread>
#include <profile_util.h>


#ifdef USEOPENMP
#include <omp.h>
#endif

#include <mpi.h>


int ThisTask, NProcs;
std::chrono::system_clock::time_point logtime;
std::time_t log_time;
char wherebuff[1000];
std::string whenbuff;

#define Where() sprintf(wherebuff,"[%04d] @%sL%d ", ThisTask,__func__, __LINE__);
#define When() logtime = std::chrono::system_clock::now(); log_time = std::chrono::system_clock::to_time_t(logtime);whenbuff=std::ctime(&log_time);whenbuff.erase(std::find(whenbuff.begin(), whenbuff.end(), '\n'), whenbuff.end());
#define LocalLogger() Where();std::cout<<wherebuff<<" : " 
#define Rank0LocalLogger() Where();if (ThisTask==0) std::cout<<wherebuff<<" : " 
#define LocalLoggerWithTime() Where();When(); std::cout<<wherebuff<<" ("<<whenbuff<<") : "
#define Rank0LocalLoggerWithTime() Where();When(); if (ThisTask==0) std::cout<<wherebuff<<" ("<<whenbuff<<") : "
#define LogMPITest() Rank0LocalLoggerWithTime()<<" running "<<mpifunc<< " test"<<std::endl;
#define LogMPIBroadcaster() if (ThisTask == itask) LocalLoggerWithTime()<<" running "<<mpifunc<<" broadcasting "<<sendsize<<" GB"<<std::endl;
#define LogMPISender() LocalLoggerWithTime()<<" Running "<<mpifunc<<" sending "<<sendsize<<" GB"<<std::endl;
#define LogMPIReceiver() if (ThisTask == itask) LocalLoggerWithTime()<<" running "<<mpifunc<<std::endl;
#define LogMPIAllComm() Rank0LocalLoggerWithTime()<<" running "<<mpifunc<<" all "<<sendsize<<" GB"<<std::endl;
#define Rank0ReportMem() if (ThisTask==0) {Where();When();std::cout<<wherebuff<<" ("<<whenbuff<<") : ";LogMemUsage();std::cout<<wherebuff<<" ("<<whenbuff<<") : ";LogSystemMem();}

/// define what type of sends to use 
#define USESEND 0
#define USESSEND 1
#define USEISEND 2

struct Options
{
    /// @brief what types of communication to test
    //@{
    /// cpu to cpu communication
    bool icpu = false;
    /// gpu to gpu communication
    bool igpu = true;
    //@}
    /// root mpi task 
    int roottask = 0;
    /// other mpi task 
    int othertask = 1;
    /// how to send information task 
    int usesend = USESEND;

    /// max message size in GB
    double maxgb = 1.0;
    /// max message size in number of doubles 
    int msize = 1000;
    /// number of iterations
    int Niter = 1;
};

std::tuple<int,
    std::vector<MPI_Comm> ,
    std::vector<std::string> ,
    std::vector<int>, 
    std::vector<int>, 
    std::vector<int>>
    MPIAllocateComms()
{
    // number of comms is 2, 4, 8, ... till MPI_COMM_WORLD;
    int numcoms = std::floor(log(static_cast<double>(NProcs))/log(2.0))+1;
    int numcomsold = numcoms;
    std::vector<MPI_Comm> mpi_comms(numcoms);
    std::vector<std::string> mpi_comms_name(numcoms);
    std::vector<int> ThisLocalTask(numcoms), NProcsLocal(numcoms), NLocalComms(numcoms);

    for (auto i=0;i<=numcomsold;i++) 
    {
        NLocalComms[i] = NProcs/pow(2,i+1);
        if (NLocalComms[i] < 2) 
        {
            numcoms = i+1;
            break;
        }
        auto ThisLocalCommFlag = ThisTask % NLocalComms[i];
        MPI_Comm_split(MPI_COMM_WORLD, ThisLocalCommFlag, ThisTask, &mpi_comms[i]);
        MPI_Comm_rank(mpi_comms[i], &ThisLocalTask[i]);
        MPI_Comm_size(mpi_comms[i], &NProcsLocal[i]);
        int tasktag = ThisTask;
        MPI_Bcast(&tasktag, 1, MPI_INTEGER, 0, mpi_comms[i]);
        mpi_comms_name[i] = "Tag_" + std::to_string(static_cast<int>(pow(2,i+1)))+"_worldrank_" + std::to_string(tasktag);
    }
    mpi_comms[numcoms-1] = MPI_COMM_WORLD;
    ThisLocalTask[numcoms-1] = ThisTask;
    NProcsLocal[numcoms-1] = NProcs;
    NLocalComms[numcoms-1] = 1;
    mpi_comms_name[numcoms-1] = "Tag_world";
    ThisLocalTask.resize(numcoms);
    NLocalComms.resize(numcoms);
    NProcsLocal.resize(numcoms);
    mpi_comms.resize(numcoms);
    mpi_comms_name.resize(numcoms);

    MPI_Barrier(mpi_comms[numcoms-1]);
    return std::make_tuple(numcoms,
        std::move(mpi_comms),
        std::move(mpi_comms_name), 
        std::move(ThisLocalTask), 
        std::move(NProcsLocal), 
        std::move(NLocalComms)
        );
}

void MPIFreeComms(std::vector<MPI_Comm> &mpi_comms, std::vector<std::string> &mpi_comms_name){
    for (auto i=0;i<mpi_comms.size()-1;i++) {
        Rank0LocalLoggerWithTime()<<"Freeing "<<mpi_comms_name[i]<<std::endl;
        MPI_Comm_free(&mpi_comms[i]);
    }
}

std::vector<unsigned long long> MPISetSize(double maxgb) 
{
    std::vector<unsigned long long> sizeofsends(4);
    sizeofsends[0] = 1024.0*1024.0*1024.0*maxgb/sizeof(double);
    for (auto i=1;i<sizeofsends.size();i++) sizeofsends[i] = sizeofsends[i-1]/8;
    std::sort(sizeofsends.begin(),sizeofsends.end());
    
    if (ThisTask==0) {for (auto &x: sizeofsends) {LocalLoggerWithTime()<<"Messages of "<<x<<" elements and "<<x*sizeof(double)/1024./1024./1024.<<" GB"<<std::endl;}}
    MPI_Barrier(MPI_COMM_WORLD);
    return sizeofsends;
}

std::vector<float> MPIGatherTimeStats(profiling_util::Timer time1, std::string f, std::string l)
{
    std::vector<float> times(NProcs);
    auto p = times.data();
    auto time_taken = profiling_util::GetTimeTaken(time1, f, l);
    MPI_Gather(&time_taken, 1, MPI_FLOAT, p, 1, MPI_FLOAT, 0, MPI_COMM_WORLD);
    return times;
}

std::tuple<float, float, float, float> TimeStats(std::vector<float> times) 
{
    auto ave = 0.0, std = 0.0;
    auto mint = times[0];
    auto maxt = times[0];
    for (auto &t:times)
    {
        ave += t;
        std += t*t;
        mint = std::min(mint,t);
        maxt = std::max(maxt,t);
    }
    float n = times.size();
    ave /= n;
    if (n>1) std = sqrt((std - ave*ave*n)/(n-1.0));
    else std = 0;
    return std::make_tuple(ave, std, mint, maxt);
}

void MPIReportTimeStats(std::vector<float> times, 
    std::string commname, std::string message_size, 
    std::string f, std::string l)
{
    auto[ave, std, mint, maxt] = TimeStats(times);
    Rank0LocalLoggerWithTime()<<"MPI Comm="<<commname<<" @"<<f<<":L"<<l<<" - message size="<<message_size<<" timing [ave,std,min,max]=[" <<ave<<","<<std<<","<<mint<<","<<maxt<<"] (microseconds)"<<std::endl;
    MPI_Barrier(MPI_COMM_WORLD);
}

void MPIReportTimeStats(profiling_util::Timer time1, 
    std::string commname, std::string message_size, 
    std::string f, std::string l)
{
    auto times = MPIGatherTimeStats(time1, f, l);
    MPIReportTimeStats(times, commname, message_size, f, l);
}

/// \defgroup Performance 
//@{

void MPITestCPUSendRecv(Options &opt) 
{
    MPI_Status status;
    auto comm_all = MPI_COMM_WORLD;
    std::string mpifunc;
    auto[numcoms, mpi_comms, mpi_comms_name, ThisLocalTask, NProcsLocal, NLocalComms] = MPIAllocateComms();
    std::vector<double> senddata, receivedata;
    
    double * p1 = nullptr, *p2 = nullptr;
    auto  sizeofsends = MPISetSize(opt.maxgb);

    // now allreduce 
    mpifunc = "CPU_sendrecv";
    LogMPITest();
    for (auto i=0;i<sizeofsends.size();i++) 
    {
        auto sendsize = sizeofsends[i]*sizeof(double)/1024./1024./1024.;
        LogMPIAllComm();
        senddata.resize(sizeofsends[i]);
        receivedata.resize(sizeofsends[i]);
        Rank0ReportMem();
        MPILog0NodeMemUsage(comm_all);
        MPILog0NodeSystemMem(comm_all);
        for (auto &d:senddata) d = pow(2.0,ThisTask);
        p1 = senddata.data();
        p2 = receivedata.data();
        auto time1 = NewTimer();
        for (auto j=0;j<mpi_comms.size();j++)
        {
#ifdef TURNOFFMPI
#else
            if (ThisLocalTask[j] == 0) {LocalLoggerWithTime()<<"Communicating using comm "<<mpi_comms_name[j]<<std::endl;}
            std::vector<float> times;
            for (auto iter=0;iter<opt.Niter;iter++) {
                auto time2 = NewTimer();
                std::vector<MPI_Request> sendreqs, recvreqs;
                for (auto isend=0;isend<NProcsLocal[j];isend++) 
                {
                    if (isend != ThisLocalTask[j]) 
                    {
                        MPI_Request request;
                        int tag = isend*NProcsLocal[j]+ThisLocalTask[j];
                        MPI_Isend(p1, sizeofsends[i], MPI_DOUBLE, isend, tag, mpi_comms[j], &request);
                        sendreqs.push_back(request);
                    }
                }
                LocalLoggerWithTime()<<" Placed isends "<<std::endl;
                for (auto irecv=0;irecv<NProcsLocal[j];irecv++) 
                {
                    if (irecv != ThisLocalTask[j]) 
                    {
                        MPI_Request request;
                        int tag = ThisLocalTask[j]*NProcsLocal[j]+irecv;
                        MPI_Irecv(p2, sizeofsends[i], MPI_DOUBLE, irecv, tag, mpi_comms[j], &request);
                        recvreqs.push_back(request);
                    }
                }
                Rank0ReportMem();
                MPILog0NodeMemUsage(comm_all);
                MPILog0NodeSystemMem(comm_all);
                MPI_Waitall(recvreqs.size(), recvreqs.data(), MPI_STATUSES_IGNORE);
                LocalLoggerWithTime()<<" Received ireceives "<<std::endl;
                auto times_tmp = MPIGatherTimeStats(time2, __func__, std::to_string(__LINE__));
                times.insert(times.end(), times_tmp.begin(), times_tmp.end());
            }
            MPI_Barrier(MPI_COMM_WORLD);
            MPIReportTimeStats(times, mpi_comms_name[j], std::to_string(sizeofsends[i]), __func__, std::to_string(__LINE__));
#endif
        }
        if (ThisTask==0) LogTimeTaken(time1);
    }
    senddata.clear();
    senddata.shrink_to_fit();
    receivedata.clear();
    receivedata.shrink_to_fit();
    Rank0ReportMem();
    MPI_Barrier(MPI_COMM_WORLD);
}

void MPITestCPUAllReduce(Options &opt) 
{
    MPI_Status status;
    auto comm_all = MPI_COMM_WORLD;
    std::string mpifunc;
    auto[numcoms, mpi_comms, mpi_comms_name, ThisLocalTask, NProcsLocal, NLocalComms] = MPIAllocateComms();
    std::vector<double> data, allreducesum;
    
    double * p1 = nullptr, *p2 = nullptr;
    auto  sizeofsends = MPISetSize(opt.maxgb);

    // now allreduce 
    mpifunc = "CPU_allreduce";
    LogMPITest();
#ifdef TURNOFFMPI
    data.resize(sizeofsends[sizeofsends.size()-1]);
    allreducesum.resize(sizeofsends[sizeofsends.size()-1]);
    for (auto &d:data) d = pow(2.0,ThisTask);
    sleep(10);
    for (auto &d:allreducesum) d = pow(2.0,ThisTask);
#endif
    for (auto i=0;i<sizeofsends.size();i++) 
    {
        auto sendsize = sizeofsends[i]*sizeof(double)/1024./1024./1024.;
        LogMPIAllComm();
        data.resize(sizeofsends[i]);
        allreducesum.resize(sizeofsends[i]);
        Rank0ReportMem();
        MPILog0NodeMemUsage(comm_all);
        MPILog0NodeSystemMem(comm_all);
        for (auto &d:data) d = pow(2.0,ThisTask);
        p1 = data.data();
        p2 = allreducesum.data();
        auto time1 = NewTimer();
        for (auto j=0;j<mpi_comms.size();j++) 
        {
#ifdef TURNOFFMPI
#else
            if (ThisLocalTask[j] == 0) {LocalLoggerWithTime()<<"Communicating using comm "<<mpi_comms_name[j]<<std::endl;}
            std::vector<float> times;
            for (auto iter=0;iter<opt.Niter;iter++) {
                auto time2 = NewTimer();
                MPI_Allreduce(p1, p2, sizeofsends[i], MPI_DOUBLE, MPI_SUM, mpi_comms[j]);
                auto times_tmp = MPIGatherTimeStats(time2, __func__, std::to_string(__LINE__));
                times.insert(times.end(), times_tmp.begin(), times_tmp.end());
            }
            Rank0ReportMem();
            MPILog0NodeMemUsage(comm_all);
            MPILog0NodeSystemMem(comm_all);
            sleep(2);
            MPI_Barrier(MPI_COMM_WORLD);
            MPIReportTimeStats(times, mpi_comms_name[j], std::to_string(sizeofsends[i]), __func__, std::to_string(__LINE__));
#endif
        }
        if (ThisTask==0) LogTimeTaken(time1);
    }
    data.clear();
    data.shrink_to_fit();
    allreducesum.clear();
    allreducesum.shrink_to_fit();
    MPIFreeComms(mpi_comms, mpi_comms_name);
    Rank0ReportMem();
    MPI_Barrier(MPI_COMM_WORLD);
}
//@}

/// Test whether the MPI interface sends the correct data 
void MPITestCPUCorrectSendRecv(Options &opt) 
{
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Status status;
    auto comm_all = MPI_COMM_WORLD;
    std::string mpifunc;
    unsigned long size = 5;
    std::vector<double> data(size);
    void * p1 = nullptr, *p2 = nullptr;

    mpifunc = "CPU_correct_values";
    for (auto &d:data) d = pow(2.0,ThisTask);
    auto time1 = NewTimer();
    if (ThisTask == opt.roottask) 
    {
        auto oldsize = size;
        for (auto itask = 0;itask<NProcs;itask++) 
        {
            if (itask == opt.roottask) continue;
            int mpi_err;
            LocalLogger()<<" receiving from "<<itask<<std::endl;
            mpi_err = MPI_Recv(&size, 1, MPI_UNSIGNED_LONG, itask, 0, MPI_COMM_WORLD, &status);
            LocalLogger()<<" size "<<size<<" received from "<<itask<<" with " <<mpi_err<<std::endl;
            if (size != oldsize) {
                LocalLogger()<<" GOT WRONG SIZE VALUE from "<<itask<<std::endl;
                MPI_Abort(MPI_COMM_WORLD,8);
            }
            data.resize(size);
            p1 = data.data();
            mpi_err = MPI_Recv(p1, size, MPI_DOUBLE, itask, 0, MPI_COMM_WORLD, &status);
            std::vector<double> refdata(oldsize);
            for (auto &d:refdata) d = pow(2.0,itask);
            for (auto i=0;i<oldsize;i++) {
                if (data[i] != refdata[i]) {
                    LocalLogger()<<" GOT WRONG data VALUE from "<<itask<<std::endl;
                    MPI_Abort(MPI_COMM_WORLD,8);
                }
            }

            std::string s;
            for (auto &d:data) s+=std::to_string(d) + " ";
            LocalLogger()<<" received from "<<itask<<" with "<<mpi_err<<std::endl;
        }
    }
    else {
        MPI_Request request;
        int mpi_err;
        LocalLogger()<<" sending to "<<opt.roottask<<" with send type of "<<opt.usesend<<std::endl;
        size = data.size();
        p1 = data.data();
        if (opt.usesend == USESEND) {
            mpi_err = MPI_Send(&size, 1, MPI_UNSIGNED_LONG, opt.roottask, 0, MPI_COMM_WORLD);
            mpi_err = MPI_Send(p1, size, MPI_DOUBLE, opt.roottask, 0, MPI_COMM_WORLD);
        }
        else if (opt.usesend == USEISEND) 
        {
            mpi_err = MPI_Isend(&size, 1, MPI_UNSIGNED_LONG, opt.roottask, 0, MPI_COMM_WORLD, &request);
            mpi_err = MPI_Isend(p1, size, MPI_DOUBLE, opt.roottask, 0, MPI_COMM_WORLD, &request);
        }
        else if (opt.usesend == USESSEND) 
        {
            mpi_err = MPI_Ssend(&size, 1, MPI_UNSIGNED_LONG, opt.roottask, 0, MPI_COMM_WORLD);
            mpi_err = MPI_Ssend(p1, size, MPI_DOUBLE, opt.roottask, 0, MPI_COMM_WORLD);
        }
        LocalLogger()<<" sent to "<<opt.roottask<<" with "<<mpi_err<<std::endl;
    }
    if (ThisTask==0) LogTimeTaken(time1);
    data.clear();
    data.shrink_to_fit();
    p1 = p2 = nullptr;
}
//@}

/// @brief GPU MPI tests
//@{
void MPITestGPUSendRecv(Options &opt){

    MPI_Status status;
    auto comm_all = MPI_COMM_WORLD;
    std::string mpifunc;
    auto[numcoms, mpi_comms, mpi_comms_name, ThisLocalTask, NProcsLocal, NLocalComms] = MPIAllocateComms();
    std::vector<double> senddata, receivedata;
    
    double * p1 = nullptr, *p2 = nullptr;
    //double *gpu_p1 = nullptr, *gpu_p2 = nullptr;
    std::vector<double*> gpu_p1, gpu_p2;
    auto  sizeofsends = MPISetSize(opt.maxgb);

    int nDevices;
    pu_gpuErrorCheck(pu_gpuGetDeviceCount(&nDevices));
    gpu_p1.resize(nDevices);
    gpu_p2.resize(nDevices);
    for (auto &x:gpu_p1) x=nullptr;
    for (auto &x:gpu_p2) x=nullptr;

    // now allreduce 
    mpifunc = "GPU_sendrecv";
    LogMPITest();
    for (auto i=0;i<sizeofsends.size();i++) 
    {
        auto sendsize = sizeofsends[i]*sizeof(double)/1024./1024./1024.;
        LogMPIAllComm();
        auto nbytes = sizeofsends[i]*sizeof(double);
        senddata.resize(sizeofsends[i]);
        receivedata.resize(sizeofsends[i]);
        for (auto idev=0;idev<nDevices;idev++) {
            LocalLoggerWithTime()<<" allocating memory on device "<<idev<<std::endl;
            pu_gpuErrorCheck(pu_gpuSetDevice(idev));
            pu_gpuErrorCheck(pu_gpuHostMalloc((void**)&gpu_p1[idev], nbytes));
            pu_gpuErrorCheck(pu_gpuHostMalloc((void**)&gpu_p2[idev], nbytes));
            pu_gpuErrorCheck(pu_gpuMemcpy(gpu_p1[idev], senddata.data(), nbytes, pu_gpuMemcpyHostToDevice));
            pu_gpuErrorCheck(pu_gpuMemcpy(gpu_p2[idev], receivedata.data(), nbytes, pu_gpuMemcpyHostToDevice));
        }
        Rank0ReportMem();
        MPILog0NodeMemUsage(comm_all);
        MPILog0NodeSystemMem(comm_all);
        for (auto &d:senddata) d = pow(2.0,ThisTask);
        // p1 = senddata.data();
        // p2 = receivedata.data();
        auto time1 = NewTimer();
        // for (auto j=0;j<mpi_comms.size();j++)
        {
            auto j=mpi_comms.size()-1;
#ifdef TURNOFFMPI
#else
            for (auto idev=0; idev<nDevices;idev++) {
                pu_gpuErrorCheck(pu_gpuSetDevice(idev));
                p1 = gpu_p1[idev];
                p2 = gpu_p2[idev];
                if (ThisLocalTask[j] == 0) {LocalLoggerWithTime()<<"Communicating using comm "<<mpi_comms_name[j]<<" with device "<<idev<<std::endl;}
                std::vector<float> times;
                for (auto iter=0;iter<opt.Niter;iter++) {
                    auto time2 = NewTimer();
                    std::vector<MPI_Request> sendreqs, recvreqs;
                    for (auto isend=0;isend<NProcsLocal[j];isend++) 
                    {
                        if (isend != ThisLocalTask[j]) 
                        {
                            MPI_Request request;
                            int tag = isend*NProcsLocal[j]+ThisLocalTask[j];
                            MPI_Isend(p1, sizeofsends[i], MPI_DOUBLE, isend, tag, mpi_comms[j], &request);
                            sendreqs.push_back(request);
                        }
                    }
                    // LocalLoggerWithTime()<<" Placed isends "<<std::endl;
                    for (auto irecv=0;irecv<NProcsLocal[j];irecv++) 
                    {
                        if (irecv != ThisLocalTask[j]) 
                        {
                            MPI_Request request;
                            int tag = ThisLocalTask[j]*NProcsLocal[j]+irecv;
                            MPI_Irecv(p2, sizeofsends[i], MPI_DOUBLE, irecv, tag, mpi_comms[j], &request);
                            recvreqs.push_back(request);
                        }
                    }
                    // Rank0ReportMem();
                    // MPILog0NodeMemUsage(comm_all);
                    // MPILog0NodeSystemMem(comm_all);
                    MPI_Waitall(recvreqs.size(), recvreqs.data(), MPI_STATUSES_IGNORE);
                    // LocalLoggerWithTime()<<" Received ireceives "<<std::endl;
                    auto times_tmp = MPIGatherTimeStats(time2, __func__, std::to_string(__LINE__));
                    times.insert(times.end(), times_tmp.begin(), times_tmp.end());
                }
                MPI_Barrier(MPI_COMM_WORLD);
                MPIReportTimeStats(times, mpi_comms_name[j], std::to_string(sizeofsends[i]), __func__, std::to_string(__LINE__));
            }
#endif
        }
        if (ThisTask==0) LogTimeTaken(time1);

        for (auto idev=0;idev<nDevices;idev++) {
            LocalLoggerWithTime()<<" Freeing memory on "<<idev<<std::endl;
            pu_gpuErrorCheck(pu_gpuSetDevice(idev));
            pu_gpuErrorCheck(pu_gpuFree(gpu_p1[idev]));
            pu_gpuErrorCheck(pu_gpuFree(gpu_p2[idev]));
        }
    }
    senddata.clear();
    senddata.shrink_to_fit();
    receivedata.clear();
    receivedata.shrink_to_fit();
    gpu_p1.clear();
    gpu_p2.clear();
    Rank0ReportMem();
    MPI_Barrier(MPI_COMM_WORLD);
};

void MPITestGPUCorrectSendRecv(Options &opt){

};
void MPITestGPUAllReduce(Options &opt){

};
//}
void MPIRunTests(Options &opt)
{
    auto comm_all = MPI_COMM_WORLD;
    if (opt.icpu) 
    {
        MPITestCPUSendRecv(opt);
        MPITestCPUCorrectSendRecv(opt);
        MPITestCPUAllReduce(opt);
    }
    if (opt.igpu) 
    {
        MPITestGPUSendRecv(opt);
        MPITestGPUCorrectSendRecv(opt);
        MPITestGPUAllReduce(opt);
    }
}

int main(int argc, char **argv) {
    MPI_Init(&argc, &argv);
    MPI_Comm comm = MPI_COMM_WORLD;
    MPI_Comm_size(comm, &NProcs);
    MPI_Comm_rank(comm, &ThisTask);
    Options opt;

    // init logger time
    logtime = std::chrono::system_clock::now();
    log_time = std::chrono::system_clock::to_time_t(logtime);
    auto start = std::chrono::system_clock::now();
    std::time_t start_time = std::chrono::system_clock::to_time_t(start);
    Rank0LocalLoggerWithTime()<<"Starting job "<<std::endl;
    Rank0ReportMem();
    MPILog0NodeMemUsage(comm);
    MPILog0NodeSystemMem(comm);
    if (argc >= 2) opt.maxgb = atof(argv[1]);
    if (argc == 3) opt.Niter = atof(argv[2]);
    opt.othertask = NProcs/2 + 1;
    // if (argc >= 2) opt.delay = atoi(argv[1]);
    // if (argc >= 3) opt.msize = atoi(argv[2]);
    // if (argc >= 4) opt.othertask = atoi(argv[3]);

    // default value for 2 node tests assuming that same number of tasks per node 
    // ensures that othertask is testing internode communication
    // alter if want intranode communcation to something like opt.roottask + 1;
    opt.othertask = NProcs/2 + 1;
    
    MPILog0ParallelAPI();
    MPILog0Binding();
    MPI_Barrier(MPI_COMM_WORLD);
    MPIRunTests(opt);

    Rank0LocalLoggerWithTime()<<"Ending job "<<std::endl;
    MPI_Finalize();
    return 0;
}