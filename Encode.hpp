#pragma once
#include <codecapi.h>
#include <string>
#include <wtypes.h>
#include "resource.h"

#define WM_ENCODE_DONE      (WM_USER+1)
#define WM_WAVEFORM_READY   (WM_USER+2)
#define WM_ENCODE_PROGRESS  (WM_USER+3)

enum AudioShortMode {
    ASM_LOOP = 0,
    ASM_SILENCE = 1
};

struct QualityPreset {
    UINT   lblId, dscId;
    UINT32 maxVidBitrate;
    UINT32 audBytesPerSec;
    int    audChannels;
    UINT32 maxWidth;
    UINT32 maxHeight;
    int    h264Profile;
};
static const QualityPreset PRESETS[] = {
    { IDS_PRE0_LBL, IDS_PRE0_DSC,       0, 24000, 2,    0,    0, eAVEncH264VProfile_High  },
    { IDS_PRE1_LBL, IDS_PRE1_DSC, 5000000, 16000, 2, 1920, 1080, eAVEncH264VProfile_High  },
    { IDS_PRE2_LBL, IDS_PRE2_DSC, 2000000, 16000, 2, 1280,  720, eAVEncH264VProfile_Main  },
    { IDS_PRE3_LBL, IDS_PRE3_DSC,  400000, 12000, 1,  854,  480, eAVEncH264VProfile_Main  },
    { IDS_PRE4_LBL, IDS_PRE4_DSC,  150000, 12000, 1,  426,  240, eAVEncH264VProfile_Base  },
};
static const int N_PRESETS = (int)(sizeof(PRESETS) / sizeof(PRESETS[0]));

struct EncodeParams {
    std::wstring videoPath, audioPath, outputPath;
    double videoStart, videoEnd;
    double audioStart, audioEnd;
    AudioShortMode audioShortMode;
    int    qualityIdx;
    float  volumeScale;
    HWND   hWnd;
};