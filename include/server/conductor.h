//
// Created by root on 2021/3/12.
//

#ifndef FASTGPU_CONDUCTOR_H
#define FASTGPU_CONDUCTOR_H
#include <future>
#include "server/commands.h"
#include "mod.h"

namespace mgpu {
    class Conductor : public Module{
    public:
        Conductor() {
            joinable = false;
        };

        virtual void init() override{};
        virtual void run() override{};
        virtual void destroy() override{};
        virtual void join() override{};

    public:
        void conduct(std::shared_ptr<Command> cmd);

    private:
        void do_cudamalloc(const std::shared_ptr<Command>& cmd);
        void do_cudamallochost(const std::shared_ptr<Command>& cmd);
        void do_cudafree(const std::shared_ptr<Command>& cmd);
        void do_cudafreehost(const std::shared_ptr<Command>& cmd);
        void do_cudamemset(const std::shared_ptr<Command>& cmd);
        void do_cudamemcpy(const std::shared_ptr<Command>& cmd);
    };
}
#endif //FASTGPU_CONDUCTOR_H