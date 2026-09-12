#pragma once

#include <cmath>
#include <algorithm>
#include "../player_profile.h"

inline float NormalizeAngle(float angle) {
    while (angle > 180.0f) angle -= 360.0f;
    while (angle < -180.0f) angle += 360.0f;
    return angle;
}

inline float AngleDifference(float a, float b) {
    return std::abs(NormalizeAngle(a - b));
}

inline float VectorDistance(const AcVec3& a, const AcVec3& b) {
    return (a - b).Length();
}

inline float DotProduct(const AcVec3& a, const AcVec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline float AngleBetweenVectors(AcVec3 a, AcVec3 b) {
    a.Normalize();
    b.Normalize();
    float dot = DotProduct(a, b);
    dot = std::clamp(dot, -1.0f, 1.0f);
    return std::acos(dot);
}

inline float DegreesToRadians(float degrees) {
    return degrees * (3.14159265358979323846f / 180.0f);
}

inline float RadiansToDegrees(float radians) {
    return radians * (180.0f / 3.14159265358979323846f);
}

inline float CalculateFOV(const AcAngle& viewAngle, const AcVec3& eyePos, const AcVec3& targetPos) {
    AcVec3 dir = targetPos - eyePos;
    dir.Normalize();

    float pitch = RadiansToDegrees(std::asin(-dir.z));
    float yaw = RadiansToDegrees(std::atan2(dir.y, dir.x));

    float dPitch = AngleDifference(viewAngle.pitch, pitch);
    float dYaw = AngleDifference(viewAngle.yaw, yaw);

    return std::sqrt(dPitch * dPitch + dYaw * dYaw);
}

// CS2AC body aim points: feet+8 / +46 / +64 (our position is eye ≈ feet+64).
inline float CalculateFOVToBody(const AcAngle& viewAngle, const AcVec3& eyePos, const AcVec3& enemyEyeOrOrigin) {
    // Treat stored position as eye; reconstruct approximate feet.
    AcVec3 feet = enemyEyeOrOrigin;
    feet.z -= 64.0f;
    static const float kHeights[] = {8.0f, 46.0f, 64.0f};
    float best = 180.0f;
    for (float h : kHeights) {
        AcVec3 p = feet;
        p.z += h;
        float fov = CalculateFOV(viewAngle, eyePos, p);
        if (fov < best)
            best = fov;
    }
    return best;
}
