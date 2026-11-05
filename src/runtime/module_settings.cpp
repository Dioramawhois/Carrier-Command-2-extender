// SPDX-License-Identifier: GPL-3.0-or-later
#include "module_settings.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <locale>
#include <sstream>
#include <vector>
namespace cc2::runtime {
namespace {
namespace fs=std::filesystem;
constexpr std::size_t maxBytes=65536,maxKeys=128;
bool Fail(std::string* reason,const char* value){if(reason)*reason=value;return false;}
struct Handle {
 HANDLE value=INVALID_HANDLE_VALUE;
 explicit Handle(HANDLE h=INVALID_HANDLE_VALUE):value(h){}
 ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
 Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
};
bool Ordinary(HANDLE file,bool directory){
 BY_HANDLE_FILE_INFORMATION info{};
 return GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&
 ((info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0)==directory&&(directory||info.nNumberOfLinks==1);
}
// Lock every existing directory against rename/delete while opening the leaf.
// Reject junctions/symlinks at any level; never follow a caller-provided path.
struct RootLock {
 std::vector<HANDLE> handles;
 ~RootLock(){for(auto h:handles)CloseHandle(h);}
 bool Open(const std::wstring& root){
 fs::path p(root);if(!p.is_absolute()||p.has_relative_path()==false)return false;
 fs::path current=p.root_path();
 for(const auto& part:p.relative_path()){
 if(part==L"."||part==L"..")return false;current/=part;
 HANDLE h=CreateFileW(current.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,
 FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
 if(h==INVALID_HANDLE_VALUE)return false;
 if(!Ordinary(h,true)){CloseHandle(h);return false;}handles.push_back(h);
 }return !handles.empty();
 }
};
std::string Trim(std::string text){
 auto space=[](char c){return c==' '||c=='\t'||c=='\r';};
 auto first=std::find_if_not(text.begin(),text.end(),space),last=std::find_if_not(text.rbegin(),text.rend(),space).base();
 return first<last?std::string(first,last):std::string{};
}
std::wstring FileName(const std::string& owner){
 // Case-sensitive module identities must never alias on Windows filesystems.
 constexpr wchar_t hex[]=L"0123456789abcdef";std::wstring name=L"settings-";
 for(unsigned char c:owner){name+=hex[c>>4];name+=hex[c&15];}return name+L".ini";
}
}
bool ModuleSettings::SafeName(const std::string& name){
 if(name.empty()||name.size()>96)return false;bool component=false;
 for(char c:name){if(c=='.'){if(!component)return false;component=false;}
 else if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-')component=true;else return false;}
 if(!component)return false;
 auto base=name.substr(0,name.find('.'));for(char& c:base)if(c>='a'&&c<='z')c=static_cast<char>(c-'a'+'A');
 return base!="CON"&&base!="PRN"&&base!="AUX"&&base!="NUL"&&
 !(base.size()==4&&(base.compare(0,3,"COM")==0||base.compare(0,3,"LPT")==0)&&base[3]>='0'&&base[3]<='9');
}
bool ModuleSettings::Read(Values* values,std::string* reason){
 if(!SafeName(owner_))return Fail(reason,"unsafe_module_id");RootLock lock;
 if(!lock.Open(root_))return Fail(reason,"unsafe_or_missing_settings_directory");
 const auto path=fs::path(root_)/FileName(owner_);
 Handle file(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
 if(file.value==INVALID_HANDLE_VALUE){if(GetLastError()==ERROR_FILE_NOT_FOUND){values->clear();return true;}return Fail(reason,"settings_open_failed");}
 LARGE_INTEGER size{};
 if(!Ordinary(file.value,false)||!GetFileSizeEx(file.value,&size)||size.QuadPart<0||size.QuadPart>maxBytes)return Fail(reason,"unsafe_or_oversized_settings");
 std::string text(static_cast<std::size_t>(size.QuadPart),'\0');DWORD read=0;
 if(!ReadFile(file.value,text.data(),static_cast<DWORD>(text.size()),&read,nullptr)||read!=text.size()||text.find('\0')!=std::string::npos)return Fail(reason,"settings_read_failed");
 if(text.compare(0,3,"\xef\xbb\xbf")==0)text.erase(0,3);
 Values staged;std::istringstream input(text);std::string line;
 while(std::getline(input,line)){
 line=Trim(line);if(line.empty()||line[0]=='#'||line[0]==';')continue;
 if(line.front()=='['){if(line!="["+owner_+"]")return Fail(reason,"unexpected_settings_section");continue;}
 const auto equal=line.find('=');if(equal==std::string::npos)return Fail(reason,"expected_key_value");
 const auto key=Trim(line.substr(0,equal)),token=Trim(line.substr(equal+1));
 if(!SafeName(key)||staged.size()>=maxKeys||staged.count(key)||token.empty())return Fail(reason,"invalid_or_duplicate_setting");
 Value v;
 if(token=="true"||token=="false"){v.isBoolean=true;v.boolean=token=="true";}
 else {std::istringstream number(token);number.imbue(std::locale::classic());number>>std::noskipws>>v.number;
 if(!number||!number.eof()||!std::isfinite(v.number))return Fail(reason,"expected_finite_number_or_boolean");}
 staged.emplace(key,v);
 }
 *values=std::move(staged);if(reason)reason->clear();return true;
}
bool ModuleSettings::Reload(std::string* reason){
 std::lock_guard<std::recursive_mutex> l(mutex_);Values staged;if(!Read(&staged,reason))return false;values_=std::move(staged);return true;
}
bool ModuleSettings::Number(const std::string& key,double* out){
 std::lock_guard<std::recursive_mutex> l(mutex_);auto it=values_.find(key);
 if(!out||it==values_.end()||it->second.isBoolean)return false;*out=it->second.number;return true;
}
bool ModuleSettings::Boolean(const std::string& key,bool* out){
 std::lock_guard<std::recursive_mutex> l(mutex_);auto it=values_.find(key);if(!out||it==values_.end())return false;
 const auto& v=it->second;if(v.isBoolean){*out=v.boolean;return true;}
 if(v.number!=0&&v.number!=1)return false;*out=v.number==1;return true;
}
bool ModuleSettings::Write(const Values& values,std::string* reason){
 RootLock lock;if(!SafeName(owner_)||!lock.Open(root_))return Fail(reason,"unsafe_or_missing_settings_directory");
 std::ostringstream text;text.imbue(std::locale::classic());text<<std::setprecision(17);
 for(const auto& item:values){text<<item.first<<'=';const auto& v=item.second;if(v.isBoolean)text<<(v.boolean?"true":"false");else text<<v.number;text<<'\n';}
 const auto bytes=text.str();if(bytes.size()>maxBytes)return Fail(reason,"settings_too_large");
 const auto target=fs::path(root_)/FileName(owner_);
 Handle old(CreateFileW(target.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
 const bool exists=old.value!=INVALID_HANDLE_VALUE;
 if((exists&&!Ordinary(old.value,false))||(!exists&&GetLastError()!=ERROR_FILE_NOT_FOUND))return Fail(reason,"unsafe_settings_target");
 // Windows 7 rename replacement refuses an open destination, even when its
 // handle shares DELETE. Replacement never follows the destination leaf.
 if(exists){CloseHandle(old.value);old.value=INVALID_HANDLE_VALUE;}
 static volatile LONG sequence=0;
 const auto temp=fs::path(root_)/(L".cc2x-settings-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(InterlockedIncrement(&sequence))+L".tmp");
 Handle file(CreateFileW(temp.c_str(),GENERIC_WRITE|DELETE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
 if(file.value==INVALID_HANDLE_VALUE)return Fail(reason,"settings_stage_failed");
 DWORD written=0;bool ok=WriteFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)&&written==bytes.size()&&FlushFileBuffers(file.value);
 // Rename by this exact staged handle, atomically. No path-based replacement
 // of a raced temporary symlink; no partial user settings on write failure.
 if(ok){const auto destination=target.wstring();std::vector<unsigned char> storage(sizeof(FILE_RENAME_INFO)+destination.size()*sizeof(wchar_t));
 auto* rename=reinterpret_cast<FILE_RENAME_INFO*>(storage.data());rename->ReplaceIfExists=exists?TRUE:FALSE;rename->RootDirectory=nullptr;
 rename->FileNameLength=static_cast<DWORD>(destination.size()*sizeof(wchar_t));std::memcpy(rename->FileName,destination.data(),rename->FileNameLength);
 ok=SetFileInformationByHandle(file.value,FileRenameInfo,rename,static_cast<DWORD>(storage.size()))!=0;}
 if(!ok){FILE_DISPOSITION_INFO remove{TRUE};SetFileInformationByHandle(file.value,FileDispositionInfo,&remove,sizeof(remove));return Fail(reason,"settings_commit_failed");}
 if(reason)reason->clear();return true;
}
bool ModuleSettings::Set(const std::string& key,Value value,std::string* reason){
 std::lock_guard<std::recursive_mutex> l(mutex_);if(!SafeName(key))return Fail(reason,"invalid_setting_key");
 Values staged;if(!Read(&staged,reason))return false;
 if(!staged.count(key)&&staged.size()>=maxKeys)return Fail(reason,"too_many_settings");
 staged[key]=value;if(!Write(staged,reason))return false;values_=std::move(staged);return true;
}
bool ModuleSettings::SetNumber(const std::string& key,double value,std::string* reason){
 if(!std::isfinite(value))return Fail(reason,"expected_finite_number");return Set(key,{value,false,false},reason);
}
bool ModuleSettings::SetBoolean(const std::string& key,bool value,std::string* reason){return Set(key,{0,value,true},reason);}
}
