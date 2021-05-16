/** \file     jvetvvc.cpp
    \brief    Encoder application main
*/

#include <ctime>
#include <iostream>
#include <unistd.h>
#include <chrono>

#include <stdio.h>
#include <string>
#include <fstream>
#include <cstring>
#include <algorithm>
#include <cstdarg>

#include "vvenc/version.h"
#include "vvenc/vvenc.h"

#include "apputils/ParseArg.h"
#include "apputils/YuvFileIO.h"
#include "apputils/VVEncAppCfg.h"
#include "EncoderLib/Analyze.h"

extern "C" {
#include "bpgenc.h"
}
#ifdef _WIN32
#include <windows.h>
#endif

vvencMsgLevel g_verbosity = VVENC_VERBOSE;

void msgFnc( void*, int level, const char* fmt, va_list args )
{
  if ( g_verbosity >= level )
  {
    vfprintf( level == 1 ? stderr : stdout, fmt, args );
  }
}

void msgApp( void* ctx, int level, const char* fmt, ... )
{
    va_list args;
    va_start( args, fmt );
    msgFnc( ctx, level, fmt, args );
    va_end( args );
}

void printVVEncErrorMsg( const std::string cAppname, const std::string cMessage, int code, const std::string cErr )
{
  std::cout << cAppname  << " [error]: " << cMessage << ", ";
  switch( code )
  {
    case VVENC_ERR_CPU :           std::cout << "SSE 4.1 cpu support required."; break;
    case VVENC_ERR_PARAMETER :     std::cout << "invalid parameter."; break;
    case VVENC_ERR_NOT_SUPPORTED : std::cout << "unsupported request."; break;
    default :                      std::cout << "error " << code; break;
  };
  if( !cErr.empty() )
  {
    std::cout << " - " << cErr;
  }
  std::cout << std::endl;
}

bool parseCfg( int argc, char* argv[], apputils::VVEncAppCfg& rcVVEncAppCfg )
{
  try
  {
    if( ! rcVVEncAppCfg.parseCfg( argc, argv ) )
    {
      return false;
    }
  }
  catch( apputils::df::program_options_lite::ParseFailure &e )
  {
    msgApp( nullptr, VVENC_ERROR, "Error parsing option \"%s\" with argument \"%s\".\n", e.arg.c_str(), e.val.c_str() );
    return false;
  }
  g_verbosity = rcVVEncAppCfg.m_verbosity;

  return true;
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

static HEVCEncoderContext *jvetvvenc_open(const HEVCEncodeParams *params)
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

static int jvetvvenc_encode(HEVCEncoderContext *s, Image *img)
{
    save_yuv1(img, s->yuv_file);
    s->frame_count++;
    return 0;
}

/* return the encoded data in *pbuf and the size. Return < 0 if error */
static int jvetvvenc_close(HEVCEncoderContext *s, uint8_t **pbuf)
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

    vvenc::m_AnalyzeAll.clear();
    vvenc::m_AnalyzeI.clear();
    vvenc::m_AnalyzeP.clear();
    vvenc::m_AnalyzeB.clear();
    //vvenc::EncGOP::m_AnalyzeAll_in.clear();

    argc = 0;
    add_opt(&argc, argv, "jvetvvenc"); /* dummy executable name */

///////////////////////////////////////////////////////////////////////////////////

    snprintf(buf, sizeof(buf),"--input=%s", s->infilename);
    add_opt(&argc, argv, buf);
    snprintf(buf, sizeof(buf),"--size=%dx%d", s->params.width, s->params.height);
    add_opt(&argc, argv, buf);

    snprintf(buf, sizeof(buf),"--framerate=%d", 8 * s->params.frame_rate);
    add_opt(&argc, argv, buf);
    snprintf(buf, sizeof(buf),"--qp=%d", s->params.qp);
    add_opt(&argc, argv, buf);

    snprintf(buf, sizeof(buf),"--internal-bitdepth=%d", s->params.bit_depth);
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

    if (s->params.lossless)
    {
      add_opt(&argc, argv, "--CostMode=lossless");
    }
    else
    {
      add_opt(&argc, argv, "--CostMode=lossy");
    }

    if (s->params.bit_depth == 8)
    {
      if (s->params.chroma_format == BPG_FORMAT_GRAY)
      {
        add_opt(&argc, argv, "--format=yuv400");
      }
      if (s->params.chroma_format == BPG_FORMAT_420)
      {
        add_opt(&argc, argv, "--format=yuv420");
      }
      if (s->params.chroma_format == BPG_FORMAT_422)
      {
        add_opt(&argc, argv, "--format=yuv422");
      }
      if (s->params.chroma_format == BPG_FORMAT_444)
      {
        add_opt(&argc, argv, "--format=yuv444");
      }
    }
    if (s->params.bit_depth == 10)
    {
      if (s->params.chroma_format == BPG_FORMAT_GRAY)
      {
        add_opt(&argc, argv, "--format=yuv400_10");
      }
      if (s->params.chroma_format == BPG_FORMAT_420)
      {
        add_opt(&argc, argv, "--format=yuv420_10");
        //add_opt(&argc, argv, "--profile=main10");
      }
      if (s->params.chroma_format == BPG_FORMAT_422)
      {
        add_opt(&argc, argv, "--format=yuv422_10");
      }
      if (s->params.chroma_format == BPG_FORMAT_444)
      {
        add_opt(&argc, argv, "--format=yuv444_10");
      }
    }

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
      add_opt(&argc, argv, "--qpa=0");
      add_opt(&argc, argv, "--hrd=off");
      add_opt(&argc, argv, "--hrdparameterspresent=0");
    }
    if (s->params.color_space == BPG_CS_YCbCr_BT709) {
      add_opt(&argc, argv, "--qpa=1");
      add_opt(&argc, argv, "--hrd=off");
      add_opt(&argc, argv, "--hrdparameterspresent=0");
    }
    if (s->params.color_space == BPG_CS_YCbCr_BT2020) {
      add_opt(&argc, argv, "--qpa=3");
      add_opt(&argc, argv, "--hrd=hlg_2020");
      add_opt(&argc, argv, "--hrdparameterspresent=1");
    }

    if (s->params.compress_level == 8 || s->params.compress_level == 9) {
      add_opt(&argc, argv, "--preset=slower");
    }
    if (s->params.compress_level == 6 || s->params.compress_level == 7) {
      add_opt(&argc, argv, "--preset=slow");
    }
    if (s->params.compress_level == 4 || s->params.compress_level == 5) {
      add_opt(&argc, argv, "--preset=medium");
    }
    if (s->params.compress_level == 2 || s->params.compress_level == 3) {
      add_opt(&argc, argv, "--preset=fast");
    }
    if (s->params.compress_level == 0 || s->params.compress_level == 1) {
      add_opt(&argc, argv, "--preset=faster");
    }

    snprintf(buf, sizeof(buf),"--SEIDecodedPictureHash=%d",
             s->params.sei_decoded_picture_hash);
    add_opt(&argc, argv, buf);

    add_opt(&argc, argv, "--verbosity=6");
    //snprintf(buf, sizeof(buf),"--frames=%d", s->frame_count);
    //add_opt(&argc, argv, buf);
    add_opt(&argc, argv, "--frames=1");
    add_opt(&argc, argv, "--threads=4");
    add_opt(&argc, argv, "--gopsize=32");
    add_opt(&argc, argv, "--intraperiod=32");
    //add_opt(&argc, argv, "--preset=medium");
    add_opt(&argc, argv, "--level=6.3");
    add_opt(&argc, argv, "--tier=main");
    add_opt(&argc, argv, "--output=image.vvc");

  std::string cAppname = argv[0];
  std::size_t iPos = (int)cAppname.find_last_of("/");
  if( std::string::npos != iPos )
  {
    cAppname = cAppname.substr(iPos+1 );
  }

  int iRet = 0;

  vvenc_set_logging_callback( nullptr, msgFnc );

  std::string cInputFile;
  std::string cOutputfile = "";

  apputils::VVEncAppCfg vvencappCfg;                           ///< encoder configuration
  vvenc_init_default( &vvencappCfg, 1920, 1080, 60, 0, 32, vvencPresetMode::VVENC_MEDIUM );

  // parse configuration
  if ( ! parseCfg( argc, argv, vvencappCfg ) )
  {
    return 1;
  }
  // assign verbosity used for encoder output
  g_verbosity = vvencappCfg.m_verbosity;

  if( vvencappCfg.m_showVersion )
  {
    std::cout << cAppname  << " version " << vvenc_get_version()<< std::endl;
    return 0;
  }

  if( !strcmp( vvencappCfg.m_inputFileName.c_str(), "-" )  )
  {
    if( vvencappCfg.m_RCNumPasses > 1 )
    {
      std::cout << cAppname << " [error]: 2 pass rate control and reading from stdin is not supported yet" << std::endl;
      return -1;
    }
    else
    {
      std::cout << cAppname << " trying to read from stdin" << std::endl;
    }
  }

  if( vvencappCfg.m_bitstreamFileName.empty() )
  {
    std::cout << cAppname  << " [error]: no output bitstream file given." << std::endl;
    return -1;
  }

  cInputFile  = vvencappCfg.m_inputFileName;
  cOutputfile = vvencappCfg.m_bitstreamFileName;

  if( vvencappCfg.m_verbosity > VVENC_SILENT && vvencappCfg.m_verbosity < VVENC_NOTICE )
  {
    std::cout << "-------------------" << std::endl;
    std::cout << cAppname  << " version " << vvenc_get_version() << std::endl;
  }

  vvencEncoder *enc = vvenc_encoder_create();
  if( nullptr == enc )
  {
    return -1;
  }

  // initialize the encoder
  iRet = vvenc_encoder_open( enc, &vvencappCfg );
  if( 0 != iRet )
  {
    printVVEncErrorMsg( cAppname, "cannot create encoder", iRet, vvenc_get_last_error( enc ) );
    vvenc_encoder_close( enc );
    return iRet;
  }

  if( vvencappCfg.m_verbosity > VVENC_WARNING )
  {
    std::cout << cAppname << ": " << vvenc_get_enc_information( enc ) << std::endl;
  }

  vvenc_get_config( enc, &vvencappCfg ); // get the adapted config, because changes are needed for the yuv reader (m_MSBExtendedBitDepth)

  if( vvencappCfg.m_verbosity >= VVENC_INFO )
  {
    std::cout << vvencappCfg.getConfigAsString( vvencappCfg.m_verbosity ) << std::endl;
  }

  // open output file
  std::ofstream cOutBitstream;
  if( !cOutputfile.empty() )
  {
    cOutBitstream.open( cOutputfile.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
    if( !cOutBitstream.is_open() )
    {
      std::cout << cAppname  << " [error]: failed to open output file " << cOutputfile << std::endl;
      return -1;
    }
  }

  // --- allocate memory for output packets
  vvencAccessUnit AU;
  vvenc_accessUnit_default( &AU );
  vvenc_accessUnit_alloc_payload( &AU, vvencappCfg.m_SourceWidth * vvencappCfg.m_SourceHeight );

  // --- allocate memory for YUV input picture
  vvencYUVBuffer cYUVInputBuffer;
  vvenc_YUVBuffer_default( &cYUVInputBuffer );
  vvenc_YUVBuffer_alloc_buffer( &cYUVInputBuffer, vvencappCfg.m_internChromaFormat, vvencappCfg.m_SourceWidth, vvencappCfg.m_SourceHeight );

  // --- start timer
  std::chrono::steady_clock::time_point cTPStartRun = std::chrono::steady_clock::now();
  if( vvencappCfg.m_verbosity > VVENC_WARNING )
  {
    std::time_t startTime2 = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::cout  << "started @ " << std::ctime(&startTime2)  << std::endl;
  }

  // calc temp. rate/scale
  int temporalRate   = vvencappCfg.m_FrameRate;
  int temporalScale  = 1;

  switch( vvencappCfg.m_FrameRate )
  {
  case 23: temporalRate = 24000; temporalScale = 1001; break;
  case 29: temporalRate = 30000; temporalScale = 1001; break;
  case 59: temporalRate = 60000; temporalScale = 1001; break;
  default: break;
  }

  unsigned int uiFrames = 0;
  for( int pass = 0; pass < vvencappCfg.m_RCNumPasses; pass++ )
  {
    // initialize the encoder pass
    iRet = vvenc_init_pass( enc, pass );
    if( 0 != iRet )
    {
      printVVEncErrorMsg( cAppname, "cannot init encoder", iRet, vvenc_get_last_error( enc ) );
      return iRet;
    }

    // open the input file
    apputils::YuvFileIO cYuvFileInput;
    if( 0 != cYuvFileInput.open( cInputFile, false, vvencappCfg.m_inputBitDepth[0], vvencappCfg.m_MSBExtendedBitDepth[0], vvencappCfg.m_internalBitDepth[0],
                                 vvencappCfg.m_inputFileChromaFormat, vvencappCfg.m_internChromaFormat, vvencappCfg.m_bClipOutputVideoToRec709Range, false ) )
    {
      std::cout << cAppname  << " [error]: failed to open input file " << cInputFile << std::endl;
      return -1;
    }

    const int iFrameSkip  = std::max( vvencappCfg.m_FrameSkip - vvenc_get_num_lead_frames(enc), 0 );
    const int64_t iMaxFrames  = vvencappCfg.m_framesToBeEncoded + vvenc_get_num_lead_frames(enc) + vvenc_get_num_trail_frames(enc);
    int64_t       iSeqNumber  = 0;
    bool          bEof        = false;
    bool          bEncodeDone = false;

    uiFrames    = 0;

    if( iFrameSkip )
    {
      cYuvFileInput.skipYuvFrames(iFrameSkip, vvencappCfg.m_SourceWidth, vvencappCfg.m_SourceHeight);
      iSeqNumber=iFrameSkip;
    }

    while( !bEof || !bEncodeDone )
    {
      vvencYUVBuffer* ptrYUVInputBuffer = nullptr;
      if( !bEof )
      {
        if( 0 != cYuvFileInput.readYuvBuf( cYUVInputBuffer, bEof ) )
        {
          std::cout << " [error]: read file failed: " << cYuvFileInput.getLastError() << std::endl;
          return -1;
        }
        if( ! bEof )
        {
          // set sequence number and cts
          cYUVInputBuffer.sequenceNumber  = iSeqNumber;
          cYUVInputBuffer.cts             = iSeqNumber * vvencappCfg.m_TicksPerSecond * temporalScale / temporalRate;
          cYUVInputBuffer.ctsValid        = true;
          ptrYUVInputBuffer               = &cYUVInputBuffer;
          iSeqNumber++;
          //std::cout << "process picture " << cYUVInputBuffer.m_uiSequenceNumber << " cts " << cYUVInputBuffer.m_uiCts << std::endl;
        }
        else if( vvencappCfg.m_verbosity > VVENC_ERROR && vvencappCfg.m_verbosity < VVENC_NOTICE )
        {
          std::cout << "EOF reached" << std::endl;
        }
      }

      // call encode
      iRet = vvenc_encode( enc, ptrYUVInputBuffer, &AU, &bEncodeDone );
      if( 0 != iRet )
      {
        printVVEncErrorMsg( cAppname, "encoding failed", iRet, vvenc_get_last_error( enc ) );
        return iRet;
      }

      if( AU.payloadUsedSize > 0 )
      {
        if( cOutBitstream.is_open() )
        {
          // write output
          cOutBitstream.write( (const char*)AU.payload, AU.payloadUsedSize );
        }
        uiFrames++;
      }

      if( iMaxFrames > 0 && iSeqNumber >= ( iFrameSkip + iMaxFrames ) )
      {
        bEof = true;
      }
    }

    cYuvFileInput.close();
  }

  std::chrono::steady_clock::time_point cTPEndRun = std::chrono::steady_clock::now();
  double dTimeSec = (double)std::chrono::duration_cast<std::chrono::milliseconds>((cTPEndRun)-(cTPStartRun)).count() / 1000;

  if( cOutBitstream.is_open() )
  {
    cOutBitstream.close();
  }

  vvenc_print_summary(enc);

  // un-initialize the encoder
  iRet = vvenc_encoder_close( enc );
  if( 0 != iRet )
  {
    printVVEncErrorMsg( cAppname, "destroy encoder failed", iRet, vvenc_get_last_error( enc ) );
    return iRet;
  }

  vvenc_YUVBuffer_free_buffer( &cYUVInputBuffer );
  vvenc_accessUnit_free_payload( &AU );

  if( 0 == uiFrames )
  {
    std::cout << "no frames encoded" << std::endl;
  }

  if( uiFrames && vvencappCfg.m_verbosity > VVENC_SILENT )
  {
    if( vvencappCfg.m_verbosity > VVENC_WARNING )
    {
      std::time_t endTime2 = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
      std::cout  << "finished @ " << std::ctime(&endTime2)  << std::endl;
    }

    double dFps = (double)uiFrames / dTimeSec;
    std::cout << "Total Time: " << dTimeSec << " sec. Fps(avg): " << dFps << " encoded Frames " << uiFrames << std::endl;
  }

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

HEVCEncoder jvetvvenc_encoder = {
  .open = jvetvvenc_open,
  .encode = jvetvvenc_encode,
  .close = jvetvvenc_close,
};
