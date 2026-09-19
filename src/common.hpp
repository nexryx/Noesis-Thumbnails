#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace nt {
namespace fs = std::filesystem;
constexpr unsigned edge = 512;
constexpr uint64_t maxSource = 512ull << 20;
constexpr uint64_t maxCache = 512ull << 20;
constexpr DWORD jobTimeout = 20000;
constexpr wchar_t clsidText[] = L"{53EAB7B8-41B9-462B-90DB-BC16434161C7}";
inline const CLSID providerClsid = {0x53eab7b8,0x41b9,0x462b,{0x90,0xdb,0xbc,0x16,0x43,0x41,0x61,0xc7}};

struct Handle {
    HANDLE h = nullptr;
    Handle() = default;
    explicit Handle(HANDLE value) : h(value) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { reset(); }
    void reset(HANDLE value = nullptr) { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h); h=value; }
    explicit operator bool() const { return h && h != INVALID_HANDLE_VALUE; }
};
inline std::string utf8(const std::wstring& s) {
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);
    if (!n && !s.empty()) throw std::runtime_error("Invalid Unicode");
    std::string out(n,0); WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),out.data(),n,nullptr,nullptr); return out;
}
inline std::wstring wide(const std::string& s) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0);
    if (!n && !s.empty()) throw std::runtime_error("Invalid UTF-8");
    std::wstring out(n,0); MultiByteToWideChar(CP_UTF8,0,s.data(),(int)s.size(),out.data(),n); return out;
}
inline fs::path moduleDirectory(HMODULE module=nullptr) {
    std::vector<wchar_t> b(32768); DWORD n=GetModuleFileNameW(module,b.data(),(DWORD)b.size());
    if (!n || n>=b.size()) throw std::runtime_error("Module path unavailable");
    return fs::path(std::wstring(b.data(),n)).parent_path();
}
inline std::wstring environment(const wchar_t* key) {
    DWORD n=GetEnvironmentVariableW(key,nullptr,0); if (!n) return {};
    std::wstring result(n,0); DWORD written=GetEnvironmentVariableW(key,result.data(),n);
    if (written>=n) throw std::runtime_error("Environment changed"); result.resize(written); return result;
}
inline fs::path cacheDirectory() {
    auto overridePath=environment(L"NT_CACHE_DIR");
    if (!overridePath.empty()) return fs::path(overridePath);
    auto local=environment(L"LOCALAPPDATA");
    if (local.empty()) throw std::runtime_error("LOCALAPPDATA missing");
    return fs::path(local)/L"NoesisThumbnails";
}
inline std::wstring hash(const std::string& data) {
    BCRYPT_ALG_HANDLE alg=nullptr; BCRYPT_HASH_HANDLE ctx=nullptr;
    if (BCryptOpenAlgorithmProvider(&alg,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0) throw std::runtime_error("SHA256 unavailable");
    DWORD size=0, got=0;
    BCryptGetProperty(alg,BCRYPT_OBJECT_LENGTH,(PUCHAR)&size,sizeof(size),&got,0);
    std::vector<unsigned char> object(size); std::array<unsigned char,32> digest{};
    NTSTATUS status=BCryptCreateHash(alg,&ctx,object.data(),size,nullptr,0,0);
    if (status>=0) status=BCryptHashData(ctx,(PUCHAR)data.data(),(ULONG)data.size(),0);
    if (status>=0) status=BCryptFinishHash(ctx,digest.data(),(ULONG)digest.size(),0);
    if (ctx) BCryptDestroyHash(ctx); BCryptCloseAlgorithmProvider(alg,0);
    if (status<0) throw std::runtime_error("SHA256 failed");
    std::wostringstream s; for (auto c:digest) s<<std::hex<<std::setw(2)<<std::setfill(L'0')<<(unsigned)c; return s.str();
}
inline uint64_t stamp(const FILETIME& t) { return (uint64_t(t.dwHighDateTime)<<32)|t.dwLowDateTime; }
inline uint64_t modified(const fs::path& p) {
    WIN32_FILE_ATTRIBUTE_DATA d{}; return GetFileAttributesExW(p.c_str(),GetFileExInfoStandard,&d)?stamp(d.ftLastWriteTime):0;
}
inline uint64_t nowFileTime() { FILETIME t; GetSystemTimeAsFileTime(&t); return stamp(t); }
inline bool exists(const fs::path& p) { DWORD a=GetFileAttributesW(p.c_str()); return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_DIRECTORY); }
inline void remove(const fs::path& p) { DeleteFileW(p.c_str()); }
inline bool localPath(const fs::path& p) {
    auto s=p.wstring(); if(s.size()<3 || s[1]!=L':' || (s[2]!=L'\\' && s[2]!=L'/')) return false;
    auto root=s.substr(0,3); auto type=GetDriveTypeW(root.c_str()); return type==DRIVE_FIXED || type==DRIVE_REMOVABLE;
}
inline std::wstring cacheKey(const fs::path& source, const fs::path& base) {
    if (!localPath(source)) throw std::runtime_error("Only local files are supported");
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(source.c_str(),GetFileExInfoStandard,&d) || (d.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_OFFLINE|0x400000|0x40000)))
        throw std::runtime_error("File unavailable or not hydrated");
    uint64_t size=(uint64_t(d.nFileSizeHigh)<<32)|d.nFileSizeLow;
    if (!size || size>maxSource) throw std::runtime_error("Source file exceeds size budget or is empty");
    // Preserve case: Windows directories may explicitly enable case sensitivity.
    // v4 includes PBR material channels, environment reflections and emission.
    std::string identity="nts-v4-512|"+utf8(source.lexically_normal().wstring())+"|"+std::to_string(size)+"|"+std::to_string(stamp(d.ftLastWriteTime));
    identity+="|"+std::to_string(modified(base/L"noesis-thumbnails.ini"));
    return hash(identity);
}
inline void atomicWrite(const fs::path& destination, const void* data, size_t size) {
    auto temporary=destination; temporary+=L"."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetCurrentThreadId())+L".tmp";
    { std::ofstream f(temporary,std::ios::binary|std::ios::trunc); f.write((const char*)data,size); if(!f) throw std::runtime_error("Cache write failed"); }
    if (!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) { nt::remove(temporary); throw std::runtime_error("Cache publish failed"); }
}
inline void atomicText(const fs::path& p,const std::string& text) { atomicWrite(p,text.data(),text.size()); }
inline std::string readText(const fs::path& p,size_t limit=131072) {
    std::ifstream f(p,std::ios::binary|std::ios::ate); auto size=f.tellg();
    if(size<0 || (uint64_t)size>limit) throw std::runtime_error("Invalid text file");
    std::string s((size_t)size,0); f.seekg(0); f.read(s.data(),s.size()); if(!f) throw std::runtime_error("Text read failed"); return s;
}
// Correct Windows argv escaping, including quotes and trailing backslashes.
inline std::wstring quote(const std::wstring& value) {
    std::wstring out=L"\""; size_t slashes=0;
    for(auto c:value) { if(c==L'\\') { ++slashes; continue; } if(c==L'\"') out.append(slashes*2+1,L'\\'); else out.append(slashes,L'\\'); slashes=0; out+=c; }
    out.append(slashes*2,L'\\'); return out+L"\"";
}
inline PROCESS_INFORMATION launch(const fs::path& exe,const std::vector<std::wstring>& args,DWORD flags=0) {
    std::wstring command=quote(exe.wstring()); for(auto& arg:args) command+=L" "+quote(arg);
    STARTUPINFOW si{}; si.cb=sizeof(si); si.dwFlags=STARTF_USESHOWWINDOW; si.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION pi{};
    if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|flags,nullptr,exe.parent_path().c_str(),&si,&pi)) throw std::runtime_error("Could not start process");
    return pi;
}
inline std::wstring mutexName(const fs::path& cache) { return L"Local\\NoesisThumbnails-"+hash(utf8(cache.wstring())).substr(0,24); }
inline void startBroker(const fs::path& base,const fs::path& cache) {
    Handle running(OpenMutexW(SYNCHRONIZE|MUTEX_MODIFY_STATE,FALSE,mutexName(cache).c_str()));
    if(running) {
        auto status=WaitForSingleObject(running.h,0);
        if(status==WAIT_TIMEOUT)return;
        if(status==WAIT_OBJECT_0||status==WAIT_ABANDONED)ReleaseMutex(running.h);
    }
    auto pi=launch(base/L"noesis-thumbnails.exe",{L"--serve"}); CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
}
inline bool coolingDown(const fs::path& cache,const std::wstring& key) {
    uint64_t t=modified(cache/L"images"/(key+L".fail")); auto now=nowFileTime();
    return t && (t>=now || now-t<300ull*10000000);
}
inline void enqueue(const fs::path& source,const fs::path& base,const fs::path& cache,const std::wstring& key) {
    fs::create_directories(cache/L"queue"); fs::create_directories(cache/L"images");
    auto request=cache/L"queue"/(key+L".job");
    if(!nt::exists(request)) {
        size_t count=0; for(auto& item:fs::directory_iterator(cache/L"queue")) if(item.path().extension()==L".job" && ++count>=256) throw std::runtime_error("Queue full");
        atomicText(request,utf8(source.wstring()));
    }
    startBroker(base,cache);
}
struct Image { uint32_t width=0,height=0; std::vector<uint8_t> pixels; }; // premultiplied BGRA
inline void saveImage(const fs::path& p,const Image& image) {
    std::vector<uint8_t> bytes(16+image.pixels.size());
    uint32_t header[]={0x3142544e,image.width,image.height,1}; std::memcpy(bytes.data(),header,16);
    std::memcpy(bytes.data()+16,image.pixels.data(),image.pixels.size()); atomicWrite(p,bytes.data(),bytes.size());
}
inline Image loadImage(const fs::path& p) {
    // Newly published files can briefly be locked by filesystem filters.
    // Retry at most three times; never wait for rendering on the Shell thread.
    std::ifstream f;
    for(unsigned attempt=0;attempt<4;++attempt){
        f.open(p,std::ios::binary);if(f.is_open())break;
        f.clear();if(attempt<3)Sleep(1);
    }
    uint32_t h[4]{}; f.read((char*)h,16);
    if(!f || h[0]!=0x3142544e || h[3]!=1 || !h[1] || !h[2] || h[1]>1024 || h[2]>1024) throw std::runtime_error("Invalid cache bitmap");
    Image i{h[1],h[2],std::vector<uint8_t>((size_t)h[1]*h[2]*4)}; f.read((char*)i.pixels.data(),i.pixels.size());
    if(!f || f.peek()!=std::char_traits<char>::eof()) throw std::runtime_error("Truncated or oversized cache bitmap"); return i;
}
inline Image resize(const Image& src,unsigned limit) {
    if(!limit) throw std::runtime_error("Invalid thumbnail size");
    if(std::max(src.width,src.height)<=limit) return src;
    float scale=float(limit)/std::max(src.width,src.height);
    Image dst{std::max(1u,unsigned(src.width*scale)),std::max(1u,unsigned(src.height*scale)),{}};
    dst.pixels.resize((size_t)dst.width*dst.height*4);
    for(unsigned y=0;y<dst.height;++y) for(unsigned x=0;x<dst.width;++x) {
        float sx=(x+.5f)*src.width/dst.width-.5f,sy=(y+.5f)*src.height/dst.height-.5f;
        int x0=std::max(0,int(sx)),y0=std::max(0,int(sy)),x1=std::min(x0+1,int(src.width)-1),y1=std::min(y0+1,int(src.height)-1);
        float tx=std::max(0.f,sx-x0),ty=std::max(0.f,sy-y0);
        for(unsigned c=0;c<4;++c) { auto p=[&](int xx,int yy){return src.pixels[((size_t)yy*src.width+xx)*4+c];};
            dst.pixels[((size_t)y*dst.width+x)*4+c]=(uint8_t)((1-ty)*((1-tx)*p(x0,y0)+tx*p(x1,y0))+ty*((1-tx)*p(x0,y1)+tx*p(x1,y1))); }
    } return dst;
}
inline HBITMAP bitmap(const Image& image) {
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth=image.width;
    info.bmiHeader.biHeight=-(LONG)image.height; info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr; HBITMAP result=CreateDIBSection(nullptr,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    if(!result) throw std::runtime_error("DIB allocation failed"); std::memcpy(bits,image.pixels.data(),image.pixels.size()); return result;
}
inline void saveBmp(const fs::path& p,const Image& image) {
    BITMAPFILEHEADER file{}; file.bfType=0x4d42; file.bfOffBits=sizeof(file)+sizeof(BITMAPINFOHEADER); file.bfSize=file.bfOffBits+(DWORD)image.pixels.size();
    BITMAPINFOHEADER info{}; info.biSize=sizeof(info); info.biWidth=image.width; info.biHeight=-(LONG)image.height; info.biPlanes=1; info.biBitCount=32;
    std::ofstream out(p,std::ios::binary); out.write((char*)&file,sizeof(file)); out.write((char*)&info,sizeof(info)); out.write((char*)image.pixels.data(),image.pixels.size());
    if(!out) throw std::runtime_error("BMP write failed");
}
} // namespace nt
