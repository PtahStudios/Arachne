#pragma once
#include "CoreMinimal.h"

/**
 * Global debug switch for everything ARACHNE draws: senses, AI state, routes, noise and visibility ranges.
 * Console: "Arachne.Debug 1", in game: 0. Actors and components also have their own bDebug flags for always-on drawing.
 * Test switch: "Arachne.IgnorePlayer 1", in game: 9 - the spider's senses skip the player (she patrols, camps, never hunts).
 */
namespace ArachneDebug
{
    ARACHNE_API bool IsEnabled();
    ARACHNE_API void SetEnabled(bool bEnabled);
    ARACHNE_API void Toggle();

    ARACHNE_API bool IsIgnoringPlayer();
    ARACHNE_API void ToggleIgnorePlayer();
}
