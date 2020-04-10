#pragma once

#if _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "hecl/hecl.hpp"
#include "hecl/Backend.hpp"
#include "hecl/HMDLMeta.hpp"
#include "hecl/TypedVariant.hpp"

#include <athena/Types.hpp>
#include <fmt/ostream.h>
#include <logvisor/logvisor.hpp>

namespace hecl::blender {
using namespace std::literals;

class Connection;
class HMDLBuffers;

extern logvisor::Module BlenderLog;

struct PoolSkinIndex {
  std::size_t m_poolSz = 0;
  std::unique_ptr<uint32_t[]> m_poolToSkinIndex;

  void allocate(std::size_t poolSz) {
    m_poolSz = poolSz;
    if (poolSz)
      m_poolToSkinIndex.reset(new uint32_t[poolSz]);
  }
};

enum class ANIMCurveType { Rotate, Translate, Scale };

class ANIMOutStream {
  Connection* m_parent;
  unsigned m_curCount = 0;
  unsigned m_totalCount = 0;
  bool m_inCurve = false;

public:
  using CurveType = ANIMCurveType;
  ANIMOutStream(Connection* parent);
  ~ANIMOutStream();
  void changeCurve(CurveType type, unsigned crvIdx, unsigned keyCount);
  void write(unsigned frame, float val);
};

class PyOutStream : public std::ostream {
  friend class Connection;
  Connection* m_parent;
  struct StreamBuf : std::streambuf {
    PyOutStream& m_parent;
    std::string m_lineBuf;
    bool m_deleteOnError;
    StreamBuf(PyOutStream& parent, bool deleteOnError) : m_parent(parent), m_deleteOnError(deleteOnError) {}
    StreamBuf(const StreamBuf& other) = delete;
    StreamBuf(StreamBuf&& other) = default;
    bool sendLine(std::string_view line);
    std::streamsize xsputn(const char_type* __s, std::streamsize __n) override;
  } m_sbuf;
  PyOutStream(Connection* parent, bool deleteOnError);

public:
  PyOutStream(const PyOutStream& other) = delete;
  PyOutStream(PyOutStream&& other) : std::ostream(&m_sbuf), m_parent(other.m_parent), m_sbuf(std::move(other.m_sbuf)) {
    other.m_parent = nullptr;
  }
  ~PyOutStream() override { close(); }
  void close();
  template <typename S, typename... Args, typename Char = fmt::char_t<S>>
  void format(const S& format, Args&&... args);
  void linkBlend(std::string_view target, std::string_view objName, bool link = true);
  void linkArmature(std::string_view target, std::string_view armName);
  void linkMesh(std::string_view target, std::string_view meshName);
  void linkBackground(std::string_view target, std::string_view sceneName = {});
  void AABBToBMesh(const atVec3f& min, const atVec3f& max);
  void centerView();

  ANIMOutStream beginANIMCurve() { return ANIMOutStream(m_parent); }
  Connection& getConnection() { return *m_parent; }
};

/* Vector types with integrated stream reading constructor */
struct Vector2f {
  atVec2f val;
  Vector2f() = default;
  void read(Connection& conn);
  Vector2f(Connection& conn) { read(conn); }
  operator const atVec2f&() const { return val; }
  bool operator==(const Vector2f& other) const {
    return val.simd[0] == other.val.simd[0] &&
           val.simd[1] == other.val.simd[1];
  }
};
struct Vector3f {
  atVec3f val;
  Vector3f() = default;
  void read(Connection& conn);
  Vector3f(Connection& conn) { read(conn); }
  operator const atVec3f&() const { return val; }
  bool operator==(const Vector3f& other) const {
    return val.simd[0] == other.val.simd[0] &&
           val.simd[1] == other.val.simd[1] &&
           val.simd[2] == other.val.simd[2];
  }
};
struct Vector4f {
  atVec4f val;
  Vector4f() = default;
  void read(Connection& conn);
  Vector4f(Connection& conn) { read(conn); }
  operator const atVec4f&() const { return val; }
  bool operator==(const Vector4f& other) const {
    return val.simd[0] == other.val.simd[0] &&
           val.simd[1] == other.val.simd[1] &&
           val.simd[2] == other.val.simd[2] &&
           val.simd[3] == other.val.simd[3];
  }
};
struct Matrix3f {
  std::array<atVec3f, 3> m;
  atVec3f& operator[](std::size_t idx) { return m[idx]; }
  const atVec3f& operator[](std::size_t idx) const { return m[idx]; }
};
struct Matrix4f {
  std::array<atVec4f, 4> val;
  Matrix4f() = default;
  Matrix4f(Connection& conn) { read(conn); }
  void read(Connection& conn);
  const atVec4f& operator[](std::size_t idx) const { return val[idx]; }
};
struct Index {
  uint32_t val;
  Index() = default;
  void read(Connection& conn);
  Index(Connection& conn) { read(conn); }
  operator const uint32_t&() const { return val; }
};
struct Float {
  float val;
  Float() = default;
  void read(Connection& conn);
  Float(Connection& conn) { read(conn); }
  operator const float&() const { return val; }
};
struct Boolean {
  bool val;
  Boolean() = default;
  void read(Connection& conn);
  Boolean(Connection& conn) { read(conn); }
  operator const bool&() const { return val; }
};

atVec3f MtxVecMul4RM(const Matrix4f& mtx, const Vector3f& vec);
atVec3f MtxVecMul3RM(const Matrix4f& mtx, const Vector3f& vec);

/** Intermediate material representation */
struct Material {
  enum class ShaderType : uint32_t {
    Invalid = 0,
    RetroShader = 'RSHD',
    RetroDynamicShader = 'RDYN',
    RetroDynamicAlphaShader = 'RDAL',
    RetroDynamicCharacterShader = 'RCHR',
  };
  enum class ChunkType : uint32_t {
    Invalid = 0,
    TexturePass = 'PASS',
    ColorPass = 'CLR ',
  };
  enum class PassType : uint32_t {
    Invalid = 0,
    Lightmap = 'LMAP',
    Diffuse = 'DIFF',
    Emissive = 'EMIS',
    Specular = 'SPEC',
    ExtendedSpecular = 'ESPC',
    Reflection = 'REFL',
    IndirectTex = 'INDR',
    Alpha = 'ALPH',
  };
  static constexpr std::string_view PassTypeToString(PassType tp) {
    switch (tp) {
    case PassType::Lightmap: return "lightmap"sv;
    case PassType::Diffuse: return "diffuse"sv;
    case PassType::Emissive: return "emissive"sv;
    case PassType::Specular: return "specular"sv;
    case PassType::ExtendedSpecular: return "extendedSpecular"sv;
    case PassType::Reflection: return "reflection"sv;
    case PassType::Alpha: return "alpha"sv;
    default:
      assert(false && "Unknown pass type");
      return ""sv;
    }
  }
  enum class UVAnimType : uint8_t {
    MvInvNoTranslation,
    MvInv,
    Scroll,
    Rotation,
    HStrip,
    VStrip,
    Model,
    CylinderEnvironment,
    Eight,
    Invalid = UINT8_MAX
  };
  using TexCoordSource = hecl::Backend::TexCoordSource;
  struct PASS : hecl::TypedRecord<ChunkType::TexturePass> {
    PassType type;
    ProjectPath tex;
    TexCoordSource source;
    UVAnimType uvAnimType;
    std::array<float, 9> uvAnimParms = {};
    bool alpha;
    explicit PASS(Connection& conn);
    bool operator==(const PASS& other) const {
      return type == other.type &&
             tex == other.tex &&
             source == other.source &&
             uvAnimType == other.uvAnimType &&
             uvAnimParms == other.uvAnimParms &&
             alpha == other.alpha;
    }
    void hash(XXH64_state_t* st) const {
      XXH64_update(st, &type, sizeof(type));
      XXH64_update(st, &source, sizeof(source));
      XXH64_update(st, &uvAnimType, sizeof(uvAnimType));
      XXH64_update(st, &alpha, sizeof(alpha));
    }
  };
  struct CLR : hecl::TypedRecord<ChunkType::ColorPass> {
    PassType type;
    Vector4f color;
    explicit CLR(Connection& conn);
    bool operator==(const CLR& other) const {
      return type == other.type && color == other.color;
    }
    void hash(XXH64_state_t* st) const {
      XXH64_update(st, &type, sizeof(type));
    }
  };
  using Chunk = hecl::TypedVariant<PASS, CLR>;

  enum class BlendMode : uint32_t {
    Opaque = 0,
    Alpha = 1,
    Additive = 2
  };

  std::string name;
  uint32_t passIndex;
  ShaderType shaderType;
  std::vector<Chunk> chunks;
  std::unordered_map<std::string, int32_t> iprops;
  BlendMode blendMode = BlendMode::Opaque;

  explicit Material(Connection& conn);
  bool operator==(const Material& other) const {
    return chunks == other.chunks && iprops == other.iprops && blendMode == other.blendMode;
  }
};

/** Intermediate mesh representation prepared by blender from a single mesh object */
struct Mesh {
  static constexpr std::size_t MaxColorLayers = 4;
  static constexpr std::size_t MaxUVLayers = 8;
  static constexpr std::size_t MaxSkinEntries = 16;

  HMDLTopology topology;

  /* Object transform in scene */
  Matrix4f sceneXf;

  /* Cumulative AABB */
  Vector3f aabbMin;
  Vector3f aabbMax;

  std::vector<std::vector<Material>> materialSets;

  /* Vertex buffer data */
  std::vector<Vector3f> pos;
  std::vector<Vector3f> norm;
  uint32_t colorLayerCount = 0;
  std::vector<Vector3f> color;
  uint32_t uvLayerCount = 0;
  std::vector<Vector2f> uv;
  uint32_t luvLayerCount = 0;
  std::vector<Vector2f> luv;

  /* Skinning data */
  std::vector<std::string> boneNames;
  struct SkinBind {
    uint32_t vg_idx = UINT32_MAX;
    float weight = 0.f;
    SkinBind() = default;
    explicit SkinBind(Connection& conn);
    bool valid() const { return vg_idx != UINT32_MAX; }
    bool operator==(const SkinBind& other) const { return vg_idx == other.vg_idx && weight == other.weight; }
  };
  std::vector<std::array<SkinBind, MaxSkinEntries>> skins;
  std::vector<std::size_t> contiguousSkinVertCounts;

  static std::size_t countSkinBinds(const std::array<SkinBind, MaxSkinEntries>& arr) {
    std::size_t ret = 0;
    for (const auto& b : arr)
      if (b.valid())
        ++ret;
      else
        break;
    return ret;
  }
  void normalizeSkinBinds();

  /** Islands of the same material/skinBank are represented here */
  struct Surface {
    Vector3f centroid;
    uint32_t materialIdx;
    Vector3f aabbMin;
    Vector3f aabbMax;
    Vector3f reflectionNormal;
    uint32_t skinBankIdx;

    /** Vertex indexing data (all primitives joined as degenerate tri-strip) */
    struct Vert {
      uint32_t iPos = 0xffffffff;
      uint32_t iNorm = 0xffffffff;
      std::array<uint32_t, 4> iColor = {0xffffffff};
      std::array<uint32_t, 8> iUv = {0xffffffff};
      uint32_t iSkin = 0xffffffff;
      uint32_t iBankSkin = 0xffffffff;

      bool operator==(const Vert& other) const;
    };
    std::vector<Vert> verts;
  };
  std::vector<Surface> surfaces;

  std::unordered_map<std::string, std::string> customProps;

  struct SkinBanks {
    struct Bank {
      std::vector<uint32_t> m_skinIdxs;
      std::vector<uint32_t> m_boneIdxs;

      void addSkins(const Mesh& parent, const std::vector<uint32_t>& skinIdxs);
    };
    std::vector<Bank> banks;
    std::vector<Bank>::iterator addSkinBank(int skinSlotCount);
    uint32_t addSurface(const Mesh& mesh, const Surface& surf, int skinSlotCount);
  } skinBanks;

  Mesh(Connection& conn, HMDLTopology topology, int skinSlotCount, bool useLuvs = false);

  Mesh getContiguousSkinningVersion() const;

  /** Prepares mesh representation for indexed access on modern APIs.
   *  Mesh must remain resident for accessing reference members
   */
  HMDLBuffers getHMDLBuffers(bool absoluteCoords, PoolSkinIndex& poolSkinIndex) const;
};

/** Intermediate collision mesh representation prepared by blender from a single mesh object */
struct ColMesh {
  /** HECL source and metadata of each material */
  struct Material {
    std::string name;
    bool unknown;
    bool surfaceStone;
    bool surfaceMetal;
    bool surfaceGrass;
    bool surfaceIce;
    bool pillar;
    bool surfaceMetalGrating;
    bool surfacePhazon;
    bool surfaceDirt;
    bool surfaceLava;
    bool surfaceSPMetal;
    bool surfaceLavaStone;
    bool surfaceSnow;
    bool surfaceMudSlow;
    bool surfaceFabric;
    bool halfPipe;
    bool surfaceMud;
    bool surfaceGlass;
    bool unused3;
    bool unused4;
    bool surfaceShield;
    bool surfaceSand;
    bool surfaceMothOrSeedOrganics;
    bool surfaceWeb;
    bool projPassthrough;
    bool solid;
    bool noPlatformCollision;
    bool camPassthrough;
    bool surfaceWood;
    bool surfaceOrganic;
    bool noEdgeCollision;
    bool surfaceRubber;
    bool seeThrough;
    bool scanPassthrough;
    bool aiPassthrough;
    bool ceiling;
    bool wall;
    bool floor;
    bool aiBlock;
    bool jumpNotAllowed;
    bool spiderBall;
    bool screwAttackWallJump;

    Material(Connection& conn);
  };
  std::vector<Material> materials;

  std::vector<Vector3f> verts;

  struct Edge {
    std::array<uint32_t, 2> verts;
    bool seam;
    Edge(Connection& conn);
  };
  std::vector<Edge> edges;

  struct Triangle {
    std::array<uint32_t, 3> edges;
    uint32_t matIdx;
    bool flip;
    Triangle(Connection& conn);
  };
  std::vector<Triangle> trianges;

  ColMesh(Connection& conn);
};

/** Intermediate world representation */
struct World {
  struct Area {
    ProjectPath path;
    std::array<Vector3f, 2> aabb;
    Matrix4f transform;
    struct Dock {
      std::array<Vector3f, 4> verts;
      Index targetArea;
      Index targetDock;
      Dock(Connection& conn);
    };
    std::vector<Dock> docks;
    Area(Connection& conn);
  };
  std::vector<Area> areas;
  World(Connection& conn);
};

/** Intermediate lamp representation */
struct Light {
  /* Object transform in scene */
  Matrix4f sceneXf;
  Vector3f color;

  uint32_t layer;

  enum class Type : uint32_t { Ambient, Directional, Custom, Spot } type;

  float energy;
  float spotCutoff;
  float constant;
  float linear;
  float quadratic;
  bool shadow;

  std::string name;

  Light(Connection& conn);
};

/** Intermediate MapArea representation */
struct MapArea {
  uint32_t visType;
  std::vector<Vector3f> verts;
  std::vector<uint32_t> indices;
  struct Surface {
    Vector3f normal;
    Vector3f centerOfMass;
    uint32_t start;
    uint32_t count;
    std::vector<std::pair<uint32_t, uint32_t>> borders;
    Surface(Connection& conn);
  };
  std::vector<Surface> surfaces;
  struct POI {
    uint32_t type;
    uint32_t visMode;
    uint32_t objid;
    Matrix4f xf;
    POI(Connection& conn);
  };
  std::vector<POI> pois;
  MapArea(Connection& conn);
};

/** Intermediate MapUniverse representation */
struct MapUniverse {
  hecl::ProjectPath hexagonPath;
  struct World {
    std::string name;
    Matrix4f xf;
    std::vector<Matrix4f> hexagons;
    Vector4f color;
    hecl::ProjectPath worldPath;
    World(Connection& conn);
  };
  std::vector<World> worlds;
  MapUniverse(Connection& conn);
};

/** Intermediate bone representation used in Armature */
struct Bone {
  std::string name;
  Vector3f origin;
  int32_t parent = -1;
  std::vector<int32_t> children;
  Bone(Connection& conn);
};

/** Intermediate armature representation used in Actor */
struct Armature {
  std::vector<Bone> bones;
  const Bone* lookupBone(const char* name) const;
  const Bone* getParent(const Bone* bone) const;
  const Bone* getChild(const Bone* bone, std::size_t child) const;
  const Bone* getRoot() const;
  Armature(Connection& conn);
};

/** Intermediate action representation used in Actor */
struct Action {
  std::string name;
  std::string animId;
  float interval;
  bool additive;
  bool looping;
  std::vector<int32_t> frames;
  struct Channel {
    std::string boneName;
    uint32_t attrMask;
    struct Key {
      Vector4f rotation;
      Vector3f position;
      Vector3f scale;
      Key(Connection& conn, uint32_t attrMask);
    };
    std::vector<Key> keys;
    Channel(Connection& conn);
  };
  std::vector<Channel> channels;
  std::vector<std::pair<Vector3f, Vector3f>> subtypeAABBs;
  Action(Connection& conn);
};

/** Intermediate actor representation prepared by blender from a single HECL actor blend */
struct Actor {
  struct ActorArmature {
    std::string name;
    ProjectPath path;
    std::optional<Armature> armature;
    ActorArmature(Connection& conn);
  };
  std::vector<ActorArmature> armatures;

  struct Subtype {
    std::string name;
    std::string cskrId;
    ProjectPath mesh;
    int32_t armature = -1;
    struct OverlayMesh {
      std::string name;
      std::string cskrId;
      ProjectPath mesh;
      OverlayMesh(Connection& conn);
    };
    std::vector<OverlayMesh> overlayMeshes;
    Subtype(Connection& conn);
  };
  std::vector<Subtype> subtypes;
  struct Attachment {
    std::string name;
    std::string cskrId;
    ProjectPath mesh;
    int32_t armature = -1;
    Attachment(Connection& conn);
  };
  std::vector<Attachment> attachments;
  std::vector<Action> actions;

  Actor(Connection& conn);
};

/** Intermediate pathfinding representation prepared by blender */
struct PathMesh {
  std::vector<uint8_t> data;
  PathMesh(Connection& conn);
};

class DataStream {
  friend class Connection;
  Connection* m_parent;
  DataStream(Connection* parent);

public:
  DataStream(const DataStream& other) = delete;
  DataStream(DataStream&& other) : m_parent(other.m_parent) { other.m_parent = nullptr; }
  ~DataStream() { close(); }
  void close();
  std::vector<std::string> getMeshList();
  std::vector<std::string> getLightList();
  std::pair<atVec3f, atVec3f> getMeshAABB();
  static const char* MeshOutputModeString(HMDLTopology topology);

  /** Compile mesh by context (MESH blends only) */
  Mesh compileMesh(HMDLTopology topology, int skinSlotCount = 10);

  /** Compile mesh by name (AREA blends only) */
  Mesh compileMesh(std::string_view name, HMDLTopology topology, int skinSlotCount = 10, bool useLuv = false);

  /** Compile collision mesh by name (AREA blends only) */
  ColMesh compileColMesh(std::string_view name);

  /** Compile all meshes as collision meshes (CMESH blends only) */
  std::vector<ColMesh> compileColMeshes();

  /** Compile world intermediate (WORLD blends only) */
  World compileWorld();

  /** Gather all lights in scene (AREA blends only) */
  std::vector<Light> compileLights();

  /** Get PathMesh from scene (PATH blends only) */
  PathMesh compilePathMesh();

  /** Compile GUI into FRME data (FRAME blends only) */
  std::vector<uint8_t> compileGuiFrame(int version);

  /** Gather all texture paths in scene */
  std::vector<ProjectPath> getTextures();

  Actor compileActor();
  Actor compileActorCharacterOnly();
  Armature compileArmature();
  Action compileActionChannelsOnly(std::string_view name);
  std::vector<std::pair<std::string, std::string>> getSubtypeNames();
  std::vector<std::pair<std::string, std::string>> getActionNames();
  std::vector<std::pair<std::string, std::string>> getSubtypeOverlayNames(std::string_view name);
  std::vector<std::pair<std::string, std::string>> getAttachmentNames();

  std::unordered_map<std::string, Matrix3f> getBoneMatrices(std::string_view name);

  bool renderPvs(std::string_view path, const atVec3f& location);
  bool renderPvsLight(std::string_view path, std::string_view lightName);

  MapArea compileMapArea();
  MapUniverse compileMapUniverse();
};

class Connection {
  friend class ANIMOutStream;
  friend class DataStream;
  friend class PyOutStream;
  friend struct Action;
  friend struct Actor;
  friend struct Armature;
  friend struct Bone;
  friend struct Boolean;
  friend struct ColMesh;
  friend struct Float;
  friend struct Index;
  friend struct Light;
  friend struct MapArea;
  friend struct MapUniverse;
  friend struct Material;
  friend struct Matrix3f;
  friend struct Matrix4f;
  friend struct Mesh;
  friend struct PathMesh;
  friend struct PyOutStream::StreamBuf;
  friend struct Vector2f;
  friend struct Vector3f;
  friend struct Vector4f;
  friend struct World;
  friend class MeshOptimizer;

  std::atomic_bool m_lock = {false};
  bool m_pyStreamActive = false;
  bool m_dataStreamActive = false;
  bool m_blenderQuit = false;
#if _WIN32
  PROCESS_INFORMATION m_pinfo = {};
  std::thread m_consoleThread;
  bool m_consoleThreadRunning = true;
#else
  pid_t m_blenderProc = 0;
#endif
  std::array<int, 2> m_readpipe{};
  std::array<int, 2> m_writepipe{};
  BlendType m_loadedType = BlendType::None;
  bool m_loadedRigged = false;
  ProjectPath m_loadedBlend;
  hecl::SystemString m_errPath;
  uint32_t _readStr(char* buf, uint32_t bufSz);
  uint32_t _writeStr(const char* str, uint32_t len, int wpipe);
  uint32_t _writeStr(const char* str, uint32_t len) { return _writeStr(str, len, m_writepipe[1]); }
  uint32_t _writeStr(std::string_view view) { return _writeStr(view.data(), view.size()); }
  std::size_t _readBuf(void* buf, std::size_t len);
  std::size_t _writeBuf(const void* buf, std::size_t len);
  std::string _readStdString() {
    uint32_t bufSz;
    _readBuf(&bufSz, 4);
    std::string ret(bufSz, ' ');
    _readBuf(&ret[0], bufSz);
    return ret;
  }
  template<typename T, std::enable_if_t<std::disjunction_v<std::is_arithmetic<T>, std::is_enum<T>>, int> = 0>
  void _readValue(T& v) { _readBuf(&v, sizeof(T)); }
  template<typename T>
  void _readItems(T enumerator) {
    uint32_t nItems;
    _readBuf(&nItems, 4);
    for (uint32_t i = 0; i < nItems; ++i)
      enumerator(*this);
  }
  template<typename T, typename... Args, std::enable_if_t<
      !std::disjunction_v<std::is_arithmetic<T>, std::is_enum<T>, std::is_same<T, std::string>>, int> = 0>
  void _readVector(std::vector<T>& container, Args&&... args) {
    uint32_t nItems;
    _readBuf(&nItems, 4);
    container.clear();
    container.reserve(nItems);
    for (uint32_t i = 0; i < nItems; ++i)
      container.emplace_back(*this, std::forward<Args>(args)...);
  }
  template<typename T, std::enable_if_t<std::disjunction_v<std::is_arithmetic<T>, std::is_enum<T>>, int> = 0>
  void _readVector(std::vector<T>& container) {
    uint32_t nItems;
    _readBuf(&nItems, 4);
    container.clear();
    container.resize(nItems);
    _readBuf(&container[0], sizeof(T) * nItems);
  }
  void _readVector(std::vector<std::string>& container) {
    uint32_t nItems;
    _readBuf(&nItems, 4);
    container.clear();
    container.reserve(nItems);
    for (uint32_t i = 0; i < nItems; ++i) {
      uint32_t strSize;
      _readBuf(&strSize, 4);
      _readBuf(&container.emplace_back(strSize, ' ')[0], strSize);
    }
  }
  template<typename T, typename F>
  void _readVectorFunc(std::vector<T>& container, F func) {
    uint32_t nItems;
    _readBuf(&nItems, 4);
    container.clear();
    container.reserve(nItems);
    for (uint32_t i = 0; i < nItems; ++i)
      func();
  }
  ProjectPath _readPath();
  bool _isStatus(const char* status) {
    char readBuf[16];
    _readStr(readBuf, 16);
    return std::strcmp(readBuf, status) == 0;
  }
  bool _isOk() { return _isStatus("OK"); }
  bool _isFinished() { return _isStatus("FINISHED"); }
  bool _isTrue() { return _isStatus("TRUE"); }
  void _checkStatus(std::string_view action, std::string_view status) {
    char readBuf[16];
    _readStr(readBuf, 16);
    if (status != readBuf)
      BlenderLog.report(logvisor::Fatal, fmt("{}: {}: {}"), m_loadedBlend.getRelativePathUTF8(), action, readBuf);
  }
  void _checkReady(std::string_view action) { _checkStatus(action, "READY"sv); }
  void _checkDone(std::string_view action) { _checkStatus(action, "DONE"sv); }
  void _checkOk(std::string_view action) { _checkStatus(action, "OK"sv); }
  void _checkAnimReady(std::string_view action) { _checkStatus(action, "ANIMREADY"sv); }
  void _checkAnimDone(std::string_view action) { _checkStatus(action, "ANIMDONE"sv); }
  void _closePipe();
  void _blenderDied();

public:
  Connection(int verbosityLevel = 1);
  ~Connection();

  Connection(const Connection&) = delete;
  Connection& operator=(const Connection&) = delete;
  Connection(Connection&&) = delete;
  Connection& operator=(Connection&&) = delete;

  bool createBlend(const ProjectPath& path, BlendType type);
  BlendType getBlendType() const { return m_loadedType; }
  const ProjectPath& getBlendPath() const { return m_loadedBlend; }
  bool getRigged() const { return m_loadedRigged; }
  bool openBlend(const ProjectPath& path, bool force = false);
  bool saveBlend();
  void deleteBlend();

  PyOutStream beginPythonOut(bool deleteOnError = false) {
    bool expect = false;
    if (!m_lock.compare_exchange_strong(expect, true))
      BlenderLog.report(logvisor::Fatal, fmt("lock already held for blender::Connection::beginPythonOut()"));
    return PyOutStream(this, deleteOnError);
  }

  DataStream beginData() {
    bool expect = false;
    if (!m_lock.compare_exchange_strong(expect, true))
      BlenderLog.report(logvisor::Fatal, fmt("lock already held for blender::Connection::beginDataIn()"));
    return DataStream(this);
  }

  void quitBlender();

  void closeStream() {
    if (m_lock)
      deleteBlend();
  }

  static Connection& SharedConnection();
  static void Shutdown();
};

template <typename S, typename... Args, typename Char>
void PyOutStream::format(const S& format, Args&&... args) {
  if (!m_parent || !m_parent->m_lock)
    BlenderLog.report(logvisor::Fatal, fmt("lock not held for PyOutStream::format()"));
  fmt::print(*this, format, std::forward<Args>(args)...);
}

class HMDLBuffers {
public:
  struct Surface;

private:
  friend struct Mesh;
  HMDLBuffers(HMDLMeta&& meta, std::size_t vboSz, const std::vector<atUint32>& iboData, std::vector<Surface>&& surfaces,
              const Mesh::SkinBanks& skinBanks);

public:
  HMDLMeta m_meta;
  std::size_t m_vboSz;
  std::unique_ptr<uint8_t[]> m_vboData;
  std::size_t m_iboSz;
  std::unique_ptr<uint8_t[]> m_iboData;

  struct Surface {
    Surface(const Mesh::Surface& origSurf, atUint32 start, atUint32 count)
    : m_origSurf(origSurf), m_start(start), m_count(count) {}
    const Mesh::Surface& m_origSurf;
    atUint32 m_start;
    atUint32 m_count;
  };
  std::vector<Surface> m_surfaces;

  const Mesh::SkinBanks& m_skinBanks;
};

} // namespace hecl::blender

namespace std {
template <>
struct hash<hecl::blender::Vector2f> {
  std::size_t operator()(const hecl::blender::Vector2f& val) const noexcept {
    std::size_t h = std::hash<float>()(val.val.simd[0]);
    hecl::hash_combine_impl(h, std::hash<float>()(val.val.simd[1]));
    return h;
  }
};
template <>
struct hash<hecl::blender::Vector3f> {
  std::size_t operator()(const hecl::blender::Vector3f& val) const noexcept {
    std::size_t h = std::hash<float>()(val.val.simd[0]);
    hecl::hash_combine_impl(h, std::hash<float>()(val.val.simd[1]));
    hecl::hash_combine_impl(h, std::hash<float>()(val.val.simd[2]));
    return h;
  }
};
template <>
struct hash<hecl::blender::Vector4f> {
  std::size_t operator()(const hecl::blender::Vector4f& val) const noexcept {
    std::size_t h = std::hash<float>()(val.val.simd[0]);
    hecl::hash_combine_impl(h, std::hash<float>()(val.val.simd[1]));
    hecl::hash_combine_impl(h, std::hash<float>()(val.val.simd[2]));
    hecl::hash_combine_impl(h, std::hash<float>()(val.val.simd[3]));
    return h;
  }
};
template <>
struct hash<array<hecl::blender::Mesh::SkinBind, 16>> {
  std::size_t operator()(const array<hecl::blender::Mesh::SkinBind, 16>& val) const noexcept {
    std::size_t h = 0;
    for (const auto& bind : val) {
      if (!bind.valid())
        break;
      hecl::hash_combine_impl(h, std::hash<float>()(bind.vg_idx));
      hecl::hash_combine_impl(h, std::hash<float>()(bind.weight));
    }
    return h;
  }
};
}
