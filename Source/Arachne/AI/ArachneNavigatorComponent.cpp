#include "AI/ArachneNavigatorComponent.h"
#include "Creature/ArachnePawn.h"
#include "Core/ArachneDebug.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

UArachneNavigatorComponent::UArachneNavigatorComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UArachneNavigatorComponent::BeginPlay()
{
    Super::BeginPlay();
    Pawn = Cast<AArachnePawn>(GetOwner());
    NoiseTime = FMath::FRandRange(0.f, 100.f);   // two spiders never wobble in sync
}

// =====================================================================================================================
// Route control
// =====================================================================================================================

void UArachneNavigatorComponent::SetRoute(const TArray<FVector>& Points, float FinalAcceptRadius, bool bInSprint)
{
    Route = Points;
    RouteIndex = 0;
    FinalRadius = FinalAcceptRadius;
    bSprint = bInSprint;
    StuckTimer = 0.f;
    StuckCount = 0;
    BestDistance = TNumericLimits<float>::Max();
    Status = Route.Num() ? EArachneMoveStatus::Moving : EArachneMoveStatus::Arrived;
    BeginSegment();
}

void UArachneNavigatorComponent::MoveTo(const FVector& Goal, float AcceptRadius, bool bInSprint)
{
    SetRoute({Goal}, AcceptRadius, bInSprint);
}

void UArachneNavigatorComponent::Stop()
{
    Route.Reset();
    Status = EArachneMoveStatus::Idle;
    bPaused = bCrab = false;
    if (Pawn)
    {
        Pawn->SetMoveDirection(FVector::ZeroVector);
        Pawn->SetFacingDirection(FVector::ZeroVector);
        Pawn->SetSprint(false);
    }
}

void UArachneNavigatorComponent::BeginSegment()
{
    if (!Pawn || !Route.IsValidIndex(RouteIndex)) return;
    const FVector Pos = Pawn->GetActorLocation();
    const FVector Target = Route[RouteIndex];
    const FVector Travel = (Target - Pos).GetSafeNormal2D();
    const EArachneSurface Now = CurrentSurface();
    GoalSurface = SurfaceNear(Route.Last());
    BestDistance = TNumericLimits<float>::Max();
    StuckTimer = 0.f;

    // Wall side: keep the wall we are on, otherwise pick one.
    if (Now == EArachneSurface::Wall)
    {
        const FVector Right = FVector::CrossProduct(FVector::UpVector, Travel);
        WallSide = FVector::DotProduct(-Pawn->SurfaceUp, Right) >= 0.0 ? 1.f : -1.f;
    }
    else
    {
        WallSide = FMath::RandBool() ? 1.f : -1.f;
    }

    // Short hops and sprints keep the current surface; long segments roll the dice.
    if (bSprint || FVector::Dist2D(Pos, Target) < MinSegmentForSurfaceChange) { Surface = Now; return; }
    const float Total = FloorWeight + WallWeight + CeilingWeight;
    const float Roll = FMath::FRand() * FMath::Max(Total, KINDA_SMALL_NUMBER);
    Surface = Roll < FloorWeight ? EArachneSurface::Floor : Roll < FloorWeight + WallWeight ? EArachneSurface::Wall : EArachneSurface::Ceiling;
}

// =====================================================================================================================
// Tick
// =====================================================================================================================

void UArachneNavigatorComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!Pawn || Status != EArachneMoveStatus::Moving || !Route.IsValidIndex(RouteIndex)) return;

    const FVector Pos = Pawn->GetActorLocation();
    const bool bLast = RouteIndex == Route.Num() - 1;
    float Dist = static_cast<float>(FVector::Dist(Pos, Route[RouteIndex]));
    // Intermediate points may be passed on any surface (walking over them on the ceiling counts).
    const bool bReached = bLast
        ? Dist <= FinalRadius
        : Dist <= PassRadius || (FVector::Dist2D(Pos, Route[RouteIndex]) <= PassRadius && FMath::Abs(Pos.Z - Route[RouteIndex].Z) < 300.0);
    if (bReached)
    {
        if (bLast)
        {
            Status = EArachneMoveStatus::Arrived;
            Pawn->SetMoveDirection(FVector::ZeroVector);
            Pawn->SetFacingDirection(FVector::ZeroVector);
            Pawn->SetSprint(false);
            return;
        }
        ++RouteIndex;
        BeginSegment();
        Dist = static_cast<float>(FVector::Dist(Pos, Route[RouteIndex]));
    }

    UpdateGait(DeltaTime);
    UpdateStuck(Dist, DeltaTime);
    if (Status != EArachneMoveStatus::Moving) return;

    FVector Steer = ComputeSteer(Route[RouteIndex]);
    if (!bSprint && bErraticGait)
    {
        const float Wobble = FMath::PerlinNoise1D(NoiseTime * WobbleFrequency) * WobbleAngle;
        Steer = FQuat(Pawn->SurfaceUp, FMath::DegreesToRadians(Wobble)).RotateVector(Steer);
        const float Speed = 1.f - SpeedJitter * (.5f + .5f * FMath::PerlinNoise1D(NoiseTime * .9f + 17.3f));
        Steer *= bPaused ? 0.f : Speed;
    }
    Pawn->SetMoveDirection(Steer);
    Pawn->SetSprint(bSprint);
    Pawn->SetFacingDirection(bCrab && !bSprint ? CrabFacing : FVector::ZeroVector);
    if (IsDebugOn()) DrawDebug();
}

void UArachneNavigatorComponent::UpdateGait(float Dt)
{
    NoiseTime += Dt;
    if (bSprint || !bErraticGait) { bPaused = bCrab = false; return; }
    GaitTimer -= Dt;
    if (GaitTimer > 0.f) return;

    // A burst ended: sometimes freeze, then set off again - maybe sideways.
    if (!bPaused && FMath::FRand() < PauseChance)
    {
        bPaused = true;
        GaitTimer = FMath::FRandRange(PauseTimeMin, PauseTimeMax);
        return;
    }
    bPaused = false;
    GaitTimer = FMath::FRandRange(BurstTimeMin, BurstTimeMax);
    bCrab = FMath::FRand() < CrabChance;
    if (bCrab && Route.IsValidIndex(RouteIndex))
    {
        const FVector Travel = FVector::VectorPlaneProject(Route[RouteIndex] - Pawn->GetActorLocation(), Pawn->SurfaceUp).GetSafeNormal();
        const float Angle = FMath::FRandRange(65.f, 100.f) * (FMath::RandBool() ? 1.f : -1.f);
        CrabFacing = FQuat(Pawn->SurfaceUp, FMath::DegreesToRadians(Angle)).RotateVector(Travel);
    }
}

void UArachneNavigatorComponent::UpdateStuck(float Dist, float Dt)
{
    if (bPaused) return;
    if (Dist < BestDistance - StuckProgress) { BestDistance = Dist; StuckTimer = 0.f; return; }
    if (BestDistance == TNumericLimits<float>::Max()) { BestDistance = Dist; return; }
    StuckTimer += Dt;
    if (StuckTimer < StuckTime) return;

    StuckTimer = 0.f;
    BestDistance = Dist;
    if (++StuckCount >= 3)
    {
        Status = EArachneMoveStatus::Stuck;
        Pawn->SetMoveDirection(FVector::ZeroVector);
        Pawn->SetSprint(false);
        return;
    }
    // Try something else: the floor if we were up somewhere, the other wall otherwise.
    Surface = Surface == EArachneSurface::Floor ? EArachneSurface::Wall : EArachneSurface::Floor;
    WallSide = -WallSide;
}

// =====================================================================================================================
// Steering
// =====================================================================================================================

bool UArachneNavigatorComponent::Trace(const FVector& From, const FVector& To, FHitResult& Hit) const
{
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_WorldStatic);
    Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ArachneNavigator), false, GetOwner());
    return GetWorld()->LineTraceSingleByObjectType(Hit, From, To, Objects, Params) && !Hit.bStartPenetrating;
}

EArachneSurface UArachneNavigatorComponent::ClassifyNormal(const FVector& Normal) const
{
    if (Normal.Z > .6) return EArachneSurface::Floor;
    if (Normal.Z < -.6) return EArachneSurface::Ceiling;
    return EArachneSurface::Wall;
}

EArachneSurface UArachneNavigatorComponent::CurrentSurface() const
{
    return Pawn ? ClassifyNormal(Pawn->SurfaceUp) : EArachneSurface::Floor;
}

EArachneSurface UArachneNavigatorComponent::SurfaceNear(const FVector& Location) const
{
    const FVector Dirs[6] = {-FVector::UpVector, FVector::UpVector, FVector::ForwardVector, -FVector::ForwardVector, FVector::RightVector, -FVector::RightVector};
    double Best = TNumericLimits<double>::Max();
    EArachneSurface Result = EArachneSurface::Floor;
    for (const FVector& D : Dirs)
    {
        FHitResult Hit;
        if (Trace(Location, Location + D * 250.0, Hit) && Hit.Distance < Best - 1.0)
        {
            Best = Hit.Distance;
            Result = ClassifyNormal(Hit.ImpactNormal);
        }
    }
    return Result;
}

bool UArachneNavigatorComponent::ScanSurface(EArachneSurface Kind, const FVector& TravelDir, float Side, FVector& OutPoint, FVector& OutNormal) const
{
    const FVector Pos = Pawn->GetActorLocation();
    FVector Dir = -FVector::UpVector;
    if (Kind == EArachneSurface::Ceiling) Dir = FVector::UpVector;
    if (Kind == EArachneSurface::Wall)
    {
        const FVector Travel = TravelDir.IsNearlyZero() ? Pawn->GetFacing().GetSafeNormal2D() : TravelDir;
        Dir = FVector::CrossProduct(FVector::UpVector, Travel).GetSafeNormal() * Side;
    }
    FHitResult Hit;
    if (!Trace(Pos, Pos + Dir * SurfaceScanDistance, Hit) || ClassifyNormal(Hit.ImpactNormal) != Kind) return false;
    OutPoint = Hit.ImpactPoint;
    OutNormal = Hit.ImpactNormal;
    return true;
}

FVector UArachneNavigatorComponent::ComputeSteer(const FVector& Target) const
{
    const FVector Pos = Pawn->GetActorLocation();
    const FVector Up = Pawn->SurfaceUp;
    const FVector ToTarget = Target - Pos;
    const FVector Travel2D = ToTarget.GetSafeNormal2D();
    const bool bFinalApproach = RouteIndex == Route.Num() - 1 && FVector::Dist2D(Pos, Target) < GoalSurfaceDistance;
    const EArachneSurface Want = bFinalApproach ? GoalSurface : Surface;

    FVector Point, Normal;
    bool bFound = ScanSurface(Want, Travel2D, WallSide, Point, Normal);
    if (!bFound && Want == EArachneSurface::Wall) bFound = ScanSurface(Want, Travel2D, -WallSide, Point, Normal);
    if (!bFound) return ToTarget.GetSafeNormal();   // nothing of that kind around: head straight for the target

    // Already on the chosen surface: travel along it.
    if (FVector::DotProduct(Up, Normal) > .8)
    {
        if (Want != EArachneSurface::Wall || bFinalApproach) return ToTarget.GetSafeNormal();
        // Along a wall: go horizontally and hold a cruising height instead of sliding down to the target's height.
        FHitResult Floor;
        const double FloorZ = Trace(Pos, Pos - FVector::UpVector * 1000.0, Floor) ? Floor.ImpactPoint.Z : Pos.Z - WallCruiseHeight;
        const double Vertical = FMath::Clamp((FloorZ + WallCruiseHeight - Pos.Z) / 150.0, -1.0, 1.0);
        FVector Along = FVector::VectorPlaneProject(ToTarget, Up);
        Along.Z = 0.0;
        return (Along.GetSafeNormal() + FVector(0, 0, Vertical * .7)).GetSafeNormal();
    }

    // Not there yet: steer at the surface; its corner arc will carry the body onto it.
    FVector Toward = FVector::VectorPlaneProject(Point - Pos, Up);
    if (Toward.Size() < 40.0)
    {
        // The surface is straight above / below (floor <-> ceiling): go via a wall.
        FVector WallPoint, WallNormal;
        if (ScanSurface(EArachneSurface::Wall, Travel2D, WallSide, WallPoint, WallNormal) ||
            ScanSurface(EArachneSurface::Wall, Travel2D, -WallSide, WallPoint, WallNormal))
            Toward = FVector::VectorPlaneProject(WallPoint - Pos, Up);
    }
    const FVector Along = FVector::VectorPlaneProject(ToTarget, Up).GetSafeNormal();
    return (Toward.GetSafeNormal() * .8 + Along * .35).GetSafeNormal();
}

// =====================================================================================================================
// Debug
// =====================================================================================================================

bool UArachneNavigatorComponent::IsDebugOn() const
{
    return bDebug || ArachneDebug::IsEnabled();
}

void UArachneNavigatorComponent::DrawDebug() const
{
    const UWorld* World = GetWorld();
    FVector Previous = Pawn->GetActorLocation();
    for (int32 I = RouteIndex; I < Route.Num(); ++I)
    {
        DrawDebugLine(World, Previous, Route[I], bSprint ? FColor::Red : FColor::Green, false, 0.f, 0, 2.f);
        DrawDebugSphere(World, Route[I], I == Route.Num() - 1 ? FinalRadius : PassRadius, 10, FColor(80, 200, 80), false, 0.f, 0, .5f);
        Previous = Route[I];
    }
    static const TCHAR* Names[] = {TEXT("floor"), TEXT("wall"), TEXT("ceiling")};
    const FString Text = FString::Printf(TEXT("%s%s%s"), Names[static_cast<int32>(Surface)], bPaused ? TEXT(" | pause") : TEXT(""), bCrab ? TEXT(" | crab") : TEXT(""));
    DrawDebugString(World, Pawn->GetActorLocation() + Pawn->SurfaceUp * 90.0, Text, nullptr, FColor::White, 0.f, true, 1.f);
}
