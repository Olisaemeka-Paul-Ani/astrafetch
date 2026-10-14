#include <cpr/cpr.h>

#include <atomic>
#include <mutex>
#include <string>

class Download {
   private:
    std::string url;
    std::string fileName;
    std::string status;
    std::atomic<cpr::cpr_off_t> dlTotal;
    std::mutex m;
    std::atomic<cpr::cpr_off_t> dlSoFar;
    std::atomic<bool> cancelRequested;
    inline static std::atomic<int> downloadIdDispenser{0};
    int id;

   public:
    Download(std::string url, std::string fileName) {
        this->url = url;
        this->fileName = fileName;

        this->status = "Queued";
        this->dlTotal = 0.0;
        this->dlSoFar = 0.0;
        this->cancelRequested = false;
        this->id = downloadIdDispenser++;
    }

    bool updateProgress(cpr::cpr_off_t dlTotal, cpr::cpr_off_t dlSoFar, cpr::cpr_off_t ulTotal,
                        cpr::cpr_off_t uSoFar, intptr_t userData) {
        if (this->cancelRequested == true) {
            return false;
        }
        this->dlSoFar = dlSoFar;
        this->dlTotal = dlTotal;
        return true;
    }

    std::string getStatus() {
        std::lock_guard<std::mutex> lock(m);
        return this->status;
    }

    void setStatus(std::string arg) {
        std::lock_guard<std::mutex> lock(m);
        this->status = arg;
    }
};