#include "renderer.hpp"
#include <initguid.h>
#include <thumbcache.h>
#include <propsys.h>
#include <iostream>

namespace nt {
inline void log(const fs::path& cache,const std::string& message){
    try{auto path=cache/L"worker.log";if(nt::exists(path)&&fs::file_size(path)>1024*1024)nt::remove(path);
        std::ofstream f(path,std::ios::app);f<<nowFileTime()<<" "<<message<<"\n";}catch(...){}
}
class Noesis {
    fs::path base,work;
    Handle process,job;
public:
    Noesis(fs::path b,fs::path w):base(std::move(b)),work(std::move(w)){}
    ~Noesis(){stop();}
    void stop(){if(process){TerminateProcess(process.h,1);WaitForSingleObject(process.h,2000);process.reset();}job.reset();}
    void start(){
        if(process && WaitForSingleObject(process.h,0)==WAIT_TIMEOUT)return;
        stop();fs::create_directories(work);
        for(const wchar_t* file:{L"request.txt",L"scene.bin",L"scene.tmp",L"error.txt",L"error.tmp",L"stop"})nt::remove(work/file);
        wchar_t value[32768]{};GetPrivateProfileStringW(L"noesis",L"executable",L"",value,32768,(base/L"noesis-thumbnails.ini").c_str());
        fs::path exe(value);if(exe.is_relative())exe=base/exe;
        if(!nt::exists(exe))throw std::runtime_error("Configure a valid Noesis executable in noesis-thumbnails.ini");
        job.reset(CreateJobObjectW(nullptr,nullptr));if(!job)throw std::runtime_error("Cannot create worker job");
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE|JOB_OBJECT_LIMIT_PROCESS_MEMORY;
        limits.ProcessMemoryLimit=1536ull<<20;
        if(!SetInformationJobObject(job.h,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw std::runtime_error("Cannot limit worker");
        auto pi=launch(exe,{L"?runtool",L"Noesis Thumbnails Worker",work.wstring()},CREATE_SUSPENDED|BELOW_NORMAL_PRIORITY_CLASS);
        process.reset(pi.hProcess);Handle thread(pi.hThread);
        if(!AssignProcessToJobObject(job.h,process.h)){stop();throw std::runtime_error("Cannot isolate worker lifetime");}
        ResumeThread(thread.h);
    }
    Image generate(const fs::path& source){
        start();for(auto name:{L"scene.bin",L"error.txt"})nt::remove(work/name);
        atomicText(work/L"request.txt",utf8(source.wstring()));ULONGLONG deadline=GetTickCount64()+jobTimeout;
        while(GetTickCount64()<deadline){
            if(nt::exists(work/L"scene.bin")){auto scene=loadScene(work/L"scene.bin");nt::remove(work/L"scene.bin");return render(std::move(scene));}
            if(nt::exists(work/L"error.txt")){auto error=readText(work/L"error.txt");nt::remove(work/L"error.txt");throw std::runtime_error(error);}
            if(WaitForSingleObject(process.h,25)!=WAIT_TIMEOUT){stop();throw std::runtime_error("Noesis exited before producing a thumbnail; verify bridge installation");}
        }stop();throw std::runtime_error("Noesis exceeded 20 second timeout");
    }
};
inline void prune(const fs::path& cache){
    struct Entry{fs::path path;uint64_t size,time;};std::vector<Entry> files;uint64_t total=0;
    for(auto& e:fs::directory_iterator(cache/L"images")){auto ext=e.path().extension();if(ext!=L".ntb"&&ext!=L".fail")continue;
        auto t=modified(e.path());if(ext==L".fail"&&nowFileTime()-t>300ull*10000000){nt::remove(e.path());continue;}
        auto size=e.file_size();total+=size;files.push_back({e.path(),size,t});}
    std::sort(files.begin(),files.end(),[](const Entry& a,const Entry& b){return a.time<b.time;});
    for(auto& e:files){if(total<=maxCache)break;nt::remove(e.path);total-=e.size;}
}
int serve(const fs::path& base,const fs::path& cache){
    Handle mutex(CreateMutexW(nullptr,FALSE,mutexName(cache).c_str()));if(!mutex)throw std::runtime_error("Broker mutex failed");
    auto acquired=WaitForSingleObject(mutex.h,0);
    if(acquired!=WAIT_OBJECT_0&&acquired!=WAIT_ABANDONED)return 0;
    fs::create_directories(cache/L"queue");fs::create_directories(cache/L"images");
    // One broker per cache. Fixed private work names cannot collide between jobs.
    Noesis noesis(base,cache/L"work");ULONGLONG idle=GetTickCount64();prune(cache);
    while(GetTickCount64()-idle<60000){
        std::vector<fs::path> jobs;for(auto& e:fs::directory_iterator(cache/L"queue"))if(e.path().extension()==L".job")jobs.push_back(e.path());
        if(jobs.empty()){Sleep(50);continue;}
        std::sort(jobs.begin(),jobs.end(),[](const fs::path& a,const fs::path& b){return modified(a)<modified(b);});
        for(auto& path:jobs){idle=GetTickCount64();fs::path source;auto key=path.stem().wstring();
            if(key.size()!=64||key.find_first_not_of(L"0123456789abcdef")!=std::wstring::npos){nt::remove(path);continue;}
            auto output=cache/L"images"/(key+L".ntb");
            try{
                source=fs::path(wide(readText(path)));
                if(cacheKey(source,base)!=key){nt::remove(path);continue;}
                if(!nt::exists(output)&&!coolingDown(cache,key)){
                    auto before=GetTickCount64();auto image=noesis.generate(source);
                    // Do not publish a thumbnail of an older revision.
                    if(cacheKey(source,base)==key){saveImage(output,image);log(cache,"render "+utf8(source.wstring())+" "+std::to_string(GetTickCount64()-before)+"ms");}
                }
                nt::remove(path);
                if(nt::exists(output))SHChangeNotify(SHCNE_UPDATEITEM,SHCNF_PATHW|SHCNF_FLUSHNOWAIT,source.c_str(),nullptr);
            }catch(const std::exception& e){
                log(cache,"failure "+utf8(source.wstring())+" "+e.what());
                try{atomicText(cache/L"images"/(key+L".fail"),e.what());}catch(...){}
                nt::remove(path);
            }
        }prune(cache);idle=GetTickCount64();
    }
    noesis.stop();ReleaseMutex(mutex.h);mutex.reset();
    // Close before the final scan: an enqueue racing with idle shutdown either
    // appears here, or sees no live owner and starts the next broker itself.
    for(auto& e:fs::directory_iterator(cache/L"queue"))if(e.path().extension()==L".job"){startBroker(base,cache);break;}
    return 0;
}
int thumbnail(const fs::path& base,const fs::path& cache,const fs::path& source,const fs::path& output){
    auto key=cacheKey(source,base);auto imagePath=cache/L"images"/(key+L".ntb");auto start=GetTickCount64();bool hit=nt::exists(imagePath);
    if(hit){try{loadImage(imagePath);}catch(...){nt::remove(imagePath);hit=false;}}
    if(!hit){if(coolingDown(cache,key))throw std::runtime_error(readText(cache/L"images"/(key+L".fail")));enqueue(source,base,cache,key);}
    while(!nt::exists(imagePath)){
        if(coolingDown(cache,key))throw std::runtime_error(readText(cache/L"images"/(key+L".fail")));
        if(GetTickCount64()-start>120000)throw std::runtime_error("Queue wait exceeded 120 seconds");Sleep(25);
    }
    auto image=loadImage(imagePath);if(!output.empty())saveBmp(output,image);
    std::cout<<(hit?"cache-hit ":"generated ")<<GetTickCount64()-start<<"ms "<<image.width<<"x"<<image.height<<"\n";return 0;
}
int probe(const fs::path& base,const fs::path& source){
    HMODULE dll=LoadLibraryW((base/L"NoesisThumbnailProvider.dll").c_str());if(!dll)throw std::runtime_error("Provider DLL not found");
    using GetClass=HRESULT (WINAPI*)(REFCLSID,REFIID,void**);auto get=(GetClass)GetProcAddress(dll,"DllGetClassObject");
    IClassFactory* factory=nullptr;IInitializeWithFile* init=nullptr;IThumbnailProvider* provider=nullptr;
    HRESULT hr=get?get(providerClsid,IID_IClassFactory,(void**)&factory):E_FAIL;
    if(SUCCEEDED(hr))hr=factory->CreateInstance(nullptr,IID_IInitializeWithFile,(void**)&init);
    if(SUCCEEDED(hr))hr=init->Initialize(source.c_str(),STGM_READ);
    if(SUCCEEDED(hr))hr=init->QueryInterface(IID_IThumbnailProvider,(void**)&provider);
    HBITMAP image=nullptr;WTS_ALPHATYPE alpha=WTSAT_UNKNOWN;LARGE_INTEGER a,b,freq;QueryPerformanceFrequency(&freq);QueryPerformanceCounter(&a);
    if(SUCCEEDED(hr))hr=provider->GetThumbnail(256,&image,&alpha);QueryPerformanceCounter(&b);
    std::cout<<"HRESULT=0x"<<std::hex<<(uint32_t)hr<<std::dec<<" elapsed="<<(b.QuadPart-a.QuadPart)*1000.0/freq.QuadPart<<"ms alpha="<<alpha<<"\n";
    if(image)DeleteObject(image);if(provider)provider->Release();if(init)init->Release();if(factory)factory->Release();FreeLibrary(dll);return SUCCEEDED(hr)?0:hr==E_PENDING?2:1;
}
int shellProbe(const fs::path& source){
    IShellItemImageFactory* factory=nullptr;
    HRESULT hr=SHCreateItemFromParsingName(source.c_str(),nullptr,IID_PPV_ARGS(&factory));
    HBITMAP image=nullptr;auto begin=GetTickCount64();
    if(SUCCEEDED(hr))hr=factory->GetImage({256,256},SIIGBF_THUMBNAILONLY,&image);
    std::cout<<"Shell HRESULT=0x"<<std::hex<<(uint32_t)hr<<std::dec<<" elapsed="<<GetTickCount64()-begin<<"ms\n";
    if(image)DeleteObject(image);if(factory)factory->Release();return SUCCEEDED(hr)?0:2;
}
}
int wmain(int argc,wchar_t** argv){
    HRESULT com=CoInitializeEx(nullptr,COINIT_MULTITHREADED);int result=1;
    try{
        auto base=nt::moduleDirectory(),cache=nt::cacheDirectory();
        if(argc==2&&std::wstring(argv[1])==L"--serve")result=nt::serve(base,cache);
        else if(argc==4&&std::wstring(argv[1])==L"thumbnail")result=nt::thumbnail(base,cache,nt::fs::absolute(argv[2]).lexically_normal(),nt::fs::absolute(argv[3]).lexically_normal());
        else if(argc==3&&std::wstring(argv[1])==L"cache")result=nt::thumbnail(base,cache,nt::fs::absolute(argv[2]).lexically_normal(),{});
        else if(argc==3&&std::wstring(argv[1])==L"probe")result=nt::probe(base,nt::fs::absolute(argv[2]).lexically_normal());
        else if(argc==3&&std::wstring(argv[1])==L"shell-probe")result=nt::shellProbe(nt::fs::absolute(argv[2]).lexically_normal());
        else if(argc==4&&std::wstring(argv[1])==L"render-scene"){nt::saveBmp(argv[3],nt::render(nt::loadScene(argv[2])));result=0;}
        else{std::cout<<"noesis-thumbnails thumbnail INPUT OUTPUT.bmp\nnoesis-thumbnails cache INPUT\nnoesis-thumbnails probe INPUT\nnoesis-thumbnails shell-probe INPUT\nnoesis-thumbnails render-scene SCENE OUTPUT.bmp\n";result=argc==1?0:1;}
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";}
    if(SUCCEEDED(com))CoUninitialize();return result;
}
