#include <cpr/cpr.h>

#include <atomic>
#include <string>

class Download {
   private:
    std::string url;
    std::string fileName;
    std::string status;
    cpr::cpr_off_t dlTotal;
    cpr::cpr_off_t dlSoFar;
    bool cancelRequested;
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
};