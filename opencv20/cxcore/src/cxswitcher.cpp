/*M///////////////////////////////////////////////////////////////////////////////////////
//
//  IMPORTANT: READ BEFORE DOWNLOADING, COPYING, INSTALLING OR USING.
//
//  By downloading, copying, installing or using the software you agree to this license.
//  If you do not agree to this license, do not download, install,
//  copy or use the software.
//
//
//                        Intel License Agreement
//                For Open Source Computer Vision Library
//
// Copyright (C) 2000, Intel Corporation, all rights reserved.
// Third party copyrights are property of their respective owners.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
//   * Redistribution's of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//
//   * Redistribution's in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//
//   * The name of Intel Corporation may not be used to endorse or promote products
//     derived from this software without specific prior written permission.
//
// This software is provided by the copyright holders and contributors "as is" and
// any express or implied warranties, including, but not limited to, the implied
// warranties of merchantability and fitness for a particular purpose are disclaimed.
// In no event shall the Intel Corporation or contributors be liable for any direct,
// indirect, incidental, special, exemplary, or consequential damages
// (including, but not limited to, procurement of substitute goods or services;
// loss of use, data, or profits; or business interruption) however caused
// and on any theory of liability, whether in contract, strict liability,
// or tort (including negligence or otherwise) arising in any way out of
// the use of this software, even if advised of the possibility of such damage.
//
//M*/


/****************************************************************************************/
/*                         Dynamic detection and loading of IPP modules                 */
/****************************************************************************************/

#include "_cxcore.h"

#if defined _MSC_VER && _MSC_VER >= 1200
#pragma warning( disable: 4115 )        /* type definition in () */
#endif

#if defined _MSC_VER && defined WIN64 && !defined EM64T
#pragma optimize( "", off )
#endif

#if defined WIN32 || defined WIN64
#include <windows.h>
#else
//#include <dlfcn.h>
//#include <sys/time.h>
#include <time.h>
#endif

#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define CV_PROC_GENERIC             0
#define CV_PROC_SHIFT               10
#define CV_PROC_ARCH_MASK           ((1 << CV_PROC_SHIFT) - 1)
#define CV_PROC_IA32_GENERIC        1
#define CV_PROC_IA32_WITH_MMX       (CV_PROC_IA32_GENERIC|(2 << CV_PROC_SHIFT))
#define CV_PROC_IA32_WITH_SSE       (CV_PROC_IA32_GENERIC|(3 << CV_PROC_SHIFT))
#define CV_PROC_IA32_WITH_SSE2      (CV_PROC_IA32_GENERIC|(4 << CV_PROC_SHIFT))
#define CV_PROC_IA64                2
#define CV_PROC_EM64T               3
#define CV_GET_PROC_ARCH(model)     ((model) & CV_PROC_ARCH_MASK)

typedef struct CvProcessorInfo
{
    int model;
    int count;
    double frequency; // clocks per microsecond
}
CvProcessorInfo;

#undef MASM_INLINE_ASSEMBLY

#if defined WIN32 && !defined  WIN64

#if defined _MSC_VER
#define MASM_INLINE_ASSEMBLY 1
#elif defined __BORLANDC__

#if __BORLANDC__ >= 0x560
#define MASM_INLINE_ASSEMBLY 1
#endif

#endif

#endif

/*
   determine processor type
*/
static void
icvInitProcessorInfo( CvProcessorInfo* cpu_info )
{
    memset( cpu_info, 0, sizeof(*cpu_info) );
    cpu_info->model = CV_PROC_GENERIC;

#if defined WIN32 || defined WIN64

#else
    cpu_info->frequency = 1;

#ifdef __x86_64__
    cpu_info->model = CV_PROC_EM64T;
#elif defined __ia64__
    cpu_info->model = CV_PROC_IA64;
#elif !defined __i386__
    cpu_info->model = CV_PROC_GENERIC;
#else

#endif

#endif
}


CV_INLINE const CvProcessorInfo*
icvGetProcessorInfo()
{
    static CvProcessorInfo cpu_info;
    static int init_cpu_info = 0;
    if( !init_cpu_info )
    {
        icvInitProcessorInfo( &cpu_info );
        init_cpu_info = 1;
    }
    return &cpu_info;
}


/****************************************************************************************/
/*                               Make functions descriptions                            */
/****************************************************************************************/

#undef IPCVAPI_EX
#define IPCVAPI_EX(type,func_name,names,modules,arg) \
    { (void**)&func_name##_p, (void*)(size_t)-1, names, modules, 0 },

#undef IPCVAPI_C_EX
#define IPCVAPI_C_EX(type,func_name,names,modules,arg) \
    { (void**)&func_name##_p, (void*)(size_t)-1, names, modules, 0 },

static CvPluginFuncInfo cxcore_ipp_tab[] =
{
#undef _CXCORE_IPP_H_
#include "_cxipp.h"
#undef _CXCORE_IPP_H_
    {0, 0, 0, 0, 0}
};


/*
   determine processor type, load appropriate dll and
   initialize all function pointers
*/
#if defined WIN32 || defined WIN64
//#define DLL_PREFIX ""
//#define DLL_SUFFIX ".dll"
#else
//#define DLL_PREFIX "lib"
//#define DLL_SUFFIX ".so"
//#define LoadLibrary(name) dlopen(name, RTLD_LAZY)
//#define FreeLibrary(name) dlclose(name)
//#define GetProcAddress dlsym
typedef void* HMODULE;
#endif

#if 0 /*def _DEBUG*/
//#define DLL_DEBUG_FLAG "d"
#else
//#define DLL_DEBUG_FLAG ""
#endif

#define VERBOSE_LOADING 0

#if VERBOSE_LOADING
#define ICV_PRINTF(args)  printf args; fflush(stdout)
#else
#define ICV_PRINTF(args)
#endif

typedef struct CvPluginInfo
{
    const char* basename;
    HMODULE handle;
    char name[100];
}
CvPluginInfo;

static CvPluginInfo plugins[CV_PLUGIN_MAX];
static CvModuleInfo cxcore_info = { 0, "cxcore", CV_VERSION, cxcore_ipp_tab };

CvModuleInfo *CvModule::first = 0, *CvModule::last = 0;

CvModule::CvModule( CvModuleInfo* _info )
{
    cvRegisterModule( _info );
    info = last;
}

CvModule::~CvModule()
{
    if( info )
    {
        CvModuleInfo* p = first;
        for( ; p != 0 && p->next != info; p = p->next )
            ;
        if( p )
            p->next = info->next;
        if( first == info )
            first = info->next;
        if( last == info )
            last = p;
        cvFree( &info );
        info = 0;
    }
}

static int
icvUpdatePluginFuncTab( CvPluginFuncInfo* func_tab )
{
    int i, loaded_functions = 0;

    return loaded_functions;
}


CV_IMPL int
cvRegisterModule( const CvModuleInfo* module )
{
    CvModuleInfo* module_copy = 0;

    return module_copy ? 0 : -1;
}


CV_IMPL int
cvUseOptimized( int load_flag )
{
    int i, loaded_modules = 0, loaded_functions = 0;

    return loaded_functions;
}

CvModule cxcore_module( &cxcore_info );

CV_IMPL void
cvGetModuleInfo( const char* name, const char **version, const char **plugin_list )
{

    CV_FUNCNAME( "cvGetLibraryInfo" );
    if( version )
        *version = 0;

    if( plugin_list )
        *plugin_list = 0;
}


typedef int64 (CV_CDECL * rdtsc_func)(void);

/* helper functions for RNG initialization and accurate time measurement */
CV_IMPL  int64  cvGetTickCount( void )
{
    const CvProcessorInfo* cpu_info = icvGetProcessorInfo();

    if( CV_GET_PROC_ARCH(cpu_info->model) == CV_PROC_IA32_GENERIC )
    {
#ifdef MASM_INLINE_ASSEMBLY
    #ifdef __BORLANDC__
        __asm db 0fh
        __asm db 31h
    #else
        __asm _emit 0x0f;
        __asm _emit 0x31;
    #endif
#elif (defined __GNUC__ || defined CV_ICC) && defined __i386__
        int64 t;
        asm volatile (".byte 0xf; .byte 0x31" /* "rdtsc" */ : "=A" (t));
        return t;
#else
        static const char code[] = "\x0f\x31\xc3";
        rdtsc_func func = (rdtsc_func)(void*)code;
        return func();
#endif
    }
    else
    {
#if defined WIN32 || defined WIN64
#else
/*zeng
        struct timeval tv;
        struct timezone tz;
        gettimeofday( &tv, &tz );
        return (int64)tv.tv_sec*1000000 + tv.tv_usec;
*/
    return clock()/(CLOCKS_PER_SEC/1000000);

#endif
    }
}

CV_IMPL  double  cvGetTickFrequency()
{
    return icvGetProcessorInfo()->frequency;
}


static int icvNumThreads = 0;
static int icvNumProcs = 0;

CV_IMPL int cvGetNumThreads(void)
{
    if( !icvNumProcs )
        cvSetNumThreads(0);
    return icvNumThreads;
}

CV_IMPL void cvSetNumThreads( int threads )
{
    if( !icvNumProcs )
    {
#ifdef _OPENMP
        icvNumProcs = omp_get_num_procs();
        icvNumProcs = MIN( icvNumProcs, CV_MAX_THREADS );
#else
        icvNumProcs = 1;
#endif
    }

    if( threads <= 0 )
        threads = icvNumProcs;
    else
        threads = MIN( threads, icvNumProcs );

    icvNumThreads = threads;
}


CV_IMPL int cvGetThreadNum(void)
{
#ifdef _OPENMP
    return omp_get_thread_num();
#else
    return 0;
#endif
}


/* End of file. */
