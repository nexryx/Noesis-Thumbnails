#include "common.hpp"
#include <msi.h>
#include <msiquery.h>
#include <tlhelp32.h>

namespace {
constexpr wchar_t backupRoot[]=L"Software\\NoesisThumbnails\\HandlerBackup";
constexpr wchar_t slot[]=L"\\ShellEx\\{E357FCCD-A995-4576-B01F-234630154E96}";
struct Key {
    HKEY h=nullptr;
    Key(const std::wstring& path,bool create=true){
        LSTATUS code=create?RegCreateKeyExW(HKEY_CURRENT_USER,path.c_str(),0,nullptr,0,KEY_READ|KEY_WRITE|KEY_WOW64_64KEY,nullptr,&h,nullptr):RegOpenKeyExW(HKEY_CURRENT_USER,path.c_str(),0,KEY_READ|KEY_WRITE|KEY_WOW64_64KEY,&h);
        if(code!=ERROR_SUCCESS && (create||code!=ERROR_FILE_NOT_FOUND))throw std::runtime_error("Registry open failed");
    }
    ~Key(){if(h)RegCloseKey(h);}
};
void set(HKEY key,const wchar_t* name,DWORD type,const void* bytes,DWORD size){if(RegSetValueExW(key,name,0,type,(const BYTE*)bytes,size)!=ERROR_SUCCESS)throw std::runtime_error("Registry write failed");}
void number(HKEY key,const wchar_t* name,DWORD n){set(key,name,REG_DWORD,&n,sizeof(n));}
DWORD number(HKEY key,const wchar_t* name){DWORD n=0,size=sizeof(n);RegQueryValueExW(key,name,nullptr,nullptr,(BYTE*)&n,&size);return n;}
struct Value {bool present=false;DWORD type=REG_SZ;std::vector<BYTE> bytes;};
Value read(HKEY key,const wchar_t* name){
    Value v;DWORD size=0;auto status=RegQueryValueExW(key,name,nullptr,&v.type,nullptr,&size);
    if(status==ERROR_FILE_NOT_FOUND)return v;
    if(status!=ERROR_SUCCESS||size>65536)throw std::runtime_error("Registry value read failed");
    v.present=true;v.bytes.resize(size);if(RegQueryValueExW(key,name,nullptr,&v.type,v.bytes.data(),&size)!=ERROR_SUCCESS)throw std::runtime_error("Registry value changed");return v;
}
bool owns(HKEY key){auto v=read(key,L"");return v.present&&v.type==REG_SZ&&v.bytes.size()==sizeof(nt::clsidText)&&std::memcmp(v.bytes.data(),nt::clsidText,sizeof(nt::clsidText))==0;}
std::wstring property(MSIHANDLE install,const wchar_t* name){DWORD length=0;wchar_t empty=0;MsiGetPropertyW(install,name,&empty,&length);std::wstring value(length+1,0);DWORD capacity=length+1;if(MsiGetPropertyW(install,name,value.data(),&capacity)!=ERROR_SUCCESS)throw std::runtime_error("MSI property unavailable");value.resize(capacity);return value;}
nt::fs::path folder(MSIHANDLE install){
    auto value=property(install,L"CustomActionData");nt::fs::path path(value);path=path.lexically_normal();
    // This MSI intentionally has a fixed per-user location; reject injected paths.
    auto expected=nt::fs::path(nt::environment(L"LOCALAPPDATA"))/L"Programs"/L"NoesisThumbnails";
    auto trim=[](std::wstring s){while(!s.empty()&&(s.back()==L'\\'||s.back()==L'/'))s.pop_back();return s;};
    if(_wcsicmp(trim(path.wstring()).c_str(),trim(expected.wstring()).c_str()))throw std::runtime_error("Unexpected install path");return expected;
}
std::vector<std::wstring> extensions(const nt::fs::path& root){
    std::ifstream file(root/L"extensions.txt");if(!file)throw std::runtime_error("Missing extension inventory");std::string line;std::vector<std::wstring> result;
    while(std::getline(file,line)){
        if(!line.empty()&&line.back()=='\r')line.pop_back();if(line.empty())continue;
        if(line.size()>49||line[0]!='.'||line.find_first_not_of(".abcdefghijklmnopqrstuvwxyz0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid extension inventory");
        result.push_back(nt::wide(line));
    }if(result.empty())throw std::runtime_error("Empty extension inventory");return result;
}
std::wstring hookPath(const std::wstring& ext){return L"Software\\Classes\\"+ext+slot;}
void registerHooks(const nt::fs::path& root){
    for(auto& ext:extensions(root)){
        Key hook(hookPath(ext));Key backup(std::wstring(backupRoot)+L"\\"+ext);
        if(!number(backup.h,L"Captured")){
            auto original=read(hook.h,L"");number(backup.h,L"Present",original.present);number(backup.h,L"Type",original.type);
            if(original.present)set(backup.h,L"Original",REG_BINARY,original.bytes.data(),(DWORD)original.bytes.size());
            number(backup.h,L"Captured",1);
        }
        set(hook.h,L"",REG_SZ,nt::clsidText,sizeof(nt::clsidText));
    }
}
void unregisterHooks(){
    Key root(backupRoot,false);if(!root.h)return;
    for(DWORD i=0;;++i){wchar_t ext[256]{};DWORD count=256;auto status=RegEnumKeyExW(root.h,i,ext,&count,nullptr,nullptr,nullptr,nullptr);if(status==ERROR_NO_MORE_ITEMS)break;if(status!=ERROR_SUCCESS)throw std::runtime_error("Backup enumeration failed");
        Key backup(std::wstring(backupRoot)+L"\\"+ext,false);Key hook(hookPath(ext),false);
        if(!number(backup.h,L"Captured")||!hook.h||!owns(hook.h))continue;
        if(number(backup.h,L"Present")){auto original=read(backup.h,L"Original");if(!original.present)throw std::runtime_error("Incomplete registry backup");set(hook.h,L"",number(backup.h,L"Type"),original.bytes.data(),(DWORD)original.bytes.size());}
        else{auto status=RegDeleteValueW(hook.h,L"");if(status!=ERROR_SUCCESS&&status!=ERROR_FILE_NOT_FOUND)throw std::runtime_error("Registry restoration failed");}
    }
}
void notify(){SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,nullptr,nullptr);}
void stop(const nt::fs::path& root){
    auto target=root/L"noesis-thumbnails.exe";nt::Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0));if(!snapshot)return;
    PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
    if(Process32FirstW(snapshot.h,&entry))do{
        if(_wcsicmp(entry.szExeFile,L"noesis-thumbnails.exe"))continue;
        nt::Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_TERMINATE|SYNCHRONIZE,FALSE,entry.th32ProcessID));if(!process)continue;
        wchar_t name[32768];DWORD size=32768;
        if(QueryFullProcessImageNameW(process.h,0,name,&size)&&!_wcsicmp(name,target.c_str())){TerminateProcess(process.h,0);WaitForSingleObject(process.h,5000);}
    }while(Process32NextW(snapshot.h,&entry));
}
// Never follow directory junctions while removing generated data.
void removeTree(const nt::fs::path& path){
    auto attrs=GetFileAttributesW(path.c_str());if(attrs==INVALID_FILE_ATTRIBUTES)return;
    if(!(attrs&FILE_ATTRIBUTE_DIRECTORY)){SetFileAttributesW(path.c_str(),FILE_ATTRIBUTE_NORMAL);if(!DeleteFileW(path.c_str()))throw std::runtime_error("Could not delete generated file");return;}
    if(!(attrs&FILE_ATTRIBUTE_REPARSE_POINT))for(auto& item:nt::fs::directory_iterator(path))removeTree(item.path());
    if(!RemoveDirectoryW(path.c_str()))throw std::runtime_error("Could not delete generated directory");
}
void log(MSIHANDLE install,const std::wstring& message){auto record=MsiCreateRecord(1);MsiRecordSetStringW(record,0,L"Noesis Thumbnails: [1]");MsiRecordSetStringW(record,1,message.c_str());MsiProcessMessage(install,INSTALLMESSAGE_INFO,record);MsiCloseHandle(record);}
template<class F>UINT action(MSIHANDLE install,F operation){try{operation();return ERROR_SUCCESS;}catch(const std::exception& e){log(install,nt::wide(e.what()));return ERROR_INSTALL_FAILURE;}catch(...){log(install,L"Unexpected installer error");return ERROR_INSTALL_FAILURE;}}
}
extern "C" __declspec(dllexport) UINT __stdcall StopWorker(MSIHANDLE install){return action(install,[&]{stop(folder(install));});}
extern "C" __declspec(dllexport) UINT __stdcall RegisterHooks(MSIHANDLE install){return action(install,[&]{registerHooks(folder(install));notify();});}
extern "C" __declspec(dllexport) UINT __stdcall UnregisterHooks(MSIHANDLE install){return action(install,[&]{folder(install);unregisterHooks();notify();});}
extern "C" __declspec(dllexport) UINT __stdcall RollbackInstall(MSIHANDLE install){return action(install,[&]{folder(install);unregisterHooks();RegDeleteTreeW(HKEY_CURRENT_USER,backupRoot);notify();});}
extern "C" __declspec(dllexport) UINT __stdcall Cleanup(MSIHANDLE install){return action(install,[&]{
    auto root=folder(install);stop(root);
    removeTree(nt::fs::path(nt::environment(L"LOCALAPPDATA"))/L"NoesisThumbnails");
    // Remove generated Python bytecode and empty directories left by Noesis.
    removeTree(root);
    RegDeleteTreeW(HKEY_CURRENT_USER,backupRoot);notify();
});}
