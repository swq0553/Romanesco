#include "OptixSceneAdaptive.h"

OptixSceneAdaptive::OptixSceneAdaptive(unsigned int _width, unsigned int _height, QOpenGLFunctions_4_3_Core* _gl)
    : OptixScene(_width, _height, _gl)
{
    m_cameraType = CameraType::AdaptivePinhole;

    optix::Program ray_gen_program = m_context->createProgramFromPTXFile( "ptx/pinhole_camera.cu.ptx", "pinhole_camera" );
    optix::Program exception_program = m_context->createProgramFromPTXFile( "ptx/pinhole_camera.cu.ptx", "exception" );
    m_context->setRayGenerationProgram( (unsigned int)CameraType::AdaptivePinhole, ray_gen_program );
    m_context->setExceptionProgram(     (unsigned int)CameraType::AdaptivePinhole, exception_program );
}

OptixSceneAdaptive::~OptixSceneAdaptive()
{

}
