#include "common.hpp"
#include <initguid.h>
#include <thumbcache.h>
#include <propsys.h>
#include <atomic>

static HMODULE module;
static std::atomic<long> objects{0};
static std::atomic<long> locks{0};

class Provider final : public IThumbnailProvider, public IInitializeWithFile, public IInitializeWithItem {
    std::atomic<ULONG> refs{1};
    nt::fs::path source;
    bool initialized=false;
public:
    Provider(){++objects;}
    ~Provider(){--objects;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(id==IID_IUnknown||id==IID_IThumbnailProvider)*out=static_cast<IThumbnailProvider*>(this);
        else if(id==IID_IInitializeWithFile)*out=static_cast<IInitializeWithFile*>(this);
        else if(id==IID_IInitializeWithItem)*out=static_cast<IInitializeWithItem*>(this);
        else return E_NOINTERFACE;AddRef();return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override {return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override {auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE Initialize(LPCWSTR path,DWORD) override {
        if(initialized)return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        if(!path||!*path)return E_INVALIDARG;
        try{source=path;if(!nt::localPath(source))return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);initialized=true;return S_OK;}catch(...){return E_FAIL;}
    }
    HRESULT STDMETHODCALLTYPE Initialize(IShellItem* item,DWORD mode) override {
        if(initialized)return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
        if(!item)return E_INVALIDARG;
        PWSTR path=nullptr;HRESULT hr=item->GetDisplayName(SIGDN_FILESYSPATH,&path);if(FAILED(hr))return hr;
        hr=Initialize(path,mode);CoTaskMemFree(path);return hr;
    }
    HRESULT STDMETHODCALLTYPE GetThumbnail(UINT cx,HBITMAP* output,WTS_ALPHATYPE* alpha) override {
        if(!output||!alpha)return E_POINTER;*output=nullptr;*alpha=WTSAT_UNKNOWN;
        if(!initialized)return E_UNEXPECTED;if(!cx)return E_INVALIDARG;
        try {
            auto base=nt::moduleDirectory(module),cache=nt::cacheDirectory();auto key=nt::cacheKey(source,base);
            auto bitmapPath=cache/L"images"/(key+L".ntb");
            if(nt::exists(bitmapPath)){
                try{auto image=nt::resize(nt::loadImage(bitmapPath),std::min(cx,nt::edge));*output=nt::bitmap(image);*alpha=WTSAT_ARGB;return S_OK;}
                catch(...){nt::remove(bitmapPath);}
            }
            if(nt::coolingDown(cache,key))return HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED);
            nt::enqueue(source,base,cache,key);
            // Never wait for Noesis. The broker sends SHCNE_UPDATEITEM on completion.
            return E_PENDING;
        }catch(const std::bad_alloc&){return E_OUTOFMEMORY;}catch(...){return E_FAIL;}
    }
};
class Factory final:public IClassFactory {
    std::atomic<ULONG> refs{1};
public:
    Factory(){++objects;}~Factory(){--objects;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(id!=IID_IUnknown&&id!=IID_IClassFactory)return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer,REFIID id,void** out)override{
        if(!out)return E_POINTER;*out=nullptr;if(outer)return CLASS_E_NOAGGREGATION;
        try{auto p=new Provider;auto hr=p->QueryInterface(id,out);p->Release();return hr;}catch(...){return E_OUTOFMEMORY;}
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock)override{if(lock)++locks;else --locks;return S_OK;}
};
extern "C" HRESULT STDMETHODCALLTYPE DllGetClassObject(REFCLSID clsid,REFIID id,void** out){
    if(!out)return E_POINTER;*out=nullptr;if(clsid!=nt::providerClsid)return CLASS_E_CLASSNOTAVAILABLE;
    try{auto f=new Factory;auto hr=f->QueryInterface(id,out);f->Release();return hr;}catch(...){return E_OUTOFMEMORY;}
}
extern "C" HRESULT STDMETHODCALLTYPE DllCanUnloadNow(){return objects==0&&locks==0?S_OK:S_FALSE;}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){module=instance;DisableThreadLibraryCalls(instance);}return TRUE;}
