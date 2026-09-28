#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "ArachneStimulusSubsystem.generated.h"

class UArachneStimulusSourceComponent;

/** A sound / vibration in the world. Radius = distance at which it can be heard with nothing in between. */
struct FArachneNoise
{
    FVector Location = FVector::ZeroVector;
    float Radius = 0.f;
    TWeakObjectPtr<AActor> Instigator;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FArachneNoiseSignature, const FArachneNoise&);

/**
 * Meeting point between prey and predators: prey registers its stimulus source here and reports noises,
 * the spider's senses listen. Keeps the player and the spider from knowing each other's classes.
 */
UCLASS()
class ARACHNE_API UArachneStimulusSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    void RegisterSource(UArachneStimulusSourceComponent* Source);
    void UnregisterSource(UArachneStimulusSourceComponent* Source);
    /** All live prey stimulus sources. */
    void GetSources(TArray<UArachneStimulusSourceComponent*>& Out) const;

    /** Emit a noise. Anything listening within Radius (reduced through walls) may hear it. */
    UFUNCTION(BlueprintCallable, Category="Arachne")
    void ReportNoise(AActor* Instigator, FVector Location, float Radius);

    FArachneNoiseSignature OnNoise;

private:
    TArray<TWeakObjectPtr<UArachneStimulusSourceComponent>> Sources;
};
