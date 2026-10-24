#include <iostream>
#include <memory>
#include <queue>

#include "download.h"

class DownloadManager {
    std::queue<std::shared_ptr<Download>> q;
};