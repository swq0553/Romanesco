#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glut.h>

#include <QWindow>
#include <QOpenGLFunctions>
#include <QOpenGLPaintDevice>
#include <QOpenGLFramebufferObject>

#include <QScreen>
#include <QDebug>
#include <QKeyEvent>
#include <unistd.h>
#include <math.h>
#include <QtMath>
#include <QDir>
#include <fstream>
#include <string>
#include <cerrno>
#include <assert.h>

#include <OpenEXR/ImfRgba.h>
#include <OpenEXR/ImfRgbaFile.h>
#include <OpenEXR/ImfChannelList.h>
#include <OpenEXR/ImfFrameBuffer.h>
#include <OpenEXR/ImfOutputFile.h>
#include <OpenEXR/half.h>

#include <iostream>
#include <boost/algorithm/string/join.hpp>
#include <ImageLoader.h>

#include "OptixScene.h"
#include "RuntimeCompiler.h"


///@todo
/// * Move ALL image stuff into it's own OIIO based class
/// * Split this into a simple base class and derive from that, OptixScene -> OptixSceneAdaptive -> OptixScenePathTracer
/// * All camera stuff should be moved into it's own, simpler class

struct Image
{
public:
    Image( float* _pixels, unsigned int _width, unsigned int _height, std::string _name = "" )
        : m_width(_width), m_height(_height), m_name(_name)
  {
    m_pixels = new Imf::Rgba[m_width * m_height];
    std::fill(m_pixels, m_pixels + (m_width * m_height), Imf::Rgba(1.f, 1.f, 1.f, 1.f) );

    for(int i = 0; i < 4 * m_width * m_height; i+=4)
    {
        //unsigned int idx = i + (j * m_width);

        float R = _pixels[i];
        float G = _pixels[i + 1];
        float B = _pixels[i + 2];
        float A = _pixels[i + 3];

        //setPixel(i, j, Imf::Rgba(R, G, B, A) );
        m_pixels[i / 4] = Imf::Rgba(R, G, B, A);
    }
  }

  void setPixel(int x, int y, Imf::Rgba _val)
  {
    m_pixels[x + (y * m_width)] = _val;
  }

  ~Image()
  {
        //delete m_pixels;
  }

//private:
  Imf::Rgba* m_pixels;
  unsigned int m_width, m_height;
    std::string m_name;
};


std::string layerChannelString( std::string _layerName, std::string _channel )
{
    return (_layerName.size() == 0) ? _channel : _layerName + "." + _channel;
}

void writeRGBA2(std::string fileName, std::vector<Image> _layers)
{
    Imf::Header header(_layers[0].m_width, _layers[0].m_height);

    Imf::ChannelList& channels = header.channels();
    Imf::FrameBuffer framebuffer;

    for(unsigned int i = 0; i < _layers.size(); i++)
    {
        Image& _image = _layers[i];

        std::string name_r = layerChannelString(_image.m_name, "R");
        std::string name_g = layerChannelString(_image.m_name, "G");
        std::string name_b = layerChannelString(_image.m_name, "B");
        std::string name_a = layerChannelString(_image.m_name, "A");

        channels.insert( name_r, Imf::Channel(Imf::HALF) );
        channels.insert( name_g, Imf::Channel(Imf::HALF) );
        channels.insert( name_b, Imf::Channel(Imf::HALF) );
        channels.insert( name_a, Imf::Channel(Imf::HALF) );

        char* channel_rPtr = (char*) &(_image.m_pixels[0].r);
        char* channel_gPtr = (char*) &(_image.m_pixels[0].g);
        char* channel_bPtr = (char*) &(_image.m_pixels[0].b);
        char* channel_aPtr = (char*) &(_image.m_pixels[0].a);

        unsigned int xstride = sizeof( half ) * 4;
        unsigned int ystride = sizeof( half ) * 4 * _image.m_width;

        framebuffer.insert( name_r, Imf::Slice( Imf::HALF, channel_rPtr, xstride, ystride ) );
        framebuffer.insert( name_g, Imf::Slice( Imf::HALF, channel_gPtr, xstride, ystride ) );
        framebuffer.insert( name_b, Imf::Slice( Imf::HALF, channel_bPtr, xstride, ystride ) );
        framebuffer.insert( name_a, Imf::Slice( Imf::HALF, channel_aPtr, xstride, ystride ) );
    }

    Imf::OutputFile file(fileName.c_str(), header);
    file.setFrameBuffer( framebuffer );
    file.writePixels( _layers[0].m_height );
}



static const unsigned int WIDTH = 1280;
static const unsigned int HEIGHT = 720;


optix::Buffer OptixScene::createOutputBuffer(RTformat _format, unsigned int _width, unsigned int _height)
{
    optix::Buffer buffer;

    GLuint vbo = 0;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    size_t element_size;
    m_context->checkError( rtuGetSizeForRTformat(_format, &element_size) );
    glBufferData(GL_ARRAY_BUFFER, element_size * _width * _height, 0, GL_STREAM_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    buffer = m_context->createBufferFromGLBO(RT_BUFFER_OUTPUT, vbo);
    buffer->setFormat(_format);
    buffer->setSize( _width, _height );

    return buffer;
}

OptixScene::OptixScene(unsigned int _width, unsigned int _height)
    : m_time(0.0f)
{
    /// ================ Output Texture Buffer ======================

    glGenTextures( 1, &m_texId );
    glBindTexture( GL_TEXTURE_2D, m_texId);

    // Change these to GL_LINEAR for super- or sub-sampling
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

    // GL_CLAMP_TO_EDGE for linear filtering, not relevant for nearest.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture( GL_TEXTURE_2D, 0);

    /// =============================================================

    m_context = optix::Context::create();
    m_context->setRayTypeCount( 3 );
    m_context->setEntryPointCount( 1 );
    m_context->setStackSize( 1800 );

    m_context["max_depth"]->setInt( 5 );
//    m_context["radiance_ray_type"]->setUint( 0u );
//    m_context["shadow_ray_type"]->setUint( 1u );
    m_context["scene_epsilon"]->setFloat( 1.e-4f );
    m_context["color_t"]->setFloat( 0.0f );
    m_context["shadowsActive"]->setUint( 0u );
    m_context["global_t"]->setFloat( 0u );

    m_context["scene_epsilon"]->setFloat( 1.e-3f );
    m_context["pathtrace_ray_type"]->setUint(0u);
    m_context["pathtrace_shadow_ray_type"]->setUint(1u);
    m_context["pathtrace_bsdf_shadow_ray_type"]->setUint(2u);
    m_context["rr_begin_depth"]->setUint(m_rr_begin_depth);

    updateBufferSize(_width, _height);

//    camera_data = InitialCameraData( optix::make_float3( 3.0f, 2.0f, -3.0f ), // eye
//                                     optix::make_float3( 0.0f, 0.3f,  0.0f ), // lookat
//                                     optix::make_float3( 0.0f, 1.0f,  0.0f ), // up
//                                     60.0f );                          // vfov

    // Declare camera variables.  The values do not matter, they will be overwritten in trace.
    m_context["eye"]->setFloat( optix::make_float3( 0.0f, 0.0f, 0.0f ) );
    m_context["U"]->setFloat( optix::make_float3( 0.0f, 0.0f, 0.0f ) );
    m_context["V"]->setFloat( optix::make_float3( 0.0f, 0.0f, 0.0f ) );
    m_context["W"]->setFloat( optix::make_float3( 0.0f, 0.0f, 0.0f ) );

//    setCamera( optix::make_float3( 3.0f, 2.0f, -3.0f ), // eye
//               optix::make_float3( 0.0f, 0.3f,  0.0f ), // lookat
//               60.f // fov
//               );

    //sprintf( path_to_ptx, "%s/%s", "ptx", "draw.cu.ptx" );
//    std::string ptx_path_str(  );
//    optix::Program ray_gen_program = m_context->createProgramFromPTXFile( "ptx/pinhole_camera.cu.ptx", "pinhole_camera" );
    optix::Program ray_gen_program = m_context->createProgramFromPTXFile( "ptx/menger.cu.ptx", "pathtrace_camera" );
    optix::Program exception_program = m_context->createProgramFromPTXFile( "ptx/menger.cu.ptx", "exception" );
    m_context->setRayGenerationProgram( 0, ray_gen_program );
    m_context->setExceptionProgram( 0, exception_program );

    m_context["bad_color"]->setFloat( 1.0f, 1.0f, 0.0f );

    // Miss program
    //m_context->setMissProgram( 0, m_context->createProgramFromPTXFile( "ptx/constantbg.cu.ptx", "miss" ) );
    m_context["bg_color"]->setFloat( optix::make_float3(108.0f/255.0f, 166.0f/255.0f, 205.0f/255.0f) * 0.5f );

    m_context->setMissProgram( 0, m_context->createProgramFromPTXFile( "ptx/raymarch.cu.ptx", "envmap_miss" ) );

    const optix::float3 default_color = optix::make_float3(1.0f, 1.0f, 1.0f);
//    m_context["envmap"]->setTextureSampler( loadTexture( m_context, "/home/i7245143/src/optix/SDK/tutorial/data/CedarCity.hdr", default_color) );
//    m_context["envmap"]->setTextureSampler( loadTexture( m_context, "/home/tom/src/Fragmentarium/Fragmentarium-Source/Examples/Include/Ditch-River_2k.hdr", default_color) );
    m_context["envmap"]->setTextureSampler( loadTexture( m_context, "/home/tom/Downloads/Milkyway/Milkyway_small.hdr", default_color) );


    m_rr_begin_depth = 1u;
    m_sqrt_num_samples = 1u;
    m_camera_changed = true;


    // Setup path tracer
    m_context["sqrt_num_samples"]->setUint( m_sqrt_num_samples );
    m_context["frame_number"]->setUint(1);

    // Index of sampling_stategy (BSDF, light, MIS)
    m_sampling_strategy = 2;
    m_context["sampling_stategy"]->setInt(m_sampling_strategy);


    // Setup lights
    m_context["ambient_light_color"]->setFloat(0.1f,0.1f,0.3f);
    BasicLight lights[] = {
      { { 0.0f, 8.0f, -5.0f }, { .8f, .8f, .6f }, 1 },
    };

    optix::Buffer light_buffer = m_context->createBuffer(RT_BUFFER_INPUT);
    light_buffer->setFormat(RT_FORMAT_USER);
    light_buffer->setElementSize(sizeof(BasicLight));
    light_buffer->setSize( sizeof(lights)/sizeof(lights[0]) );
    memcpy(light_buffer->map(), lights, sizeof(lights));
    light_buffer->unmap();

    m_context["lights"]->set(light_buffer);


    float3 test_data[] = {
        { 1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, 1.0f }
    };

    optix::Buffer test_buffer = m_context->createBuffer(RT_BUFFER_INPUT, RT_FORMAT_FLOAT3, sizeof(test_data)/sizeof(test_data[0]) );
    memcpy( test_buffer->map(), test_data, sizeof(test_data) );
    test_buffer->unmap();

    m_context["test"]->set(test_buffer);

    // Create scene geom
    createGeometry();

//    m_camera = new MyPinholeCamera( camera_data.eye,
//                                  camera_data.lookat,
//                                  camera_data.up,
//                                  -1.0f, // hfov is ignored when using keep vertical
//                                  camera_data.vfov,
//                                  MyPinholeCamera::KeepVertical );

//    setCamera( camera_data.eye,
//               camera_data.lookat,
//               60.0f,
//               _width, _height);



    //ray_gen_program["draw_color"]->setFloat( optix::make_float3(0.462f, 0.725f, 0.0f) );

    m_context->validate();
    m_context->compile();
}

void OptixScene::setCamera(optix::float3 _eye, /*optix::float3 _lookat, */float _fov, int _width, int _height)
{
//    m_camera->setParameters( _eye,
//                             optix::make_float3(0,0,0),
//                             camera_data.up,
//                             _fov, // hfov is ignored when using keep vertical
//                             _fov,
//                             MyPinholeCamera::KeepHorizontal );

//    optix::float3 eye, U, V, W;
//    m_camera->setAspectRatio( static_cast<float>(_width)/static_cast<float>(_height) );

    m_context["eye"]->setFloat( _eye );
    m_context["U"]->setFloat( optix::make_float3(1, 0, 0) );
    m_context["V"]->setFloat( optix::make_float3(0, 1, 0) );
    m_context["W"]->setFloat( optix::make_float3(0, 0, 1) );

    m_camera_changed = true;

//    m_camera->getEyeUVW( eye, U, V, W );

//    m_context["eye"]->setFloat( eye );
//    m_context["U"]->setFloat( U );
//    m_context["V"]->setFloat( V );
//    m_context["W"]->setFloat( W );
}

void OptixScene::setVar(const std::string& _name, float _v)
{
    m_context[_name]->setFloat(_v);
}

void OptixScene::setVar(const std::string& _name, optix::float3 _v )
{
    m_context[_name]->setFloat( _v  );
}

void OptixScene::setVar(const std::string& _name, optix::Matrix4x4 _v )
{
    m_context[_name]->setMatrix4x4fv(false, _v.getData());
}


void OptixScene::updateBufferSize(unsigned int _width, unsigned int _height)
{
    static std::vector<std::string> bufferNames = { "output_buffer",
                                                    /*"output_buffer_nrm",
                                                    "output_buffer_depth",
                                                    "output_buffer_world" */};

    for( auto& bufferName : bufferNames )
    {

        if(m_context[bufferName]->getType() == RT_OBJECTTYPE_UNKNOWN )
        {
            m_context[bufferName]->set( createOutputBuffer(RT_FORMAT_FLOAT4, _width, _height) );
        }
        else
        {
            m_context[bufferName]->getBuffer()->setSize(_width, _height);

            m_context[bufferName]->getBuffer()->unregisterGLBuffer();
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, m_context[bufferName]->getBuffer()->getGLBOId());
            glBufferData(GL_PIXEL_UNPACK_BUFFER, m_context[bufferName]->getBuffer()->getElementSize() * _width * _height, 0, GL_STREAM_DRAW);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            m_context[bufferName]->getBuffer()->registerGLBuffer();
        }
    }
}


std::vector<std::string> FileToVector(const std::string& _filename)
{
    std::vector<std::string> result;
    std::ifstream file(_filename);

    if(!file)
    {
        qDebug("Can't open file %s", qPrintable(_filename.c_str()) );
        throw std::runtime_error("Can't open file");
    }

    std::string line;
    while (std::getline(file, line))
    {
        result.push_back(line);
    }

    return result;
}

std::vector<std::string> StringToVector(const std::string& _str)
{
    std::vector<std::string> result;
    {
        std::istringstream src_stream( _str );
        std::string line;
        while(std::getline(src_stream, line))
        {
            result.push_back(line);
        }
    }

    return result;
}

bool findString(std::vector<std::string>& _lines,
                const std::string& _searchString,
                std::vector<std::string>::iterator* o_result)
{
    for(auto line = _lines.begin(); line != _lines.end(); ++line)
    {
        if ( line->find(_searchString) != std::string::npos)
        {
            *o_result = line + 1;
            return true;
        }
    }

    return false;
}

bool findFunction(std::vector<std::string>& _lines,
                  const std::string& _funcName,
                  std::vector<std::string>::iterator* o_start,
                  std::vector<std::string>::iterator* o_end )
{
    bool isExtern = false;
    bool found = false;

    for(auto line = _lines.begin(); line != _lines.end(); ++line)
    {
        if ( line->find(".func") != std::string::npos)
        {
            if ( line->find(_funcName) != std::string::npos)
            {
                if ( line->find(".extern") != std::string::npos) { isExtern = true; }

                found = true;
                *o_start = line;
                break;
            }
        }
    }

    if(found)
    {
        for(auto line = *o_start; line != _lines.end(); ++line)
        {
            if( (isExtern && (line->find(";") != std::string::npos)) ||
               (!isExtern && (line->find("}") != std::string::npos)) )
            {
                        *o_end = line + 1;
                        return true;
            }
        }
    }

    return false;
}

#include <sstream>
#include <algorithm>

bool hookPtxFunction(  const std::string& _ptxPath,
                                      const std::string& _functionName,
                                      const std::string& _functionSource,
                                      std::string& _result)
{
    // Compile function source to ptx
    RuntimeCompiler program(_functionName, _functionSource);
    qDebug() << program.getResult();
    // Convert the runtime compiled ptx to a vector of strings
    std::vector<std::string> hit_ptx = StringToVector( program.getResult() );

    // Convert the optix ptx to a vector of strings
    std::vector<std::string> ptx = FileToVector(_ptxPath);


    /* Process ptx code to be patched into the optix ptx */

    // This particular function (seems to be related to the CUDA runtime API) always seems to be the last of the API stuff,
    // so we can (hopefully) safely ignore anything prior to it and only copy the functions defined after it.
    {
        std::vector<std::string>::iterator CUDAAPI_StartIterator;
        std::vector<std::string>::iterator CUDAAPI_EndIterator;
        if( !findFunction( hit_ptx, "cudaOccupancyMaxActiveBlocksPerMultiprocessorWithFlags", &CUDAAPI_StartIterator, &CUDAAPI_EndIterator) )
        {
            qWarning("Unable to find CUDA API function definitions, copying every function definition");

            // If no cuda runtime functions were found, remove the compiler info and copy over every function definition anyway
            if(!findString(hit_ptx, ".address_size 64", &CUDAAPI_EndIterator))
            {
                throw std::runtime_error("Can't find .address_size, ptx string must be invalid");
                return false;
            }
        }

        // Remove the unnecessary lines, leaving just the code we need for the patch function
        hit_ptx.erase(hit_ptx.begin(), CUDAAPI_EndIterator);
    }


    std::vector<std::string>::iterator ptxInsertionIterator;
    /* Process the optix ptx ready for patching, search for the function we're patching and delete it so the new code can be copied over */
    {
        std::vector<std::string>::iterator hookFunction_StartIterator;
        std::vector<std::string>::iterator hookFunction_EndIterator;
        if( !findFunction( ptx, _functionName, &hookFunction_StartIterator, &hookFunction_EndIterator) )
        {
            qWarning("Unable to find function to patch");
            return false;
        }

        // Remove the current function definition so we can replace it with the new one
        ptxInsertionIterator = ptx.erase(hookFunction_StartIterator, hookFunction_EndIterator);
    }

    // Replace with our actual function ptx
    ptx.insert(ptxInsertionIterator, hit_ptx.begin(), hit_ptx.end());

    // Join the list into a string
    std::string concatptx = boost::algorithm::join(ptx, "\n");

    // Return the string result
    _result = concatptx;

//    qDebug() << _result.c_str();

    return true;
}


#include "Base_SDFOP.h"
#include "Primitive/Sphere_SDFOP.h"
#include "DomainOp/Transform_SDFOP.h"
#include <glm/gtc/matrix_transform.hpp>

#include "path_tracer/path_tracer.h"

GeometryInstance createParallelogram( optix::Context* m_context,
                                      optix::Program* m_pgram_bounding_box,
                                      optix::Program* m_pgram_intersection,
                                      const float3& anchor,
                                      const float3& offset1,
                                      const float3& offset2)
{
  Geometry parallelogram = (*m_context)->createGeometry();
  parallelogram->setPrimitiveCount( 1u );
  parallelogram->setIntersectionProgram( *m_pgram_intersection );
  parallelogram->setBoundingBoxProgram( *m_pgram_bounding_box );

  float3 normal = normalize( cross( offset1, offset2 ) );
  float d = dot( normal, anchor );
  float4 plane = make_float4( normal, d );

  float3 v1 = offset1 / dot( offset1, offset1 );
  float3 v2 = offset2 / dot( offset2, offset2 );

  parallelogram["plane"]->setFloat( plane );
  parallelogram["anchor"]->setFloat( anchor );
  parallelogram["v1"]->setFloat( v1 );
  parallelogram["v2"]->setFloat( v2 );

  GeometryInstance gi = (*m_context)->createGeometryInstance();
  gi->setGeometry(parallelogram);
  return gi;
}

void setMaterial( GeometryInstance& gi,
                                   Material material,
                                   const std::string& color_name,
                                   const float3& color)
{
  gi->addMaterial(material);
  gi[color_name]->setFloat(color);
}

#define DEMO

void OptixScene::createGeometry(std::string _hit_src)
{
#ifndef DEMO
//    std::string sphere_hit_src =
//            "#include \"cutil_math.h\" \n"
//            "extern \"C\" {\n "
//            "__device__ float distancehit_hook("
//                    "float3 x, float3* test"
//                    ")\n"
//            "{"
//                "return length(x) - 1.0f;\n"
//            "}\n"
//            "}\n";

    std::string sphere_hit_src = R"(
#include "cutil_math.h"
extern "C" {
__device__ float distancehit_hook(float3 p, float3* test)
{
            float3 arse = test[0];

            return length(p) - 1.0f;
}
}
)";


    std::string hit_src = (_hit_src == "") ? sphere_hit_src : _hit_src;

    std::string ptx;
//    hookPtxFunction("ptx/raymarch.cu.ptx", "shade_hook", _hit_src, ptx);

//    qDebug() << mandelbulb_hit_src.c_str();
//    qDebug() << hit_src.c_str();

    if(!hookPtxFunction("ptx/raymarch.cu.ptx", "distancehit_hook", hit_src, ptx))
    {
        qWarning("Patching failed");
        return;
    }
#endif

    ///@todo Optix error checking
    optix::Geometry julia = m_context->createGeometry();
    julia->setPrimitiveCount( 1u );

#ifndef DEMO
    julia->setBoundingBoxProgram( m_context->createProgramFromPTXString( ptx, "bounds" ) );
    julia->setIntersectionProgram( m_context->createProgramFromPTXString( ptx, "intersect" ) );

    optix::Program julia_ch = m_context->createProgramFromPTXString( ptx, "radiance" );
    optix::Program julia_ah = m_context->createProgramFromPTXString( ptx, "shadow" );
#else
    julia->setBoundingBoxProgram( m_context->createProgramFromPTXFile( "ptx/menger.cu.ptx", "bounds" ) );
    julia->setIntersectionProgram( m_context->createProgramFromPTXFile( "ptx/menger.cu.ptx", "intersect" ) );

//    optix::Program julia_ch = m_context->createProgramFromPTXFile( "ptx/menger.cu.ptx", "julia_ch_radiance" );
//    optix::Program julia_ah = m_context->createProgramFromPTXFile( "ptx/menger.cu.ptx", "julia_ah_shadow" );
#endif

    // Julia material
//    optix::Material julia_matl = m_context->createMaterial();
//    julia_matl->setClosestHitProgram( 0, julia_ch );
//    julia_matl->setAnyHitProgram( 1, julia_ah );

    m_context["Ka"]->setFloat(0.5f,0.0f,0.0f);
    m_context["Kd"]->setFloat(.6f, 0.1f, 0.1f);
    m_context["Ks"]->setFloat(.6f, .2f, .1f);
    m_context["phong_exp"]->setFloat(32);
    m_context["reflectivity"]->setFloat(.4f, .4f, .4f);

    ParallelogramLight light;
    light.corner   = make_float3( 343.0f, 548.6f, 227.0f);
    light.v1       = make_float3( -130.0f, 0.0f, 0.0f);
    light.v2       = make_float3( 0.0f, 0.0f, 105.0f);
    light.normal   = normalize( cross(light.v1, light.v2) );
    light.emission = make_float3( 100.0f );

    Buffer light_buffer = m_context->createBuffer( RT_BUFFER_INPUT );
    light_buffer->setFormat( RT_FORMAT_USER );
    light_buffer->setElementSize( sizeof( ParallelogramLight ) );
    light_buffer->setSize( 1u );
    memcpy( light_buffer->map(), &light, sizeof( light ) );
    light_buffer->unmap();
    m_context["lights"]->setBuffer( light_buffer );

    std::vector<optix::GeometryInstance> gis;

    GeometryInstance gi = m_context->createGeometryInstance();
    gi->setGeometry(julia);

    std::string ptx_path = "/home/tom/src/optix/build/lib/ptx/path_tracer_generated_parallelogram.cu.ptx";
    auto m_pgram_bounding_box = m_context->createProgramFromPTXFile( ptx_path, "bounds" );
    auto m_pgram_intersection = m_context->createProgramFromPTXFile( ptx_path, "intersect" );


    const float3 white = make_float3( 0.8f, 0.8f, 0.8f );
    const float3 green = make_float3( 0.05f, 0.8f, 0.05f );
    const float3 red   = make_float3( 0.8f, 0.05f, 0.05f );
    const float3 light_em = make_float3( 15.0f, 15.0f, 5.0f );

    Material diffuse = m_context->createMaterial();
    Program diffuse_ch = m_context->createProgramFromPTXFile( "ptx/menger.cu.ptx", "diffuse" );
    Program diffuse_ah = m_context->createProgramFromPTXFile( "ptx/menger.cu.ptx", "shadow" );
    diffuse->setClosestHitProgram( 0, diffuse_ch );
    diffuse->setAnyHitProgram( 1, diffuse_ah );

//    m_geometrygroup = m_context->createGeometryGroup();
//    m_geometrygroup->setChildCount( 1 );
//    m_geometrygroup->setChild( (int)0, m_context->createGeometryInstance( julia,  &julia_matl, &julia_matl+1 ));
//    m_geometrygroup->setAcceleration( m_context->createAcceleration("NoAccel","NoAccel") );


    Material diffuse_light = m_context->createMaterial();
    Program diffuse_em = m_context->createProgramFromPTXFile( "ptx/menger.cu.ptx", "diffuseEmitter" );
    diffuse_light->setClosestHitProgram( 0, diffuse_em );

    gis.push_back( gi );
    setMaterial(gis.back(), diffuse, "diffuse_color", white);

    // Floor
//    gis.push_back( createParallelogram( &m_context,
//                                        &m_pgram_bounding_box,
//                                        &m_pgram_intersection,
//                                        make_float3( 0.0f, 0.0f, 0.0f ),
//                                        make_float3( 0.0f, 0.0f, 559.2f ),
//                                        make_float3( 556.0f, 0.0f, 0.0f ) ) );
//    setMaterial(gis.back(), diffuse, "diffuse_color", white);
//    gis.push_back( gi );
//    setMaterial(gis.back(), diffuse, "diffuse_color", red);


    // Create shadow group (no light)
    GeometryGroup shadow_group = m_context->createGeometryGroup(gis.begin(), gis.end());
    shadow_group->setAcceleration( m_context->createAcceleration("NoAccel","NoAccel") );
    m_context["top_shadower"]->set( shadow_group );

    // Light
    gis.push_back( createParallelogram( &m_context,
                                        &m_pgram_bounding_box,
                                        &m_pgram_intersection,
                                        make_float3( -2500, 2000.0, -2500),
                                        make_float3( 5000.0f, 0.0f, 0.0f),
                                        make_float3( 0.0f, 0.0f, 5000.0f) ) );
    setMaterial(gis.back(), diffuse_light, "emission_color", light_em);

    GeometryGroup m_geometrygroup = m_context->createGeometryGroup();
    m_geometrygroup->setChildCount( static_cast<unsigned int>(gis.size()) );
    for(size_t i = 0; i < gis.size(); ++i) {
      m_geometrygroup->setChild( (int)i, gis[i] );
    }
    m_geometrygroup->setAcceleration( m_context->createAcceleration("NoAccel","NoAccel") );


    // Top level group
    Group topgroup = m_context->createGroup();
    topgroup->setChildCount( 1 );
    topgroup->setChild( 0, m_geometrygroup );
    //topgroup->setChild( 1, floor_gg );
    topgroup->setAcceleration( m_context->createAcceleration("Bvh","Bvh") );


    m_context["top_object"]->set( m_geometrygroup );
    m_context["top_shadower"]->set( m_geometrygroup );





    float  m_alpha;
    float  m_delta;
    float m_DEL;
    unsigned int m_max_iterations;

    m_alpha = 0.003f;
    m_delta = 0.00001f;
    m_DEL = 0.0001f;
    m_max_iterations = 32;

    m_context[ "c4" ]->setFloat( optix::make_float4( -0.5f, 0.1f, 0.2f, 0.3f) );
    m_context[ "alpha" ]->setFloat( m_alpha );
    m_context[ "delta" ]->setFloat( m_delta );
    m_context[ "max_iterations" ]->setUint( m_max_iterations );
    m_context[ "DEL" ]->setFloat( m_DEL );
    m_context[ "particle" ]->setFloat( 0.5f, 0.5f, 0.4f );
    m_context[ "global_t" ]->setFloat( m_time );
}

OptixScene::~OptixScene()
{

}

void OptixScene::drawToBuffer()
{
    if( m_camera_changed ) {
        m_camera_changed = false;
        m_frame = 1;
    }

    m_context["frame_number"]->setUint( m_frame++ );

    RTsize buffer_width, buffer_height;
    m_context["output_buffer"]->getBuffer()->getSize( buffer_width, buffer_height );
    m_context->launch( 0,
                       static_cast<unsigned int>(buffer_width),
                       static_cast<unsigned int>(buffer_height)
                       );


    /// ==================  Copy to texture =======================

    optix::Buffer buffer = m_context["output_buffer"]->getBuffer();
    RTformat buffer_format = buffer->getFormat();



    // Debug dump
//    {
//        const unsigned int totalPixels = 4 * static_cast<unsigned int>(buffer_width) * static_cast<unsigned int>(buffer_height);

//        float* h_ptrDiffuse = new float[totalPixels];
//        CUdeviceptr d_ptrDiffuse = buffer->getDevicePointer( 0 );
//        cudaMemcpy( (void*)h_ptrDiffuse,   (void*)d_ptrDiffuse,    sizeof(float) * totalPixels, cudaMemcpyDeviceToHost );

//        std::vector<Image> passes;

//        passes.push_back( Image( h_ptrDiffuse, static_cast<unsigned int>(buffer_width), static_cast<unsigned int>(buffer_height)) );

//        writeRGBA2("test.exr", passes );
//    }


    vboId = buffer->getGLBOId();

    if (vboId)
    {
        glBindTexture( GL_TEXTURE_2D, m_texId );

        // send pbo to texture
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, vboId);

        RTsize elementSize = buffer->getElementSize();
        if      ((elementSize % 8) == 0) glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
        else if ((elementSize % 4) == 0) glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        else if ((elementSize % 2) == 0) glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
        else                             glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        {
            if(buffer_format == RT_FORMAT_UNSIGNED_BYTE4) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, buffer_width, buffer_height, 0, GL_BGRA, GL_UNSIGNED_BYTE, 0);
            } else if(buffer_format == RT_FORMAT_FLOAT4) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F_ARB, buffer_width, buffer_height, 0, GL_RGBA, GL_FLOAT, 0);
            } else if(buffer_format == RT_FORMAT_FLOAT3) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F_ARB, buffer_width, buffer_height, 0, GL_RGB, GL_FLOAT, 0);
            } else if(buffer_format == RT_FORMAT_FLOAT) {
                glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE32F_ARB, buffer_width, buffer_height, 0, GL_LUMINANCE, GL_FLOAT, 0);
            } else {
                assert(0 && "Unknown buffer format");
            }
        }

        glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );

        // Initialize offsets to pixel center sampling.

  //      float u = 0.5f/buffer_width;
  //      float v = 0.5f/buffer_height;
    }
    else
    {
        assert("Couldn't bind GL Buffer Object");
    }

    /// ===========================================================

  //  RT_CHECK_ERROR( sutilDisplayFilePPM( "/home/tom/src/OptixQt/out.ppm", buffer->get() ) );
}