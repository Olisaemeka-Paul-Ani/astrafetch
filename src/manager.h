#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>

#include "download.h"

class DownloadManager {
   private:
    std::queue<std::shared_ptr<Download>> q;
    std::mutex m;
    std::mutex mapMutex;
    std::condition_variable cv;
    std::unordered_map<int, std::shared_ptr<Download>> store;

   public:
    auto getDownload() {
        std::unique_lock<std::mutex> lock(m);
        while (q.empty()) {
            cv.wait(lock);
        }

        std::shared_ptr<Download> d = q.front();
        q.pop();
        return d;
    }

    void addDownload(std::string url, std::string fileName) {
        std::unique_lock<std::mutex> lock(m);
        auto temp = std::make_shared<Download>(url, fileName);

        q.push(temp);
        lock.unlock();
        std::unique_lock<std::mutex> lockT(mapMutex);
        store[temp->getId()] = temp;
        lockT.unlock();
        cv.notify_one();
    }
};