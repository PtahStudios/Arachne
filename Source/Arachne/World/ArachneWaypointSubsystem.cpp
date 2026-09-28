#include "World/ArachneWaypointSubsystem.h"
#include "World/ArachneWaypoint.h"
#include "World/ArachneCampPoint.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

void UArachneWaypointSubsystem::Register(AArachneWaypoint* Waypoint)
{
    if (!Waypoint || Waypoints.Contains(Waypoint)) return;
    Waypoints.Add(Waypoint);
    bDirty = true;
}

void UArachneWaypointSubsystem::Unregister(AArachneWaypoint* Waypoint)
{
    Waypoints.RemoveAll([Waypoint](const TWeakObjectPtr<AArachneWaypoint>& W) { return !W.IsValid() || W.Get() == Waypoint; });
    bDirty = true;
}

bool UArachneWaypointSubsystem::HasClearPath(const UWorld* World, const FVector& From, const FVector& To, float Radius)
{
    if (!World) return false;
    FCollisionObjectQueryParams Objects;
    Objects.AddObjectTypesToQuery(ECC_WorldStatic);
    Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ArachneClearPath), false);
    FHitResult Hit;
    return !World->SweepSingleByObjectType(Hit, From, To, FQuat::Identity, Objects, FCollisionShape::MakeSphere(Radius), Params);
}

bool UArachneWaypointSubsystem::CanAutoLink(const AArachneWaypoint* A, const AArachneWaypoint* B)
{
    if (!A || !B || A == B || !A->bAutoLink || !B->bAutoLink) return false;
    const FVector PA = A->GetArrivalLocation(), PB = B->GetArrivalLocation();
    if (FVector::Dist(PA, PB) > FMath::Min(A->AutoLinkDistance, B->AutoLinkDistance)) return false;
    return HasClearPath(A->GetWorld(), PA, PB);
}

int32 UArachneWaypointSubsystem::IndexOf(const AArachneWaypoint* Waypoint) const
{
    for (int32 I = 0; I < Waypoints.Num(); ++I) if (Waypoints[I].Get() == Waypoint) return I;
    return INDEX_NONE;
}

void UArachneWaypointSubsystem::RebuildIfDirty() const
{
    if (!bDirty) return;
    bDirty = false;
    const int32 N = Waypoints.Num();
    Links.Reset();
    Links.SetNum(N);
    for (int32 I = 0; I < N; ++I)
    {
        const AArachneWaypoint* A = Waypoints[I].Get();
        if (!A) continue;
        for (int32 J = I + 1; J < N; ++J)
        {
            const AArachneWaypoint* B = Waypoints[J].Get();
            if (!B) continue;
            const bool bManual = A->ManualLinks.Contains(B) || B->ManualLinks.Contains(A);
            if (bManual || CanAutoLink(A, B)) { Links[I].Add(J); Links[J].Add(I); }
        }
    }
}

AArachneWaypoint* UArachneWaypointSubsystem::FindNearestReachable(const FVector& Location, bool bAllowCampPoints) const
{
    // Nearest first; the first one with a clear path wins.
    TArray<TPair<double, AArachneWaypoint*>> Sorted;
    for (const TWeakObjectPtr<AArachneWaypoint>& W : Waypoints)
    {
        AArachneWaypoint* Waypoint = W.Get();
        if (!Waypoint || (!bAllowCampPoints && Waypoint->IsCampPoint())) continue;
        Sorted.Add({FVector::DistSquared(Location, Waypoint->GetArrivalLocation()), Waypoint});
    }
    Sorted.Sort([](const TPair<double, AArachneWaypoint*>& A, const TPair<double, AArachneWaypoint*>& B) { return A.Key < B.Key; });
    for (const TPair<double, AArachneWaypoint*>& Entry : Sorted)
        if (HasClearPath(GetWorld(), Location, Entry.Value->GetArrivalLocation())) return Entry.Value;
    return Sorted.Num() ? Sorted[0].Value : nullptr;   // nothing visible: best guess is the closest one
}

bool UArachneWaypointSubsystem::FindPath(const FVector& From, const AArachneWaypoint* Goal, TArray<AArachneWaypoint*>& OutPath) const
{
    OutPath.Reset();
    RebuildIfDirty();
    const int32 GoalIndex = IndexOf(Goal);
    AArachneWaypoint* Entry = FindNearestReachable(From);
    const int32 StartIndex = IndexOf(Entry);
    if (GoalIndex == INDEX_NONE || StartIndex == INDEX_NONE) return false;

    // Dijkstra: the graph is a few dozen nodes.
    const int32 N = Waypoints.Num();
    TArray<double> Cost;
    TArray<int32> Previous;
    TArray<bool> Done;
    Cost.Init(TNumericLimits<double>::Max(), N);
    Previous.Init(INDEX_NONE, N);
    Done.Init(false, N);
    Cost[StartIndex] = 0.0;
    for (;;)
    {
        int32 Current = INDEX_NONE;
        for (int32 I = 0; I < N; ++I)
            if (!Done[I] && Cost[I] < TNumericLimits<double>::Max() && (Current == INDEX_NONE || Cost[I] < Cost[Current])) Current = I;
        if (Current == INDEX_NONE || Current == GoalIndex) break;
        Done[Current] = true;
        const AArachneWaypoint* A = Waypoints[Current].Get();
        if (!A) continue;
        for (const int32 Next : Links[Current])
        {
            const AArachneWaypoint* B = Waypoints[Next].Get();
            if (!B || Done[Next]) continue;
            const double NewCost = Cost[Current] + FVector::Dist(A->GetArrivalLocation(), B->GetArrivalLocation());
            if (NewCost < Cost[Next]) { Cost[Next] = NewCost; Previous[Next] = Current; }
        }
    }
    if (GoalIndex != StartIndex && Previous[GoalIndex] == INDEX_NONE) return false;
    for (int32 I = GoalIndex; I != INDEX_NONE; I = Previous[I]) OutPath.Insert(Waypoints[I].Get(), 0);
    return OutPath.Num() > 0;
}

void UArachneWaypointSubsystem::GetNeighbours(const AArachneWaypoint* Waypoint, TArray<AArachneWaypoint*>& Out) const
{
    Out.Reset();
    RebuildIfDirty();
    const int32 Index = IndexOf(Waypoint);
    if (Index == INDEX_NONE) return;
    for (const int32 Next : Links[Index]) if (AArachneWaypoint* W = Waypoints[Next].Get()) Out.Add(W);
}

void UArachneWaypointSubsystem::GetPatrolPoints(TArray<AArachneWaypoint*>& Out) const
{
    Out.Reset();
    for (const TWeakObjectPtr<AArachneWaypoint>& W : Waypoints)
        if (AArachneWaypoint* Waypoint = W.Get(); Waypoint && !Waypoint->IsCampPoint()) Out.Add(Waypoint);
}

void UArachneWaypointSubsystem::GetCampPoints(TArray<AArachneCampPoint*>& Out) const
{
    Out.Reset();
    for (const TWeakObjectPtr<AArachneWaypoint>& W : Waypoints)
        if (AArachneCampPoint* Camp = Cast<AArachneCampPoint>(W.Get())) Out.Add(Camp);
}

void UArachneWaypointSubsystem::DrawDebug() const
{
    RebuildIfDirty();
    const UWorld* World = GetWorld();
    for (int32 I = 0; I < Waypoints.Num(); ++I)
    {
        const AArachneWaypoint* A = Waypoints[I].Get();
        if (!A) continue;
        const FColor Color = A->IsCampPoint() ? FColor(255, 60, 200) : FColor(255, 170, 40);
        DrawDebugSphere(World, A->GetArrivalLocation(), 18.f, 8, Color, false, 0.f, 0, 1.f);
        for (const int32 J : Links[I])
            if (J > I)
                if (const AArachneWaypoint* B = Waypoints[J].Get())
                    DrawDebugLine(World, A->GetArrivalLocation(), B->GetArrivalLocation(), FColor(90, 90, 90), false, 0.f, 0, 1.f);
    }
}
