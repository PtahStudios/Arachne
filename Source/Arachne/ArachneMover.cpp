#include "ArachneMover.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AArachneMover::AArachneMover()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;
    Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
    SetRootComponent(Mesh);
    Mesh->SetMobility(EComponentMobility::Movable);
    Mesh->SetCollisionProfileName(TEXT("BlockAll"));
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Cube.Succeeded()) Mesh->SetStaticMesh(Cube.Object);
}

void AArachneMover::BeginPlay()
{
    Super::BeginPlay();
    Origin = GetActorLocation();
}

void AArachneMover::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    Time += DeltaSeconds;
    const double Phase = 2.0 * PI * Time / FMath::Max(Period, .1f);
    SetActorLocation(Origin + Amplitude * FMath::Sin(Phase));
    if (!SpinRate.IsNearlyZero()) AddActorWorldRotation(SpinRate * DeltaSeconds);
}
