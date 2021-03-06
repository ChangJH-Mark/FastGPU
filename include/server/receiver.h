//
// Created by root on 2021/3/12.
//

#ifndef FASTGPU_RECEIVER_H
#define FASTGPU_RECEIVER_H
#include <thread>
#include <atomic>
#include <map>
#include <unordered_map>
#include <sys/socket.h>
#include <sys/epoll.h>
#include "mod.h"
#include "common/message.h"
#include "common/ThreadPool.h"
#include "commands.h"
#include "server/server.h"
#define MAX_CONNECTION 100

namespace mgpu{
    class Receiver : public Module {
    public:
        Receiver() : pool(3, 20){
            joinable = true;
        };
        void init() override;
        void run() override;
        void join() override;
        void destroy() override;

    private:
        void do_accept();
        void do_worker(uint conn);
        void do_newconn();
        void do_close(uint conn);
        void push_command(uint conn);

    private:
        uint server_socket{};
        std::thread listener;
        ThreadPool pool;
        int epfd;
        int stopfd[2];
        unordered_map<pid_t, int> p_conns; // pid - connections
    };
}
#endif //FASTGPU_RECEIVER_H
