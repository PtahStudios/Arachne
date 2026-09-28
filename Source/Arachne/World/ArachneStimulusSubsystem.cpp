#include "World/ArachneStimulusSubsystem.h"
#include "World/ArachneStimulusSourceComponent.h"

void UArachneStimulusSubsystem::RegisterSource(UArachneStimulusSourceComponent* Source)
{
    if (Source) Sources.AddUnique(Source);
}

void UArachneStimulusSubsystem::UnregisterSource(UArachneStimulusSourceComponent* Source)
{
    Sources.RemoveAll([Source](const TWeakObjectPtr<UArachneStimulusSourceComponent>& S) { return !S.IsValid() || S.Get() == Source; });
}

void UArachneStimulusSubsystem::GetSources(TArray<UArachneStimulusSourceComponent*>& Out) const
{
    Out.Reset();
    for (const TWeakObjectPtr<UArachneStimulusSourceComponent>& S : Sources)
        if (UArachneStimulusSourceComponent* Source = S.Get()) Out.Add(Source);
}

void UArachneStimulusSubsystem::ReportNoise(AActor* Instigator, FVector Location, float Radius)
{
    if (Radius <= 0.f) return;
    FArachneNoise Noise;
    Noise.Location = Location;
    Noise.Radius = Radius;
    Noise.Instigator = Instigator;
    OnNoise.Broadcast(Noise);
}
