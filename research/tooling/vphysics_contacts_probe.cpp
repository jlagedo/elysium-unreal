// Read-only public VPhysics030/Collision007 contact observer, not an IVP implementation.
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <string>
#include <fstream>
#include <iterator>

template<class T> static T Method(void* Object, int Slot)
{ return reinterpret_cast<T>((*reinterpret_cast<void***>(Object))[Slot]); }
using CreateEnv = void* (__thiscall*)(void*);
using ParseSurface = int (__thiscall*)(void*, const char*, const char*);
using SurfaceIndex = int (__thiscall*)(void*, const char*);
using Bbox = void* (__thiscall*)(void*, const float*, const float*);
using Convex = void* (__thiscall*)(void*, const float* const*, int);
using ConvertConvex = void* (__thiscall*)(void*, void**, int);
// The public 48-byte object parameter protocol recovered at260189f0/26018ba0.
struct ObjectParams {
  const float* MassCenter = nullptr;
  float Mass = 1.f, Inertia = 1.f, Damping = .1f, RotDamping = .1f, RotLimit = .5f;
  const char* Name = nullptr;
  void* GameData = nullptr;
  float Volume = 0.f, Drag = 1.f, RollingDrag = 0.f;
  bool Collisions = true;
};
static_assert(sizeof(ObjectParams) == 48, "public x86 object parameter ABI");
using CreateBody = void* (__thiscall*)(void*, void*, int, const float*, const float*, const ObjectParams*);
using SetVector = void (__thiscall*)(void*, const float*);
using SetFloat = void (__thiscall*)(void*, float);
using SetInt = void (__thiscall*)(void*, int);
using SetShadow = void (__thiscall*)(void*, float, float, bool, bool);
using UpdateShadow = void (__thiscall*)(void*, const float*, const float*, bool, float);
using Simulate = void (__thiscall*)(void*, float);
using Contact = bool (__thiscall*)(void*, float*, void**);
using GameData = void* (__thiscall*)(void*);
using Pose = void (__thiscall*)(void*, float*, float*);
using ShadowPose = int (__thiscall*)(void*, float*, float*);
using DestroyBody = void (__thiscall*)(void*, void*);

struct Body { const char* Name; void* Object = nullptr; float Target[3] = {}; unsigned Incarnation = 0, UpdateCalls = 0; float LastArrival = 0.f; };
static std::string Variant = "baseline";
static float SourceInches(double Cm) { return static_cast<float>(Cm / 2.54); }
static void Target(Body& Item, double X, double Y, double Z)
{
  if (Variant == "mirror") X = -X;
  if (Variant == "swizzle") { const double OldX=X; X=Y; Y=OldX; }
  Item.Target[0] = SourceInches(X); Item.Target[1] = SourceInches(-Y); Item.Target[2] = SourceInches(Z);
}

int main(int argc, char** argv)
{
  if (argc < 4 || argc > 7) return 2;
  if (argc >= 5) Variant=argv[4];
  const std::string Clock = argc >= 6 ? argv[5] : "raw";
  const std::string Arrival = argc == 7 ? argv[6] : "frame";
  if (Clock!="raw" && Clock!="admitted") return 2;
  if (Arrival!="frame" && Arrival!="zero") return 2;
  if (Variant!="baseline" && Variant!="swapped" && Variant!="mirror" && Variant!="swizzle") return 2;
  printf("variant=%s clock=%s arrival=%s; second recreation remains at .45\n",Variant.c_str(),Clock.c_str(),Arrival.c_str());
  const std::string Bin = argv[1]; SetDllDirectoryA(Bin.c_str());
  HMODULE Dll = LoadLibraryA((Bin + "/vphysics.dll").c_str());
  if (!Dll) return 3;
  using Factory = void* (__cdecl*)(const char*, int*);
  auto FactoryFn = reinterpret_cast<Factory>(GetProcAddress(Dll, "CreateInterface"));
  if (!FactoryFn) return 4;
  int Status = -1;
  void* Physics = FactoryFn("VPhysics030", &Status);
  void* Collision = FactoryFn("VPhysicsCollision007", &Status);
  void* Surfaces = FactoryFn("VPhysicsSurfaceProps001", &Status);
  if (!Physics || !Collision || !Surfaces) return 5;
  std::ifstream Input(argv[2], std::ios::binary);
  const std::string Source((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
  if (Source.empty()) return 6;
  const int Rows = Method<ParseSurface>(Surfaces, 1)(Surfaces, "scripts/surfaceproperties.txt", Source.c_str());
  const int Material = Method<SurfaceIndex>(Surfaces, 3)(Surfaces, "default");
  void* Environment = Method<CreateEnv>(Physics, 0)(Physics);
  if (!Environment || Rows <= 0 || Material < 0) return 7;
  const float Zero[3] = {};
  Method<SetVector>(Environment, 3)(Environment, Zero);
  Method<SetFloat>(Environment, 29)(Environment, .015f);
  float Vertices[8][3]; const float* Pointers[8]; int Vertex=0;
  for (double X : {-10.,10.}) for(double Y : {-10.,10.}) for(double Z : {-10.,10.}) {
    Vertices[Vertex][0] = SourceInches(X); Vertices[Vertex][1] = SourceInches(-Y); Vertices[Vertex][2] = SourceInches(Z);
    Pointers[Vertex] = Vertices[Vertex]; ++Vertex;
  }
  int ConvexSlot=-1, ConvertSlot=-1;
  auto CollisionTable = *reinterpret_cast<void***>(Collision);
  for (int Index=0; Index<26; ++Index) {
    const auto Offset = reinterpret_cast<uintptr_t>(CollisionTable[Index])-reinterpret_cast<uintptr_t>(Dll);
    printf("public Collision007 slot%d offset=%08x\n",Index,static_cast<unsigned>(Offset));
    if (Offset == 0x9b10) ConvexSlot=Index;
    if (Offset == 0x9fd0) ConvertSlot=Index;
  }
  fflush(stdout);
  if (ConvexSlot<0 || ConvertSlot<0) return 12;
  void* Hull = Method<Convex>(Collision,ConvexSlot)(Collision,Pointers,8);
  printf("public hull=%p\n",Hull); fflush(stdout);
  if (!Hull) return 8;
  void* Shape = Method<ConvertConvex>(Collision,ConvertSlot)(Collision,&Hull,1);
  if (!Shape) return 8;
  Body Subject{"subject"}, First{"first"}, Second{"second"};
  Target(Subject,0,0,1000); Target(First,19,0,1000); Target(Second,-100,0,1000);
  Body* Bodies[] = {&Subject,&First,&Second};
  auto Create = [&](Body& Item) {
    ObjectParams Params; Params.Name = Item.Name; Params.GameData = &Item;
    Item.Object = Method<CreateBody>(Environment, 7)(Environment, Shape, Material, Item.Target, Zero, &Params);
    if (!Item.Object) return false;
    Method<SetInt>(Item.Object, 66)(Item.Object, 1);
    Method<SetShadow>(Item.Object, 45)(Item.Object, 10000.f, 10000.f, false, false);
    Method<UpdateShadow>(Item.Object, 46)(Item.Object, Item.Target, Zero, false, 0.f);
    ++Item.Incarnation; ++Item.UpdateCalls; Item.LastArrival=0.f;
    printf("shadow create body=%s incarnation=%u SetShadow45=(10000,10000,false,false) contents=1 mass=1 surface=default UpdateShadow46=(target,false,0)\n",Item.Name,Item.Incarnation);
    return true;
  };
  Body* CreationOrder[] = {&Subject,Variant=="swapped"?&Second:&First,Variant=="swapped"?&First:&Second};
  for (Body* Item : CreationOrder) if (!Create(*Item)) return 9;
  FILE* Output = nullptr; fopen_s(&Output, argv[3], "w"); if (!Output) return 10;
  fprintf(Output,"phase,time,dt,present,partner,subject_x,subject_y,subject_z,first_x,first_y,first_z,second_x,second_y,second_z,contact_x,contact_y,contact_z,first_present,first_partner,second_present,second_partner,subject_target_x,subject_target_y,subject_target_z,first_target_x,first_target_y,first_target_z,second_target_x,second_target_y,second_target_z,host_dt,clock,subject_shadow_x,subject_shadow_y,subject_shadow_z,first_shadow_x,first_shadow_y,first_shadow_z,second_shadow_x,second_shadow_y,second_shadow_z,subject_pitch,subject_yaw,subject_roll,first_pitch,first_yaw,first_roll,second_pitch,second_yaw,second_roll\n");
  double Time = 0.; float LastDt=0.f, LastHostDt=0.f;
  auto Observe = [&](const char* Phase) {
    void* Partner = nullptr; float Point[3] = {}; float Positions[3][3] = {}, ShadowPositions[3][3] = {}, ShadowAngles[3][3] = {};
    const bool Present = Method<Contact>(Subject.Object,44)(Subject.Object,Point,&Partner);
    Body* Owner = Partner ? static_cast<Body*>(Method<GameData>(Partner,6)(Partner)) : nullptr;
    for (int Index=0; Index<3; ++Index)
      if (Bodies[Index]->Object) {
        Method<Pose>(Bodies[Index]->Object,37)(Bodies[Index]->Object,Positions[Index],nullptr);
        Method<ShadowPose>(Bodies[Index]->Object,47)(Bodies[Index]->Object,ShadowPositions[Index],ShadowAngles[Index]);
      }
    fprintf(Output,"%s,%.17g,%.9g,%d,%s",Phase,Time,LastDt,Present,Owner ? Owner->Name : "null");
    for (auto& Position : Positions) for(float Value : Position) fprintf(Output,",%.9g",Value);
    for(float Value : Point) fprintf(Output,",%.9g",Value);
    bool OtherPresent[2] = {}; const char* OtherOwner[2] = {"null","null"};
    for(int Index=0; Index<2; ++Index) {
      Body* Item = Bodies[Index+1]; void* Other = nullptr;
      if (Item->Object) OtherPresent[Index] = Method<Contact>(Item->Object,44)(Item->Object,nullptr,&Other);
      Body* Named = Other ? static_cast<Body*>(Method<GameData>(Other,6)(Other)) : nullptr;
      if (Named) OtherOwner[Index] = Named->Name;
      fprintf(Output,",%d,%s",OtherPresent[Index],OtherOwner[Index]);
    }
    for (Body* Item : Bodies) for(float Value : Item->Target) fprintf(Output,",%.9g",Value);
    fprintf(Output,",%.9g,%s",LastHostDt,Clock.c_str());
    for (auto& Position : ShadowPositions) for(float Value : Position) fprintf(Output,",%.9g",Value);
    for (auto& Angles : ShadowAngles) for(float Value : Angles) fprintf(Output,",%.9g",Value);
    fprintf(Output,"\n"); fflush(Output);
    printf("%s t=%.9g present=%d partner=%s first=%d/%s second=%d/%s\n",Phase,Time,Present,Owner ? Owner->Name : "null",OtherPresent[0],OtherOwner[0],OtherPresent[1],OtherOwner[1]); fflush(stdout);
    for (Body* Item : Bodies) if (Item->Object)
      printf("shadow submitted phase=%s time=%.17g body=%s incarnation=%u updates=%u arrival=%.9g gravityMode=false target=(%.9g,%.9g,%.9g)\n",Phase,Time,Item->Name,Item->Incarnation,Item->UpdateCalls,Item->LastArrival,Item->Target[0],Item->Target[1],Item->Target[2]);
  };
  auto Advance = [&](const char* Phase, double Until) {
    while (Time < Until) {
      const double Now = (Time + 1./60. < Until) ? Time + 1./60. : Until;
      const double Step = Now - Time;
      const float Dt = static_cast<float>(Clock=="admitted" ? std::fmax(.001,std::fmin(.1,Step)) : Step);
      const float ArrivalSeconds = Arrival=="zero" ? 0.f : Dt;
      for (Body* Item : Bodies) if (Item->Object) {
        Method<UpdateShadow>(Item->Object,46)(Item->Object,Item->Target,Zero,false,ArrivalSeconds);
        ++Item->UpdateCalls; Item->LastArrival=ArrivalSeconds;
      }
      Method<Simulate>(Environment,26)(Environment,Dt); Time = Now; LastDt=Dt; LastHostDt=static_cast<float>(Step);
      Observe(Phase);
    }
  };
  Advance("first_birth",.05);
  Target(Second,-19,0,1000); Advance("second_birth",.10);
  Target(First,23.1,0,1000); Advance("qualification_exit",.15);
  Target(First,19,0,1000); Advance("qualification_reentry",.20);
  Target(First,0,19,1000); Advance("first_face_change",.25);
  Target(Second,-100,0,1000); Advance("second_separation",.30);
  Target(Second,-19,0,1000); Advance("second_rebirth",.35);
  Method<DestroyBody>(Environment,20)(Environment,Second.Object); Second.Object=nullptr; Observe("second_retired");
  Target(First,0,22.8,1000); Advance("positive_skin",.40);
  Target(First,0,23.1,1000); Advance("outside_skin",.45);
  if (!Create(Second)) return 11;
  Target(First,0,100,1000); Target(Second,-100,0,1000); Advance("same_step_clear",.50);
  Target(First,0,19,1000); Target(Second,-19,0,1000); Advance("same_step_birth",.55);
  Target(First,100,0,1000); Target(Second,-100,0,1000); Advance("opposed_clear",.60);
  Target(First,19,0,1000); Target(Second,-19,0,1000); Advance("opposed_birth",.65);
  fclose(Output);
  // Public environment destruction owns bodies. Process teardown owns service/shape allocation.
  using DestroyEnv = void (__thiscall*)(void*,void*);
  Method<DestroyEnv>(Physics,1)(Physics,Environment);
  return 0;
}
