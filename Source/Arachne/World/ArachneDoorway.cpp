#include "World/ArachneDoorway.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"

AArachneDoorway::AArachneDoorway()
{
    bPatrolDestination = false;
    WaitTimeMin = WaitTimeMax = 0.f;
    AcceptRadius = 60.f;
#if WITH_EDITORONLY_DATA
    OpeningPreview = CreateEditorOnlyDefaultSubobject<UBoxComponent>(TEXT("OpeningPreview"));
    if (OpeningPreview)
    {
        OpeningPreview->SetupAttachment(Root);
        OpeningPreview->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        OpeningPreview->SetHiddenInGame(true);
        OpeningPreview->ShapeColor = FColor(120, 220, 255);
        OpeningPreview->SetUsingAbsoluteScale(true);
    }
#endif
}

FVector AArachneDoorway::GetPassageDirection() const
{
    const FVector Dir = GetActorForwardVector().GetSafeNormal2D();
    return Dir.IsNearlyZero() ? FVector::ForwardVector : Dir;
}

bool AArachneDoorway::MeasureOpening(float& OutLeft, float& OutRight) const
{
    const UWorld* World = GetWorld();
    if (!World) return false;
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_WorldStatic);
    Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ArachneDoorway), false, this);
    const FVector P = GetActorLocation();
    const FVector Right = FVector::CrossProduct(FVector::UpVector, GetPassageDirection());
    FHitResult L, R;
    const bool bL = World->LineTraceSingleByObjectType(L, P, P - Right * 300.0, Objects, Params) && !L.bStartPenetrating;
    const bool bR = World->LineTraceSingleByObjectType(R, P, P + Right * 300.0, Objects, Params) && !R.bStartPenetrating;
    OutLeft = bL ? L.Distance : (bR ? R.Distance : 0.f);
    OutRight = bR ? R.Distance : OutLeft;
    return bL || bR;
}

void AArachneDoorway::Remeasure()
{
    float Left = 0.f, Right = 0.f;
    if (bAutoMeasure && MeasureOpening(Left, Right)) OpeningHalfWidth = FMath::Max(20.f, FMath::Min(Left, Right));
#if WITH_EDITORONLY_DATA
    if (OpeningPreview)
    {
        OpeningPreview->SetBoxExtent(FVector(ApproachDistance, OpeningHalfWidth, 60.f));
        OpeningPreview->SetWorldRotation(GetPassageDirection().Rotation());
    }
#endif
}

void AArachneDoorway::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    Remeasure();
}

void AArachneDoorway::BeginPlay()
{
    Remeasure();
    Super::BeginPlay();
}

void AArachneDoorway::SnapToOpening()
{
    float Left = 0.f, Right = 0.f;
    if (!MeasureOpening(Left, Right)) return;
    Modify();
    const FVector Side = FVector::CrossProduct(FVector::UpVector, GetPassageDirection());
    SetActorLocation(GetActorLocation() + Side * ((Right - Left) * .5f));
    Remeasure();
}
