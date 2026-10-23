#include <cpr/cpr.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <tuple>

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

        this->status = "Download Queued";
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

    void cancelDownload() {
        if (this->cancelRequested == true) {
            return;
        }
        this->cancelRequested = true;
        return;
    }

    std::string getUrl() { return this->url; }

    int getId() { return this->id; }

    std::tuple<int, std::string, std::string, cpr::cpr_off_t, cpr::cpr_off_t> getSnapshot() {
        std::lock_guard<std::mutex> lock(m);
        return {this->id, this->fileName, this->status, this->dlSoFar, this->dlTotal};
    }

    void download() {
        std::string par = "Download In progress";
        this->setStatus(par);
        std::ofstream file(this->fileName, std::ios::binary);
        auto progressLambda = [this](cpr::cpr_off_t dlTotal, cpr::cpr_off_t dlSoFar,
                                     cpr::cpr_off_t ulTotal, cpr::cpr_off_t uSoFar,
                                     intptr_t userData) {
            return this->updateProgress(dlTotal, dlSoFar, 0, 0, 0);
        };
        cpr::Response response =
            cpr::Download(file, cpr::Url{this->url}, cpr::ProgressCallback(progressLambda, 0));
        if (response.error.code == cpr::ErrorCode::ABORTED_BY_CALLBACK) {
            par = "Download Cancelled";
            this->setStatus(par);
        } else if (response.error.code != cpr::ErrorCode::OK) {
            par = "Download Failed";
            this->setStatus(par);
        } else if (response.status_code != 200) {
            par = "Download Failed";
            this->setStatus(par);
        } else {
            file.flush();
            par = "Download Successful";
            this->setStatus(par);
        }
        return;
    }
};