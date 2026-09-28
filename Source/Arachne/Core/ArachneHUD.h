#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ArachneHUD.generated.h"

class AArachnePlayerCharacter;

/** Controls line, the "caught" screen with a reset button, and the debug readout (0). */
UCLASS()
class ARACHNE_API AArachneHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
    virtual void NotifyHitBoxClick(FName BoxName) override;

private:
    void DrawCaughtScreen(AArachnePlayerCharacter* Player);
    void DrawDebugReadout(AArachnePlayerCharacter* Player);
};
