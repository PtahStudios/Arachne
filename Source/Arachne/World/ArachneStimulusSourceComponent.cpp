#include "World/ArachneStimulusSourceComponent.h"
#include "World/ArachneStimulusSubsystem.h"
#include "Core/ArachneDebug.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "DrawDebugHelpers.h"

UArachneStimulusSourceComponent::UArachneStimulusSourceComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UArachneStimulusSourceComponent::BeginPlay()
{
    Super::BeginPlay();
    if (UArachneStimulusSubsystem* Stimuli = GetWorld()->GetSubsystem<UArachneStimulusSubsystem>()) Stimuli->RegisterSource(this);
}

void UArachneStimulusSourceComponent::EndPlay(const EEndPlayReason::Type Reason)
{
    if (UWorld* World = GetWorld())
        if (UArachneStimulusSubsystem* Stimuli = World->GetSubsystem<UArachneStimulusSubsystem>()) Stimuli->UnregisterSource(this);
    Super::EndPlay(Reason);
}

float UArachneStimulusSourceComponent::GetVisibleDistance() const
{
    const float Base = bFlashlight ? FlashlightVisibleDistance : VisibleDistance;
    return Base * (bSprinting && GetCurrentNoiseRadius() > 0.f ? SprintVisibilityMultiplier : 1.f);
}

float UArachneStimulusSourceComponent::GetCurrentNoiseRadius() const
{
    const AActor* Owner = GetOwner();
    const ACharacter* Character = Cast<ACharacter>(Owner);
    if (!Owner || bCaught) return 0.f;
    if (Character && !Character->GetCharacterMovement()->IsMovingOnGround()) return 0.f;
    if (Owner->GetVelocity().Size2D() < SilentSpeed) return 0.f;
    return bSprinting ? SprintNoiseRadius : WalkNoiseRadius;
}

void UArachneStimulusSourceComponent::MakeNoiseAt(FVector Location, float Radius)
{
    if (UArachneStimulusSubsystem* Stimuli = GetWorld()->GetSubsystem<UArachneStimulusSubsystem>())
        Stimuli->ReportNoise(GetOwner(), Location, Radius);
}

void UArachneStimulusSourceComponent::GetSensePoints(TArray<FVector>& Out) const
{
    Out.Reset();
    const AActor* Owner = GetOwner();
    if (!Owner) return;
    FVector Eyes;
    FRotator EyesRotation;
    Owner->GetActorEyesViewPoint(Eyes, EyesRotation);
    Out.Add(Eyes);
    Out.Add(Owner->GetActorLocation());
}

void UArachneStimulusSourceComponent::NotifyCaught(AActor* Predator)
{
    if (bCaught) return;
    bCaught = true;
    OnCaught.Broadcast(Predator);
}

void UArachneStimulusSourceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    LastStepAge += DeltaTime;

    // Footsteps: one noise event per step length travelled on the ground.
    const float Radius = GetCurrentNoiseRadius();
    if (Radius > 0.f)
    {
        StepAccumulator += GetOwner()->GetVelocity().Size2D() * DeltaTime;
        const float StepLength = bSprinting ? SprintStepLength : WalkStepLength;
        if (StepAccumulator >= StepLength)
        {
            StepAccumulator = 0.f;
            const FVector Feet = GetOwner()->GetActorLocation() - FVector(0, 0, GetOwner()->GetSimpleCollisionHalfHeight());
            MakeNoiseAt(Feet, Radius);
            LastStepRadius = Radius;
            LastStepAge = 0.f;
        }
    }
    else
    {
        StepAccumulator = 0.f;
    }
    if (IsDebugOn()) DrawDebug();
}

bool UArachneStimulusSourceComponent::IsDebugOn() const
{
    return bDebug || ArachneDebug::IsEnabled();
}

void UArachneStimulusSourceComponent::DrawDebug() const
{
    const UWorld* World = GetWorld();
    const FVector Feet = GetOwner()->GetActorLocation() - FVector(0, 0, GetOwner()->GetSimpleCollisionHalfHeight() - 3.0);
    const FVector X(1, 0, 0), Y(0, 1, 0);
    // Blue: distance from which you can be seen.
    DrawDebugCircle(World, Feet, GetVisibleDistance(), 64, FColor(60, 140, 255), false, 0.f, 0, 2.f, X, Y, false);
    // Yellow / red: how far your footsteps carry right now.
    const float Noise = GetCurrentNoiseRadius();
    if (Noise > 0.f) DrawDebugCircle(World, Feet, Noise, 48, bSprinting ? FColor::Red : FColor::Yellow, false, 0.f, 0, 2.f, X, Y, false);
    // Flash of the last footstep.
    if (LastStepAge < .35f) DrawDebugCircle(World, Feet, LastStepRadius * (LastStepAge / .35f), 48, FColor::Orange, false, 0.f, 0, 4.f, X, Y, false);
}
