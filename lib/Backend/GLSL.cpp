#include "HECL/Backend/GLSL.hpp"
#include "HECL/Runtime.hpp"
#include <Athena/MemoryReader.hpp>
#include <Athena/MemoryWriter.hpp>
#include <boo/graphicsdev/GL.hpp>

static LogVisor::LogModule Log("HECL::Backend::GLSL");

namespace HECL
{
namespace Backend
{

std::string GLSL::EmitTexGenSource2(TexGenSrc src, int uvIdx) const
{
    switch (src)
    {
    case TG_POS:
        return "posIn.xy\n";
    case TG_NRM:
        return "normIn.xy\n";
    case TG_UV:
        return HECL::Format("uvIn[%u]", uvIdx);
    default: break;
    }
    return std::string();
}

std::string GLSL::EmitTexGenSource4(TexGenSrc src, int uvIdx) const
{
    switch (src)
    {
    case TG_POS:
        return "vec4(posIn, 1.0)\n";
    case TG_NRM:
        return "vec4(normIn, 1.0)\n";
    case TG_UV:
        return HECL::Format("vec4(uvIn[%u], 0.0, 1.0)", uvIdx);
    default: break;
    }
    return std::string();
}

std::string GLSL::GenerateVertInStruct(unsigned col, unsigned uv, unsigned w) const
{
    std::string retval =
    "layout(location=0) in vec3 posIn;\n"
    "layout(location=1) in vec3 normIn;\n";

    unsigned idx = 2;
    if (col)
    {
        retval += HECL::Format("layout(location=%u) in vec4 colIn[%u];\n", idx, col);
        idx += col;
    }

    if (uv)
    {
        retval += HECL::Format("layout(location=%u) in vec2 uvIn[%u];\n", idx, uv);
        idx += uv;
    }

    if (w)
    {
        retval += HECL::Format("layout(location=%u) in vec4 weightIn[%u];\n", idx, w);
    }

    return retval;
}

std::string GLSL::GenerateVertToFragStruct() const
{
    std::string retval =
    "struct VertToFrag\n"
    "{\n"
    "    vec4 mvPos;\n"
    "    vec4 mvNorm;\n";

    if (m_tcgs.size())
        retval += HECL::Format("    vec2 tcgs[%u];\n", unsigned(m_tcgs.size()));

    return retval + "};\n";
}

std::string GLSL::GenerateVertUniformStruct(unsigned skinSlots, unsigned texMtxs) const
{
    if (skinSlots == 0)
        skinSlots = 1;
    std::string retval = HECL::Format("uniform HECLVertUniform\n"
                                      "{\n"
                                      "    mat4 mv[%u];\n"
                                      "    mat4 mvInv[%u];\n"
                                      "    mat4 proj;\n",
                                      skinSlots, skinSlots);
    if (texMtxs)
        retval += HECL::Format("    mat4 texMtxs[%u];\n", texMtxs);
    return retval + "} vu;\n";
}

void GLSL::reset(const IR& ir, Diagnostics& diag)
{
    /* Common programmable interpretation */
    ProgrammableCommon::reset(ir, diag, "GLSL");
}

std::string GLSL::makeVert(const char* glslVer, unsigned col, unsigned uv, unsigned w,
                           unsigned s, unsigned tm) const
{
    std::string retval = std::string(glslVer) + "\n" +
            GenerateVertInStruct(col, uv, w) + "\n" +
            GenerateVertToFragStruct() + "\n" +
            GenerateVertUniformStruct(s, tm) +
            "out VertToFrag vtf;\n\n"
            "void main()\n{\n";

    if (s)
    {
        /* skinned */
        retval += "    vec4 posAccum = vec4(0.0,0.0,0.0,0.0);\n"
                  "    vec4 normAccum = vec4(0.0,0.0,0.0,0.0);\n";
        for (size_t i=0 ; i<s ; ++i)
            retval += HECL::Format("    posAccum += (vu.mv[%" PRISize "] * vec4(posIn, 1.0)) * weightIn[%" PRISize "][%" PRISize "];\n"
                                   "    normAccum += (vu.mvInv[%" PRISize "] * vec4(normIn, 1.0)) * weightIn[%" PRISize "][%" PRISize "];\n",
                                   i, i/4, i%4, i, i/4, i%4);
        retval += "    posAccum[3] = 1.0;\n"
                  "    vtf.mvPos = posAccum;\n"
                  "    vtf.mvNorm = vec4(normalize(normAccum.xyz), 0.0);\n"
                  "    gl_Position = vu.proj * posAccum;\n";
    }
    else
    {
        /* non-skinned */
        retval += "    vtf.mvPos = vu.mv[0] * vec4(posIn, 1.0);\n"
                  "    vtf.mvNorm = vu.mvInv[0] * vec4(normIn, 0.0);\n"
                  "    gl_Position = vu.proj * vtf.mvPos;\n";
    }

    int tcgIdx = 0;
    for (const TexCoordGen& tcg : m_tcgs)
    {
        if (tcg.m_mtx < 0)
            retval += HECL::Format("    vtf.tcgs[%u] = %s;\n", tcgIdx,
                                   EmitTexGenSource2(tcg.m_src, tcg.m_uvIdx).c_str());
        else
            retval += HECL::Format("    vtf.tcgs[%u] = (vu.texMtxs[%u] * %s).xy;\n", tcgIdx, tcg.m_mtx,
                                   EmitTexGenSource4(tcg.m_src, tcg.m_uvIdx).c_str());
        ++tcgIdx;
    }

    return retval + "}\n";
}

std::string GLSL::makeFrag(const char* glslVer,
                           const ShaderFunction& lighting) const
{
    std::string lightingSrc;
    if (lighting.m_source)
        lightingSrc = lighting.m_source;

    std::string texMapDecl;
    if (m_texMapEnd)
        texMapDecl = HECL::Format("uniform sampler2D texs[%u];\n", m_texMapEnd);

    std::string retval = std::string(glslVer) + "\n" +
            GenerateVertToFragStruct() +
            "\nlayout(location=0) out vec4 colorOut;\n" +
            texMapDecl +
            "in VertToFrag vtf;\n\n" +
            lightingSrc + "\n" +
            "void main()\n{\n";


    if (m_lighting)
    {
        if (lighting.m_entry)
            retval += HECL::Format("    vec4 lighting = %s();\n", lighting.m_entry);
        else
            retval += "    vec4 lighting = vec4(1.0,1.0,1.0,1.0);\n";
    }

    unsigned sampIdx = 0;
    for (const TexSampling& sampling : m_texSamplings)
        retval += HECL::Format("    vec4 sampling%u = texture(texs[%u], vtf.tcgs[%u]);\n",
                               sampIdx++, sampling.mapIdx, sampling.tcgIdx);

    if (m_alphaExpr.size())
        retval += "    colorOut = vec4(" + m_colorExpr + ", " + m_alphaExpr + ");\n";
    else
        retval += "    colorOut = vec4(" + m_colorExpr + ", 1.0);\n";

    return retval + "}\n";
}

std::string GLSL::makeFrag(const char* glslVer,
                           const ShaderFunction& lighting,
                           const ShaderFunction& post) const
{
    std::string lightingSrc;
    if (lighting.m_source)
        lightingSrc = lighting.m_source;

    std::string postSrc;
    if (post.m_source)
        postSrc = post.m_source;

    std::string postEntry;
    if (post.m_entry)
        postEntry = post.m_entry;

    std::string texMapDecl;
    if (m_texMapEnd)
        texMapDecl = HECL::Format("uniform sampler2D texs[%u];\n", m_texMapEnd);

    std::string retval = std::string(glslVer) + "\n" +
            GenerateVertToFragStruct() +
            "\nlayout(location=0) out vec4 colorOut;\n" +
            texMapDecl +
            "in VertToFrag vtf;\n\n" +
            lightingSrc + "\n" +
            postSrc +
            "\nvoid main()\n{\n";

    if (m_lighting)
    {
        if (lighting.m_entry)
            retval += HECL::Format("    vec4 lighting = %s();\n", lighting.m_entry);
        else
            retval += "    vec4 lighting = vec4(1.0,1.0,1.0,1.0);\n";
    }

    unsigned sampIdx = 0;
    for (const TexSampling& sampling : m_texSamplings)
        retval += HECL::Format("    vec4 sampling%u = texture(texs[%u], vtf.tcgs[%u]);\n",
                               sampIdx++, sampling.mapIdx, sampling.tcgIdx);

    if (m_alphaExpr.size())
        retval += "    colorOut = " + postEntry + "(vec4(" + m_colorExpr + ", " + m_alphaExpr + "));\n";
    else
        retval += "    colorOut = " + postEntry + "(vec4(" + m_colorExpr + ", 1.0));\n";

    return retval + "}\n";
}

}
namespace Runtime
{

static const char* STD_BLOCKNAMES[] = {"HECLVertUniform"};

struct GLSLBackendFactory : IShaderBackendFactory
{
    Backend::GLSL m_backend;
    boo::GLDataFactory* m_gfxFactory;

    GLSLBackendFactory(boo::IGraphicsDataFactory* gfxFactory)
    : m_gfxFactory(dynamic_cast<boo::GLDataFactory*>(gfxFactory)) {}

    ShaderCachedData buildShaderFromIR(const ShaderTag& tag,
                                       const HECL::Frontend::IR& ir,
                                       HECL::Frontend::Diagnostics& diag,
                                       boo::IShaderPipeline** objOut)
    {
        m_backend.reset(ir, diag);
        size_t cachedSz = 3;

        std::string vertSource =
        m_backend.makeVert("#version 330",
                           tag.getColorCount(), tag.getUvCount(), tag.getWeightCount(),
                           tag.getSkinSlotCount(), tag.getTexMtxCount());
        cachedSz += vertSource.size() + 1;

        std::string fragSource = m_backend.makeFrag("#version 330");
        cachedSz += fragSource.size() + 1;
        *objOut =
        m_gfxFactory->newShaderPipeline(vertSource.c_str(), fragSource.c_str(),
                                        m_backend.m_texMapEnd, "texs",
                                        1, STD_BLOCKNAMES,
                                        m_backend.m_blendSrc, m_backend.m_blendDst,
                                        tag.getDepthTest(), tag.getDepthWrite(),
                                        tag.getBackfaceCulling());
        if (!*objOut)
            Log.report(LogVisor::FatalError, "unable to build shader");

        ShaderCachedData dataOut(tag, cachedSz);
        Athena::io::MemoryWriter w(dataOut.m_data.get(), dataOut.m_sz);
        w.writeUByte(m_backend.m_texMapEnd);
        w.writeUByte(m_backend.m_blendSrc);
        w.writeUByte(m_backend.m_blendDst);
        w.writeString(vertSource);
        w.writeString(fragSource);

        return dataOut;
    }

    boo::IShaderPipeline* buildShaderFromCache(const ShaderCachedData& data)
    {
        const ShaderTag& tag = data.m_tag;
        Athena::io::MemoryReader r(data.m_data.get(), data.m_sz);
        atUint8 texMapEnd = r.readUByte();
        boo::BlendFactor blendSrc = boo::BlendFactor(r.readUByte());
        boo::BlendFactor blendDst = boo::BlendFactor(r.readUByte());
        std::string vertSource = r.readString();
        std::string fragSource = r.readString();
        boo::IShaderPipeline* ret =
        m_gfxFactory->newShaderPipeline(vertSource.c_str(), fragSource.c_str(),
                                        texMapEnd, "texs",
                                        1, STD_BLOCKNAMES,
                                        blendSrc, blendDst,
                                        tag.getDepthTest(), tag.getDepthWrite(),
                                        tag.getBackfaceCulling());
        if (!ret)
            Log.report(LogVisor::FatalError, "unable to build shader");
        return ret;
    }

    ShaderCachedData buildExtendedShaderFromIR(const ShaderTag& tag,
                                               const HECL::Frontend::IR& ir,
                                               HECL::Frontend::Diagnostics& diag,
                                               const std::vector<ShaderCacheExtensions::ExtensionSlot>& extensionSlots,
                                               FReturnExtensionShader returnFunc)
    {
        m_backend.reset(ir, diag);
        size_t cachedSz = 3;

        std::string vertSource =
        m_backend.makeVert("#version 330",
                           tag.getColorCount(), tag.getUvCount(), tag.getWeightCount(),
                           tag.getSkinSlotCount(), tag.getTexMtxCount());
        cachedSz += vertSource.size() + 1;

        std::vector<std::string> fragSources;
        fragSources.reserve(extensionSlots.size());
        for (const ShaderCacheExtensions::ExtensionSlot& slot : extensionSlots)
        {
            fragSources.push_back(m_backend.makeFrag("#version 330", slot.lighting, slot.post));
            cachedSz += fragSources.back().size() + 1;
            boo::IShaderPipeline* ret =
            m_gfxFactory->newShaderPipeline(vertSource.c_str(), fragSources.back().c_str(),
                                            m_backend.m_texMapEnd, "texs",
                                            1, STD_BLOCKNAMES,
                                            m_backend.m_blendSrc, m_backend.m_blendDst,
                                            tag.getDepthTest(), tag.getDepthWrite(),
                                            tag.getBackfaceCulling());
            if (!ret)
                Log.report(LogVisor::FatalError, "unable to build shader");
            returnFunc(ret);
        }

        ShaderCachedData dataOut(tag, cachedSz);
        Athena::io::MemoryWriter w(dataOut.m_data.get(), dataOut.m_sz);
        w.writeUByte(m_backend.m_texMapEnd);
        w.writeUByte(m_backend.m_blendSrc);
        w.writeUByte(m_backend.m_blendDst);
        w.writeString(vertSource);
        for (const std::string src : fragSources)
            w.writeString(src);

        return dataOut;
    }

    void buildExtendedShaderFromCache(const ShaderCachedData& data,
                                      const std::vector<ShaderCacheExtensions::ExtensionSlot>& extensionSlots,
                                      FReturnExtensionShader returnFunc)
    {
        const ShaderTag& tag = data.m_tag;
        Athena::io::MemoryReader r(data.m_data.get(), data.m_sz);
        atUint8 texMapEnd = r.readUByte();
        boo::BlendFactor blendSrc = boo::BlendFactor(r.readUByte());
        boo::BlendFactor blendDst = boo::BlendFactor(r.readUByte());
        std::string vertSource = r.readString();
        for (const ShaderCacheExtensions::ExtensionSlot& slot : extensionSlots)
        {
            std::string fragSource = r.readString();
            boo::IShaderPipeline* ret =
            m_gfxFactory->newShaderPipeline(vertSource.c_str(), fragSource.c_str(),
                                            texMapEnd, "texs",
                                            1, STD_BLOCKNAMES,
                                            blendSrc, blendDst,
                                            tag.getDepthTest(), tag.getDepthWrite(),
                                            tag.getBackfaceCulling());
            if (!ret)
                Log.report(LogVisor::FatalError, "unable to build shader");
            returnFunc(ret);
        }
    }
};

IShaderBackendFactory* _NewGLSLBackendFactory(boo::IGraphicsDataFactory* gfxFactory)
{
    return new struct GLSLBackendFactory(gfxFactory);
}

}
}
