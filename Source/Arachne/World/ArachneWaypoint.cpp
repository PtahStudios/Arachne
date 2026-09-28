#include "World/ArachneWaypoint.h"
#include "World/ArachneWaypointSubsystem.h"
#include "Components/BillboardComponent.h"
#include "Components/ArrowComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"

AArachneWaypoint::AArachneWaypoint()
{
    PrimaryActorTick.bCanEverTick = false;
    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    SetRootComponent(Root);
#if WITH_EDITORONLY_DATA
    Sprite = CreateEditorOnlyDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
    if (Sprite) Sprite->SetupAttachment(Root);
    Arrow = CreateEditorOnlyDefaultSubobject<UArrowComponent>(TEXT("Arrow"));
    if (Arrow)
    {
        Arrow->SetupAttachment(Root);
        Arrow->ArrowColor = FColor(255, 170, 40);
        Arrow->ArrowSize = .6f;
    }
#endif
}

void AArachneWaypoint::BeginPlay()
{
    Super::BeginPlay();
    if (UArachneWaypointSubsystem* Waypoints = GetWorld()->GetSubsystem<UArachneWaypointSubsystem>()) Waypoints->Register(this);
}

void AArachneWaypoint::EndPlay(const EEndPlayReason::Type Reason)
{
    if (UWorld* World = GetWorld())
        if (UArachneWaypointSubsystem* Waypoints = World->GetSubsystem<UArachneWaypointSubsystem>()) Waypoints->Unregister(this);
    Super::EndPlay(Reason);
}

void AArachneWaypoint::PreviewLinks()
{
    UWorld* World = GetWorld();
    if (!World) return;
    for (TActorIterator<AArachneWaypoint> It(World); It; ++It)
    {
        AArachneWaypoint* Other = *It;
        if (Other == this) continue;
        const bool bManual = ManualLinks.Contains(Other) || Other->ManualLinks.Contains(this);
        if (!bManual && !UArachneWaypointSubsystem::CanAutoLink(this, Other)) continue;
        DrawDebugLine(World, GetArrivalLocation(), Other->GetArrivalLocation(), bManual ? FColor::Cyan : FColor::Green, false, 10.f, 0, 3.f);
    }
    DrawDebugSphere(World, GetArrivalLocation(), AcceptRadius, 16, FColor::Orange, false, 10.f);
}
