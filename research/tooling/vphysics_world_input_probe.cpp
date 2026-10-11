// Read-only instrument for the recovered Collision007 public query protocol.
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

static std::vector<unsigned char> Read(const char* Path)
{
  FILE* File = nullptr; fopen_s(&File, Path, "rb");
  if (!File) return {};
  fseek(File, 0, SEEK_END); const long Size = ftell(File); rewind(File);
  if (Size <= 0) { fclose(File); return {}; }
  std::vector<unsigned char> Bytes(static_cast<size_t>(Size));
  const size_t Count = fread(Bytes.data(), 1, Bytes.size(), File); fclose(File);
  if (Count != Bytes.size()) return {};
  return Bytes;
}
int main(int argc, char** argv)
{
  if (argc != 6) return 2;
  const std::string Bin = argv[1]; SetDllDirectoryA(Bin.c_str());
  HMODULE Dll = LoadLibraryA((Bin + "/vphysics.dll").c_str());
  if (!Dll) return 3;
  using Factory = void* (__cdecl*)(const char*, int*);
  auto Create = reinterpret_cast<Factory>(GetProcAddress(Dll, "CreateInterface"));
  int Status = -1; void* Service = Create ? Create("VPhysicsCollision007", &Status) : nullptr;
  if (!Service) return 4;
  auto Table = *reinterpret_cast<void***>(Service);
  using Load = void (__thiscall*)(void*, uint32_t*, int, const void*, int);
  using Unload = void (__thiscall*)(void*, uint32_t*);
  using TraceBox = void (__thiscall*)(void*, const float*, const float*, const float*, const float*, void*, const float*, const float*, void*);
  std::vector<unsigned char> Data = Read(argv[2]), Samples = Read(argv[3]);
  const int Solids = atoi(argv[4]);
  if (Data.empty() || Samples.empty() || Samples.size() % 16 || Solids <= 0 || Solids > 32767) return 5;
  uint32_t Loaded[3] = {};
  reinterpret_cast<Load>(Table[24])(Service, Loaded, Solids, Data.data(), static_cast<int>(Data.size()));
  if (!Loaded[1] || static_cast<int>(Loaded[0] & 0x7fff) != Solids) return 6;
  FILE* Output = nullptr; fopen_s(&Output, argv[5], "w");
  if (!Output) return 7;
  fprintf(Output, "sample,contents,x,y,z,fraction,startsolid,allsolid,solid\n");
  const float Zero[3] = {}, Mins[3] = {-0.01f,-0.01f,-0.01f}, Maxs[3] = {0.01f,0.01f,0.01f};
  auto Shapes = reinterpret_cast<void**>(Loaded[1]);
  for (size_t Offset = 0; Offset < Samples.size(); Offset += 16)
  {
    uint32_t Contents = 0; float Point[3];
    memcpy(&Contents, Samples.data() + Offset, 4); memcpy(Point, Samples.data() + Offset + 4, 12);
    for (int Solid = 0; Solid < Solids; ++Solid)
    {
      alignas(16) unsigned char Result[128] = {};
      reinterpret_cast<TraceBox>(Table[22])(Service, Point, Point, Mins, Maxs, Shapes[Solid], Zero, Zero, Result);
      float Fraction; memcpy(&Fraction, Result + 0x2c, 4);
      fprintf(Output, "%zu,0x%x,%.9g,%.9g,%.9g,%.9g,%u,%u,%d\n", Offset/16, Contents, Point[0], Point[1], Point[2], Fraction, Result[0x36], Result[0x37], Solid);
    }
  }
  fclose(Output); reinterpret_cast<Unload>(Table[25])(Service, Loaded);
  printf("Collision007 status=%d solids=%d samples=%zu\n", Status, Solids, Samples.size()/16);
  return 0;
}
