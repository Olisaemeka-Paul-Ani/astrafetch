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
};