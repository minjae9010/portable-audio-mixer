#include "cable_installer.hpp"
#include <bcrypt.h>
#include <iostream>
#include <array>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <fstream>

std::string sha256(const void* bytes, DWORD size) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    if(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) throw std::runtime_error("SHA256 provider");
    std::array<unsigned char,32> digest{};
    const auto result=BCryptHash(algorithm,nullptr,0,static_cast<PUCHAR>(const_cast<void*>(bytes)),size,digest.data(),static_cast<ULONG>(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm,0);
    if(result<0) throw std::runtime_error("SHA256 hash");
    std::ostringstream out;
    for(auto c:digest) out<<std::hex<<std::setfill('0')<<std::setw(2)<<unsigned(c);
    return out.str();
}
int main(int argc,char** argv) {
    HMODULE module=nullptr;
    try {
        if(argc!=2) throw std::runtime_error("Expected app path");
        module=LoadLibraryExA(argv[1],nullptr,LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        if(!module) throw std::runtime_error("Read app resources");
        unsigned checked=0;
        for(const auto& entry:cable_bundle::entries) {
            const auto data=cable_installer::resource(module,entry);
            if(sha256(data.bytes,data.size)!=entry.sha256) throw std::runtime_error("Embedded vendor file differs from original package");
            ++checked;
        }
        FreeLibrary(module); module=nullptr;
        std::cout<<checked<<" embedded vendor files match the pinned original package. Installer was not executed.\n";
        return 0;
    } catch(const std::exception& error) {
        if(module) FreeLibrary(module);
        std::cerr<<error.what()<<'\n'; return 1;
    }
}
