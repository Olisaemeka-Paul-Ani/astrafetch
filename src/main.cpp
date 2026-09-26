#include <iostream>
#include <cpr/cpr.h>

int main(){
  cpr::Response response = cpr::Get(
    cpr::Url{"https://example.com"}
  );
   if (response.error.code != cpr::ErrorCode::OK){
     std::cerr << "Transport-level failure: " << response.error.message << std::endl;
     return 1;
   }

   if (response.status_code != 200 ){
    std::cerr << "HTTP-level failure: " << response.status_code<< std::endl;
    return 1;
   }

   std::cout << response.text.length() << std::endl;
   return 0;

} 