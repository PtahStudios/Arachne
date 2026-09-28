#include "Core/ArachneHUD.h"
#include "Core/ArachneDebug.h"
#include "Player/ArachnePlayerCharacter.h"
#include "World/ArachneStimulusSourceComponent.h"
#include "Creature/ArachnePawn.h"
#include "AI/ArachneBrainComponent.h"
#include "AI/ArachneMemoryComponent.h"
#include "AI/ArachneNavigatorComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"

static const FName ResetBox(TEXT("Reset"));

void AArachneHUD::DrawHUD()
{
    Super::DrawHUD();
    AArachnePlayerCharacter* Player = Cast<AArachnePlayerCharacter>(GetOwningPawn());
    DrawText(TEXT("WASD move  |  Mouse look  |  Shift sprint  |  F flashlight  |  0 debug  |  9 Arachne ignores you  |  R reset"), FColor(200, 200, 200), 24, 24, nullptr, 1.f);
    if (ArachneDebug::IsIgnoringPlayer())
        DrawText(TEXT("ARACHNE IGNORES THE PLAYER (9)"), FColor(120, 255, 140), 24.f, static_cast<float>(Canvas->SizeY) - 48.f, nullptr, 1.2f);
    if (!Player) return;
    if (ArachneDebug::IsEnabled()) DrawDebugReadout(Player);
    if (Player->IsCaught()) DrawCaughtScreen(Player);
}

void AArachneHUD::DrawCaughtScreen(AArachnePlayerCharacter* Player)
{
    const float W = static_cast<float>(Canvas->SizeX), H = static_cast<float>(Canvas->SizeY);
    DrawRect(FLinearColor(0.f, 0.f, 0.f, .45f), 0.f, 0.f, W, H);
    UFont* Font = GEngine ? GEngine->GetLargeFont() : nullptr;
    DrawText(TEXT("CAUGHT"), FColor(220, 40, 40), W * .5f - 60.f, H * .38f, Font, 2.f);

    const FVector2D Size(220.f, 56.f);
    const FVector2D Pos(W * .5f - Size.X * .5f, H * .55f);
    DrawRect(FLinearColor(.12f, .12f, .12f, .9f), Pos.X, Pos.Y, Size.X, Size.Y);
    DrawText(TEXT("RESET  (R)"), FColor::White, Pos.X + 52.f, Pos.Y + 16.f, Font, 1.f);
    AddHitBox(Pos, Size, ResetBox, true);
}

void AArachneHUD::NotifyHitBoxClick(FName BoxName)
{
    Super::NotifyHitBoxClick(BoxName);
    if (BoxName == ResetBox)
        if (AArachnePlayerCharacter* Player = Cast<AArachnePlayerCharacter>(GetOwningPawn())) Player->RestartLevel();
}

void AArachneHUD::DrawDebugReadout(AArachnePlayerCharacter* Player)
{
    float Y = 52.f;
    auto Line = [this, &Y](const FString& Text, const FColor& Color)
    {
        DrawText(Text, Color, 24.f, Y, nullptr, 1.f);
        Y += 18.f;
    };
    const UArachneStimulusSourceComponent* Stimulus = Player->Stimulus;
    Line(FString::Printf(TEXT("PLAYER  noise %.0f cm  |  visible from %.0f cm  |  flashlight %s  |  %s"),
        Stimulus->GetCurrentNoiseRadius(), Stimulus->GetVisibleDistance(),
        Player->IsFlashlightOn() ? TEXT("ON") : TEXT("off"), Player->IsSprinting() ? TEXT("SPRINT") : TEXT("walk")), FColor(120, 190, 255));

    static const TCHAR* Surfaces[] = {TEXT("floor"), TEXT("wall"), TEXT("ceiling")};
    static const TCHAR* Moves[] = {TEXT("idle"), TEXT("moving"), TEXT("arrived"), TEXT("stuck")};
    for (TActorIterator<AArachnePawn> It(GetWorld()); It; ++It)
    {
        const AArachnePawn* Spider = *It;
        if (!Spider->Brain || !Spider->Memory || !Spider->Navigator) continue;
        const float Since = Spider->Memory->GetTimeSinceConfirmed();
        Line(FString::Printf(TEXT("ARACHNE  %s  |  awareness %.2f  |  contact %s  |  move %s on %s%s%s"),
            *Spider->Brain->GetStateName(), Spider->Memory->GetAwareness(),
            Since < 100.f ? *FString::Printf(TEXT("%.1fs ago"), Since) : TEXT("none"),
            Moves[static_cast<int32>(Spider->Navigator->GetStatus())], Surfaces[static_cast<int32>(Spider->Navigator->GetSurface())],
            Spider->IsSprinting() ? TEXT("  SPRINT") : TEXT(""), Spider->IsAnchored() ? TEXT("  ANCHORED") : TEXT("")), FColor::Orange);
    }
}
