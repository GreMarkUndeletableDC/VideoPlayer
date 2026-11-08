#pragma once
#include "eck\PchInclude.h"
#include "eck\SystemHelper.h"
#include "eck\D3DHelper.h"
#include "eck\MathHelper.h"
#include "eck\CDWriteFontFactory.h"
#include "eck\CDuiTrackBar.h"
#include "eck\CDuiListView.h"
#include "eck\CDuiButton.h"
#include "eck\DuiCompositor2DAffineTransform.h"
#include "eck\CCriticalSection.h"
#include "eck\CConditionVariable.h"
#include "eck\Utility2.h"
#include "eck\CScopeGuard.h"
#include "eck\ImageHelper.h"
#include "eck\MediaTag.h"
#include "eck\MediaTagID3v2.h"
#include "eck\MediaTagApe.h"
#include "eck\MediaTagFlac.h"

#include <deque>

EXTERN_C_START
#pragma warning(push)
#pragma warning(disable: 4819)// 该文件包含不能在当前代码页(936)中表示的字符
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext_d3d11va.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/csp.h>
#include <libswscale/swscale.h>
#pragma warning(pop)
EXTERN_C_END

#include <mmdeviceapi.h>
#include <Audioclient.h>
#include <d3d10.h>

const inline IID IID_IAudioClient = __uuidof(IAudioClient);
const inline IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);
const inline CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
const inline IID IID_IAudioClock = __uuidof(IAudioClock);

using eck::PCVOID;
using eck::PCBYTE;
using eck::SafeRelease;
using eck::ComPtr;
using eck::RCWH;

namespace Dui = eck::Dui;
namespace Dx3D = eck::Dx3D;
namespace Kw = eck::Kw;
namespace Tag = eck::MediaTag;

#include "CRingBuffer.h"