// Bounded public retail selected-material observer. No IVP/private layout access.
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

template<class T> static T Method(void* Object, int Slot)
{ return reinterpret_cast<T>((*reinterpret_cast<void***>(Object))[Slot]); }
using CreateEnvironment = void* (__thiscall*)(void*);
using DestroyEnvironment = void (__thiscall*)(void*, void*);
using Convex = void* (__thiscall*)(void*, const float* const*, int);
using Convert = void* (__thiscall*)(void*, void**, int);
using QueryCreate = void* (__thiscall*)(void*, void*);
using Destroy = void (__thiscall*)(void*, void*);
using Count = int (__thiscall*)(void*);
using IndexedCount = int (__thiscall*)(void*, int);
using TriangleVertices = void (__thiscall*)(void*, int, int, float*);
using TriangleMaterial = int (__thiscall*)(void*, int, int);
using SetTriangleMaterial = void (__thiscall*)(void*, int, int, int);
using Bbox = void* (__thiscall*)(void*, const float*, const float*);
using Parse = int (__thiscall*)(void*, const char*, const char*);
using Index = int (__thiscall*)(void*, const char*);
using SurfaceName = const char* (__thiscall*)(void*, int);
using Properties = void (__thiscall*)(void*, int, float*, float*, float*, float*);
using InstallTable = void (__thiscall*)(void*, const int*, int);
using SetVector = void (__thiscall*)(void*, const float*);
using SetFloat = void (__thiscall*)(void*, float);
using SetBool = void (__thiscall*)(void*, bool);
using SetInt = void (__thiscall*)(void*, int);
using SetFlags = void (__thiscall*)(void*, unsigned short);
using SetVelocity = void (__thiscall*)(void*, const float*, const float*);
using PairRead = void (__thiscall*)(void*, float*, float*);
using Scalar = float (__thiscall*)(void*);
using Pose = void (__thiscall*)(void*, float*, float*);
using Contact = bool (__thiscall*)(void*, float*, void**);
using VectorRead = void (__thiscall*)(void*, float*);
using Simulate = void (__thiscall*)(void*, float);

// Existing recovered public x86 object parameter ABI260189f0/26018ba0.
struct ObjectParams {
  const float* MassCenter = nullptr;
  float Mass = 1.f, Inertia = 1.f, Damping = 0.f, RotDamping = 0.f, RotLimit = .5f;
  const char* Name = nullptr;
  void* GameData = nullptr;
  float Volume = 0.f, Drag = 0.f, RollingDrag = 0.f;
  bool Collisions = true;
};
static_assert(sizeof(ObjectParams) == 48, "public objectparams x86 ABI");
using CreateBody = void* (__thiscall*)(void*, void*, int, const float*, const float*, const ObjectParams*);

// Original Bloodlines public declaration; emitted offsets checked in installed PE.
struct CollisionEvent {
  void* Objects[2];
  int SurfaceRows[2];
  bool IsCollision, IsShadowCollision;
  float DeltaCollisionTime, CollisionSpeed;
  void* InternalData;
};
static_assert(sizeof(CollisionEvent) == 32 && offsetof(CollisionEvent, SurfaceRows) == 8 &&
  offsetof(CollisionEvent, DeltaCollisionTime) == 20 && offsetof(CollisionEvent, InternalData) == 28,
  "installed public event ABI");
struct CollisionHandler {
  virtual void PreCollision(CollisionEvent*) {}
  virtual void PostCollision(CollisionEvent*) {}
  virtual void Friction(void*, float, int, int, void*) {}
  virtual void StartTouch(void*, void*, void*) {}
  virtual void EndTouch(void*, void*, void*) {}
  virtual void FluidStartTouch(void*, void*) {}
  virtual void FluidEndTouch(void*, void*) {}
  virtual void PostSimulationFrame() {}
  virtual void ObjectEnterTrigger(void*, void*) {}
  virtual void ObjectLeaveTrigger(void*, void*) {}
};
using SetHandler = void (__thiscall*)(void*, CollisionHandler*);

struct EventCopy {
  int Frame = 0, Phase = 0, Ids[2] = {}, Rows[2] = {};
  bool IsCollision = false, IsShadow = false;
  float Delta = 0.f, Speed = 0.f, Normal[3] = {}, Point[3] = {}, RelativeSpeed[3] = {};
};
struct Observer : CollisionHandler {
  void* Body = nullptr;
  void* Floor = nullptr;
  int Frame = 0;
  std::vector<EventCopy> Copies;
  void Copy(CollisionEvent* Event, int Phase) {
    EventCopy Item;
    Item.Frame = Frame; Item.Phase = Phase;
    for(int Side = 0; Side < 2; ++Side) {
      Item.Ids[Side] = Event->Objects[Side] == Body ? 1 : Event->Objects[Side] == Floor ? 2 : 0;
      Item.Rows[Side] = Event->SurfaceRows[Side];
    }
    Item.IsCollision = Event->IsCollision; Item.IsShadow = Event->IsShadowCollision;
    Item.Delta = Event->DeltaCollisionTime;
    Item.Speed = Phase ? Event->CollisionSpeed : 0.f; // pre speed is not valid
    if(Event->InternalData) {
      Method<VectorRead>(Event->InternalData, 0)(Event->InternalData, Item.Normal);
      Method<VectorRead>(Event->InternalData, 1)(Event->InternalData, Item.Point);
      Method<VectorRead>(Event->InternalData, 2)(Event->InternalData, Item.RelativeSpeed);
    }
    Copies.push_back(Item); // only values survive callback; no borrowed pointers
  }
  void PreCollision(CollisionEvent* Event) override { Copy(Event, 0); }
  void PostCollision(CollisionEvent* Event) override { Copy(Event, 1); }
};

static float Inches(float Cm) { return Cm / 2.54f; }
struct Triangle {
  int Ordinal = 0, Selector = 0;
  float Vertices[9] = {};
};
static bool Corner(float Value, float ExpectedExtent) {
  const float Allowed = 8.f * FLT_EPSILON * std::max(1.f, ExpectedExtent);
  return std::fabs(std::fabs(Value) - ExpectedExtent) <= Allowed;
}
static bool ReadGeometry(void* Query, std::vector<Triangle>& Triangles) {
  if(Method<Count>(Query, 1)(Query) != 1 || Method<IndexedCount>(Query, 2)(Query, 0) != 12)
    return false;
  int TopSeven = 0, TopEleven = 0, TopCount = 0;
  for(int Ordinal = 0; Ordinal < 12; ++Ordinal) {
    Triangle Item; Item.Ordinal = Ordinal;
    Method<TriangleVertices>(Query, 4)(Query, 0, Ordinal, Item.Vertices);
    bool Top = true; unsigned TopCorners = 0;
    for(int Vertex = 0; Vertex < 3; ++Vertex) {
      const float* Point = Item.Vertices + Vertex * 3;
      if(!Corner(Point[0], Inches(100.f)) || !Corner(Point[1], Inches(100.f)) || !Corner(Point[2], Inches(5.f)))
        return false;
      Top = Top && Point[2] > 0.f;
      const unsigned CornerIndex = (Point[0] > 0.f ? 2u : 0u) + (Point[1] < 0.f ? 1u : 0u);
      TopCorners |= 1u << CornerIndex; // authored Unreal Y=-SourceY, not cook ordinal
    }
    if(Top) {
      ++TopCount;
      if(TopCorners == 13u) { Item.Selector = 7; ++TopSeven; }
      else if(TopCorners == 11u) { Item.Selector = 11; ++TopEleven; }
      else return false; // unexpected diagonal: terminate, never choose first triangle
    }
    Triangles.push_back(Item);
  }
  return TopCount == 2 && TopSeven == 1 && TopEleven == 1;
}

struct Lifetime {
  void* Physics = nullptr;
  void* Collision = nullptr;
  void* Environment = nullptr;
  void* Cube = nullptr;
  void* Query = nullptr;
  void* Body = nullptr;
  void* Floor = nullptr;
  CollisionHandler* Noop = nullptr;
  ~Lifetime() {
    if(Query) Method<Destroy>(Collision, 31)(Collision, Query);
    if(Environment) {
      Method<SetHandler>(Environment, 32)(Environment, Noop); // null is not a supported handler
      if(Body) Method<Destroy>(Environment, 20)(Environment, Body);
      if(Floor) Method<Destroy>(Environment, 20)(Environment, Floor);
      Method<DestroyEnvironment>(Physics, 1)(Physics, Environment);
    }
    if(Cube) Method<Destroy>(Collision, 14)(Collision, Cube);
    // BBoxToCollide shape is borrowed from the collision service cache.
  }
};

int main(int argc, char** argv) {
  if(argc != 5) return 2;
  const std::string Bin = argv[1], Prefix = argv[3];
  const bool Capture = std::string(argv[4]) == "on";
  if(!Capture && std::string(argv[4]) != "off") return 2;
  SetDllDirectoryA(Bin.c_str());
  HMODULE Dll = LoadLibraryA((Bin + "/vphysics.dll").c_str());
  if(!Dll) return 3;
  using Factory = void* (__cdecl*)(const char*, int*);
  auto FactoryFn = reinterpret_cast<Factory>(GetProcAddress(Dll, "CreateInterface"));
  if(!FactoryFn) return 4;
  int Status = -1;
  void* Physics = FactoryFn("VPhysics030", &Status);
  void* Collision = FactoryFn("VPhysicsCollision007", &Status);
  void* Surfaces = FactoryFn("VPhysicsSurfaceProps001", &Status);
  if(!Physics || !Collision || !Surfaces) return 5;
  CollisionHandler Noop; Observer Capturing;
  Lifetime Owned; Owned.Physics = Physics; Owned.Collision = Collision; Owned.Noop = &Noop;

  std::ifstream Input(argv[2], std::ios::binary);
  const std::string Recipe((std::istreambuf_iterator<char>(Input)), std::istreambuf_iterator<char>());
  if(Recipe.empty()) return 6;
  const int ParsedCount = Method<Parse>(Surfaces, 1)(Surfaces, "independent_material_selection", Recipe.c_str());
  printf("actual_parsed_count=%d expected14 (13authored+once_created_shadow)\n", ParsedCount);
  const char* Names[] = {"floor", "filler1", "filler2", "filler3", "filler4", "filler5", "filler6",
    "seven", "filler8", "filler9", "filler10", "eleven", "body"};
  float Friction[13] = {}, Elasticity[13] = {};
  FILE* Materials = nullptr; fopen_s(&Materials, (Prefix + ".materials.csv").c_str(), "w");
  if(!Materials) return 7;
  fprintf(Materials, "expected_row,actual_row,name,friction,elasticity\n");
  bool RowsValid = ParsedCount == 14;
  for(int Row = 0; Row < 13; ++Row) {
    const int ActualRow = Method<Index>(Surfaces, 3)(Surfaces, Names[Row]);
    RowsValid = RowsValid && ActualRow == Row;
    float Density = 0.f, Thickness = 0.f;
    if(ActualRow >= 0)
      Method<Properties>(Surfaces, 4)(Surfaces, ActualRow, &Density, &Thickness, &Friction[Row], &Elasticity[Row]);
    fprintf(Materials, "%d,%d,%s,%.17g,%.17g\n", Row, ActualRow, Names[Row], Friction[Row], Elasticity[Row]);
    printf("declared_name=%s expected_row=%d actual_row=%d friction=%.9g elasticity=%.9g\n",
      Names[Row], Row, ActualRow, Friction[Row], Elasticity[Row]);
  }
  const char* ShadowName = Method<SurfaceName>(Surfaces, 7)(Surfaces, 13);
  const int ShadowIndex = ShadowName ? Method<Index>(Surfaces, 3)(Surfaces, ShadowName) : -1;
  float ShadowDensity = 0.f, ShadowThickness = 0.f, ShadowFriction = 0.f, ShadowElasticity = 0.f;
  float ReservedDensity = 0.f, ReservedThickness = 0.f, ReservedFriction = 0.f, ReservedElasticity = 0.f;
  Method<Properties>(Surfaces, 4)(Surfaces, 13, &ShadowDensity, &ShadowThickness, &ShadowFriction, &ShadowElasticity);
  Method<Properties>(Surfaces, 4)(Surfaces, 0xf000, &ReservedDensity, &ReservedThickness, &ReservedFriction, &ReservedElasticity);
  fprintf(Materials, "13,13,%s,%.17g,%.17g\n", ShadowName ? ShadowName : "null", ShadowFriction, ShadowElasticity);
  printf("reserved_shadow row13 name=%s name_index=%d row_properties=%.9g/%.9g reserved_properties=%.9g/%.9g\n",
    ShadowName ? ShadowName : "null", ShadowIndex, ShadowFriction, ShadowElasticity, ReservedFriction, ReservedElasticity);
  RowsValid = RowsValid && ShadowIndex == 0xf000 && ShadowFriction == .8f && ShadowElasticity == 0.f &&
    ReservedFriction == .8f && ReservedElasticity == 0.f;
  fclose(Materials);
  if(!RowsValid || Friction[0] != .7f || Elasticity[0] != .3f || Friction[7] != .2f || Elasticity[7] != .25f ||
     Friction[11] != .9f || Elasticity[11] != 2.f || Friction[12] != .6f || Elasticity[12] != 1.f) {
    printf("INVALID independent parsed row identities/properties\n"); return 8;
  }
  int Mapping[128]; for(int Slot = 0; Slot < 128; ++Slot) Mapping[Slot] = Slot;
  Method<InstallTable>(Surfaces, 8)(Surfaces, Mapping, 128);
  printf("independent_rows=13 mapping7=%d mapping11=%d current_floor=0 current_body=12\n", Mapping[7], Mapping[11]);

  float Vertices[8][3]; const float* Pointers[8]; int Vertex = 0;
  for(float X : {-100.f, 100.f}) for(float Y : {-100.f, 100.f}) for(float Z : {-5.f, 5.f}) {
    Vertices[Vertex][0] = Inches(X); Vertices[Vertex][1] = Inches(-Y); Vertices[Vertex][2] = Inches(Z);
    Pointers[Vertex] = Vertices[Vertex]; ++Vertex;
  }
  void* Hull = Method<Convex>(Collision, 1)(Collision, Pointers, 8);
  if(!Hull) return 9;
  Owned.Cube = Method<Convert>(Collision, 4)(Collision, &Hull, 1); // public convert consumes hull
  if(!Owned.Cube) return 9;
  Owned.Query = Method<QueryCreate>(Collision, 30)(Collision, Owned.Cube);
  if(!Owned.Query) return 9;
  std::vector<Triangle> Triangles;
  const bool GeometryValid = ReadGeometry(Owned.Query, Triangles);
  FILE* Geometry = nullptr; fopen_s(&Geometry, (Prefix + ".geometry.csv").c_str(), "w");
  if(!Geometry) return 7;
  fprintf(Geometry, "triangle,selector,x0,y0,z0,x1,y1,z1,x2,y2,z2,unchanged\n");
  if(!GeometryValid) {
    // Retain every actual triangle even on mismatch; no mutation or recook follows.
    const int Pieces = Method<Count>(Owned.Query, 1)(Owned.Query);
    printf("INVALID geometry: convex_count=%d expected1; top split y=x required\n", Pieces);
    if(Pieces == 1) {
      const int TriangleCount = Method<IndexedCount>(Owned.Query, 2)(Owned.Query, 0);
      printf("actual_triangle_count=%d expected12\n", TriangleCount);
      for(int Ordinal = 0; Ordinal < TriangleCount && Ordinal < 64; ++Ordinal) {
        float Actual[9] = {}; Method<TriangleVertices>(Owned.Query, 4)(Owned.Query, 0, Ordinal, Actual);
        fprintf(Geometry, "%d,%d", Ordinal, Method<TriangleMaterial>(Owned.Query, 6)(Owned.Query, 0, Ordinal));
        for(float Value : Actual) fprintf(Geometry, ",%.17g", Value);
        fprintf(Geometry, ",1\n");
      }
    }
    fclose(Geometry); return 21;
  }
  for(const Triangle& Item : Triangles)
    Method<SetTriangleMaterial>(Owned.Query, 7)(Owned.Query, 0, Item.Ordinal, Item.Selector);
  bool Unchanged = true;
  for(const Triangle& Item : Triangles) {
    float Again[9] = {}; Method<TriangleVertices>(Owned.Query, 4)(Owned.Query, 0, Item.Ordinal, Again);
    const int Selector = Method<TriangleMaterial>(Owned.Query, 6)(Owned.Query, 0, Item.Ordinal);
    const bool Same = !std::memcmp(Again, Item.Vertices, sizeof(Again)) && Selector == Item.Selector;
    Unchanged = Unchanged && Same;
    fprintf(Geometry, "%d,%d", Item.Ordinal, Selector);
    for(float Value : Again) fprintf(Geometry, ",%.17g", Value);
    fprintf(Geometry, ",%d\n", Same ? 1 : 0);
  }
  fclose(Geometry);
  if(!Unchanged) return 21;
  printf("geometry_proved convex1 triangles12 top_y_equals_x selectors7/11 unchanged_vertices=1\n");
  Method<Destroy>(Collision, 31)(Collision, Owned.Query); Owned.Query = nullptr;

  Owned.Environment = Method<CreateEnvironment>(Physics, 0)(Physics);
  if(!Owned.Environment) return 10;
  Method<SetHandler>(Owned.Environment, 32)(Owned.Environment, &Noop);
  const float Zero[3] = {};
  Method<SetVector>(Owned.Environment, 3)(Owned.Environment, Zero);
  Method<SetFloat>(Owned.Environment, 29)(Owned.Environment, .015f);
  const float Minimum[3] = {-Inches(10.f), -Inches(10.f), -Inches(10.f)};
  const float Maximum[3] = { Inches(10.f),  Inches(10.f),  Inches(10.f)};
  void* Box = Method<Bbox>(Collision, 19)(Collision, Minimum, Maximum);
  if(!Box) return 10;
  ObjectParams FloorParams; FloorParams.Name = "independent_cube";
  ObjectParams BodyParams; BodyParams.Name = "independent_body";
  const float Position[3] = {0.f, 0.f, Inches(40.f)};
  const float Velocity[3] = {0.f, 0.f, -Inches(600.f)};
  Owned.Floor = Method<CreateBody>(Owned.Environment, 8)(Owned.Environment, Owned.Cube, 0, Zero, Zero, &FloorParams);
  Owned.Body = Method<CreateBody>(Owned.Environment, 7)(Owned.Environment, Box, 12, Position, Zero, &BodyParams);
  if(!Owned.Floor || !Owned.Body) return 10;
  for(void* Object : {Owned.Floor, Owned.Body}) {
    Method<SetFlags>(Object, 9)(Object, 0x21);
    Method<SetInt>(Object, 66)(Object, 0x2);
    Method<SetBool>(Object, 22)(Object, false);
    Method<SetBool>(Object, 23)(Object, false);
  }
  if(Method<Count>(Owned.Floor, 19)(Owned.Floor) != 0 || Method<Count>(Owned.Body, 19)(Owned.Body) != 12) return 11;
  Method<SetVelocity>(Owned.Body, 39)(Owned.Body, Velocity, Zero);
  Capturing.Body = Owned.Body; Capturing.Floor = Owned.Floor;
  Method<SetHandler>(Owned.Environment, 32)(Owned.Environment, Capture ? static_cast<CollisionHandler*>(&Capturing) : &Noop);
  FILE* Frames = nullptr; fopen_s(&Frames, (Prefix + ".frames.csv").c_str(), "w");
  if(!Frames) return 7;
  fprintf(Frames, "frame,host_dt,admitted_dt,timestep,before,after,x,y,z,pitch,yaw,roll,vx,vy,vz,wx,wy,wz,contact,partner,cx,cy,cz,floor_row,body_row\n");
  bool ContactReached = false, ResponseReached = false; int FirstContact = 0;
  const float HostDt = 1.f / 60.f, AdmittedDt = std::fmin(HostDt, .1f);
  for(int Frame = 1; Frame <= 12; ++Frame) {
    Capturing.Frame = Frame;
    const float Before = Method<Scalar>(Owned.Environment, 30)(Owned.Environment);
    const float Step = Method<Scalar>(Owned.Environment, 28)(Owned.Environment);
    Method<Simulate>(Owned.Environment, 26)(Owned.Environment, AdmittedDt);
    const float After = Method<Scalar>(Owned.Environment, 30)(Owned.Environment);
    float Actual[3] = {}, Angles[3] = {}, Linear[3] = {}, Angular[3] = {}, Point[3] = {};
    Method<Pose>(Owned.Body, 37)(Owned.Body, Actual, Angles);
    Method<PairRead>(Owned.Body, 40)(Owned.Body, Linear, Angular);
    void* Partner = nullptr;
    const bool Present = Method<Contact>(Owned.Body, 44)(Owned.Body, Point, &Partner);
    const int PartnerId = Partner == Owned.Floor ? 2 : Partner ? 0 : -1;
    const int FloorRow = Method<Count>(Owned.Floor, 19)(Owned.Floor), BodyRow = Method<Count>(Owned.Body, 19)(Owned.Body);
    fprintf(Frames, "%d,%.17g,%.17g,%.17g,%.17g,%.17g", Frame, HostDt, AdmittedDt, Step, Before, After);
    for(float Value : Actual) fprintf(Frames, ",%.17g", Value);
    for(float Value : Angles) fprintf(Frames, ",%.17g", Value);
    for(float Value : Linear) fprintf(Frames, ",%.17g", Value);
    for(float Value : Angular) fprintf(Frames, ",%.17g", Value);
    fprintf(Frames, ",%d,%d", Present ? 1 : 0, PartnerId);
    for(float Value : Point) fprintf(Frames, ",%.17g", Value);
    fprintf(Frames, ",%d,%d\n", FloorRow, BodyRow);
    if(Present && PartnerId == 2 && After > Before) {
      ContactReached = true; if(!FirstContact) FirstContact = Frame;
      if(Linear[2] != Velocity[2]) ResponseReached = true;
    }
    if(FloorRow != 0 || BodyRow != 12 || Step != .015f || !std::isfinite(After)) { fclose(Frames); return 11; }
  }
  fclose(Frames);
  // Stop capture before lifetime teardown callbacks; keep both public handlers alive.
  Method<SetHandler>(Owned.Environment, 32)(Owned.Environment, &Noop);
  FILE* Events = nullptr; fopen_s(&Events, (Prefix + ".events.csv").c_str(), "w");
  if(!Events) return 7;
  fprintf(Events, "sequence,frame,phase,id0,id1,row0,row1,is_collision,is_shadow,delta,post_speed,nx,ny,nz,px,py,pz,relative_vx,relative_vy,relative_vz,computed_mu,computed_elasticity\n");
  bool Matched = Capturing.Copies.size() % 2 == 0;
  for(size_t Sequence = 0; Sequence < Capturing.Copies.size(); ++Sequence) {
    const EventCopy& Item = Capturing.Copies[Sequence];
    if(Item.Ids[0] + Item.Ids[1] != 3 || Item.Rows[0] < 0 || Item.Rows[0] >= 13 || Item.Rows[1] < 0 || Item.Rows[1] >= 13)
      Matched = false;
    if(Sequence % 2 == 0) {
      if(Item.Phase != 0 || Sequence + 1 >= Capturing.Copies.size()) Matched = false;
      else {
        const EventCopy& Post = Capturing.Copies[Sequence + 1];
        if(Post.Phase != 1 || Post.Frame != Item.Frame || Post.Ids[0] != Item.Ids[0] || Post.Ids[1] != Item.Ids[1] ||
           Post.Rows[0] != Item.Rows[0] || Post.Rows[1] != Item.Rows[1]) Matched = false;
      }
    }
    const bool Known = Item.Rows[0] >= 0 && Item.Rows[0] < 13 && Item.Rows[1] >= 0 && Item.Rows[1] < 13;
    const float Mu = Known ? Friction[Item.Rows[0]] * Friction[Item.Rows[1]] : NAN;
    const float Elastic = Known ? Elasticity[Item.Rows[0]] * Elasticity[Item.Rows[1]] : NAN;
    fprintf(Events, "%zu,%d,%s,%d,%d,%d,%d,%d,%d,%.17g,%.17g", Sequence, Item.Frame, Item.Phase ? "post" : "pre",
      Item.Ids[0], Item.Ids[1], Item.Rows[0], Item.Rows[1], Item.IsCollision ? 1 : 0, Item.IsShadow ? 1 : 0, Item.Delta, Item.Speed);
    for(float Value : Item.Normal) fprintf(Events, ",%.17g", Value);
    for(float Value : Item.Point) fprintf(Events, ",%.17g", Value);
    for(float Value : Item.RelativeSpeed) fprintf(Events, ",%.17g", Value);
    fprintf(Events, ",%.17g,%.17g\n", Mu, Elastic);
  }
  fclose(Events);
  printf("capture=%d first_contact_frame=%d actual_contact44=%d solved_velocity_changed=%d pre_post_copies=%zu matched=%d\n",
    Capture ? 1 : 0, FirstContact, ContactReached ? 1 : 0, ResponseReached ? 1 : 0, Capturing.Copies.size(), Matched ? 1 : 0);
  if(!ContactReached || !ResponseReached) { printf("INVALID contact/solved branch not reached in12frames\n"); return 22; }
  if(Capture && (Capturing.Copies.empty() || !Matched)) { printf("INVALID public event branch/matched pair not proved\n"); return 23; }
  return 0;
}
