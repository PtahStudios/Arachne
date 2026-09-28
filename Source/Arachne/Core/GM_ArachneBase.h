#pragma once
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GM_ArachneBase.generated.h"

/** Just the defaults: first-person player + HUD. Game rules live in their own classes, not here. */
UCLASS(Blueprintable)
class ARACHNE_API AGM_ArachneBase : public AGameModeBase
{
    GENERATED_BODY()
public:
    AGM_ArachneBase();
    virtual void BeginPlay() override;
};
