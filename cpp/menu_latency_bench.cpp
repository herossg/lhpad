// Audit only: display callbacks, never Invoke, registration, or editor startup.
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <vector>
#include <algorithm>
#include <string>
using Microsoft::WRL::ComPtr;
struct Counters { unsigned count{}, item{}, attributes{}, path{}; DWORD delay{}; };
class Item final : public IShellItem {
    std::atomic_ulong refs{1}; Counters &c;
public:
    Item(Counters &value):c(value){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void **out) override {if(!out)return E_POINTER;*out=nullptr;if(id!=IID_IUnknown&&id!=IID_IShellItem)return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override{auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE BindToHandler(IBindCtx*,REFGUID,REFIID,void**) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetParent(IShellItem**) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetDisplayName(SIGDN,PWSTR *out) override{++c.path;if(c.delay)Sleep(c.delay); const wchar_t path[]=L"C:\\audit-fixture.txt";*out=(PWSTR)CoTaskMemAlloc(sizeof(path));if(!*out)return E_OUTOFMEMORY;memcpy(*out,path,sizeof(path));return S_OK;}
    HRESULT STDMETHODCALLTYPE GetAttributes(SFGAOF mask,SFGAOF *out) override{++c.attributes;if(c.delay)Sleep(c.delay);*out=mask&SFGAO_FILESYSTEM;return S_OK;}
    HRESULT STDMETHODCALLTYPE Compare(IShellItem*,SICHINTF,int*) override{return E_NOTIMPL;}
};
class Items final : public IShellItemArray {
    std::atomic_ulong refs{1}; Counters &c; DWORD size;
public:
    Items(Counters &value,DWORD n):c(value),size(n){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void **out) override{if(!out)return E_POINTER;*out=nullptr;if(id!=IID_IUnknown&&id!=IID_IShellItemArray)return E_NOINTERFACE;*out=this;AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef() override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release() override{auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE BindToHandler(IBindCtx*,REFGUID,REFIID,void**) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetPropertyStore(GETPROPERTYSTOREFLAGS,REFIID,void**) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetPropertyDescriptionList(REFPROPERTYKEY,REFIID,void**) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetAttributes(SIATTRIBFLAGS,SFGAOF,SFGAOF*) override{return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetCount(DWORD *out) override{++c.count;*out=size;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetItemAt(DWORD index,IShellItem **out) override{++c.item;if(index>=size)return E_INVALIDARG;*out=new Item(c);return S_OK;}
    HRESULT STDMETHODCALLTYPE EnumItems(IEnumShellItems**) override{return E_NOTIMPL;}
};
using Clock=std::chrono::steady_clock;
using GetFactory=HRESULT(__stdcall*)(REFCLSID,REFIID,void**);
static ComPtr<IExplorerCommand> command(GetFactory get,const GUID &id){ComPtr<IClassFactory> f;ComPtr<IExplorerCommand> v;if(FAILED(get(id,IID_PPV_ARGS(&f)))||FAILED(f->CreateInstance(nullptr,IID_PPV_ARGS(&v))))throw 1;return v;}
static double elapsed(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
static void run(GetFactory get,const GUID &id,const char *name,DWORD n,DWORD delay){
    auto root=command(get,id); Counters c{};c.delay=delay;ComPtr<IShellItemArray> items;items.Attach(new Items(c,n));
    PWSTR text{};root->GetTitle(items.Get(),&text);CoTaskMemFree(text);root->GetIcon(items.Get(),&text);CoTaskMemFree(text);root->GetToolTip(items.Get(),&text);CoTaskMemFree(text);
    EXPCMDSTATE state{};auto start=Clock::now();auto fast=root->GetState(items.Get(),FALSE,&state);auto fastMs=elapsed(start);auto fastC=c;
    c={};c.delay=delay;start=Clock::now();auto slow=root->GetState(items.Get(),TRUE,&state);auto slowMs=elapsed(start);
    std::cout<<"{\"target\":\""<<name<<"\",\"selection_count\":"<<n<<",\"injected_delay_per_attribute_or_path_ms\":"<<delay<<",\"fast_hr\":"<<(unsigned long)fast<<",\"fast_ms\":"<<fastMs<<",\"fast_item_calls\":"<<fastC.item<<",\"fast_attribute_calls\":"<<fastC.attributes<<",\"fast_path_calls\":"<<fastC.path<<",\"slow_hr\":"<<(unsigned long)slow<<",\"slow_ms\":"<<slowMs<<",\"slow_count_calls\":"<<c.count<<",\"slow_item_calls\":"<<c.item<<",\"slow_attribute_calls\":"<<c.attributes<<",\"slow_path_calls\":"<<c.path<<"}\n";
}
int wmain(int argc,wchar_t **argv){
    if(argc!=4)return 2;CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    GUID lp{0xd77fb6f2,0x1c04,0x4e31,{0x98,0x68,0x9c,0xb1,0x39,0xf3,0xd2,0xc8}};
    GUID zip=lp;
    for(int i=1;i<=2;++i){auto mod=LoadLibraryW(argv[i]);if(!mod)return 3;auto get=(GetFactory)GetProcAddress(mod,"DllGetClassObject");if(!get)return 4;
        for(auto n:{0ul,1ul,100ul,1000ul})run(get,i==1?lp:zip,i==1?"LHPad-1.1.0":"LHPad-1.1.1",n,0);
        run(get,i==1?lp:zip,i==1?"LHPad-1.1.0":"LHPad-1.1.1",10,10);
        // Real local file: 31 warm samples of the two state callbacks; no file contents read.
        ComPtr<IShellItem> item;ComPtr<IShellItemArray> items;
        if(FAILED(SHCreateItemFromParsingName(argv[3],nullptr,IID_PPV_ARGS(&item)))||FAILED(SHCreateShellItemArrayFromShellItem(item.Get(),IID_PPV_ARGS(&items))))return 5;
        std::vector<double> timings;unsigned pending=0;
        for(int sample=0;sample<31;++sample){auto root=command(get,i==1?lp:zip);EXPCMDSTATE state{};auto start=Clock::now();auto hr=root->GetState(items.Get(),FALSE,&state);if(hr==E_PENDING)++pending;else if(FAILED(hr))return 6;if(FAILED(root->GetState(items.Get(),TRUE,&state)))return 7;timings.push_back(elapsed(start));}
        std::sort(timings.begin(),timings.end());std::cout<<"{\"target\":\""<<(i==1?"LHPad-1.1.0":"LHPad-1.1.1")<<"\",\"real_local_file_samples\":31,\"pending_returns\":"<<pending<<",\"state_pair_median_ms\":"<<timings[15]<<",\"state_pair_max_ms\":"<<timings.back()<<"}\n";
    }
    CoUninitialize();return 0;
}
