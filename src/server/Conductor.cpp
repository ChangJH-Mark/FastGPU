//
// Created by root on 2021/3/21.
//
#include <cuda_runtime.h>
#include <cuda.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <string>
#include "server/task.h"
#include "server/conductor.h"
#include "common/helper.h"
#include "common/Log.h"

#define MAX_STREAMS 32
#define WORKER_GRID 60

using namespace mgpu;

void Conductor::init() {
    func_table[MSG_CUDA_MALLOC] = &Conductor::do_cudamalloc;
    func_table[MSG_CUDA_MALLOC_HOST] = &Conductor::do_cudamallochost;
    func_table[MSG_CUDA_FREE] = &Conductor::do_cudafree;
    func_table[MSG_CUDA_FREE_HOST] = &Conductor::do_cudafreehost;
    func_table[MSG_CUDA_MEMSET] = &Conductor::do_cudamemset;
    func_table[MSG_CUDA_MEMCPY] = &Conductor::do_cudamemcpy;
    func_table[MSG_CUDA_LAUNCH_KERNEL] = &Conductor::do_cudalaunchkernel;
    func_table[MSG_CUDA_STREAM_CREATE] = &Conductor::do_cudastreamcreate;
    func_table[MSG_CUDA_STREAM_SYNCHRONIZE] = &Conductor::do_cudastreamsynchronize;
    func_table[MSG_CUDA_GET_DEVICE_COUNT] = &Conductor::do_cudagetdevicecount;
    func_table[MSG_CUDA_EVENT_CREATE] = &Conductor::do_cudaeventcreate;
    func_table[MSG_CUDA_EVENT_DESTROY] = &Conductor::do_cudaeventdestroy;
    func_table[MSG_CUDA_EVENT_RECORD] = &Conductor::do_cudaeventrecord;
    func_table[MSG_CUDA_EVENT_SYNCHRONIZE] = &Conductor::do_cudaeventsynchronize;
    func_table[MSG_CUDA_EVENT_ELAPSED_TIME] = &Conductor::do_cudaeventelapsedtime;
    func_table[MSG_MATRIX_MUL_GPU] = &Conductor::do_matrixmultgpu;
}

std::shared_ptr<bool> Conductor::conduct(const std::shared_ptr<Command>& cmd) {
    pool.commit(func_table[cmd->get_type()], this, cmd);
    return cmd->get_status();
}

void Conductor::do_cudamalloc(const std::shared_ptr<Command> &cmd) {
    dout(DEBUG) << " cmd_id: " << cmd->get_id() << " size: " << cmd->get_msg<CudaMallocMsg>()->size << dendl;
    void *dev_ptr;
    cudaCheck(::cudaSetDevice(cmd->get_device()));
    cudaCheck(::cudaMalloc(&dev_ptr, cmd->get_msg<CudaMallocMsg>()->size));
    dout(DEBUG) << " cmd_id: " << cmd->get_id() << " address: " << dev_ptr << dendl;
    cmd->finish<void *>(dev_ptr);
}

void Conductor::do_cudamallochost(const std::shared_ptr<Command> &cmd) {
    auto msg = cmd->get_msg<CudaMallocHostMsg>();
    int shm_id;
    if ((shm_id  = shmget(IPC_PRIVATE, msg->size, IPC_CREAT))< 0) {
        perror("fail to shmget");
        exit(1);
    } else {
        dout(DEBUG) << " cmd_id: " << cmd->get_id() << " shm_id: " << shm_id << dendl;
    }
    void *host_ptr = shmat(shm_id, NULL, 0);
    if(!shms_id.count(host_ptr))
    {
        shms_id[host_ptr] = shm_id;
        dout(DEBUG) << " cmd_id: " << cmd->get_id() << " share memory address: " << host_ptr << dendl;
    } else {
        dout(DEBUG) << " cmd_id: " << cmd->get_id() << " share memory already exist shm_id: " << shms_id[host_ptr] << dendl;
        perror("fail to shmget");
        exit(1);
    }
//    cudaCheck(::cudaSetDevice(cmd->get_device()));
//    cudaCheck(::cudaHostRegister(host_ptr, msg->size, cudaHostRegisterDefault));
    cmd->finish<CudaMallocHostRet>(mgpu::CudaMallocHostRet{host_ptr, shm_id});
    dout(DEBUG) << " cmd_id: " << cmd->get_id() << " finished " << dendl;
}

void Conductor::do_cudafree(const std::shared_ptr<Command> &cmd) {
    dout(DEBUG) << " cmd_id: " << cmd->get_id() << " free: " << cmd->get_msg<CudaFreeMsg>()->devPtr << dendl;
    cudaCheck(::cudaSetDevice(cmd->get_device()));
    auto dev_ptr = cmd->get_msg<CudaFreeMsg>()->devPtr;
    cudaCheck(::cudaFree(dev_ptr));
    cmd->finish<bool>(true);
}

void Conductor::do_cudafreehost(const std::shared_ptr<Command> &cmd) {
    dout(DEBUG)<< " free: " << (cmd->get_msg<CudaFreeHostMsg>()->ptr) << dendl;
    auto host_ptr = cmd->get_msg<CudaFreeHostMsg>()->ptr;
//    cudaCheck(::cudaSetDevice(cmd->get_device()));
//    cudaCheck(::cudaHostUnregister(host_ptr))
    if(0 > shmdt(host_ptr))
    {
        perror("server fail to release share memory");
        exit(1);
    }
    if(0 > shmctl(shms_id[host_ptr], IPC_RMID, NULL))
    {
        perror("server fail to delete share memory");
        exit(1);
    }
    shms_id.erase(host_ptr);
    cmd->finish<bool>(true);
}

void Conductor::do_cudamemset(const std::shared_ptr<Command> &cmd) {
    dout(DEBUG)<< " set address: " << cmd->get_msg<CudaMemsetMsg>()->devPtr << dendl;
    cudaCheck(::cudaSetDevice(cmd->get_device()));
    auto msg = cmd->get_msg<CudaMemsetMsg>();
    cudaCheck(::cudaMemset(msg->devPtr, msg->value, msg->count));
    cmd->finish<bool>(true);
}

void Conductor::do_cudamemcpy(const std::shared_ptr<Command> &cmd) {
    cudaCheck(::cudaSetDevice(cmd->get_device()));
    auto msg = cmd->get_msg<CudaMemcpyMsg>();
    dout(DEBUG)<< " copy from: " << msg->src << " to: " << msg->dst << dendl;
    cudaCheck(::cudaMemcpy(msg->dst, msg->src, msg->count, msg->kind));
    cmd->finish<bool>(true);
}

void Conductor::do_cudalaunchkernel(const std::shared_ptr<Command> &cmd) {
    cudaCheck(::cudaSetDevice(cmd->get_device()));
    auto msg = cmd->get_msg<CudaLaunchKernelMsg>();
    CUmodule cuModule;
    cudaCheck(static_cast<cudaError_t>(::cuModuleLoad(&cuModule, msg->ptx)));
    CUfunction func;
    cudaCheck(static_cast<cudaError_t>(::cuModuleGetFunction(&func, cuModule, (string(msg->kernel) + "Proxy").c_str())));
    dout(DEBUG) << "ptx: " << msg->ptx << " kernel: " << std::string(msg->kernel) << " address :" << func << dendl;
    msg->p_size = fillParameters(msg->param, msg->p_size, 0, 6, msg->conf.grid,
                                 (msg->conf.grid.x * msg->conf.grid.y * msg->conf.grid.z));
    void *extra[] = {
            CU_LAUNCH_PARAM_BUFFER_POINTER, msg->param,
            CU_LAUNCH_PARAM_BUFFER_SIZE, &(msg->p_size),
            CU_LAUNCH_PARAM_END
    };
    dout(DEBUG)<< " launch from ptx" << msg->ptx << " kernel: " << std::string(msg->kernel) + "Proxy"
              << " at : device : "
              << cmd->get_device() << " cudaStream_t : " << cmd->get_stream() << " gridDim " + to_string(msg->conf.grid.x) + " "
              << to_string(msg->conf.grid.y) + " " << to_string(msg->conf.grid.z) + " "
              << dendl;
    cudaCheck(static_cast<cudaError_t>(::cuLaunchKernel(func, WORKER_GRID, 1, 1,
                                                        msg->conf.block.x, msg->conf.block.y, msg->conf.block.z,
                                                        msg->conf.share_memory,
                                                        cmd->get_stream(),
                                                        NULL, extra)));
    cudaCheck(static_cast<cudaError_t>(cuModuleUnload(cuModule)));
    cmd->finish<bool>(true);
}

void Conductor::do_cudastreamcreate(const std::shared_ptr<Command> &cmd) {
    cudaCheck(::cudaSetDevice(cmd->get_device()));
    dout(DEBUG) << " cmd_id: " << cmd->get_id() << " create stream at device: " << cmd->get_device() << dendl;
    auto msg = cmd->get_msg<CudaStreamCreateMsg>();
    auto ret = TASK_HOLDER->create_streams(cmd->get_msg<CudaStreamCreateMsg>());
    if(ret == nullptr)
        cmd->finish<stream_t>(nullptr, 0);
    cmd->finish<stream_t>(ret, msg->num);
}

void Conductor::do_cudastreamsynchronize(const std::shared_ptr<Command> &cmd) {
    cudaCheck(::cudaSetDevice(cmd->get_device()));
    dout(DEBUG)<< " synchronize stream: at device: " << cmd->get_device() << " stream: "
              << cmd->get_stream() << dendl;
    cudaCheck(::cudaStreamSynchronize(cmd->get_stream()));
    cmd->finish<bool>(true);
}

void Conductor::do_cudagetdevicecount(const std::shared_ptr<Command> &cmd) {
    dout(DEBUG)<< " cuda get device count " << dendl;
    int count;
    cudaCheck(::cudaGetDeviceCount(&count));
    cmd->finish<int>(count);
}

void Conductor::do_cudaeventcreate(const std::shared_ptr<Command> &cmd) {
    cudaEvent_t event;
    dout(DEBUG) << " create event at device: " << cmd->get_device() << " stream: " << cmd->get_stream() << dendl;
    cudaCheck(::cudaEventCreate(&event));
    cmd->finish<cudaEvent_t>(event);
}

void Conductor::do_cudaeventdestroy(const std::shared_ptr<Command> &cmd) {
    cudaCheck(::cudaEventDestroy(cmd->get_msg<CudaEventDestroyMsg>()->event));
    dout(DEBUG) << " event destroy device: " << cmd->get_device() << " stream: " << cmd->get_stream() << dendl;
    cmd->finish<bool>(true);
}

void Conductor::do_cudaeventrecord(const std::shared_ptr<Command> &cmd) {
    cudaCheck(::cudaSetDevice(cmd->get_device()));
    auto msg = cmd->get_msg<CudaEventRecordMsg>();
    dout(DEBUG) << " device: " << cmd->get_device() << " stream: " << cmd->get_stream() << dendl;
    cudaCheck(::cudaEventRecord(msg->event, msg->stream));
    cmd->finish<bool>(true);
}

void Conductor::do_cudaeventsynchronize(const std::shared_ptr<Command> &cmd) {
    cudaCheck(::cudaSetDevice(cmd->get_device()));
    auto msg = cmd->get_msg<CudaEventSyncMsg>();
    cudaCheck(::cudaEventSynchronize(msg->event));
    cmd->finish<bool>(true);
}

void Conductor::do_cudaeventelapsedtime(const std::shared_ptr<Command> &cmd) {
    cudaCheck(::cudaSetDevice(cmd->get_device()));
    auto msg = cmd->get_msg<CudaEventElapsedTimeMsg>();
    float ret;
    dout(DEBUG) << " start device: " <<  cmd->get_device() << " stream: " << cmd->get_stream() << dendl;
    cudaCheck(::cudaEventElapsedTime(&ret, msg->start, msg->end));
    dout(DEBUG) << " end device: " << cmd->get_device() << " stream: " << cmd->get_stream() << dendl;
    cmd->finish(ret);
}

void Conductor::do_matrixmultgpu(const std::shared_ptr<Command> &cmd) {
    auto msg = cmd->get_msg<MatrixMulMsg>();
    Matrix A = msg->A;
    Matrix B = msg->B;
    auto conf = cmd->get_msg<MatrixMulMsg>()->conf;
    int shm_id = shmget(IPC_PRIVATE, sizeof(float) * A.height * B.width, IPC_CREAT);
    if(shm_id < 0)
    {
        perror("fail to shmget");
        exit(1);
    } else {
        dout(DEBUG)<< " shm_id: " << shm_id << dendl;
    }
    void * res = shmat(shm_id, NULL, 0);
    if (!shms_id.count(res)) {
        shms_id[res] = shm_id;
    } else {
        perror("fail to shmat, address already used");
        exit(1);
    }
    cudaCheck(::cudaHostRegister(res, sizeof(float) * A.height * B.width, cudaHostRegisterDefault));

    auto device = get_server()->get_device();
    std::vector<cudaEvent_t> starts(device->counts());
    std::vector<cudaEvent_t> ends(device->counts());

    for(int i = 0; i < device->counts(); i++)
    {
        dout(DEBUG)<< " device: " << i << " do matrix multi " << dendl;
        cudaCheck(::cudaSetDevice(i));
        cudaCheck(::cudaEventCreateWithFlags(&(starts[i]), cudaEventBlockingSync));
        cudaCheck(::cudaEventCreateWithFlags(&(ends[i]), cudaEventBlockingSync));
        void * dev_A, *dev_B, *dev_C;
        cudaCheck(::cudaMalloc(&dev_A, sizeof(float) * A.height * A.width));
        cudaCheck(::cudaMalloc(&dev_B, sizeof(float) * B.height * B.width));
        cudaCheck(::cudaMalloc(&dev_C, sizeof(float) * A.height * B.width));
        cudaEventRecord(starts[i], 0);
        cudaMemcpyAsync(dev_A, A.data, sizeof(float) * A.height * A.width, cudaMemcpyHostToDevice, 0);
        cudaMemcpyAsync(dev_B, B.data, sizeof(float) * B.height * B.width, cudaMemcpyHostToDevice, 0);
        CUmodule module;
        cudaCheck(static_cast<cudaError_t>(::cuModuleLoad(&module, "/opt/custom/ptx/matrixMul.ptx")));
        CUfunction func;
        cudaCheck(static_cast<cudaError_t>(::cuModuleGetFunction(&func, module, "matrixMulProxy")));
        char params[1024];
        auto p_size = fillParameters(params, 0, static_cast<void*>(dev_C), static_cast<void*>(dev_A), static_cast<void*>(dev_B), A.width, B.width, 0,2, conf.grid, (conf.grid.x * conf.grid.y));
        void *extra[] = {
                CU_LAUNCH_PARAM_BUFFER_POINTER, params,
                CU_LAUNCH_PARAM_BUFFER_SIZE, &p_size,
                CU_LAUNCH_PARAM_END
        };
        cudaCheck(static_cast<cudaError_t>(::cuLaunchKernel(func, WORKER_GRID, 1, 1, conf.block.x, conf.block.y, conf.block.z, conf.share_memory, 0, nullptr, extra)));
        int area = A.height * B.width / device->counts();
        cudaCheck(::cudaMemcpyAsync(static_cast<float*>(res) + area * i, static_cast<float*>(dev_C) + area * i, sizeof(float) * area, cudaMemcpyDeviceToHost, 0));
        cudaCheck(::cudaEventRecord(ends[i], 0));
    }
    for(int i = 0; i<device->counts();i++){
        cudaCheck(::cudaEventSynchronize(ends[i]));
    }
    dout(DEBUG)<< " finish " << dendl;
    cmd->finish<CudaMallocHostRet>(mgpu::CudaMallocHostRet{res, shm_id});
}