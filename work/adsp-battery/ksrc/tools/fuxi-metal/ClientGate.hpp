/* Original fail-closed client policy; no GPU or Objective-C dependencies. */
#pragma once
#include <cstdio>
#include <cstdint>
#include <cerrno>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#endif
namespace fuxi {
enum class ClientMode { deny, allow, census };
static inline bool systemString(const char *key,char *out,size_t capacity) {
#ifdef __APPLE__
 size_t n=capacity;bool ok=!sysctlbyname(key,out,&n,nullptr,0) && n>0 && n<=capacity &&
  out[n-1]==0 && strlen(out)==n-1;
 if(!ok && capacity)out[0]=0;return ok;
#else
 (void)key;if(capacity)out[0]=0;return false;
#endif
}
static inline unsigned backboardMode(const char *args) {
 if(!args)return 0;unsigned keys=0,mode=0;
 for(const char *p=args;*p;) {
  p+=strspn(p," \t\r\n");size_t n=strcspn(p," \t\r\n");
  if(n>=10 && !strncmp(p,"fuxibbgpu=",10)) {++keys;mode=n==11 && (p[10]=='1' || p[10]=='2')?unsigned(p[10]-'0'):0;}
  p+=n;
 }
 return keys==1?mode:0;
}
static inline bool backboardArmed(const char *args){return backboardMode(args)!=0;}
static inline unsigned systemBackboardMode(){char args[4096]={};systemString("kern.bootargs",args,sizeof(args));return backboardMode(args);}
static inline bool clientName(const char *s) {
 size_t n=strlen(s);if(!n || n>63)return false;
 for(size_t i=0;i<n;++i)if(!((s[i]>='a'&&s[i]<='z')||(s[i]>='A'&&s[i]<='Z')||
     (s[i]>='0'&&s[i]<='9')||s[i]=='_'||s[i]=='-'||s[i]=='.'))return false;
 return true;
}
static inline char asciiLower(char c) {
 return (c>='A' && c<='Z')?char(c+('a'-'A')):c;
}
static inline bool equalsIgnoreCase(const char *a,const char *b) {
 if(!a || !b)return false;
 while(*a && *b){if(asciiLower(*a)!=asciiLower(*b))return false;++a;++b;}
 return *a==0 && *b==0;
}
static inline bool containsIgnoreCase(const char *haystack,const char *needle) {
 if(!haystack || !needle || !*needle)return false;
 size_t nlen=strlen(needle);
 for(const char *p=haystack;*p;++p) {
  size_t i=0;
  while(i<nlen && p[i] && asciiLower(p[i])==asciiLower(needle[i]))++i;
  if(i==nlen)return true;
 }
 return false;
}
// Processes that must never see a Metal device, not even a census one. HackiOS
// (hackios-xnu-userland, MIT) observed both aborting on a visible device:
// mediaserverd's CMCapture preloads AppleCVAPhoto's shader library (28 aborts
// per boot) and SpringBoard's Siri orb prewarms its shaders (abort every
// 8.3 s). U-026 Phase A also permanently blocks all *poster* wallpaper extensions (case-insensitive).
static inline bool hiddenClient(const char *s) {
 if(!s || !*s)return true;
 if(equalsIgnoreCase(s,"SpringBoard") || equalsIgnoreCase(s,"mediaserverd"))return true;
 if(containsIgnoreCase(s,"poster"))return true;
 return false;
}
static inline bool parseAdtClientsData(const uint8_t *bytes,size_t len,char *out,size_t capacity) {
 if(!out || !capacity)return false;
 out[0]=0;
 if(!bytes || !len || len>=capacity)return false;
 if(bytes[len-1]!=0)return false;
 if(memchr(bytes,0,len-1)!=nullptr)return false;
 if(bytes[0]==0)return false;
 memcpy(out,bytes,len);
 return true;
}
static inline bool matchAllowlistToken(const char *list,const char *process) {
 if(!list || !*list || !process || hiddenClient(process) || !clientName(process))return false;
 const size_t plen=strlen(process);
 for(const char *p=list;*p;) {
  while(*p==',' || *p==' ' || *p=='\t' || *p=='\r' || *p=='\n')++p;
  if(!*p)break;
  const char *end=p;
  while(*end && *end!=',' && *end!=' ' && *end!='\t' && *end!='\r' && *end!='\n')++end;
  if(size_t(end-p)==plen && !strncmp(p,process,plen))return true;
  p=end;
 }
 return false;
}
static inline bool systemAdtGpuClients(char *out,size_t capacity) {
 if(!out || !capacity)return false;
 out[0]=0;
#ifdef __APPLE__
 void *io=dlopen("/System/Library/Frameworks/IOKit.framework/IOKit",RTLD_NOW|RTLD_LOCAL);
 void *cf=dlopen("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation",RTLD_NOW|RTLD_LOCAL);
 bool ok=false;
 if(io && cf) {
  auto entryFromPath=(uint32_t(*)(uint32_t,const char *))dlsym(io,"IORegistryEntryFromPath");
  auto copyProp=(const void *(*)(uint32_t,const void *,const void *,uint32_t))dlsym(io,"IORegistryEntryCreateCFProperty");
  auto objRelease=(int(*)(uint32_t))dlsym(io,"IOObjectRelease");
  auto cfStrCreate=(const void *(*)(const void *,const char *,uint32_t))dlsym(cf,"CFStringCreateWithCString");
  auto cfRelease=(void(*)(const void *))dlsym(cf,"CFRelease");
  auto cfGetTypeID=(unsigned long(*)(const void *))dlsym(cf,"CFGetTypeID");
  auto cfDataTypeID=(unsigned long(*)())dlsym(cf,"CFDataGetTypeID");
  auto cfStrTypeID=(unsigned long(*)())dlsym(cf,"CFStringGetTypeID");
  auto cfDataLen=(long(*)(const void *))dlsym(cf,"CFDataGetLength");
  auto cfDataPtr=(const uint8_t *(*)(const void *))dlsym(cf,"CFDataGetBytePtr");
  auto cfStrC=(int(*)(const void *,char *,long,uint32_t))dlsym(cf,"CFStringGetCString");
  if(entryFromPath && copyProp && objRelease && cfStrCreate && cfRelease && cfGetTypeID) {
   uint32_t chosen=entryFromPath(0,"IODeviceTree:/chosen");
   if(chosen) {
    const void *key=cfStrCreate(nullptr,"fuxi,gpu-clients",0x08000100u);
    if(key) {
     const void *val=copyProp(chosen,key,nullptr,0);
     if(val) {
      unsigned long tid=cfGetTypeID(val);
      if(cfDataTypeID && cfDataLen && cfDataPtr && tid==cfDataTypeID()) {
       long n=cfDataLen(val);
       if(n>0)ok=parseAdtClientsData(cfDataPtr(val),(size_t)n,out,capacity);
      } else if(cfStrTypeID && cfStrC && tid==cfStrTypeID()) {
       ok=cfStrC(val,out,(long)capacity,0x08000100u)!=0 && out[0]!=0 && strlen(out)<capacity-1;
      }
      cfRelease(val);
     }
     cfRelease(key);
    }
    objRelease(chosen);
   }
  }
 }
 if(io)dlclose(io);
 if(cf)dlclose(cf);
 return ok;
#else
 return false;
#endif
}
static inline ClientMode parseClients(char *data,size_t length,const char *process,const char *bootargs=nullptr,const char *adtClients=nullptr) {
 bool backboard=!strcmp(process,"backboardd");
 if(hiddenClient(process) || (backboard && !backboardArmed(bootargs)))return ClientMode::deny;
 if(!clientName(process)||memchr(data,0,length))return ClientMode::deny;
 data[length]=0;ClientMode result=ClientMode::deny;
 for(char *line=data;line;) {
  char *next=strchr(line,'\n');if(next)*next++=0;
  size_t n=strlen(line);while(n && (line[n-1]==' '||line[n-1]=='\t'||line[n-1]=='\r'))line[--n]=0;
  while(*line==' '||*line=='\t')++line;
  if(*line && *line!='#') {
   ClientMode mode=ClientMode::allow;
   if(!strncmp(line,"census ",7)) {mode=ClientMode::census;line+=7;while(*line==' '||*line=='\t')++line;}
   if(!clientName(line))return ClientMode::deny;
   if(!strcmp(line,process)) {
    if(result!=ClientMode::deny && result!=mode)return ClientMode::deny;
    result=mode;
   }
  }
  line=next;
 }
 if(result==ClientMode::deny && !backboard && adtClients && matchAllowlistToken(adtClients,process))
  result=ClientMode::allow;
 return backboard && result!=ClientMode::allow?ClientMode::deny:result;
}
static inline ClientMode readClients(const char *path,const char *process,const char *adtOverride=nullptr) {
 if(hiddenClient(process))return ClientMode::deny;
 char adtBuf[256]={};
 const char *adt=adtOverride;
 if(!adt && strcmp(process,"backboardd") && systemAdtGpuClients(adtBuf,sizeof(adtBuf)))adt=adtBuf;
 int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
 if(fd<0) {
  if(errno==ENOENT && strcmp(process,"backboardd") && adt && adt[0]) {
   char empty[1]={0};return parseClients(empty,0,process,nullptr,adt);
  }
  return ClientMode::deny;
 }
 struct stat st={};char data[8193];size_t used=0;bool ok=false;
 if(!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==0&&!(st.st_mode&0022)&&st.st_size<=8192) {
  ssize_t n;while(used<sizeof(data)-1 && (n=read(fd,data+used,sizeof(data)-1-used))>0)used+=(size_t)n;
  char tail;ok=read(fd,&tail,1)==0 && used==(size_t)st.st_size;
 }
 char args[4096]={};if(!strcmp(process,"backboardd"))systemString("kern.bootargs",args,sizeof(args));
 close(fd);return ok?parseClients(data,used,process,args,adt):ClientMode::deny;
}
}
