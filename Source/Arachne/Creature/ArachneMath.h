#pragma once
#include "CoreMinimal.h"

/** Small vector helpers shared by the body simulation, the rig and the AI steering. */
namespace ArachneMath
{
    /** Frame-rate independent exponential smoothing factor. */
    inline double Damp(double Rate, double Dt) { return 1.0 - FMath::Exp(-Rate * Dt); }

    /** V projected onto the plane with the given normal and normalised; Fallback when degenerate. */
    inline FVector PlaneDir(const FVector& V, const FVector& Normal, const FVector& Fallback)
    {
        const FVector P = FVector::VectorPlaneProject(V, Normal).GetSafeNormal();
        return P.IsNearlyZero() ? Fallback : P;
    }

    /** Signed angle (radians) from From to To measured around Axis. */
    inline double SignedAngleAround(const FVector& From, const FVector& To, const FVector& Axis)
    {
        const FVector A = FVector::VectorPlaneProject(From, Axis).GetSafeNormal();
        const FVector B = FVector::VectorPlaneProject(To, Axis).GetSafeNormal();
        if (A.IsNearlyZero() || B.IsNearlyZero()) return 0.0;
        return FMath::Atan2(FVector::DotProduct(FVector::CrossProduct(A, B), Axis), FVector::DotProduct(A, B));
    }

    inline FVector SlerpNormal(const FVector& From, const FVector& To, double Alpha)
    {
        const FQuat Full = FQuat::FindBetweenNormals(From, To);
        return FQuat::Slerp(FQuat::Identity, Full, Alpha).RotateVector(From).GetSafeNormal();
    }

    /** Rigid rotation Q about a pivot point, as a transform applied after a component-space pose. */
    inline FTransform RotateAbout(const FVector& Pivot, const FQuat& Q)
    {
        return FTransform(Q, Pivot - Q.RotateVector(Pivot));
    }
}
