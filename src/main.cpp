#include <cpr/cpr.h>
#include <string>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <utility>
#include <sstream>

#include "download.h"
#include "manager.h"
enum class loopState {Running, Quit, InputClosed};

bool checkProgress(cpr::cpr_off_t dlTotal, cpr::cpr_off_t dSoFar, cpr::cpr_off_t ulTotal,
                   cpr::cpr_off_t uSoFar, intptr_t userData) {
    float percentage;
    if (dlTotal == 0) {
        percentage = dSoFar;
    } else {
        percentage = (dSoFar * 1.0) / (dlTotal * 1.0) * 100;
    }
    if (dlTotal == 0) {
        std::cout << "Currently downloaded: " << percentage << " bytes ,";
    } else {
        std::cout << percentage << "% " << "complete.";
    }
    if (dlTotal != 0 && percentage > 50) {
        return false;
    }
    return true;
}

void downloadFile() {
    std::ofstream file("downloaded.html", std::ios::binary);
    cpr::Response response = cpr::Download(file, cpr::Url{"https://httpbin.org/bytes/1000000"},
                                           cpr::ProgressCallback(checkProgress, 0));

    if (response.error.code != cpr::ErrorCode::OK) {
        std::cerr << "Error in sending GET request:" << response.error.message;
    }

    else if (response.status_code != 200) {
        std::cerr << "Error in receiving response:" << response.status_code;
    } else {
        file.flush();

        std::cout << std::filesystem::file_size("downloaded.html");
    }
}
int main() {
    DownloadManager manager;
    
    manager.addDownload("https://speed.hetzner.de/100MB.bin", "dl1.bin");
    manager.addDownload("https://proof.ovh.net/files/100Mb.dat", "dl2.bin");
    manager.addDownload("https://speed.hetzner.de/100MB.bin", "dl3.bin");
    manager.addDownload("https://httpbin.org/bytes/10000000", "dl4.bin");
    manager.addDownload("https://speed.hetzner.de/100MB.bin", "dl5.bin");
    manager.addDownload("https://proof.ovh.net/files/100Mb.dat", "dl6.bin");
    manager.addDownload("https://httpbin.org/bytes/10000000", "dl7.bin");
    manager.addDownload("https://speed.hetzner.de/100MB.bin", "dl8.bin");
   loopState state = loopState::Running;
    
    while (state == loopState::Running){
        std::cout<<"PLEASE TYPE COMMAND: ";
        std::string input;
        if (!(std::getline(std::cin, input))){
            state = loopState::InputClosed;
            
        }
       
        else if (input == ""){
            continue;
        }
        else {
            std::string word;
            std::pair <std::string,std::string> p = {"",""};
            std::stringstream ss(input);
            bool wrongComand = false;
            while (ss >> word ){
                if (p.first != "" && p.second != ""){
                    wrongComand = true;
                    
                }
                 else if (p.first == "" && p.second == ""){
                    p.first = word;
                }else{
                    p.second = word;
                    
                }
                
            }
            if (p.first == "quit"){
                state = loopState::Quit;
            }
            else if (wrongComand){
                std::cout<<"Invalid command: Too many arguments"<<std::endl;
            }else if (p.first == "list"){
                 std::cout<<"Snapshot requested"<<std::endl;
                std::vector<DownloadManager::snapshotEntry> overview = manager.getSnapshot();
                
                int i = 0;
                while (i < overview.size()){
                    if (overview[i].dlTotal == 0.0){
                        std::cout<<"Download id: "<< overview[i].id << " "<< "Download url "<<overview[i].url<<" "<< "Download status: " << overview[i].status << " "<< "Progress: "<<overview[i].dlSoFar << "bytes" <<std::endl;
                     }else{
                        std::cout<<"Download id: "<< overview[i].id << " "<< "Download url "<<overview[i].url<<" "<< "Download status: " << overview[i].status << " "<< "Progress: "<<((overview[i].dlSoFar*1.0) /overview[i].dlTotal)*100 << "%" <<std::endl;
                    }
                    i=i+1;
            }
            
        }else {
            std::cout<<"Invalid command"<<std::endl;
            continue;
        }
            
            
        }
        
      
    }
    if (state == loopState::Quit){
    std::cout << "User quit application" << std::endl;
    } if (state == loopState::InputClosed){
        std::cout<<"ERROR: Input Closed"<<std::endl;
    }
     

    return 0;
}