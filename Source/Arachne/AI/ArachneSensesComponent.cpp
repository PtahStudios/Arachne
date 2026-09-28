#include "AI/ArachneSensesComponent.h"
#include "AI/ArachneMemoryComponent.h"
#include "AI/ArachneAITypes.h"
#include "Creature/ArachnePawn.h"
#include "World/ArachneStimulusSubsystem.h"
#include "World/ArachneStimulusSourceComponent.h"
#include "Core/ArachneDebug.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

UArachneSensesComponent::UArachneSensesComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UArachneSensesComponent::BeginPlay()
{
    Super::BeginPlay();
    Pawn = Cast<AArachnePawn>(GetOwner());
    Memory = GetOwner()->FindComponentByClass<UArachneMemoryComponent>();
    if (UArachneStimulusSubsystem* Stimuli = GetWorld()->GetSubsystem<UArachneStimulusSubsystem>())
        NoiseHandle = Stimuli->OnNoise.AddUObject(this, &UArachneSensesComponent::HandleNoise);
}

void UArachneSensesComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (UWorld* World = GetWorld())
        if (UArachneStimulusSubsystem* Stimuli = World->GetSubsystem<UArachneStimulusSubsystem>())
            Stimuli->OnNoise.Remove(NoiseHandle);
    Super::EndPlay(Reason);
}

bool UArachneSensesComponent::HasLineOfSight(const FVector& From, const FVector& To, const AActor* Target) const
{
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ArachneSight), false, GetOwner());
    FHitResult Hit;
    if (!GetWorld()->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params)) return true;
    return Target && Hit.GetActor() == Target;
}

void UArachneSensesComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    bSeeingPrey = false;
    if (!Pawn || !Memory) return;
    if (UArachneStimulusSubsystem* Stimuli = GetWorld()->GetSubsystem<UArachneStimulusSubsystem>())
    {
        TArray<UArachneStimulusSourceComponent*> Sources;
        Stimuli->GetSources(Sources);
        for (UArachneStimulusSourceComponent* Source : Sources) SenseSource(Source, DeltaTime);
    }
    if (IsDebugOn()) DrawDebug();
}

void UArachneSensesComponent::SenseSource(UArachneStimulusSourceComponent* Source, float Dt)
{
    if (!Source || !Source->CanBeSensed()) return;
    AActor* Prey = Source->GetOwner();
    const FVector Body = Pawn->GetActorLocation();

    // Touch: anything this close is felt through the hairs, whatever the direction.
    if (FVector::Dist(Body, Prey->GetActorLocation()) <= TouchRadius * Sensitivity && HasLineOfSight(Body, Prey->GetActorLocation(), Prey))
    {
        FArachneStimulus S;
        S.Sense = EArachneSense::Touch;
        S.Location = Prey->GetActorLocation();
        S.Strength = 1.f;
        S.bConfirmed = true;
        S.Source = Prey;
        Memory->ReportStimulus(S);
        return;
    }

    // Sight: cone + range + line of sight to the eyes or the body.
    const FVector Eye = Pawn->GetEyeLocation();
    const double Range = FMath::Min(SightRange * Sensitivity, Source->GetVisibleDistance());
    const double CosHalf = FMath::Cos(FMath::DegreesToRadians(SightHalfAngle));
    TArray<FVector> Points;
    Source->GetSensePoints(Points);
    for (const FVector& Point : Points)
    {
        const FVector To = Point - Eye;
        const double Dist = To.Size();
        if (Dist > Range || Dist < 1.0) continue;
        if (FVector::DotProduct(To / Dist, Pawn->GetFacing()) < CosHalf) continue;
        if (!HasLineOfSight(Eye, Point, Prey)) continue;

        bSeeingPrey = true;
        SeenPoint = Point;
        // Close = quick recognition, far = slow.
        const float BuildUp = SightBuildUpTime * static_cast<float>(.35 + .65 * Dist / FMath::Max(Range, 1.0));
        FArachneStimulus S;
        S.Sense = EArachneSense::Sight;
        S.Location = Prey->GetActorLocation();
        S.Strength = Dt / FMath::Max(BuildUp, .05f);
        S.bConfirmed = Memory->GetAwareness() + S.Strength >= SightConfirmAwareness;
        S.Source = Prey;
        Memory->ReportStimulus(S);
        return;
    }
}

void UArachneSensesComponent::HandleNoise(const FArachneNoise& Noise)
{
    if (!Pawn || !Memory || Noise.Instigator.Get() == GetOwner()) return;
    const FVector Ear = Pawn->GetActorLocation();
    double Radius = Noise.Radius * HearingScale * Sensitivity;
    FCollisionQueryParams Params(SCENE_QUERY_STAT(ArachneHearing), false, GetOwner());
    Params.AddIgnoredActor(Noise.Instigator.Get());
    FHitResult Hit;
    const bool bOccluded = GetWorld()->LineTraceSingleByChannel(Hit, Noise.Location + FVector(0, 0, 20), Ear, ECC_Visibility, Params);
    if (bOccluded) Radius *= OccludedHearingScale;
    const double Dist = FVector::Dist(Noise.Location, Ear);
    if (Dist > Radius) return;

    FArachneStimulus S;
    S.Sense = EArachneSense::Hearing;
    S.Location = Noise.Location;
    S.Strength = HearingAwareness * static_cast<float>(FMath::Sqrt(1.0 - Dist / Radius)) + .05f;
    S.bConfirmed = false;
    S.Source = Noise.Instigator;
    Memory->ReportStimulus(S);
    if (IsDebugOn()) DrawDebugLine(GetWorld(), Ear, Noise.Location, bOccluded ? FColor(140, 90, 0) : FColor::Yellow, false, 1.5f, 0, 1.5f);
}

bool UArachneSensesComponent::IsDebugOn() const
{
    return bDebug || ArachneDebug::IsEnabled();
}

void UArachneSensesComponent::DrawDebug() const
{
    const UWorld* World = GetWorld();
    const FVector Eye = Pawn->GetEyeLocation();
    const float Angle = FMath::DegreesToRadians(SightHalfAngle);
    DrawDebugCone(World, Eye, Pawn->GetFacing(), SightRange * Sensitivity, Angle, Angle, 20, bSeeingPrey ? FColor::Red : FColor(90, 200, 255), false, 0.f, 0, 1.f);
    DrawDebugSphere(World, Pawn->GetActorLocation(), TouchRadius * Sensitivity, 16, FColor(200, 120, 255), false, 0.f, 0, .5f);
    if (bSeeingPrey) DrawDebugLine(World, Eye, SeenPoint, FColor::Red, false, 0.f, 0, 2.f);
}
