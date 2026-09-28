#include "Core/ArachneDebug.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarArachneDebug(
    TEXT("Arachne.Debug"), 0,
    TEXT("ARACHNE debug drawing. 0 = off, 1 = senses, AI state, routes, noise and visibility ranges."),
    ECVF_Default);

static TAutoConsoleVariable<int32> CVarArachneIgnorePlayer(
    TEXT("Arachne.IgnorePlayer"), 0,
    TEXT("1 = Arachne's senses ignore the player (testing patrol / camping / movement without being hunted)."),
    ECVF_Default);

namespace ArachneDebug
{
    bool IsEnabled() { return CVarArachneDebug.GetValueOnGameThread() != 0; }
    void SetEnabled(bool bEnabled) { CVarArachneDebug.AsVariable()->Set(bEnabled ? 1 : 0, ECVF_SetByConsole); }
    void Toggle() { SetEnabled(!IsEnabled()); }

    bool IsIgnoringPlayer() { return CVarArachneIgnorePlayer.GetValueOnGameThread() != 0; }
    void ToggleIgnorePlayer() { CVarArachneIgnorePlayer.AsVariable()->Set(IsIgnoringPlayer() ? 0 : 1, ECVF_SetByConsole); }
}
