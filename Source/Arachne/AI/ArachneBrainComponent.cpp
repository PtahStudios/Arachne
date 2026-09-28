#include "AI/ArachneBrainComponent.h"
#include "AI/ArachneSensesComponent.h"
#include "AI/ArachneMemoryComponent.h"
#include "AI/ArachneNavigatorComponent.h"
#include "Creature/ArachnePawn.h"
#include "World/ArachneWaypoint.h"
#include "World/ArachneCampPoint.h"
#include "World/ArachneWaypointSubsystem.h"
#include "World/ArachneStimulusSourceComponent.h"
#include "Core/ArachneDebug.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

/** Drops leading route points while the one after them can be reached in a straight line. */
static void PullString(const UWorld* World, const FVector& From, TArray<FVector>& Points)
{
    while (Points.Num() > 1 && UArachneWaypointSubsystem::HasClearPath(World, From, Points[1])) Points.RemoveAt(0);
}

UArachneBrainComponent::UArachneBrainComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UArachneBrainComponent::BeginPlay()
{
    Super::BeginPlay();
    Pawn = Cast<AArachnePawn>(GetOwner());
    Senses = GetOwner()->FindComponentByClass<UArachneSensesComponent>();
    Memory = GetOwner()->FindComponentByClass<UArachneMemoryComponent>();
    Navigator = GetOwner()->FindComponentByClass<UArachneNavigatorComponent>();
    Waypoints = GetWorld()->GetSubsystem<UArachneWaypointSubsystem>();

    // Start with a short pause: waypoints register in their own BeginPlay, which may come after ours.
    State = EArachneState::Patrol;
    bWaiting = true;
    WaitTimer = .5f;
}

FString UArachneBrainComponent::GetStateName() const
{
    return StaticEnum<EArachneState>()->GetNameStringByValue(static_cast<int64>(State));
}

void UArachneBrainComponent::SetBrainEnabled(bool bEnabled)
{
    if (bBrainEnabled == bEnabled) return;
    bBrainEnabled = bEnabled;
    if (!bEnabled)
    {
        if (Navigator) Navigator->Stop();
        if (Pawn && Pawn->IsAnchored() && State == EArachneState::Camp) Pawn->EndAnchor();
    }
}

bool UArachneBrainComponent::IsDebugOn() const
{
    return bDebug || ArachneDebug::IsEnabled();
}

// =====================================================================================================================
// State machine
// =====================================================================================================================

void UArachneBrainComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!bBrainEnabled || !Pawn || !Memory || !Navigator || !Senses) return;
    StateTime += DeltaTime;

    EvaluateTransitions();
    switch (State)
    {
    case EArachneState::Patrol:      TickPatrol(DeltaTime); break;
    case EArachneState::Camp:        TickCamp(DeltaTime); break;
    case EArachneState::Investigate: TickInvestigate(DeltaTime); break;
    case EArachneState::Hunt:        TickHunt(DeltaTime); break;
    case EArachneState::Attack:      TickAttack(DeltaTime); break;
    case EArachneState::Search:      TickSearch(DeltaTime); break;
    case EArachneState::Feeding:     break;
    }
    if (IsDebugOn()) DrawDebug();
}

void UArachneBrainComponent::EvaluateTransitions()
{
    if (State == EArachneState::Attack || State == EArachneState::Feeding) return;

    // Confirmed contact (seen long enough / touched) -> hunt. This is the only way to start sprinting.
    const bool bContact = Memory->GetPrey() && Memory->GetTimeSinceConfirmed() < .3f;
    if (bContact && Memory->GetAwareness() >= HuntAwareness)
    {
        if (State != EArachneState::Hunt) EnterState(EArachneState::Hunt);
        return;
    }
    if (State == EArachneState::Hunt) return;

    // Ambush: anything sensed close to the camp point while she is frozen there.
    AArachneCampPoint* Camp = CurrentCamp.Get();
    if (State == EArachneState::Camp && bWaiting && Camp && Memory->GetPrey() && Memory->GetTimeSinceStimulus() < .3f &&
        FVector::Dist(Memory->GetLastKnownLocation(), Camp->GetArrivalLocation()) < Camp->AmbushRadius)
    {
        EnterState(EArachneState::Hunt);
        return;
    }

    // Something new was noticed: go and check.
    const int32 Serial = Memory->GetStimulusSerial();
    const bool bNew = Serial != SeenStimulusSerial;
    SeenStimulusSerial = Serial;
    if (!bNew || !Memory->HasLastKnownLocation()) return;
    const float Threshold = State == EArachneState::Camp ? CampBreakAwareness : InvestigateAwareness;
    if (Memory->GetAwareness() < Threshold) return;
    if (State == EArachneState::Investigate)
    {
        // Already on the way: only re-plan if the sound came from somewhere else.
        if (FVector::Dist(Memory->GetLastKnownLocation(), InvestigateTarget) > 250.0)
        {
            InvestigateTarget = Memory->GetLastKnownLocation();
            bWaiting = false;
            RouteTo(InvestigateTarget, InvestigateAcceptRadius, false);
        }
        return;
    }
    EnterState(EArachneState::Investigate);
}

void UArachneBrainComponent::ExitState(EArachneState OldState)
{
    if (OldState == EArachneState::Camp)
    {
        if (Pawn->IsAnchored()) Pawn->EndAnchor();
        Senses->SetSensitivity(1.f);
        LastCamp = CurrentCamp;
        CurrentCamp.Reset();
    }
    if (OldState != EArachneState::Attack) Pawn->SetStillness(0.f);
    Pawn->SetFacingDirection(FVector::ZeroVector);
}

void UArachneBrainComponent::EnterState(EArachneState NewState)
{
    const EArachneState OldState = State;
    ExitState(OldState);
    State = NewState;
    StateTime = 0.f;
    bWaiting = false;
    WaitTimer = 0.f;
    RepathTimer = 0.f;

    switch (NewState)
    {
    case EArachneState::Patrol:
        Navigator->Stop();
        bWaiting = true;
        WaitTimer = .3f;
        break;
    case EArachneState::Camp:
        if (AArachneCampPoint* Camp = CurrentCamp.Get()) RouteToWaypoint(Camp, false);
        break;
    case EArachneState::Investigate:
        InvestigateTarget = Memory->GetLastKnownLocation();
        RouteTo(InvestigateTarget, InvestigateAcceptRadius, false);
        break;
    case EArachneState::Hunt:
        break;   // TickHunt plans the chase straight away
    case EArachneState::Attack:
        Navigator->Stop();
        bGrabbed = false;
        if (AActor* Prey = Memory->GetPrey())
            if (UArachneStimulusSourceComponent* Source = Prey->FindComponentByClass<UArachneStimulusSourceComponent>()) Source->NotifyCaught(Pawn);
        break;
    case EArachneState::Search:
        BuildSearchQueue();
        if (!StartNextSearchLeg()) { bWaiting = true; WaitTimer = FMath::FRandRange(LookAroundTimeMin, LookAroundTimeMax); BeginLookAround(); }
        break;
    case EArachneState::Feeding:
        Navigator->Stop();
        break;
    }
    OnStateChanged.Broadcast(OldState, NewState);
}

// =====================================================================================================================
// States
// =====================================================================================================================

void UArachneBrainComponent::TickPatrol(float Dt)
{
    if (bWaiting)
    {
        WaitTimer -= Dt;
        if (WaitTimer > 0.f) return;
        bWaiting = false;
        Pawn->SetStillness(0.f);
        // After a stop, sometimes go and lie in wait instead.
        if (CurrentWaypoint.IsValid() && FMath::FRand() < CampChance)
        {
            if (AArachneCampPoint* Camp = PickCampPoint())
            {
                CurrentCamp = Camp;
                EnterState(EArachneState::Camp);
                return;
            }
        }
        PickNextPatrolPoint();
        return;
    }

    switch (Navigator->GetStatus())
    {
    case EArachneMoveStatus::Arrived:
        bWaiting = true;
        if (AArachneWaypoint* W = CurrentWaypoint.Get())
        {
            WaitTimer = FMath::FRandRange(W->WaitTimeMin, FMath::Max(W->WaitTimeMin, W->WaitTimeMax));
            RememberVisit(W);
        }
        else
        {
            WaitTimer = 1.f;
        }
        Pawn->SetStillness(PatrolStopStillness);
        break;
    case EArachneMoveStatus::Stuck:
        PickNextPatrolPoint();
        break;
    case EArachneMoveStatus::Idle:
        bWaiting = true;   // nothing to walk to (no waypoints yet): try again shortly
        WaitTimer = 1.f;
        break;
    case EArachneMoveStatus::Moving:
        break;
    }
}

void UArachneBrainComponent::TickCamp(float Dt)
{
    AArachneCampPoint* Camp = CurrentCamp.Get();
    if (!Camp) { EnterState(EArachneState::Patrol); return; }

    if (!bWaiting)
    {
        const EArachneMoveStatus Status = Navigator->GetStatus();
        if (Status == EArachneMoveStatus::Arrived)
        {
            // Glide into the corner, freeze, sharpen the senses.
            const FArachneAnchor Anchor = Camp->GetAnchor();
            Pawn->BeginAnchor(Anchor);
            Pawn->SetStillness(1.f);
            Senses->SetSensitivity(CampSensitivity);
            bWaiting = true;
            WaitTimer = Anchor.BlendTime + FMath::FRandRange(Camp->CampTimeMin, FMath::Max(Camp->CampTimeMin, Camp->CampTimeMax));
            RememberVisit(Camp);
        }
        else if (Status != EArachneMoveStatus::Moving)
        {
            EnterState(EArachneState::Patrol);
        }
        return;
    }

    WaitTimer -= Dt;
    if (WaitTimer <= 0.f)
    {
        CurrentWaypoint = Camp;
        EnterState(EArachneState::Patrol);
    }
}

void UArachneBrainComponent::TickInvestigate(float Dt)
{
    if (!bWaiting)
    {
        if (Navigator->GetStatus() == EArachneMoveStatus::Moving) return;
        bWaiting = true;
        WaitTimer = FMath::FRandRange(LookAroundTimeMin, LookAroundTimeMax);
        BeginLookAround();
        return;
    }
    UpdateLookAround(Dt);
    WaitTimer -= Dt;
    if (WaitTimer <= 0.f) EnterState(Memory->GetAwareness() >= InvestigateAwareness ? EArachneState::Search : EArachneState::Patrol);
}

void UArachneBrainComponent::TickHunt(float Dt)
{
    AActor* Prey = Memory->GetPrey();
    // Any sense keeps the chase alive: she follows the footsteps even without seeing.
    const float SinceContact = FMath::Min(Memory->GetTimeSinceConfirmed(), Memory->GetTimeSinceStimulus());
    if (!Prey || SinceContact > LoseTargetTime)
    {
        EnterState(EArachneState::Search);
        return;
    }
    const bool bSees = Memory->GetTimeSinceConfirmed() < .4f;
    if (bSees && FVector::Dist(Pawn->GetActorLocation(), Prey->GetActorLocation()) <= AttackRange)
    {
        EnterState(EArachneState::Attack);
        return;
    }
    RepathTimer -= Dt;
    if (RepathTimer > 0.f && Navigator->GetStatus() == EArachneMoveStatus::Moving) return;
    RepathTimer = ChaseRepathInterval;
    if (bSees) RouteTo(Prey->GetActorLocation(), AttackRange * .5f, true);
    else RouteAlongTrail();
}

void UArachneBrainComponent::TickAttack(float Dt)
{
    AActor* Prey = Memory->GetPrey();
    if (!Prey) { EnterState(EArachneState::Search); return; }
    if (bGrabbed) return;
    Pawn->SetFacingDirection(Prey->GetActorLocation() - Pawn->GetActorLocation());
    if (StateTime < AttackWindup) return;
    GrabPreyFace();
    bGrabbed = true;
    EnterState(EArachneState::Feeding);
}

void UArachneBrainComponent::TickSearch(float Dt)
{
    if (bWaiting)
    {
        UpdateLookAround(Dt);
        WaitTimer -= Dt;
        if (WaitTimer > 0.f) return;
        bWaiting = false;
        Pawn->SetFacingDirection(FVector::ZeroVector);
        Pawn->SetStillness(0.f);
        if (StateTime > SearchDuration || !StartNextSearchLeg()) EnterState(EArachneState::Patrol);
        return;
    }
    if (Navigator->GetStatus() == EArachneMoveStatus::Moving) return;
    bWaiting = true;
    WaitTimer = FMath::FRandRange(LookAroundTimeMin, LookAroundTimeMax);
    BeginLookAround();
}

// =====================================================================================================================
// Helpers
// =====================================================================================================================

void UArachneBrainComponent::RememberVisit(AArachneWaypoint* Waypoint)
{
    RecentWaypoints.Add(Waypoint);
    while (RecentWaypoints.Num() > AvoidRecentWaypoints) RecentWaypoints.RemoveAt(0);
}

void UArachneBrainComponent::PickNextPatrolPoint()
{
    AArachneWaypoint* Current = CurrentWaypoint.Get();
    auto IsRecent = [this](const AArachneWaypoint* W)
    {
        return RecentWaypoints.ContainsByPredicate([W](const TWeakObjectPtr<AArachneWaypoint>& R) { return R.Get() == W; });
    };

    // Prefer linked neighbours that were not visited lately, then any patrol point, then anything but the current one.
    TArray<AArachneWaypoint*> Candidates;
    if (Waypoints && Current) Waypoints->GetNeighbours(Current, Candidates);
    Candidates.RemoveAll([&](const AArachneWaypoint* W) { return W->IsCampPoint() || W == Current || IsRecent(W); });
    if (Candidates.Num() == 0 && Waypoints)
    {
        Waypoints->GetPatrolPoints(Candidates);
        TArray<AArachneWaypoint*> Fresh = Candidates.FilterByPredicate([&](const AArachneWaypoint* W) { return W != Current && !IsRecent(W); });
        if (Fresh.Num()) Candidates = Fresh;
        else Candidates.RemoveAll([Current](const AArachneWaypoint* W) { return W == Current; });
    }
    if (Candidates.Num() == 0)
    {
        Navigator->Stop();
        bWaiting = true;
        WaitTimer = 2.f;
        return;
    }
    AArachneWaypoint* Next = Candidates[FMath::RandRange(0, Candidates.Num() - 1)];
    CurrentWaypoint = Next;
    RouteToWaypoint(Next, false);
}

AArachneCampPoint* UArachneBrainComponent::PickCampPoint() const
{
    if (!Waypoints) return nullptr;
    TArray<AArachneCampPoint*> Camps;
    Waypoints->GetCampPoints(Camps);
    AArachneCampPoint* Best = nullptr;
    float BestScore = -1.f;
    for (AArachneCampPoint* Camp : Camps)
    {
        if (Camps.Num() > 1 && Camp == LastCamp.Get()) continue;
        // Random, pulled towards places where the prey has been sensed before.
        const float Score = FMath::FRand() + CampHeatPreference * Memory->GetHeatAt(Camp->GetArrivalLocation(), 1000.f);
        if (Score > BestScore) { BestScore = Score; Best = Camp; }
    }
    return Best;
}

void UArachneBrainComponent::RouteToWaypoint(AArachneWaypoint* Waypoint, bool bSprint)
{
    if (!Waypoint) return;
    const FVector Pos = Pawn->GetActorLocation();
    TArray<FVector> Points;
    TArray<AArachneWaypoint*> Path;
    if (Waypoints && Waypoints->FindPath(Pos, Waypoint, Path))
        for (const AArachneWaypoint* W : Path) Points.Add(W->GetArrivalLocation());
    else
        Points.Add(Waypoint->GetArrivalLocation());
    PullString(GetWorld(), Pos, Points);
    Navigator->SetRoute(Points, Waypoint->AcceptRadius, bSprint);
}

void UArachneBrainComponent::RouteTo(const FVector& Goal, float AcceptRadius, bool bSprint)
{
    const FVector Pos = Pawn->GetActorLocation();
    if (!Waypoints || UArachneWaypointSubsystem::HasClearPath(GetWorld(), Pos, Goal))
    {
        Navigator->MoveTo(Goal, AcceptRadius, bSprint);
        return;
    }
    // No straight line: through the waypoint graph to the waypoint nearest the goal, then on to the goal.
    TArray<FVector> Points;
    TArray<AArachneWaypoint*> Path;
    if (AArachneWaypoint* Near = Waypoints->FindNearestReachable(Goal, false))
        if (Waypoints->FindPath(Pos, Near, Path))
            for (const AArachneWaypoint* W : Path) Points.Add(W->GetArrivalLocation());
    Points.Add(Goal);
    PullString(GetWorld(), Pos, Points);
    Navigator->SetRoute(Points, AcceptRadius, bSprint);
}

void UArachneBrainComponent::RouteAlongTrail()
{
    const TArray<FArachneTrailPoint>& Trail = Memory->GetTrail();
    if (Trail.Num() == 0)
    {
        RouteTo(Memory->GetLastKnownLocation(), AttackRange * .5f, true);
        return;
    }
    // Follow the prey's path from the trail point closest to us, then to where it was heard last.
    const FVector Pos = Pawn->GetActorLocation();
    int32 Start = 0;
    for (int32 I = 1; I < Trail.Num(); ++I)
        if (FVector::DistSquared(Trail[I].Location, Pos) < FVector::DistSquared(Trail[Start].Location, Pos)) Start = I;
    TArray<FVector> Points;
    for (int32 I = Start; I < Trail.Num(); ++I) Points.Add(Trail[I].Location);
    if (Memory->GetTimeSinceStimulus() < Memory->GetTimeSinceConfirmed()) Points.Add(Memory->GetLastKnownLocation());
    PullString(GetWorld(), Pos, Points);
    Navigator->SetRoute(Points, AttackRange * .5f, true);
}

void UArachneBrainComponent::BuildSearchQueue()
{
    SearchQueue.Reset();
    if (!Memory->HasLastKnownLocation()) return;
    const FVector Center = Memory->GetLastKnownLocation();
    SearchQueue.Add(Center);
    if (!Waypoints) return;
    TArray<AArachneWaypoint*> Nearby;
    Waypoints->GetPatrolPoints(Nearby);
    Nearby.Sort([&Center](const AArachneWaypoint& A, const AArachneWaypoint& B)
    {
        return FVector::DistSquared(A.GetArrivalLocation(), Center) < FVector::DistSquared(B.GetArrivalLocation(), Center);
    });
    for (const AArachneWaypoint* W : Nearby)
    {
        if (SearchQueue.Num() > SearchSpots) break;
        if (FVector::Dist(W->GetArrivalLocation(), Center) > 200.0) SearchQueue.Add(W->GetArrivalLocation());
    }
}

bool UArachneBrainComponent::StartNextSearchLeg()
{
    if (SearchQueue.Num() == 0) return false;
    const FVector Next = SearchQueue[0];
    SearchQueue.RemoveAt(0);
    RouteTo(Next, InvestigateAcceptRadius, false);
    return true;
}

void UArachneBrainComponent::BeginLookAround()
{
    Pawn->SetStillness(.2f);
    LookTimer = 0.f;
}

void UArachneBrainComponent::UpdateLookAround(float Dt)
{
    LookTimer -= Dt;
    if (LookTimer > 0.f) return;
    LookTimer = FMath::FRandRange(.6f, 1.4f);
    const FVector Direction = FVector::VectorPlaneProject(FMath::VRand(), Pawn->SurfaceUp).GetSafeNormal();
    if (!Direction.IsNearlyZero()) Pawn->SetFacingDirection(Direction);
}

void UArachneBrainComponent::GrabPreyFace()
{
    AActor* Prey = Memory->GetPrey();
    if (!Prey) return;
    FVector Eyes;
    FRotator View;
    Prey->GetActorEyesViewPoint(Eyes, View);
    const FVector Forward = View.Vector();
    const FVector ScreenUp = FRotationMatrix(View).GetUnitAxis(EAxis::Z);

    // Belly towards the lens, head towards the top of the screen, legs grabbing a plane just in front of the eyes.
    FArachneAnchor Anchor;
    Anchor.bVirtualSurface = true;
    Anchor.VirtualPoint = Eyes + Forward * FaceGrabDistance;
    Anchor.VirtualNormal = Forward;
    Anchor.Body = FTransform(FRotationMatrix::MakeFromZX(Forward, ScreenUp).ToQuat(), Anchor.VirtualPoint + Forward * (Pawn->BodyHeight * .55f));
    Anchor.BlendTime = FaceGrabBlendTime;
    Pawn->BeginAnchor(Anchor);
}

void UArachneBrainComponent::DrawDebug() const
{
    const UWorld* World = GetWorld();
    const FString Text = FString::Printf(TEXT("%s  %.1fs  | awareness %.2f"), *GetStateName(), StateTime, Memory->GetAwareness());
    DrawDebugString(World, Pawn->GetActorLocation() + Pawn->SurfaceUp * 130.0, Text, nullptr, FColor::Orange, 0.f, true, 1.2f);
    if (const AArachneCampPoint* Camp = CurrentCamp.Get())
    {
        const FArachneAnchor Anchor = Camp->GetAnchor();
        DrawDebugCoordinateSystem(World, Anchor.Body.GetLocation(), Anchor.Body.Rotator(), 50.f, false, 0.f, 0, 1.5f);
        DrawDebugCircle(World, Camp->GetArrivalLocation(), Camp->AmbushRadius, 32, FColor(255, 60, 200), false, 0.f, 0, 1.f, FVector(1, 0, 0), FVector(0, 1, 0), false);
    }
    if (Waypoints) Waypoints->DrawDebug();
}
