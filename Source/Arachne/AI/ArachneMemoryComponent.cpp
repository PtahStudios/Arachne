#include "AI/ArachneMemoryComponent.h"
#include "Core/ArachneDebug.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

UArachneMemoryComponent::UArachneMemoryComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UArachneMemoryComponent::ReportStimulus(const FArachneStimulus& S)
{
    Awareness = FMath::Clamp(Awareness + S.Strength, 0.f, 1.f);
    LastKnownLocation = S.Location;
    bHasLastKnown = true;
    LastSense = S.Sense;
    TimeSinceStimulus = 0.f;
    ++StimulusSerial;
    if (S.Source.IsValid()) Prey = S.Source;
    if (S.bConfirmed)
    {
        TimeSinceConfirmed = 0.f;
        AddTrailPoint(S.Location);
    }
    AddHeat(S.Location, S.bConfirmed ? 1.f : .35f);
}

void UArachneMemoryComponent::ForgetContact()
{
    Awareness = 0.f;
    bHasLastKnown = false;
    LastSense = EArachneSense::None;
    TimeSinceStimulus = TimeSinceConfirmed = 1e6f;
    Trail.Reset();
}

void UArachneMemoryComponent::AddTrailPoint(const FVector& Location)
{
    if (Trail.Num() && FVector::DistSquared(Trail.Last().Location, Location) < FMath::Square(TrailSpacing))
    {
        Trail.Last().Time = Clock;   // still around the same spot: refresh it
        return;
    }
    Trail.Add({Location, Clock});
    if (Trail.Num() > MaxTrailPoints) Trail.RemoveAt(0);
}

void UArachneMemoryComponent::AddHeat(const FVector& Location, float Amount)
{
    for (FArachneHeatSpot& Spot : Heat)
    {
        if (FVector::DistSquared(Spot.Location, Location) < FMath::Square(HeatMergeRadius))
        {
            Spot.Weight += Amount * .1f;   // continuous stimuli (sight every frame) must not explode the weight
            Spot.Location = FMath::Lerp(Spot.Location, Location, .05);
            return;
        }
    }
    Heat.Add({Location, Amount});
}

float UArachneMemoryComponent::GetHeatAt(const FVector& Location, float Radius) const
{
    float Sum = 0.f;
    for (const FArachneHeatSpot& Spot : Heat)
    {
        const float D = static_cast<float>(FVector::Dist(Spot.Location, Location));
        if (D < Radius) Sum += Spot.Weight * (1.f - D / Radius);
    }
    return Sum;
}

void UArachneMemoryComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    Clock += DeltaTime;
    TimeSinceStimulus += DeltaTime;
    TimeSinceConfirmed += DeltaTime;
    if (TimeSinceStimulus > AwarenessHoldTime) Awareness = FMath::Max(0.f, Awareness - AwarenessDecay * DeltaTime);

    Trail.RemoveAll([this](const FArachneTrailPoint& P) { return Clock - P.Time > TrailLifetime; });
    const float Keep = FMath::Pow(FMath::Max(0.f, 1.f - HeatDecayPerMinute), DeltaTime / 60.f);
    for (FArachneHeatSpot& Spot : Heat) Spot.Weight *= Keep;
    Heat.RemoveAll([](const FArachneHeatSpot& Spot) { return Spot.Weight < .02f; });
    if (bDebug || ArachneDebug::IsEnabled()) DrawDebug();
}

void UArachneMemoryComponent::DrawDebug() const
{
    const UWorld* World = GetWorld();
    if (bHasLastKnown) DrawDebugSphere(World, LastKnownLocation, 30.f, 10, FColor::Red, false, 0.f, 0, 2.f);
    for (int32 I = 0; I < Trail.Num(); ++I)
    {
        DrawDebugPoint(World, Trail[I].Location, 10.f, FColor(255, 80, 80), false, 0.f);
        if (I > 0) DrawDebugLine(World, Trail[I - 1].Location, Trail[I].Location, FColor(255, 80, 80), false, 0.f, 0, 1.f);
    }
    for (const FArachneHeatSpot& Spot : Heat)
        DrawDebugCircle(World, Spot.Location, 40.f + Spot.Weight * 60.f, 24, FColor(255, 120, 0), false, 0.f, 0, 1.5f, FVector(1, 0, 0), FVector(0, 1, 0), false);
}
