#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AI/ArachneAITypes.h"
#include "ArachneMemoryComponent.generated.h"

/** A place where the prey was pinned down; the hunt follows these in order (it went through that door...). */
struct FArachneTrailPoint
{
    FVector Location = FVector::ZeroVector;
    float Time = 0.f;
};

/** Long-term: where the prey tends to be. Camp choice is pulled towards hot spots. */
struct FArachneHeatSpot
{
    FVector Location = FVector::ZeroVector;
    float Weight = 0.f;
};

/**
 * What Arachne knows. Senses write here, the brain reads.
 *  - Awareness 0..1: how sure she is something is there (decays after a quiet period)
 *  - Last known location + the trail of confirmed positions (short-term)
 *  - Heat spots: where she met the prey before (long-term, slow decay)
 */
UCLASS(ClassGroup=(Arachne), meta=(BlueprintSpawnableComponent))
class ARACHNE_API UArachneMemoryComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UArachneMemoryComponent();
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    /** Awareness lost per second once nothing has been sensed for AwarenessHoldTime. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Memory|Awareness") float AwarenessDecay = .12f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Memory|Awareness") float AwarenessHoldTime = 2.5f;
    /** Minimum spacing between trail points (cm) and how long they are kept (s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Memory|Trail") float TrailSpacing = 120.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Memory|Trail") float TrailLifetime = 20.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Memory|Trail") int32 MaxTrailPoints = 40;
    /** Stimuli closer than this to an existing heat spot reinforce it instead of adding a new one. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Memory|Heat") float HeatMergeRadius = 500.f;
    /** Fraction of heat forgotten per minute. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Memory|Heat") float HeatDecayPerMinute = .35f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Memory|Debug") bool bDebug = false;

    void ReportStimulus(const FArachneStimulus& Stimulus);
    /** Forget the current prey contact (after feeding / a reset). Heat stays. */
    void ForgetContact();

    UFUNCTION(BlueprintPure, Category="Memory") float GetAwareness() const { return Awareness; }
    UFUNCTION(BlueprintPure, Category="Memory") bool HasLastKnownLocation() const { return bHasLastKnown; }
    UFUNCTION(BlueprintPure, Category="Memory") FVector GetLastKnownLocation() const { return LastKnownLocation; }
    UFUNCTION(BlueprintPure, Category="Memory") EArachneSense GetLastSense() const { return LastSense; }
    UFUNCTION(BlueprintPure, Category="Memory") float GetTimeSinceStimulus() const { return TimeSinceStimulus; }
    UFUNCTION(BlueprintPure, Category="Memory") float GetTimeSinceConfirmed() const { return TimeSinceConfirmed; }
    UFUNCTION(BlueprintPure, Category="Memory") AActor* GetPrey() const { return Prey.Get(); }
    /** Incremented on every stimulus: lets the brain notice new information without events. */
    int32 GetStimulusSerial() const { return StimulusSerial; }
    const TArray<FArachneTrailPoint>& GetTrail() const { return Trail; }
    /** Sum of heat around a location (falls off linearly to 0 at Radius). */
    float GetHeatAt(const FVector& Location, float Radius) const;

    void DrawDebug() const;

private:
    void AddTrailPoint(const FVector& Location);
    void AddHeat(const FVector& Location, float Amount);

    float Awareness = 0.f;
    bool bHasLastKnown = false;
    FVector LastKnownLocation = FVector::ZeroVector;
    EArachneSense LastSense = EArachneSense::None;
    float TimeSinceStimulus = 1e6f;
    float TimeSinceConfirmed = 1e6f;
    float Clock = 0.f;
    int32 StimulusSerial = 0;
    TWeakObjectPtr<AActor> Prey;
    TArray<FArachneTrailPoint> Trail;
    TArray<FArachneHeatSpot> Heat;
};
