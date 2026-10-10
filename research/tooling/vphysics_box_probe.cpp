#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
int main(int argc, char** argv) {
  if (argc != 3 && argc != 9) return 2;
  const std::string bin = argv[1];
  SetDllDirectoryA(bin.c_str());
  HMODULE dll = LoadLibraryA((bin + "/vphysics.dll").c_str());
  if (!dll) { printf("LoadLibrary error %lu\n", GetLastError()); return 3; }
  using Factory = void* (__cdecl*)(const char*,int*);
  auto factory = reinterpret_cast<Factory>(GetProcAddress(dll,"CreateInterface"));
  int status=-1; void* service=factory("VPhysicsCollision007", &status);
  printf("service=%p status=%d\n",service,status); fflush(stdout);
  if (!service) return 4;
  using BBox = void* (__thiscall*)(void*,const float*,const float*);
  auto bbox = reinterpret_cast<BBox>((*reinterpret_cast<void***>(service))[19]);
  float mins[3]={-16,-16,0}, maxs[3]={16,16,72};
  if (argc == 9) for (int i=0;i<3;++i) { mins[i]=static_cast<float>(atof(argv[3+i])); maxs[i]=static_cast<float>(atof(argv[6+i])); }
  void* shape=bbox(service,mins,maxs);
  if (!shape) return 5;
  uint32_t packed=0; memcpy(&packed,static_cast<char*>(shape)+0x1c,4);
  uint32_t size=packed>>8;
  printf("shape=%p size=%u\n",shape,size); fflush(stdout);
  if (!size || size > 1024*1024) return 6;
  FILE* file=nullptr; fopen_s(&file,argv[2],"wb");
  if (!file) return 7;
  fwrite(shape,1,size,file); fclose(file);
  return 0;
}
