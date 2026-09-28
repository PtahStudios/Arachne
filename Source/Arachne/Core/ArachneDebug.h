#pragma once
#include "CoreMinimal.h"

/**
 * Global debug switch for everything ARACHNE draws: senses, AI state, routes, noise and visibility ranges.
 * Console: "Arachne.Debug 1", in game: F1. Actors and components also have their own bDebug flags for always-on drawing.
 */
namespace ArachneDebug
{
    ARACHNE_API bool IsEnabled();
    ARACHNE_API void SetEnabled(bool bEnabled);
    ARACHNE_API void Toggle();
}
