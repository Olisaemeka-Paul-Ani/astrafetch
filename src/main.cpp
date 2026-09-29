#include <iostream>
#include <filesystem>
#include <fstream>
#include <cpr/cpr.h>


bool checkProgress(cpr::cpr_off_t dlTotal, cpr::cpr_off_t dSoFar, cpr::cpr_off_t ulTotal, cpr::cpr_off_t uSoFar,intptr_t userData ){
  float percentage;
  if (dlTotal == 0){
    percentage = dSoFar;
  }else{
   percentage = (dSoFar*1.0)/(dlTotal*1.0)*100;
  }
  if (dlTotal == 0){
    std::cout<< "Currently downloaded: " << percentage << "bytes";
  }else{
  std::cout<<percentage << "% "<< "complete.";}
  if (dlTotal != 0 && percentage >50){
    return false;
  }
  return true;
}
int main(){
  std::ofstream file( "downloaded.html", std::ios::binary);
  cpr::Response response = cpr::Download(file, 
  cpr::Url{"https://httpbin.org/bytes/1000000"},
  cpr::ProgressCallback(checkProgress, 0));
  

if (response.error.code !=  cpr::ErrorCode::OK){
  std::cerr<<"Error in sending GET request:"<< response.error.message;
  return 1;
}

if (response.status_code != 200){
  std::cerr<<"Error in receiving response:"<<response.status_code;
  return 1;
}
file.close();

std::cout<< std::filesystem::file_size("downloaded.html");
return 0;


} 