#include "AI/ArachneNavigatorComponent.h"
#include "Creature/ArachnePawn.h"
#include "World/ArachneWaypointSubsystem.h"
#include "World/ArachneDoorway.h"
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
    BuildRoute(Points);
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
    SetRoute(TArray<FVector>{Goal}, AcceptRadius, bInSprint);
}

void UArachneNavigatorComponent::BuildRoute(const TArray<FVector>& Points)
{
    // A crossing in progress is finished first, whatever the new route says (chases re-plan every fraction of a second).
    AArachneDoorway* Crossing = DoorPhase == EArachneDoorPhase::Cross ? ActiveDoor.Get() : nullptr;
    if (!Crossing) EndDoor();
    Route.Reset();
    RouteDoors.Reset();
    RouteTight.Reset();

    TArray<AArachneDoorway*> Doors;
    if (UArachneWaypointSubsystem* Waypoints = GetWorld()->GetSubsystem<UArachneWaypointSubsystem>()) Waypoints->GetDoorways(Doors);
    auto DoorAt = [&Doors](const FVector& P) -> AArachneDoorway*
    {
        for (AArachneDoorway* D : Doors)
            if (FVector::Dist2D(P, D->GetPassageCenter()) < 60.0 && FMath::Abs(P.Z - D->GetPassageCenter().Z) < 120.0) return D;
        return nullptr;
    };
    auto Add = [this](const FVector& P, AArachneDoorway* Door)
    {
        if (Door && RouteDoors.Num() && RouteDoors.Last().Get() == Door) return;   // same doorway twice in a row
        Route.Add(Door ? Door->GetPassageCenter() : P);
        RouteDoors.Add(Door);
    };

    if (Crossing) Add(Crossing->GetPassageCenter(), Crossing);
    FVector Previous = Pawn ? Pawn->GetActorLocation() : (Points.Num() ? Points[0] : FVector::ZeroVector);
    for (const FVector& P : Points)
    {
        // Doorways this segment passes through, in order along the segment.
        TArray<TPair<double, AArachneDoorway*>> Crossed;
        for (AArachneDoorway* D : Doors)
        {
            const FVector C = D->GetPassageCenter(), N = D->GetPassageDirection();
            const double A = FVector::DotProduct(Previous - C, N), B = FVector::DotProduct(P - C, N);
            if (A * B >= 0.0) continue;
            const double T = A / (A - B);
            const FVector X = Previous + (P - Previous) * T;
            const FVector Right = FVector::CrossProduct(FVector::UpVector, N);
            if (FMath::Abs(FVector::DotProduct(X - C, Right)) > D->GetHalfWidth() + 40.0 || FMath::Abs(X.Z - C.Z) > 150.0) continue;
            Crossed.Add({T, D});
        }
        Crossed.Sort([](const TPair<double, AArachneDoorway*>& A, const TPair<double, AArachneDoorway*>& B) { return A.Key < B.Key; });
        for (const TPair<double, AArachneDoorway*>& Entry : Crossed) Add(Entry.Value->GetPassageCenter(), Entry.Value);
        Add(P, DoorAt(P));
        Previous = P;
    }

    for (int32 I = 0; I < Route.Num(); ++I) RouteTight.Add(RouteDoors[I].IsValid() || IsTightPoint(Route[I]));
    if (RouteTight.Num() && !RouteDoors.Last().IsValid()) RouteTight.Last() = false;   // the goal may be up in a corner (camp)
}

void UArachneNavigatorComponent::EndDoor()
{
    if (Pawn) Pawn->EndPassage();
    DoorPhase = EArachneDoorPhase::None;
    ActiveDoor.Reset();
    DoorTimer = 0.f;
}

void UArachneNavigatorComponent::AdvanceRoute()
{
    if (RouteIndex >= Route.Num() - 1)
    {
        Status = EArachneMoveStatus::Arrived;
        Pawn->SetMoveDirection(FVector::ZeroVector);
        Pawn->SetFacingDirection(FVector::ZeroVector);
        Pawn->SetSprint(false);
        return;
    }
    ++RouteIndex;
    BeginSegment();
}

void UArachneNavigatorComponent::TickDoor(AArachneDoorway* Door, float Dt)
{
    const FVector Pos = Pawn->GetActorLocation();
    const FVector Center = Door->GetPassageCenter();
    const FVector Axis = Door->GetPassageDirection();
    if (ActiveDoor.Get() != Door || DoorPhase == EArachneDoorPhase::None)
    {
        EndDoor();
        ActiveDoor = Door;
        DoorPhase = EArachneDoorPhase::Approach;
        // Travel from our side to the other; the next route point decides when we are already in the plane.
        DoorTravel = FVector::DotProduct(Pos - Center, Axis) <= 0.0 ? Axis : -Axis;
        if (Route.IsValidIndex(RouteIndex + 1))
        {
            const double NextSide = FVector::DotProduct(Route[RouteIndex + 1] - Center, Axis);
            if (FMath::Abs(NextSide) > 30.0) DoorTravel = NextSide > 0.0 ? Axis : -Axis;
        }
        BestDistance = TNumericLimits<float>::Max();
        StuckTimer = 0.f;
    }
    DoorTimer += Dt;
    const double Approach = Door->ApproachDistance;
    const FVector Entry = Center - DoorTravel * Approach;
    const FVector Right = FVector::CrossProduct(FVector::UpVector, DoorTravel);
    const double Along = FVector::DotProduct(Pos - Center, DoorTravel);
    const double Lateral = FVector::DotProduct(Pos - Center, Right);
    Pawn->SetSprint(bSprint);

    if (DoorPhase == EArachneDoorPhase::Approach)
    {
        const bool bLinedUp = FMath::Abs(Lateral) < FMath::Max(15.0, Door->GetHalfWidth() * .5) && Along > -Approach - 60.0 && Along < 20.0 &&
                              FMath::Abs(Pos.Z - Center.Z) < 80.0;
        if (CurrentSurface() == EArachneSurface::Floor && bLinedUp)
        {
            DoorPhase = EArachneDoorPhase::Cross;
            DoorTimer = 0.f;
            Pawn->BeginPassage(Center, DoorTravel, Door->GetHalfWidth());
        }
        else
        {
            // Down to the floor if needed, then to the entry point; face the opening for the last stretch.
            Pawn->SetMoveDirection(ComputeSteer(Entry, true));
            Pawn->SetFacingDirection(FVector::Dist2D(Pos, Entry) < 150.0 ? DoorTravel : FVector::ZeroVector);
            UpdateStuck(static_cast<float>(FVector::Dist(Pos, Entry)), Dt);
            if (Status != EArachneMoveStatus::Moving) EndDoor();
            return;
        }
    }

    // Cross: straight through, pulled back onto the centre line.
    if (Along >= Approach - 10.0)
    {
        EndDoor();
        AdvanceRoute();
        return;
    }
    const FVector Steer = (DoorTravel - Right * (FMath::Clamp(Lateral / 30.0, -1.0, 1.0) * .8)).GetSafeNormal();
    Pawn->SetMoveDirection(Steer);
    Pawn->SetFacingDirection(DoorTravel);
    if (DoorTimer > 6.f)
    {
        EndDoor();
        Status = EArachneMoveStatus::Stuck;
        Pawn->SetMoveDirection(FVector::ZeroVector);
        Pawn->SetSprint(false);
    }
}

void UArachneNavigatorComponent::Stop()
{
    EndDoor();
    Route.Reset();
    RouteTight.Reset();
    RouteDoors.Reset();
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
    int32 CloseSurfaces = 0;
    GoalSurface = SurfaceNear(Route.Last(), &CloseSurfaces);
    bGoalInCorner = CloseSurfaces >= 2;
    SegmentWall = FVector::ZeroVector;
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

    if (AArachneDoorway* Door = RouteDoors.IsValidIndex(RouteIndex) ? RouteDoors[RouteIndex].Get() : nullptr)
    {
        TickDoor(Door, DeltaTime);
        if (IsDebugOn()) DrawDebug();
        return;
    }

    const FVector Pos = Pawn->GetActorLocation();
    const bool bLast = RouteIndex == Route.Num() - 1;
    float Dist = static_cast<float>(FVector::Dist(Pos, Route[RouteIndex]));
    // Intermediate points may be passed on any surface (walking over them on the ceiling counts) - but not through a floor.
    const bool bReached = bLast
        ? Dist <= FinalRadius
        : Dist <= PassRadius || (FVector::Dist2D(Pos, Route[RouteIndex]) <= PassRadius && FMath::Abs(Pos.Z - Route[RouteIndex].Z) < 350.0 &&
                                 UArachneWaypointSubsystem::HasClearPath(GetWorld(), Pos, Route[RouteIndex], 5.f));
    if (bReached)
    {
        AdvanceRoute();
        if (Status != EArachneMoveStatus::Moving || (RouteDoors.IsValidIndex(RouteIndex) && RouteDoors[RouteIndex].IsValid())) return;
        Dist = static_cast<float>(FVector::Dist(Pos, Route[RouteIndex]));
    }

    UpdateGait(DeltaTime);
    UpdateStuck(Dist, DeltaTime);
    if (Status != EArachneMoveStatus::Moving) return;

    // Travelling along a wall and the body turned a corner onto another wall: the wall ride is over, go down.
    if (Surface == EArachneSurface::Wall && CurrentSurface() == EArachneSurface::Wall)
    {
        if (SegmentWall.IsNearlyZero()) SegmentWall = Pawn->SurfaceUp;
        else if (FVector::DotProduct(SegmentWall, Pawn->SurfaceUp) < .8) Surface = EArachneSurface::Floor;
    }
    const bool bTight = IsNearTightPoint();
    FVector Steer = ComputeSteer(Route[RouteIndex], bTight);
    if (!bSprint && bErraticGait && !bTight)
    {
        const float Wobble = FMath::PerlinNoise1D(NoiseTime * WobbleFrequency) * WobbleAngle;
        Steer = FQuat(Pawn->SurfaceUp, FMath::DegreesToRadians(Wobble)).RotateVector(Steer);
        const float Speed = 1.f - SpeedJitter * (.5f + .5f * FMath::PerlinNoise1D(NoiseTime * .9f + 17.3f));
        Steer *= bPaused ? 0.f : Speed;
    }
    Pawn->SetMoveDirection(Steer);
    Pawn->SetSprint(bSprint);
    Pawn->SetFacingDirection(bCrab && !bSprint && !bTight ? CrabFacing : FVector::ZeroVector);
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

EArachneSurface UArachneNavigatorComponent::SurfaceNear(const FVector& Location, int32* OutCloseSurfaces) const
{
    const FVector Dirs[6] = {-FVector::UpVector, FVector::UpVector, FVector::ForwardVector, -FVector::ForwardVector, FVector::RightVector, -FVector::RightVector};
    double Best = TNumericLimits<double>::Max();
    EArachneSurface Result = EArachneSurface::Floor;
    int32 Close = 0;
    for (const FVector& D : Dirs)
    {
        FHitResult Hit;
        if (!Trace(Location, Location + D * 250.0, Hit)) continue;
        if (Hit.Distance < 150.f) ++Close;
        if (Hit.Distance < Best - 1.0)
        {
            Best = Hit.Distance;
            Result = ClassifyNormal(Hit.ImpactNormal);
        }
    }
    if (OutCloseSurfaces) *OutCloseSurfaces = Close;
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

bool UArachneNavigatorComponent::IsTightPoint(const FVector& Point) const
{
    FHitResult Hit;
    const FVector Z = FVector::UpVector;
    // Low headroom: door lintel, under a staircase, basement pipes...
    if (Trace(Point, Point + Z * 200.0, Hit)) return true;
    // Jambs / narrow passage on both sides.
    auto Narrow = [&](const FVector& Axis)
    {
        FHitResult A, B;
        return Trace(Point, Point + Axis * 80.0, A) && Trace(Point, Point - Axis * 80.0, B);
    };
    if (Narrow(FVector::ForwardVector) || Narrow(FVector::RightVector)) return true;
    // Stairs: the ground height changes right around the point.
    FHitResult Ground;
    if (!Trace(Point, Point - Z * 250.0, Ground)) return false;
    const FVector Around[4] = {FVector::ForwardVector, -FVector::ForwardVector, FVector::RightVector, -FVector::RightVector};
    for (const FVector& D : Around)
    {
        FHitResult G;
        const FVector P = Point + D * 60.0;
        if (Trace(P, P - Z * 250.0, G) && FMath::Abs(G.ImpactPoint.Z - Ground.ImpactPoint.Z) > 10.0) return true;
    }
    return false;
}

bool UArachneNavigatorComponent::IsNearTightPoint() const
{
    const FVector Pos = Pawn->GetActorLocation();
    if (RouteIndex > 0 && RouteTight.IsValidIndex(RouteIndex - 1) && RouteTight[RouteIndex - 1] && FVector::Dist(Pos, Route[RouteIndex - 1]) < 200.0)
        return true;   // just came through a doorway: clear it before climbing anything
    double Travel = 0.0;
    FVector Previous = Pos;
    for (int32 I = RouteIndex; I < Route.Num() && RouteTight.IsValidIndex(I); ++I)
    {
        Travel += FVector::Dist(Previous, Route[I]);
        if (Travel > TightLookahead) break;
        if (RouteTight[I]) return true;
        Previous = Route[I];
    }
    return false;
}

FVector UArachneNavigatorComponent::ComputeSteer(const FVector& Target, bool bTight) const
{
    const FVector Pos = Pawn->GetActorLocation();
    const FVector Up = Pawn->SurfaceUp;
    const FVector ToTarget = Target - Pos;
    const FVector Travel2D = ToTarget.GetSafeNormal2D();
    const bool bFinalApproach = RouteIndex == Route.Num() - 1 && FVector::Dist2D(Pos, Target) < GoalSurfaceDistance;
    // Goal in a corner (camp): no surface change on the final approach - the anchor glide takes it from wherever she is.
    if (bFinalApproach && bGoalInCorner && !bTight) return ToTarget.GetSafeNormal();
    const EArachneSurface Want = bTight ? EArachneSurface::Floor : bFinalApproach ? GoalSurface : Surface;

    // Already on a wall and a wall is wanted: keep this one (never ping-pong between two walls of a corner).
    if (Want == EArachneSurface::Wall && !bFinalApproach && !bTight && ClassifyNormal(Up) == EArachneSurface::Wall)
    {
        FHitResult Floor;
        const double FloorZ = Trace(Pos, Pos - FVector::UpVector * 1000.0, Floor) ? Floor.ImpactPoint.Z : Pos.Z - WallCruiseHeight;
        const double Vertical = FMath::Clamp((FloorZ + WallCruiseHeight - Pos.Z) / 150.0, -1.0, 1.0);
        FVector Along = FVector::VectorPlaneProject(ToTarget, Up);
        Along.Z = 0.0;
        return (Along.GetSafeNormal() + FVector(0, 0, Vertical * .7)).GetSafeNormal();
    }

    FVector Point, Normal;
    bool bFound = ScanSurface(Want, Travel2D, WallSide, Point, Normal);
    if (!bFound && Want == EArachneSurface::Wall) bFound = ScanSurface(Want, Travel2D, -WallSide, Point, Normal);
    if (!bFound) return ToTarget.GetSafeNormal();   // nothing of that kind around: head straight for the target

    // Already on the chosen surface: travel along it.
    if (FVector::DotProduct(Up, Normal) > .8)
    {
        if (Want != EArachneSurface::Wall || bFinalApproach || bTight) return ToTarget.GetSafeNormal();
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
        if (RouteDoors.IsValidIndex(I) && RouteDoors[I].IsValid())
            DrawDebugBox(World, Route[I], FVector(20.f), FColor(120, 220, 255), false, 0.f, 0, 2.f);
        else
            DrawDebugSphere(World, Route[I], I == Route.Num() - 1 ? FinalRadius : PassRadius, 10, FColor(80, 200, 80), false, 0.f, 0, .5f);
        Previous = Route[I];
    }
    static const TCHAR* Names[] = {TEXT("floor"), TEXT("wall"), TEXT("ceiling")};
    static const TCHAR* Doors[] = {TEXT(""), TEXT(" | door: approach"), TEXT(" | door: CROSS")};
    const FString Text = FString::Printf(TEXT("%s%s%s%s"), Names[static_cast<int32>(Surface)], bPaused ? TEXT(" | pause") : TEXT(""),
        bCrab ? TEXT(" | crab") : TEXT(""), Doors[static_cast<int32>(DoorPhase)]);
    DrawDebugString(World, Pawn->GetActorLocation() + Pawn->SurfaceUp * 90.0, Text, nullptr, FColor::White, 0.f, true, 1.f);
}
