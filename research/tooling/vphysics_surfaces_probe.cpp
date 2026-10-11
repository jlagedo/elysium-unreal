#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <fstream>
#include <iterator>

int main(int argc, char** argv) {
  if (argc != 4) return 2;
  const std::string bin = argv[1]; SetDllDirectoryA(bin.c_str());
  HMODULE dll = LoadLibraryA((bin + "/vphysics.dll").c_str());
  if (!dll) return 3;
  using Factory = void* (__cdecl*)(const char*,int*);
  auto factory = reinterpret_cast<Factory>(GetProcAddress(dll,"CreateInterface"));
  int status=-1; void* service=factory("VPhysicsSurfaceProps001", &status);
  if (!service) return 4;
  auto table=*reinterpret_cast<void***>(service);
  using Parse = int (__thiscall*)(void*,const char*,const char*);
  using Data = void* (__thiscall*)(void*,int);
  using Name = const char* (__thiscall*)(void*,int);
  using Index = int (__thiscall*)(void*,const char*);
  using Scalar = float (__thiscall*)(void*);
  using String = const char* (__thiscall*)(void*,unsigned short);
  std::ifstream stream(argv[2],std::ios::binary);
  if (!stream) return 5;
  std::string source((std::istreambuf_iterator<char>(stream)),std::istreambuf_iterator<char>());
  int count=reinterpret_cast<Parse>(table[1])(service,"scripts/surfaceproperties.txt",source.c_str());
  FILE* output=nullptr; fopen_s(&output,argv[3],"wb"); if (!output) return 6;
  using TemplateCtor = void* (__thiscall*)(void*);
  using TemplateInit = void (__cdecl*)(void*,int,const void*,int,const float*,const void*);
  using TemplateDtor = void (__thiscall*)(void*);
  auto address=[dll](int offset) { return reinterpret_cast<char*>(dll)+offset; };
  for (int i=0;i<count;++i) {
    auto row=static_cast<uint8_t*>(reinterpret_cast<Data>(table[10])(service,i));
    const char* name=reinterpret_cast<Name>(table[7])(service,i);
    auto materialTable=*reinterpret_cast<void***>(row);
    float friction=reinterpret_cast<Scalar>(materialTable[1])(row);
    float elasticity=reinterpret_cast<Scalar>(materialTable[3])(row);
    uint32_t fb,eb; memcpy(&fb,&friction,4); memcpy(&eb,&elasticity,4);
    alignas(8) uint8_t objectTemplate[256]={};
    reinterpret_cast<TemplateCtor>(address(0x41d30))(objectTemplate);
    uint32_t parameters[12]={}; parameters[1]=0x41200000; parameters[2]=0x3f800000;
    parameters[6]=reinterpret_cast<uint32_t>("material-probe");
    float angles[3]={};
    reinterpret_cast<TemplateInit>(address(0x18ba0))(objectTemplate,i,parameters,0,angles,nullptr);
    void* selected=nullptr; memcpy(&selected,objectTemplate+0x18,4);
    fprintf(output,"%s\t",name);
    for(int byte=0;byte<116;++byte) fprintf(output,"%02x",row[0x10+byte]);
    fprintf(output,"\t%08x\t%08x\t%d\n",fb,eb,selected==row);
    // Semantic sound names, not intern/packed identity, are authored observable values.
    fprintf(output,"sounds\t%s",name);
    for (int offset : {44,46,48,50,52,54,58,60,62,64,66,68,70,72,74,76,78,80,82,84,86,88,90,92,94,96,98,100}) {
      unsigned short symbol; memcpy(&symbol,row+0x10+offset,2);
      fprintf(output,"\t%s",reinterpret_cast<String>(table[6])(service,symbol));
    }
    fprintf(output,"\n");
    reinterpret_cast<TemplateDtor>(address(0x41da0))(objectTemplate);
  }
  for (const char* query : {"default", "DEFAULT", "missing", "$material_index_shadow", "$MATERIAL_INDEX_SHADOW", "$other"})
    fprintf(output,"lookup\t%s\t%d\n",query,reinterpret_cast<Index>(table[3])(service,query));
  for (int index : {-1, -2147483647-1, 127, 128, 65535, 0xf000}) {
    void* selected=reinterpret_cast<Data>(table[10])(service,index);
    int rowIndex=-1; for(int i=0;i<count;++i) if(selected==reinterpret_cast<Data>(table[10])(service,i)) rowIndex=i;
    fprintf(output,"index\t%d\t%d\n",index,rowIndex);
  }
  fprintf(output,"dedupe\t%d\n",reinterpret_cast<Parse>(table[1])(service,"scripts/surfaceproperties.txt","other { friction 2 }"));
  fprintf(output,"casefile\t%d\n",reinterpret_cast<Parse>(table[1])(service,"SCRIPTS/SURFACEPROPERTIES.TXT","other { friction 2 }"));
  fclose(output); printf("rows=%d; template material identity checked for each row\n",count);
  return 0;
}
