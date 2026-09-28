#include "Core/ArachneDebug.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarArachneDebug(
    TEXT("Arachne.Debug"), 0,
    TEXT("ARACHNE debug drawing. 0 = off, 1 = senses, AI state, routes, noise and visibility ranges."),
    ECVF_Default);

namespace ArachneDebug
{
    bool IsEnabled() { return CVarArachneDebug.GetValueOnGameThread() != 0; }
    void SetEnabled(bool bEnabled) { CVarArachneDebug.AsVariable()->Set(bEnabled ? 1 : 0, ECVF_SetByConsole); }
    void Toggle() { SetEnabled(!IsEnabled()); }
}
