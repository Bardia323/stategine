#include "sg/render/Defaults.hpp"
namespace sg::render {
void standard_look(LookState &l, const Quality &q) {
    l.uniform(passes::scene, "uFogColor", 0.05, 0.06, 0.09)
        .uniform(passes::scene, "uFogDensity", 0.018)
        .uniform(passes::scene, "uFogFull", 0.0)
        .uniform(passes::scene, "uSky", 0.10, 0.13, 0.20)
        .uniform(passes::scene, "uGround", 0.14, 0.10, 0.07)
        .uniform(passes::scene, "uAmbient", 0.55)
        .uniform(passes::scene, "uShadowSoft", 1.0)
        .uniform(passes::scene, "uShadowFloor", 0.0)
        .uniform(passes::scene, "uWind", 0.0)
        .uniform(passes::scene, "uStars", 0.0)
        .uniform(passes::scene, "uClouds", 0.0)
        .uniform(passes::scene, "uCloudColor", 1.0, 0.95, 0.92)
        .uniform(passes::scene, "uCloudShade", 0.55, 0.52, 0.62)
        .uniform(passes::scene, "uSkyTop", 0.20, 0.40, 0.75)
        .uniform(passes::scene, "uSkyHorizon", 0.72, 0.78, 0.84)
        .setting(passes::scene, "clear.x", 0.012)
        .setting(passes::scene, "clear.y", 0.014)
        .setting(passes::scene, "clear.z", 0.022)
        .uniform(passes::bright, "uThreshold", q.bloom_threshold)
        .setting(passes::blur, "passes", q.bloom_passes)
        .uniform(passes::composite, "uBloomStrength", q.bloom_strength)
        .uniform(passes::composite, "uExposure", q.exposure)
        .uniform(passes::composite, "uTint", 1.0, 1.0, 1.0)
        .uniform(passes::composite, "uSaturation", 1.0)
        .uniform(passes::composite, "uVignette", 0.55)
        .uniform(passes::composite, "uGrain", 0.015)
        .uniform(passes::composite, "uRays", 0.0)
        .uniform(passes::composite, "uRayCut", 1.0)
        .uniform(passes::composite, "uRaySpread", 0.3)
        .uniform(passes::composite, "uRayColor", 1.0, 1.0, 1.0)
        .uniform(passes::composite, "uRayDir", 0.0, 1.0, 0.0)
        .uniform(passes::composite, "uCamFwd", 0.0, 0.0, -1.0)
        .uniform(passes::composite, "uTanHalf", 0.7);
}

} // namespace sg::render
