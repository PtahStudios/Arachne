#include "ArachneHUD.h"
#include "ArachnePawn.h"
#include "GameFramework/PlayerController.h"

void AArachneHUD::DrawHUD()
{
    Super::DrawHUD();
    DrawText(TEXT("ARACHNE  |  WASD move  |  Mouse look  |  Shift sprint  |  Space jump  |  Wheel zoom  |  R reset  |  F1 debug"), FColor::White, 24, 24, nullptr, 1.f);
    if (AArachnePawn* P = Cast<AArachnePawn>(GetOwningPawn()))
    {
        if (!P->IsRigValid()) DrawText(TEXT("Rig not found: run Scripts/setup_arachne.py (Tools > Execute Python Script)"), FColor::Red, 24, 48, nullptr, 1.f);
        else if (P->bShowDebug)
        {
            const FString Status = FString::Printf(TEXT("%s  |  Planted feet: %d / 8  |  IK error: %.1f cm  |  Speed: %.0f  |  Up: %s"),
                P->IsTransitioning() ? TEXT("TRANSITION") : P->bAttached ? TEXT("ATTACHED") : TEXT("AIR"),
                P->SupportedFeet, P->MaxFootError, P->GetSurfaceVelocity().Size(), *P->SurfaceUp.ToCompactString());
            DrawText(Status, FColor::Cyan, 24, 48, nullptr, 1.f);
        }
    }
}
