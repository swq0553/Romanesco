#include "include/runtimecompiler.h"

#ifndef NVRTC_AVAILABLE
#include <stdlib.h>
#include <cstring>
#endif

#include "macrohelpers.h"

RuntimeCompiler::RuntimeCompiler(const std::string &_name, const std::string _source,
                                 std::vector<std::string> _includePaths,
                                 std::vector<std::string> _includeFiles)
    : m_result(nullptr)
{
    std::vector<const char*> opts;
    opts.push_back("--gpu-architecture=compute_35");
    opts.push_back("-rdc=true");
    opts.push_back("-I$HOME/src/Romanesco/QtTest/kernel");
    opts.push_back("-I/usr/local/cuda-7.0/targets/x86_64-linux/include");

#ifdef NVRTC_AVAILABLE
    NVRTC_SAFE_CALL( nvrtcCreateProgram(&m_prog, _source.c_str(), _name.c_str(), 0, NULL, NULL) );

    nvrtcResult compileResult = nvrtcCompileProgram(m_prog, opts.size(), opts.data());

    size_t logSize;
    NVRTC_SAFE_CALL( nvrtcGetProgramLogSize(m_prog, &logSize) );
    if (logSize > 1)
    {
        std::vector<char> log(logSize);
        NVRTC_SAFE_CALL(nvrtcGetProgramLog(m_prog, &log[0]));
        qDebug() << &log[0];
    }

    if (compileResult != NVRTC_SUCCESS)
    {
        exit(1);
    }

    // Obtain PTX from the program.
    size_t ptxSize;
    NVRTC_SAFE_CALL(nvrtcGetPTXSize(m_prog, &ptxSize));

    m_result = new char[ptxSize];
    NVRTC_SAFE_CALL(nvrtcGetPTX(m_prog, m_result));
#else
    const std::string nvccbin = MACROTOSTRING(CUDA_EXE);
    const std::string nvccflags= "--compiler-options -fno-strict-aliasing -use_fast_math --ptxas-options=-v -ptx";
    const std::string nvcccall = nvccbin + " -m64 -gencode arch=compute_50,code=sm_50 " + nvccflags;

    system( nvcccall.c_str() );

    m_result = new char[strlen(_source.c_str())];
    strcpy(m_result, _source.c_str());
#endif
}

RuntimeCompiler::~RuntimeCompiler()
{
    delete m_result;

#ifdef NVRTC_AVAILABLE
    // Destroy the program.
    NVRTC_SAFE_CALL(nvrtcDestroyProgram(&m_prog));
#endif
}
