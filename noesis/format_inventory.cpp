// Build against the SDK bundled in the user's Noesis/pluginsource.zip.
// The SDK is deliberately not vendored or redistributed by this project.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>
#include "pluginshare.h"

noePluginFn_t* g_nfn=nullptr;
mathImpFn_t* g_mfn=nullptr;

#include "material_export.hpp"

// Called by the Python bridge while its loaded model's module is active.
// Noesis exposes this reader-supplied preview rotation only through the SDK.
extern "C" __declspec(dllexport) int NT_GetPreviewRotation(int module,float* output,int count){
    if(!g_nfn||!output||count!=9)return 0;
    auto rapi=g_nfn->NPAPI_GetModuleRAPI(module);if(!rapi)return 0;
    modelMatrix_t matrix{};rapi->Noesis_GetPreviewAngleTransform(&matrix);
    const float* rows[]={matrix.x1,matrix.x2,matrix.x3};
    for(int row=0;row<3;++row)for(int col=0;col<3;++col){
        auto value=rows[row][col];if(!std::isfinite(value))return 0;
        // Remove trigonometric roundoff for exact quarter turns.
        if(std::abs(value)<1e-6f)value=0;
        else if(std::abs(value-1)<1e-6f)value=1;
        else if(std::abs(value+1)<1e-6f)value=-1;
        output[row*3+col]=value;
    }
    return 1;
}

static void gather(const std::string& bytes,std::set<std::string>& candidates){
    for(size_t i=0;i<bytes.size();++i){
        if(bytes[i]!='.')continue;size_t end=i+1;
        while(end<bytes.size()&&end-i<=48){char c=bytes[end];if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))break;++end;}
        if(end>i+1&&end-i<=49){auto s=bytes.substr(i,end-i);while(!s.empty()&&s.back()=='.')s.pop_back();if(s.size()>1)candidates.insert(s);}
    }
}
static void gatherFile(const std::filesystem::path& path,std::set<std::string>& candidates){
    std::error_code error;auto size=std::filesystem::file_size(path,error);if(error||size>64*1024*1024)return;
    std::ifstream in(path,std::ios::binary);std::string bytes((size_t)size,0);in.read(bytes.data(),bytes.size());if(!in)return;
    gather(bytes,candidates);
    // Built-in format extension tables may be UTF-16 rather than ASCII.
    for(int parity=0;parity<2;++parity){std::string ascii;ascii.reserve(bytes.size()/2);for(size_t i=parity;i+1<bytes.size();i+=2)ascii+=bytes[i+1]==0?bytes[i]:' ';gather(ascii,candidates);}
}

static int inventory(int,void*) {
    wchar_t destination[MAX_NOESIS_PATH]{};g_nfn->NPAPI_GetSelectedFile(destination);
    if(!destination[0])return 0;
    FILE* out=_wfopen(destination,L"wb");if(!out)return 0;
    g_nfn->NPAPI_PushRAPIContext(nullptr);
    int module=g_nfn->NPAPI_InstantiateModule(nullptr);
    auto rapi=g_nfn->NPAPI_GetModuleRAPI(module);
    if(rapi){
        g_nfn->NPAPI_SetGlobalMemForRAPI(rapi);
        std::set<std::string> candidates;
        // Some handlers are internal and have no extension. Skip these holes.
        int misses=0;
        for(int handle=0;handle<65536;++handle){
            char extensions[MAX_NOESIS_PATH]{};
            if(!rapi->Noesis_GetTypeExtension(extensions,handle)){if(++misses>=256)break;continue;}
            misses=0;
            char* token=extensions;
            while(*token){
                char* end=std::strchr(token,';');if(end)*end=0;
                if(*token)candidates.insert(token);
                if(!end)break;token=end+1;
            }
        }
        // GetTypeExtension enumerates plugin handlers but omits built-in readers.
        // Gather their candidate names from the installed runtime; accept ONLY
        // extensions the live API confirms. Never infer support from strings alone.
        try{
            wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);auto root=std::filesystem::path(executable).parent_path();
            gatherFile(executable,candidates);gatherFile(root/L"noesis.dll",candidates);gatherFile(root/L"noex64"/L"noesis.dll",candidates);
            for(auto& file:std::filesystem::recursive_directory_iterator(root/L"plugins")){
                auto extension=file.path().extension();if(extension==L".dll"||extension==L".py")gatherFile(file.path(),candidates);
            }
        }catch(...){std::fclose(out);g_nfn->NPAPI_FreeModule(module);g_nfn->NPAPI_PopRAPIContext(nullptr);DeleteFileW(destination);return 0;}
        for(const auto& candidate:candidates){
            wchar_t ext[MAX_NOESIS_PATH]{};MultiByteToWideChar(CP_UTF8,0,candidate.c_str(),-1,ext,MAX_NOESIS_PATH);
            int handlers=0;int flags=g_nfn->NPAPI_GetFormatExtensionFlags(ext,&handlers);
            // A second Python tool pass resolves lazily loaded Python readers.
            std::fprintf(out,"%s\t%d\n",candidate.c_str(),flags);
        }
    }
    g_nfn->NPAPI_FreeModule(module);g_nfn->NPAPI_PopRAPIContext(nullptr);std::fclose(out);return 0;
}
extern "C" __declspec(dllexport) bool NPAPI_Init(mathImpFn_t* math,noePluginFn_t* api){
    g_mfn=math;g_nfn=api;api->NPAPI_RegisterTool((char*)"Noesis Thumbnails Formats",inventory,nullptr);return true;
}
extern "C" __declspec(dllexport) void NPAPI_Shutdown(){}
extern "C" __declspec(dllexport) int NPAPI_GetPluginVer(){return NOESIS_PLUGIN_VERSION;}
extern "C" __declspec(dllexport) bool NPAPI_GetPluginInfo(noePluginInfo_t* info){
    strcpy_s(info->pluginName,sizeof(info->pluginName),"noesis_thumbnails_formats");
    strcpy_s(info->pluginDesc,sizeof(info->pluginDesc),"Enumerates installed Noesis thumbnail-capable formats");return true;
}
