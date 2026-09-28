#pragma once
#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ArachneHUD.generated.h"
UCLASS()
class ARACHNE_API AArachneHUD : public AHUD
{
    GENERATED_BODY()
public: virtual void DrawHUD() override;
};
