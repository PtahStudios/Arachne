#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ArachneNavigatorComponent.generated.h"

class AArachnePawn;

UENUM(BlueprintType)
enum class EArachneMoveStatus : uint8
{
    Idle,
    Moving,
    Arrived,
    Stuck
};

UENUM(BlueprintType)
enum class EArachneSurface : uint8
{
    Floor,
    Wall,
    Ceiling
};

/**
 * Turns a route (list of points) into steering for AArachnePawn. No navmesh: the route comes from the waypoint graph
 * or the prey trail, and the path between points is felt out with raycasts:
 *  - Surface choice per segment: floor, a side wall or the ceiling (weighted random), found by casting from the body.
 *    To change surface it steers at the chosen surface and the body's corner arcs do the climbing.
 *  - Spider gait (when not sprinting): stop-and-go bursts, speed jitter, heading wobble, occasional crab walk.
 *  - Stuck detection: no progress -> fall back to the floor / other wall, then report Stuck.
 */
UCLASS(ClassGroup=(Arachne), meta=(BlueprintSpawnableComponent))
class ARACHNE_API UArachneNavigatorComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UArachneNavigatorComponent();
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // ---------------------------------------------------------------- route
    /** Arrival radius for intermediate route points (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigator|Route") float PassRadius = 120.f;

    // ---------------------------------------------------------------- surfaces
    /** Relative chance to travel a segment on the floor / a wall / the ceiling. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigator|Surfaces", meta=(ClampMin="0")) float FloorWeight = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigator|Surfaces", meta=(ClampMin="0")) float WallWeight = .8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigator|Surfaces", meta=(ClampMin="0")) float CeilingWeight = .6f;
    /** Segments shorter than this keep the current surface. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigator|Surfaces") float MinSegmentForSurfaceChange = 450.f;
    /** How far to cast for walls / floor / ceiling. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigator|Surfaces") float SurfaceScanDistance = 700.f;
    /** Height above the floor kept while travelling along a wall (cm). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigator|Surfaces") float WallCruiseHeight = 165.f;
    /** Within this (horizontal) distance of the last point, switch to the surface the target sits on. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigator|Surfaces") float GoalSurfaceDistance = 300.f;

    // ---------------------------------------------------------------- spider gait
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigator|Gait") bool bErraticGait = true;
    /** Duration of one burst of walking (s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigator|Gait") float BurstTimeMin = .7f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigator|Gait") float BurstTimeMax = 2.6f;
    /** Chance to freeze after a burst, and for how long (s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigator|Gait", meta=(ClampMin="0", ClampMax="1")) float PauseChance = .5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigator|Gait") float PauseTimeMin = .15f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigator|Gait") float PauseTimeMax = 1.1f;
    /** 0..1: how much the walking speed wanders. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigator|Gait", meta=(ClampMin="0", ClampMax="1")) float SpeedJitter = .35f;
    /** Heading wobble amplitude (deg) and frequency (Hz). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigator|Gait") float WobbleAngle = 16.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigator|Gait") float WobbleFrequency = .6f;
    /** Chance per burst to walk sideways (body keeps its facing while travelling). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigator|Gait", meta=(ClampMin="0", ClampMax="1")) float CrabChance = .2f;

    // ---------------------------------------------------------------- stuck
    /** Seconds without getting StuckProgress closer before trying something else. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigator|Stuck") float StuckTime = 2.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigator|Stuck") float StuckProgress = 40.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Navigator|Debug") bool bDebug = false;

    /** Follow Points in order. FinalAcceptRadius applies to the last one. */
    void SetRoute(const TArray<FVector>& Points, float FinalAcceptRadius, bool bInSprint);
    void MoveTo(const FVector& Goal, float AcceptRadius, bool bInSprint);
    /** Stops and releases the pawn (no more steering writes until the next route). */
    void Stop();

    UFUNCTION(BlueprintPure, Category="Navigator") EArachneMoveStatus GetStatus() const { return Status; }
    UFUNCTION(BlueprintPure, Category="Navigator") EArachneSurface GetSurface() const { return Surface; }
    bool IsSprinting() const { return bSprint; }
    const TArray<FVector>& GetRoute() const { return Route; }

private:
    void BeginSegment();
    void UpdateGait(float Dt);
    void UpdateStuck(float Dist, float Dt);
    FVector ComputeSteer(const FVector& Target) const;
    bool ScanSurface(EArachneSurface Kind, const FVector& TravelDir, float Side, FVector& OutPoint, FVector& OutNormal) const;
    bool Trace(const FVector& From, const FVector& To, FHitResult& Hit) const;
    EArachneSurface ClassifyNormal(const FVector& Normal) const;
    EArachneSurface CurrentSurface() const;
    EArachneSurface SurfaceNear(const FVector& Location) const;
    bool IsDebugOn() const;
    void DrawDebug() const;

    UPROPERTY() TObjectPtr<AArachnePawn> Pawn;

    TArray<FVector> Route;
    int32 RouteIndex = 0;
    float FinalRadius = 100.f;
    bool bSprint = false;
    EArachneMoveStatus Status = EArachneMoveStatus::Idle;

    EArachneSurface Surface = EArachneSurface::Floor;
    EArachneSurface GoalSurface = EArachneSurface::Floor;
    float WallSide = 1.f;

    float GaitTimer = 0.f;
    bool bPaused = false;
    bool bCrab = false;
    FVector CrabFacing = FVector::ZeroVector;
    float NoiseTime = 0.f;

    float BestDistance = 0.f;
    float StuckTimer = 0.f;
    int32 StuckCount = 0;
};
