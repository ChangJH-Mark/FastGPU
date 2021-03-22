//
// Created by root on 2021/3/11.
//
#include "server/server.h"
#include <functional>
using namespace mgpu;

Server* Server::single_instance = nullptr;

Server* mgpu::get_server() {
    if(Server::single_instance != nullptr){
        return Server::single_instance;
    }

    auto server = new Server;
    Server::single_instance = server;
    server->mod["device"] = make_shared<Device>();
    server->mod["scheduler"] = make_shared<Scheduler>();
    server->mod["receiver"] = make_shared<Receiver>();
    server->mod["conductor"] = make_shared<Conductor>();
    for(const auto& m : server->mod) {
        m.second->init();
    }

    for(const auto& m : server->mod){
        m.second->run();
    }
    return Server::single_instance;
}

void Server::join() {
    for(const auto& m : this->mod) {
        if(m.second->joinable){
            m.second->join();
        }
    }
    while (1) {}
}

void mgpu::destroy_server() {
    auto server = Server::single_instance;
    if(server == nullptr)
        return;
    for(const auto& m : server->mod) {
        m.second->destroy();
    }
}