#pragma once
#include "CoreMinimal.h"
#include "ArachneAITypes.generated.h"

/** What the brain is doing. */
UENUM(BlueprintType)
enum class EArachneState : uint8
{
    Patrol,        // walking between patrol waypoints
    Camp,          // going to / frozen in a camp point, waiting for prey
    Investigate,   // checking the place something was heard
    Hunt,          // prey confirmed: sprint after it
    Attack,        // in reach: wind-up before the grab
    Search,        // lost the prey: checks around the last known position
    Feeding        // prey caught
};

/** Which sense produced a stimulus. */
UENUM(BlueprintType)
enum class EArachneSense : uint8
{
    None,
    Sight,     // movement in the eye cone, needs line of sight, builds up over time
    Hearing,   // footsteps / noises, travel through walls at reduced range
    Touch      // air movement / vibration right next to the body, any direction
};

/** One thing the spider noticed. */
USTRUCT(BlueprintType)
struct FArachneStimulus
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="Stimulus") EArachneSense Sense = EArachneSense::None;
    UPROPERTY(BlueprintReadOnly, Category="Stimulus") FVector Location = FVector::ZeroVector;
    /** Awareness added (0..1). */
    UPROPERTY(BlueprintReadOnly, Category="Stimulus") float Strength = 0.f;
    /** True when the sense pins the prey down exactly (seen long enough, touched). Only confirmed stimuli start a hunt. */
    UPROPERTY(BlueprintReadOnly, Category="Stimulus") bool bConfirmed = false;
    UPROPERTY() TWeakObjectPtr<AActor> Source;
};
