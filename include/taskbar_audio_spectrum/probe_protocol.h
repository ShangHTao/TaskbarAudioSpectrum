#pragma once

#include "platform.h"

namespace tas {
constexpr wchar_t kOverlayProbeMessageName[] =
    L"TaskbarAudioSpectrum.OverlayProbe.v1";
enum OverlayProbeFlags : LRESULT {
    kOverlayPositioned = 1 << 0,
    kOverlayEligible = 1 << 1,
    kOverlayVisible = 1 << 2,
    kOverlayPainted = 1 << 3,
};
}  // namespace tas
