#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>
#include <tuple>
#include "download.h"

class DownloadManager {
   private:
    std::queue<std::shared_ptr<Download>> q;
    std::mutex m;
    std::mutex mapMutex;
    std::condition_variable cv;
    std::unordered_map<int, std::shared_ptr<Download>> store;
    std::array<std::thread, 3> workers;
    std::atomic<bool> running = true;
    


   public:
   struct snapshotEntry{
        int id; 
        std::string url;
        std::string status;
        cpr::cpr_off_t dlTotal;
        cpr::cpr_off_t dlSoFar;

    };
    ~DownloadManager() {
        for (auto& [name, age] : this->store) {
            age->cancelDownload();
        }
        int i = 0;
        this->running = false;
        cv.notify_all();

        std::cout << "Destructor: about to join workers" << std::endl;
        while (i < this->workers.size()) {
            this->workers[i].join();
            std::cout << "Worker " << i + 1 << " has shut down" << std::endl;
            i = i + 1;
        }
    }

    auto getDownload() {
        std::unique_lock<std::mutex> lock(m);
        while (q.empty()) {
            cv.wait(lock);
            if (this->running == false) {
                std::shared_ptr<Download> d(nullptr);
                return d;
            } 
        }
        
                std::shared_ptr<Download> d = q.front();
                q.pop();
                return d;
          
    }

    DownloadManager() {
        int i = 0;
        while (i < this->workers.size()) {
            workers[i] = std::thread(&DownloadManager::workerLoop, this);
            std::cout << "Worker " << i + 1 << " has been created" << std::endl;
            i = i + 1;
        }
    }

    void workerLoop() {
        while (running) {
            auto d = this->getDownload();
            if (d != nullptr) {
                std::cout << "Download has been popped from queue" << std::endl;
                d->download();
            } else {
                std::cout << "Worker thread records empty download" << std::endl;
                return;
            }
            std::cout << "Download " << d->getId() << " ended: " << d->getStatus() << std::endl;
        }
    }

    void addDownload(std::string url, std::string fileName) {
        std::unique_lock<std::mutex> lock(m);
        auto temp = std::make_shared<Download>(url, fileName);
        q.push(temp);

        lock.unlock();

        std::unique_lock<std::mutex> lockT(mapMutex);
        store[temp->getId()] = temp;
        lockT.unlock();
        std::cout << "Download has been added to queue" << std::endl;
        cv.notify_one();
    }

    std::vector<snapshotEntry> getSnapshot(){
        std::unique_lock<std::mutex> lockTh(mapMutex);
        std::vector<snapshotEntry> output;
        for (auto& [num, dwnl]: this->store){
            snapshotEntry placeHolder;
            std::tuple<int, std::string, std::string, std::string, cpr::cpr_off_t, cpr::cpr_off_t> placeTwo = (*dwnl).getSnapshot();
            placeHolder.id = std::get<0>(placeTwo);
            placeHolder.status = std::get<2>(placeTwo);
            placeHolder.url = std::get<3>(placeTwo);
            
            placeHolder.dlTotal = std::get<5>(placeTwo);
            placeHolder.dlSoFar = std::get<4>(placeTwo);

            output.push_back(placeHolder);
        }
        return output;
    }

    
};