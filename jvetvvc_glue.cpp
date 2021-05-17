/** \file     jvetvvc.cpp
    \brief    Encoder application main
*/

#include <time.h>
#include <iostream>
#include <unistd.h>
#include <chrono>
#include <ctime>

#include "EncoderLib/EncLibCommon.h"
#include "EncApp.h"
#include "Utilities/program_options_lite.h"

extern "C" {
#include "bpgenc.h"
}
#ifdef _WIN32
#include <windows.h>
#endif



//! \ingroup EncoderApp
//! \{

static const uint32_t settingNameWidth = 66;
static const uint32_t settingHelpWidth = 84;
static const uint32_t settingValueWidth = 3;
// --------------------------------------------------------------------------------------------------------------------- //

//macro value printing function

#define PRINT_CONSTANT(NAME, NAME_WIDTH, VALUE_WIDTH) std::cout << std::setw(NAME_WIDTH) << #NAME << " = " << std::setw(VALUE_WIDTH) << NAME << std::endl;

static void printMacroSettings()
{
  if( g_verbosity >= DETAILS )
  {
    std::cout << "Non-environment-variable-controlled macros set as follows: \n" << std::endl;

    //------------------------------------------------

    //setting macros

    PRINT_CONSTANT( RExt__DECODER_DEBUG_BIT_STATISTICS,                         settingNameWidth, settingValueWidth );
    PRINT_CONSTANT( RExt__HIGH_BIT_DEPTH_SUPPORT,                               settingNameWidth, settingValueWidth );
    PRINT_CONSTANT( RExt__HIGH_PRECISION_FORWARD_TRANSFORM,                     settingNameWidth, settingValueWidth );
    PRINT_CONSTANT( ME_ENABLE_ROUNDING_OF_MVS,                                  settingNameWidth, settingValueWidth );

    //------------------------------------------------

    std::cout << std::endl;
  }
}

struct HEVCEncoderContext {
    HEVCEncodeParams params;
    char infilename[1024];
    char outfilename[1024];
    FILE *yuv_file;
    int frame_count;
};

#define ARGV_MAX 256

static void add_opt(int *pargc, char **argv,
                    const char *str)
{
    int argc;
    argc = *pargc;
    if (argc >= ARGV_MAX)
        abort();
    argv[argc++] = strdup(str);
    *pargc = argc;
}

static HEVCEncoderContext *jvetvvc_open(const HEVCEncodeParams *params)
{
    HEVCEncoderContext *s;
    char buf[1024];
    static int tmp_idx = 1;

    s = (HEVCEncoderContext *)malloc(sizeof(HEVCEncoderContext));
    memset(s, 0, sizeof(*s));

    s->params = *params;
#ifdef _WIN32
    if (GetTempPath(sizeof(buf), buf) > sizeof(buf) - 1) {
        fprintf(stderr, "Temporary path too long\n");
        free(s);
        return NULL;
    }
#else
    strcpy(buf, "/tmp/");
#endif
    snprintf(s->infilename, sizeof(s->infilename), "%sout%d-%d.yuv", buf, getpid(), tmp_idx);
    snprintf(s->outfilename, sizeof(s->outfilename), "%sout%d-%d.bin", buf, getpid(), tmp_idx);
    tmp_idx++;

    s->yuv_file = fopen(s->infilename, "wb");
    if (!s->yuv_file) {
        fprintf(stderr, "Could not open '%s'\n", s->infilename);
        free(s);
        return NULL;
    }
    return s;
}

static int jvetvvc_encode(HEVCEncoderContext *s, Image *img)
{
    save_yuv1(img, s->yuv_file);
    s->frame_count++;
    return 0;
}

/* return the encoded data in *pbuf and the size. Return < 0 if error */
static int jvetvvc_close(HEVCEncoderContext *s, uint8_t **pbuf)
{
    //    BPGImageFormatEnum *preferred_chroma_format;
    int argc;
    char *argv[ARGV_MAX + 1];
    char buf[1024];
    const char *str;
    FILE *f;
    uint8_t *out_buf;
    int out_buf_len, i;

    fclose(s->yuv_file);
    s->yuv_file = NULL;

    m_gcAnalyzeAll.clear();
    m_gcAnalyzeI.clear();
    m_gcAnalyzeP.clear();
    m_gcAnalyzeB.clear();
    m_gcAnalyzeAll_in.clear();

    argc = 0;
    add_opt(&argc, argv, "jvetvvc"); /* dummy executable name */

    fprintf( stdout, "\nLibrary encoder:  libBPG        0.9.8        c   [28 Apr 2018]\n"
                        "                  libJPEG-turbo 2.0.5 8bit   c   [18 Feb 2020]\n"
                        "                  libPNG        1.6.38       c   [24 May 2019]\n"
                        "                    zlib        1.2.11.1     c   [09 Jul 2019]\n"
                        "                  Compiled by Jamaika\n\n");
    fprintf( stdout, "\n" );
    fprintf( stdout, "VVCSoftware: VTM Encoder Version %s ", VTM_VERSION );
    fprintf( stdout, NVM_ONOS );
    fprintf( stdout, NVM_COMPILEDBY );
    fprintf( stdout, NVM_BITS );
/*#if ENABLE_SIMD_OPT
    std::string SIMD;
    df::program_options_lite::Options opts;
    opts.addOptions()
    ( "SIMD", SIMD, string( "" ), "" )
    ( "c", df::program_options_lite::parseConfigFile, "" );
    df::program_options_lite::SilentReporter err;
    df::program_options_lite::scanArgv( opts, argc, ( const char** ) argv, err );
    fprintf( stdout, "[SIMD=%s] ", read_x86_extension( SIMD ) );
#endif*/
#if USE_AVX512
    fprintf( stdout, "[SIMD=AVX512]" );
#elif USE_AVX2
    fprintf( stdout, "[SIMD=AVX2]" );
#elif USE_AVX
    fprintf( stdout, "[SIMD=AVX]" );
#elif USE_SSE42
    fprintf( stdout, "[SIMD=SSE4.2]" );
#elif USE_SSE41
    fprintf( stdout, "[SIMD=SSE4.1]" );
#else
    fprintf( stdout, "[SIMD=NONE]" );
#endif
#if ENABLE_TRACING
    fprintf( stdout, "[ENABLE_TRACING]" );
#endif
#if EXTENSION_360_VIDEO
    fprintf( stdout, "\nVVCSoftware: 360Lib Soft Version %s", VERSION_360Lib );
#endif
#if EXTENSION_HDRTOOLS
    fprintf( stdout, "\nVVCSoftware: HDRTools Version %s", VERSION );
#endif
#if ENABLE_SPLIT_PARALLELISM
#include "libgomp/gomp-constants.h"
#include "libgomp/pthread_win32/_ptw32.h"
    fprintf( stdout, "\nVVCSoftware: libgomp / pthreads 32bit  : %d.0 / %d.%d ", GOMP_VERSION, __PTW32_VERSION_MAJOR, __PTW32_VERSION_MINOR );
    fprintf( stdout, "[SPLIT_PARALLEL (%d jobs)]", PARL_SPLIT_MAX_NUM_JOBS );
    const char* waitPolicy = getenv( "OMP_WAIT_POLICY" );
    const char* maxThLim   = getenv( "OMP_THREAD_LIMIT" );
    fprintf( stdout, waitPolicy ? "[OMP: WAIT_POLICY=%s," : "[OMP: WAIT_POLICY=,", waitPolicy );
    fprintf( stdout, maxThLim   ? "THREAD_LIMIT=%s" : "THREAD_LIMIT=", maxThLim );
    fprintf( stdout, "]" );
#endif
    fprintf( stdout, "\n" );

    snprintf(buf, sizeof(buf),"--InputFile=%s", s->infilename);
    add_opt(&argc, argv, buf);

    /*int number_frames = gop_size - 1;
    if (gop_size >= 1000)
    {
        snprintf(buf, sizeof(buf), "--BitstreamFile=image%d.vvc", number_frames);
    }
    else if (gop_size >= 100)
    {
        snprintf(buf, sizeof(buf), "--BitstreamFile=image0%d.vvc", number_frames);
    }
    else if (gop_size >= 10)
    {
        snprintf(buf, sizeof(buf), "--BitstreamFile=image00%d.vvc", number_frames);
    }
    else
    {
        snprintf(buf, sizeof(buf), "--BitstreamFile=image000%d.vvc", number_frames);
    }
    add_opt(&argc, argv, buf);
    number_frames++;*/

    //snprintf(buf, sizeof(buf),"--BitstreamFile=%s", s->outfilename);
    //add_opt(&argc, argv, buf);

    snprintf(buf, sizeof(buf),"--SourceWidth=%d", s->params.width);
    add_opt(&argc, argv, buf);
    snprintf(buf, sizeof(buf),"--SourceHeight=%d", s->params.height);
    add_opt(&argc, argv, buf);
    snprintf(buf, sizeof(buf),"--InputBitDepth=%d", s->params.bit_depth);
    add_opt(&argc, argv, buf);
    snprintf(buf, sizeof(buf),"--MSBExtendedBitDepth=%d", s->params.bit_depth);
    add_opt(&argc, argv, buf);

    snprintf(buf, sizeof(buf),"--FrameRate=%d", 8 * s->params.frame_rate);
    add_opt(&argc, argv, buf);

    switch(s->params.chroma_format) {
    case BPG_FORMAT_GRAY:
        str = "400";
        break;
    case BPG_FORMAT_420:
        str = "420";
        break;
    case BPG_FORMAT_422:
        str = "422";
        break;
    case BPG_FORMAT_444:
        str = "444";
        break;
    default:
        abort();
    }

    if (s->params.chroma_format == BPG_FORMAT_GRAY)
    {
        add_opt(&argc, argv, "--InputChromaFormat=400");
        add_opt(&argc, argv, "--ChromaFormatIDC=400");
        add_opt(&argc, argv, "--VerCollocatedChroma=0");
    }
    if (s->params.chroma_format == BPG_FORMAT_420)
    {
        add_opt(&argc, argv, "--InputChromaFormat=420");
        add_opt(&argc, argv, "--ChromaFormatIDC=420");
        add_opt(&argc, argv, "--VerCollocatedChroma=0");
    }
    if (s->params.chroma_format == BPG_FORMAT_422)
    {
        add_opt(&argc, argv, "--InputChromaFormat=422");
        add_opt(&argc, argv, "--ChromaFormatIDC=422");
        add_opt(&argc, argv, "--VerCollocatedChroma=1");
    }
    if (s->params.chroma_format == BPG_FORMAT_444)
    {
        add_opt(&argc, argv, "--InputChromaFormat=444");
        add_opt(&argc, argv, "--ChromaFormatIDC=444");
        add_opt(&argc, argv, "--VerCollocatedChroma=1");
        add_opt(&argc, argv, "--BDPCM=1");
    }

    /*snprintf(buf, sizeof(buf),"--MaxBTLumaISlice=%d", s->params.bit_depth);
    add_opt(&argc, argv, buf);
    snprintf(buf, sizeof(buf),"--MaxBTNonISlice=%d", s->params.bit_depth);
    add_opt(&argc, argv, buf);
    snprintf(buf, sizeof(buf),"--MaxTTLumaISlice=%d", s->params.bit_depth);
    add_opt(&argc, argv, buf);
    snprintf(buf, sizeof(buf),"--MaxTTNonISlice=%d", s->params.bit_depth);
    add_opt(&argc, argv, buf);*/

    switch(s->params.color_space) {
    case BPG_CS_YCbCr:
        str = "ycbcr";
        break;
    case BPG_CS_RGB:
        str = "rgb";
        break;
    case BPG_CS_YCgCo:
        str = "ycgco";
        break;
    case BPG_CS_YCbCr_BT709:
        str = "ycbcr_bt709";
        break;
    case BPG_CS_YCbCr_BT2020:
        str = "ycbcr_bt2020";
        break;
    default:
        abort();
    }

    if (s->params.color_space == BPG_CS_YCbCr) {
      add_opt(&argc, argv, "--MatrixCoefficients=2");
      add_opt(&argc, argv, "--LumaLevelToDeltaQPMode=0");
      add_opt(&argc, argv, "--WCGPPSEnable=0");
      add_opt(&argc, argv, "--QpInValCb=17 27 32 44");
      add_opt(&argc, argv, "--QpOutValCb=17 29 34 41");
      add_opt(&argc, argv, "--ColorTransform=0");
      add_opt(&argc, argv, "--DualITree=1");
        add_opt(&argc, argv, "--LMCSEnable=0");
        if (s->params.limited_range == 1)
        {
          add_opt(&argc, argv, "--InputSampleRange=0");
          add_opt(&argc, argv, "--VideoFullRange=0");
          //add_opt(&argc, argv, "--isSDR=1");
        }
        else
        {
          add_opt(&argc, argv, "--InputSampleRange=1");
          add_opt(&argc, argv, "--VideoFullRange=1");
        }
    }
    if (s->params.color_space == BPG_CS_RGB) {
      add_opt(&argc, argv, "--MatrixCoefficients=0");
      add_opt(&argc, argv, "--LumaLevelToDeltaQPMode=0");
      add_opt(&argc, argv, "--WCGPPSEnable=0");
      add_opt(&argc, argv, "--QpInValCb=17 27 32 44");
      add_opt(&argc, argv, "--QpOutValCb=17 29 34 41");
      add_opt(&argc, argv, "--ColorTransform=1");
      add_opt(&argc, argv, "--DualITree=0");
      add_opt(&argc, argv, "--InputSampleRange=1");
      add_opt(&argc, argv, "--VideoFullRange=1");
      add_opt(&argc, argv, "--LMCSEnable=0");
      if (s->params.limited_range == 1)
      {
        EXIT( "Error: Color range isn't full for RGB(A).\n");
      }
    }
    if (s->params.color_space == BPG_CS_YCbCr_BT709) {
      add_opt(&argc, argv, "--MatrixCoefficients=1");
      add_opt(&argc, argv, "--LumaLevelToDeltaQPMode=0");
      add_opt(&argc, argv, "--WCGPPSEnable=0");
      add_opt(&argc, argv, "--QpInValCb=17 27 32 44");
      add_opt(&argc, argv, "--QpOutValCb=17 29 34 41");
      add_opt(&argc, argv, "--ColorTransform=0");
      add_opt(&argc, argv, "--DualITree=1");
        if (s->params.limited_range == 1)
        {
          add_opt(&argc, argv, "--InputSampleRange=0");
          add_opt(&argc, argv, "--VideoFullRange=0");
          add_opt(&argc, argv, "--LMCSEnable=1");
          add_opt(&argc, argv, "--LMCSSignalType=0");
          add_opt(&argc, argv, "--LMCSOffset=0");
        }
        else
        {
          add_opt(&argc, argv, "--InputSampleRange=1");
          add_opt(&argc, argv, "--VideoFullRange=1");
        }
    }
    if (s->params.color_space == BPG_CS_YCgCo) {
      add_opt(&argc, argv, "--MatrixCoefficients=8");
      add_opt(&argc, argv, "--LumaLevelToDeltaQPMode=0");
      add_opt(&argc, argv, "--WCGPPSEnable=0");
      add_opt(&argc, argv, "--QpInValCb=17 27 32 44");
      add_opt(&argc, argv, "--QpOutValCb=17 29 34 41");
      add_opt(&argc, argv, "--ColorTransform=1");
      add_opt(&argc, argv, "--DualITree=0");
        add_opt(&argc, argv, "--InputSampleRange=1");
        add_opt(&argc, argv, "--VideoFullRange=1");
        add_opt(&argc, argv, "--LMCSEnable=0");
        if (s->params.limited_range == 1)
        {
          EXIT( "Error: Color range isn't full for YCgCo<->RGB.\n");
        }
    }
    if (s->params.color_space == BPG_CS_YCbCr_BT2020) {
      add_opt(&argc, argv, "--MatrixCoefficients=9");
      add_opt(&argc, argv, "--LumaLevelToDeltaQPMode=0");
      add_opt(&argc, argv, "--WCGPPSEnable=0");
      add_opt(&argc, argv, "--QpInValCb=9 23 33 42");
      add_opt(&argc, argv, "--QpOutValCb=9 24 33 37");
      add_opt(&argc, argv, "--ColorTransform=0");
      add_opt(&argc, argv, "--DualITree=1");
        add_opt(&argc, argv, "--InputSampleRange=1");
        add_opt(&argc, argv, "--VideoFullRange=0");
        add_opt(&argc, argv, "--LMCSEnable=1");
        add_opt(&argc, argv, "--LMCSSignalType=2");
        add_opt(&argc, argv, "--LMCSOffset=0");
        if (s->params.limited_range == 1)
        {
          EXIT( "Error: Color range isn't HDR for BT2020.\n");
        }
    }

    //snprintf(buf, sizeof(buf),"--ChromaFormatIDC=%s", str);
    //add_opt(&argc, argv, buf);

    snprintf(buf, sizeof(buf),"--QP=%d", s->params.qp);
    add_opt(&argc, argv, buf);

    snprintf(buf, sizeof(buf),"--decodedpicturehash=%d",
             s->params.sei_decoded_picture_hash);
    add_opt(&argc, argv, buf);
    add_opt(&argc, argv, "--MaxLayers=1");
    add_opt(&argc, argv, "--CbQpOffset=0");
    add_opt(&argc, argv, "--CrQpOffset=0");
    add_opt(&argc, argv, "--TemporalSubsampleRatio=8");
    add_opt(&argc, argv, "--SameCQPTablesForAllChroma=1");
    add_opt(&argc, argv, "--ReWriteParamSets=1");


    if (!s->params.verbose)
      add_opt(&argc, argv, "--Verbosity=6");

    /* single frame */
    snprintf(buf, sizeof(buf),"--FramesToBeEncoded=%d", s->frame_count);
    add_opt(&argc, argv, buf);

    /* no padding necessary (it is done before) */
    add_opt(&argc, argv, "--ConformanceWindowMode=1");

    /* dummy frame rate */
    //add_opt(&argc, argv, "--FrameRate=25.000");



    /* general config */
//    add_opt(&argc, argv, "--LargeCTU=1");
    add_opt(&argc, argv, "--Log2MaxTbSize=5");
    add_opt(&argc, argv, "--CTUSize=32");
    add_opt(&argc, argv, "--MaxCUWidth=16");
    add_opt(&argc, argv, "--MaxCUHeight=16");
    add_opt(&argc, argv, "--MaxBTLumaISlice=32");
    add_opt(&argc, argv, "--MaxBTChromaISlice=32");
    add_opt(&argc, argv, "--MaxBTNonISlice=32");
    add_opt(&argc, argv, "--MaxTTLumaISlice=32");
    add_opt(&argc, argv, "--MaxTTChromaISlice=32");
    add_opt(&argc, argv, "--MaxTTNonISlice=32");
    //add_opt(&argc, argv, "--MaxPartitionDepth=4");
    add_opt(&argc, argv, "--LCTUFast=1");

    //snprintf(buf, sizeof(buf), "--InputSampleRange=1-%d", s->params.limited_range);
    add_opt(&argc, argv, "--InputColorPrimaries=-1");


    /*if (s->params.compress_level == 9) {
        add_opt(&argc, argv, "--QuadtreeTUMaxDepthIntra=4");
        add_opt(&argc, argv, "--QuadtreeTUMaxDepthInter=4");
    } else {
        add_opt(&argc, argv, "--QuadtreeTUMaxDepthIntra=3");
        add_opt(&argc, argv, "--QuadtreeTUMaxDepthInter=3");
    }*/

    //add_opt(&argc, argv, "--QuadtreeTULog2MinSize=2");

    //add_opt(&argc, argv, "--Profile=auto");
    add_opt(&argc, argv, "--DecodingRefreshType=1");

    add_opt(&argc, argv, "--TemporalFilter=0");
    //add_opt(&argc, argv, "--TemporalFilterFutureReference=1");
    //add_opt(&argc, argv, "--TemporalFilterStrengthFrame8=0.95");
    //add_opt(&argc, argv, "--TemporalFilterStrengthFrame16=1.5");

    //snprintf(buf, sizeof(buf),"--SEIDecodedPictureHash=%d",
    //         s->params.sei_decoded_picture_hash);
    add_opt(&argc, argv, buf);

    /* Note: Format Range extension */

        if (s->params.lossless) {
          add_opt(&argc, argv, "--CostMode=lossless");
          //add_opt(&argc, argv, "--BDPCM=1");
          add_opt(&argc, argv, "--QP=0");
          add_opt(&argc, argv, "--ChromaTS=1");
          add_opt(&argc, argv, "--DepQuant=0");
          add_opt(&argc, argv, "--RDOQ=0");
          add_opt(&argc, argv, "--RDOQTS=0");
          add_opt(&argc, argv, "--SBT=0");
          add_opt(&argc, argv, "--ISP=0");
          add_opt(&argc, argv, "--MTS=0");
          add_opt(&argc, argv, "--LFNST=0");
          add_opt(&argc, argv, "--JointCbCr=0");
          add_opt(&argc, argv, "--DeblockingFilterDisable=1");
          add_opt(&argc, argv, "--SAO=0");
          add_opt(&argc, argv, "--ALF=0");
          add_opt(&argc, argv, "--CCALF=0");
          add_opt(&argc, argv, "--DMVR=0");
          add_opt(&argc, argv, "--BIO=0");
          add_opt(&argc, argv, "--PROF=0");
          add_opt(&argc, argv, "--InternalBitDepth=0");
          add_opt(&argc, argv, "--TSRCdisableLL=1");

          if (s->params.color_space == BPG_CS_RGB) {
            add_opt(&argc, argv, "--IBC=1");
            add_opt(&argc, argv, "--HashME=1");
            add_opt(&argc, argv, "--PLT=1");
          }
          else
          {
            EXIT( "Error: Color space isn't RGB(A) for lossless.\n");
          }
          if (s->params.limited_range == 1)
          {
            EXIT( "Error: Color range isn't full for lossless.\n");
          }
        }
    /*else if (s->params.mixed_lossless_lossy)
    {
        add_opt(&argc, argv, "--CostMode=mixed_lossless_lossy");
        snprintf(buf, sizeof(buf),"--QP=%d", s->params.qp);
        add_opt(&argc, argv, buf);
    }*/
    else
    {
        add_opt(&argc, argv, "--CostMode=lossy");
        snprintf(buf, sizeof(buf),"--QP=%d", s->params.qp);
        add_opt(&argc, argv, buf);
        add_opt(&argc, argv, "--ChromaTS=1");
        add_opt(&argc, argv, "--DepQuant=1");
        add_opt(&argc, argv, "--RDOQ=1");
        add_opt(&argc, argv, "--RDOQTS=1");
        add_opt(&argc, argv, "--SBT=1");
        add_opt(&argc, argv, "--ISP=1");
        add_opt(&argc, argv, "--MTS=1");
        add_opt(&argc, argv, "--MTSIntraMaxCand=4");
        add_opt(&argc, argv, "--MTSInterMaxCand=4");
        add_opt(&argc, argv, "--LFNST=1");
        add_opt(&argc, argv, "--JointCbCr=1");
        add_opt(&argc, argv, "--DeblockingFilterDisable=0");
        add_opt(&argc, argv, "--SAO=1");
        add_opt(&argc, argv, "--ALF=1");
        add_opt(&argc, argv, "--PROF=1");
        add_opt(&argc, argv, "--DualITree=1");

        snprintf(buf, sizeof(buf),"--InternalBitDepth=%d", s->params.bit_depth);
        add_opt(&argc, argv, buf);
        snprintf(buf, sizeof(buf),"--MaxBitDepthConstraint=%d", s->params.bit_depth);
        add_opt(&argc, argv, buf);
    }

    if (s->params.bit_depth >= 12)
    {
        add_opt(&argc, argv, "--Profile=none");
        add_opt(&argc, argv, "--TSRCRicePresent=1");
        add_opt(&argc, argv, "--MMVD=0");
        add_opt(&argc, argv, "--SMVD=0");
        add_opt(&argc, argv, "--DMVR=0");
        add_opt(&argc, argv, "--Affine=0");
        add_opt(&argc, argv, "--ExtendedPrecision=1");
    }
    else
    {
        add_opt(&argc, argv, "--Profile=auto");
        add_opt(&argc, argv, "--Affine=1");
    }

    if (s->params.intra_only) {
        add_opt(&argc, argv, "--BitstreamFile=image.vvc");
        add_opt(&argc, argv, "--GOPSize=1");
        add_opt(&argc, argv, "--IntraPeriod=1");
        add_opt(&argc, argv, "--OnePictureOnlyConstraintFlag=1");
        add_opt(&argc, argv, "--GciPresentFlag=1");
        add_opt(&argc, argv, "--Level=15.5");
        add_opt(&argc, argv, "--Tier=high");

        add_opt(&argc, argv, "--SearchRange=64");
        //add_opt(&argc, argv, "--BCW=0");
        //add_opt(&argc, argv, "--BcwFast=0");
        //add_opt(&argc, argv, "--BIO=0");
        add_opt(&argc, argv, "--CIIP=0");
        //add_opt(&argc, argv, "--MHIntra=0");
        //add_opt(&argc, argv, "--Geo=0");
        add_opt(&argc, argv, "--AffineAmvr=0");
        add_opt(&argc, argv, "--LMCSUpdateCtrl=1");
        add_opt(&argc, argv, "--LMCSOffset=0");

        //add_opt(&argc, argv, "--PPSorSliceMode=0");
        add_opt(&argc, argv, "--ISPFast=1");
        add_opt(&argc, argv, "--FastMIP=1");
        add_opt(&argc, argv, "--FastLFNST=1");
        add_opt(&argc, argv, "--FastLocalDualTreeMode=0");

        add_opt(&argc, argv, "--AffineAmvrEncOpt=0");
        add_opt(&argc, argv, "--MmvdDisNum=8");
        //add_opt(&argc, argv, "--TemporalSubsampleRatio              : 8
    } else {
        add_opt(&argc, argv, "--BitstreamFile=animation.vvc");
        int gop_size = 1;
        snprintf(buf, sizeof(buf), "--GOPSize=%d", gop_size);
        add_opt(&argc, argv, buf);
        add_opt(&argc, argv, "--IntraPeriod=-1");
        add_opt(&argc, argv, "--SearchRange=64");
        //add_opt(&argc, argv, "--ASR=1");
        add_opt(&argc, argv, "--MinSearchWindow=96");
        add_opt(&argc, argv, "--BipredSearchRange=4");

        add_opt(&argc, argv, "--IntraQPOffset=-3");
        add_opt(&argc, argv, "--LambdaFromQpEnable=1");

        //add_opt(&argc, argv, "--BCW=1");
        //add_opt(&argc, argv, "--BcwFast=1");
        //add_opt(&argc, argv, "--BIO=1");
        add_opt(&argc, argv, "--CIIP=1");
        //add_opt(&argc, argv, "--MHIntra=1");
        //add_opt(&argc, argv, "--Geo=1");
        add_opt(&argc, argv, "--AffineAmvr=0");
        add_opt(&argc, argv, "--LMCSUpdateCtrl=2");
        add_opt(&argc, argv, "--LMCSOffset=1");
        //add_opt(&argc, argv, "--DMVR=1");
        //add_opt(&argc, argv, "--MMVD=1");
        add_opt(&argc, argv, "--AllowDisFracMMVD=1");
        //add_opt(&argc, argv, "--SMVD=1");
        //add_opt(&argc, argv, "--PROF=1");
        //add_opt(&argc, argv, "--PPSorSliceMode=1");
        add_opt(&argc, argv, "--ISPFast=0");
        add_opt(&argc, argv, "--FastMIP=0");
        add_opt(&argc, argv, "--FastLFNST=0");
        add_opt(&argc, argv, "--FastLocalDualTreeMode=2");

        add_opt(&argc, argv, "--AffineAmvrEncOpt=0");
        add_opt(&argc, argv, "--MmvdDisNum=6");

        for(i = 0; i < gop_size; i++) {
            snprintf(buf, sizeof(buf), "--Frame%d=P 1 5 -6.5 0.2590 0 0 1.0 0 0 0 4 4 1 5 9 13 0", i + 1);
            add_opt(&argc, argv, buf);
        }
    }

        add_opt(&argc, argv, "--MinQTLumaISlice=8");
        add_opt(&argc, argv, "--MinQTChromaISliceInChromaSamples=4");
        add_opt(&argc, argv, "--MinQTNonISlice=8");
        add_opt(&argc, argv, "--MaxMTTHierarchyDepth=3");
        add_opt(&argc, argv, "--MaxMTTHierarchyDepthISliceL=3");
        add_opt(&argc, argv, "--MaxMTTHierarchyDepthISliceC=3");

        //add_opt(&argc, argv, "--MMVD=1");
        add_opt(&argc, argv, "--SbTMVP=1");

        add_opt(&argc, argv, "--MaxNumMergeCand=6");
        add_opt(&argc, argv, "--LMChroma=1");
        add_opt(&argc, argv, "--IMV=1");
        add_opt(&argc, argv, "--MRL=1");

        //add_opt(&argc, argv, "--IBC=0");

        add_opt(&argc, argv, "--MIP=1");

        add_opt(&argc, argv, "--PBIntraFast=1");
        add_opt(&argc, argv, "--FastMrg=1");
        add_opt(&argc, argv, "--AMaxBT=1");

        add_opt(&argc, argv, "--HadamardME=1");
        add_opt(&argc, argv, "--FEN=1");
        add_opt(&argc, argv, "--FDM=1");

        add_opt(&argc, argv, "--TransformSkip=1");
        add_opt(&argc, argv, "--TransformSkipFast=1");
        add_opt(&argc, argv, "--TransformSkipLog2MaxSize=5");
        add_opt(&argc, argv, "--SAOLcuBoundary=0");



#if 0
    /* TEST with several slices */
    add_opt(&argc, argv, "--SliceMode=2");
    add_opt(&argc, argv, "--SliceArgument=5");
#endif

    /* trailing NULL */
    argv[argc] = NULL;

    if (s->params.verbose >= 2) {
        int i;
        printf("Encode options:");
        for(i = 0; i < argc; i++) {
            printf(" %s", argv[i]);
        }
        printf("\n");
    }

  std::fstream bitstream;
  EncLibCommon encLibCommon;

  std::vector<EncApp*> pcEncApp(1);
  bool resized = false;
  int layerIdx = 0;

  initROM();
  TComHash::initBlockSizeToIndex();

  char** layerArgv = new char*[argc];

  do
  {
    pcEncApp[layerIdx] = new EncApp( bitstream, &encLibCommon );
    // create application encoder class per layer
    pcEncApp[layerIdx]->create();

    // parse configuration per layer
    try
    {
      int j = 0;
      for( int i = 0; i < argc; i++ )
      {
        if( argv[i][0] == '-' && argv[i][1] == 'l' )
        {
          if (argc <= i + 1)
          {
            THROW("Command line parsing error: missing parameter after -lx\n");
          }
          int numParams = 1; // count how many parameters are consumed
          // check for long parameters, which start with "--"
          const std::string param = argv[i + 1];
          if (param.rfind("--", 0) != 0)
          {
            // only short parameters have a second parameter for the value
            if (argc <= i + 2)
            {
              THROW("Command line parsing error: missing parameter after -lx\n");
            }
            numParams++;
          }
          // check if correct layer index
          if( argv[i][2] == std::to_string( layerIdx ).c_str()[0] )
          {
            layerArgv[j] = argv[i + 1];
            if (numParams > 1)
            {
              layerArgv[j + 1] = argv[i + 2];
            }
            j+= numParams;
          }
          i += numParams;
        }
        else
        {
          layerArgv[j] = argv[i];
          j++;
        }
      }

      if( !pcEncApp[layerIdx]->parseCfg( j, layerArgv ) )
      {
        pcEncApp[layerIdx]->destroy();
        return 1;
      }
    }
    catch( df::program_options_lite::ParseFailure &e )
    {
      std::cerr << "Error parsing option \"" << e.arg << "\" with argument \"" << e.val << "\"." << std::endl;
      return 1;
    }

    pcEncApp[layerIdx]->createLib( layerIdx );

    if( !resized )
    {
      pcEncApp.resize( pcEncApp[layerIdx]->getMaxLayers() );
      resized = true;
    }

    layerIdx++;
  } while( layerIdx < pcEncApp.size() );

  delete[] layerArgv;

  if (layerIdx > 1)
  {
    VPS* vps = pcEncApp[0]->getVPS();
    //check chroma format and bit-depth for dependent layers
    for (uint32_t i = 0; i < layerIdx; i++)
    {
      int curLayerChromaFormatIdc = pcEncApp[i]->getChromaFormatIDC();
      int curLayerBitDepth = pcEncApp[i]->getBitDepth();
      for (uint32_t j = 0; j < layerIdx; j++)
      {
        if (vps->getDirectRefLayerFlag(i, j))
        {
          int refLayerChromaFormatIdcInVPS = pcEncApp[j]->getChromaFormatIDC();
          CHECK(curLayerChromaFormatIdc != refLayerChromaFormatIdcInVPS, "The chroma formats of the current layer and the reference layer are different");
          int refLayerBitDepthInVPS = pcEncApp[j]->getBitDepth();
          CHECK(curLayerBitDepth != refLayerBitDepthInVPS, "The bit-depth of the current layer and the reference layer are different");
        }
      }
    }
  }

#if PRINT_MACRO_VALUES
  printMacroSettings();
#endif

  // starting time
  auto startTime  = std::chrono::steady_clock::now();
  std::time_t startTime2 = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  fprintf(stdout, " started @ %s", std::ctime(&startTime2) );
  clock_t startClock = clock();

  // call encoding function per layer
  bool eos = false;

  while( !eos )
  {
    // read GOP
    bool keepLoop = true;
    while( keepLoop )
    {
      for( auto & encApp : pcEncApp )
      {
#ifndef _DEBUG
        try
        {
#endif
          keepLoop = encApp->encodePrep( eos );
#ifndef _DEBUG
        }
        catch( Exception &e )
        {
          std::cerr << e.what() << std::endl;
          return EXIT_FAILURE;
        }
        catch( const std::bad_alloc &e )
        {
          std::cout << "Memory allocation failed: " << e.what() << std::endl;
          return EXIT_FAILURE;
        }
#endif
      }
    }

    // encode GOP
    keepLoop = true;
    while( keepLoop )
    {
      for( auto & encApp : pcEncApp )
      {
#ifndef _DEBUG
        try
        {
#endif
          keepLoop = encApp->encode();
#ifndef _DEBUG
        }
        catch( Exception &e )
        {
          std::cerr << e.what() << std::endl;
          return EXIT_FAILURE;
        }
        catch( const std::bad_alloc &e )
        {
          std::cout << "Memory allocation failed: " << e.what() << std::endl;
          return EXIT_FAILURE;
        }
#endif
      }
    }
  }
  // ending time
  clock_t endClock = clock();
  auto endTime = std::chrono::steady_clock::now();
  std::time_t endTime2 = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
#if JVET_O0756_CALCULATE_HDRMETRICS
  auto metricTime = pcEncApp[0]->getMetricTime();

  for( int layerIdx = 1; layerIdx < pcEncApp.size(); layerIdx++ )
  {
    metricTime += pcEncApp[layerIdx]->getMetricTime();
  }
  auto totalTime      = std::chrono::duration_cast<std::chrono::milliseconds>( endTime - startTime ).count();
  auto encTime        = std::chrono::duration_cast<std::chrono::milliseconds>( endTime - startTime - metricTime ).count();
  auto metricTimeuser = std::chrono::duration_cast<std::chrono::milliseconds>( metricTime ).count();
#else
  auto encTime = std::chrono::duration_cast<std::chrono::milliseconds>( endTime - startTime).count();
#endif

  for( auto & encApp : pcEncApp )
  {
    encApp->destroyLib();

    // destroy application encoder class per layer
    encApp->destroy();

    delete encApp;
  }

  // destroy ROM
  destroyROM();

  pcEncApp.clear();

  printf( "\n finished @ %s", std::ctime(&endTime2) );

#if JVET_O0756_CALCULATE_HDRMETRICS
  printf(" Encoding Time (Total Time): %12.3f ( %12.3f ) sec. [user] %12.3f ( %12.3f ) sec. [elapsed]\n",
         ((endClock - startClock) * 1.0 / CLOCKS_PER_SEC) - (metricTimeuser/1000.0),
         (endClock - startClock) * 1.0 / CLOCKS_PER_SEC,
         encTime / 1000.0,
         totalTime / 1000.0);
#else
  printf(" Total Time: %12.3f sec. [user] %12.3f sec. [elapsed]\n",
         (endClock - startClock) * 1.0 / CLOCKS_PER_SEC,
         encTime / 1000.0);
#endif

    for(i = 0; i < argc; i++)
        free(argv[i]);
    unlink(s->infilename);

    /* read output bitstream */
    f = fopen(s->outfilename, "rb");
    if (!f) {
        fprintf(stderr, "Could not open '%s'\n", s->outfilename);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    out_buf_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    out_buf = (uint8_t *)malloc(out_buf_len);
    if (fread(out_buf, 1, out_buf_len, f) != out_buf_len) {
        fprintf(stderr, "read error\n");
        fclose(f);
        free(out_buf);
        return -1;
    }
    fclose(f);
    unlink(s->outfilename);
    *pbuf = out_buf;
    free(s);
    return out_buf_len;
}

HEVCEncoder jvetvvc_encoder = {
  .open = jvetvvc_open,
  .encode = jvetvvc_encode,
  .close = jvetvvc_close,
};
