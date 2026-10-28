#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#include "download.h"

class DownloadManager {
   private:
    std::queue<std::shared_ptr<Download>> q;
    std::mutex m;
    std::condition_variable cv;

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
        q.push(std::make_shared<Download>(url, fileName));
        lock.unlock();
        cv.notify_one();
    }
};