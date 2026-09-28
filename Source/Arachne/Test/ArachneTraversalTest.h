#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArachneTraversalTest.generated.h"

class AArachnePawn;
class AArachneMover;
class AArachneCampPoint;

/**
 * Runtime integration harness for the body (brain disabled), spawned only with the -ArachneTest command-line flag.
 * Builds its own test geometry far from the level, drives the pawn and writes Saved/ArachneTraversalTest.json.
 */
UCLASS(NotBlueprintable)
class AArachneTraversalTest : public AActor
{
    GENERATED_BODY()
public:
    AArachneTraversalTest();
    virtual void BeginPlay() override;
    virtual void Tick(float Dt) override;

    /** Spawns the harness when the game was started with -ArachneTest (non-shipping builds only). */
    static void SpawnIfRequested(UWorld* World);

private:
    void BeginCase();
    void EndCase();
    void Finish();
    void Box(FVector Position, FVector Extent, FRotator Rotation = FRotator::ZeroRotator);
    UFUNCTION() void HandleFootfall(int32 LegIndex, FVector Location, FVector Normal, float Strength);

    UPROPERTY() TObjectPtr<AArachnePawn> Pawn;
    UPROPERTY() TObjectPtr<AArachneMover> Platform;
    UPROPERTY() TObjectPtr<AArachneCampPoint> Corner;
    TArray<TSharedPtr<class FJsonValue>> Results;
    int32 Case = 0;
    float Time = 0.f, TotalTime = 0.f;
    bool Floor = false, Wall = false, Ceiling = false, Slope = false, Convex = false, AnchorIssued = false;
    float Highest = 0.f, WorstFoot = 0.f, MaxFrameMove = 0.f, MaxUpTurn = 0.f;
    int32 MinFeet = 8, SteadySamples = 0, Footfalls = 0;
    FVector Previous = FVector::ZeroVector, Start = FVector::ZeroVector, PreviousUp = FVector::UpVector, PlatformStart = FVector::ZeroVector;
};
