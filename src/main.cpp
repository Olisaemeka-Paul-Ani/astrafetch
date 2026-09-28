#include <iostream>
#include <filesystem>
#include <fstream>
#include <cpr/cpr.h>

int main(){
  std::ofstream file( "downloaded.html", std::ios::binary);
  cpr::Response response = cpr::Download(file, cpr::Url{"https://example.com"});
  

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