struct SandboxVertexInput
{
    float3 position : POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

struct SandboxVertexOutput
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float3 worldPosition : TEXCOORD1;
};

struct SandboxGBufferOutput
{
    float4 albedo : SV_TARGET0;
    float4 normal : SV_TARGET1;
    float4 material : SV_TARGET2;
};

struct SandboxFullscreenVertexInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD0;
};

struct SandboxFullscreenVertexOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

Texture2D gSandboxTexture : register(t0);
SamplerState gSandboxSampler : register(s0);
Texture2D gSsaoNormal : register(t0);
Texture2D gSsaoDepth : register(t1);
Texture2D gSsaoMaterial : register(t2);
SamplerState gSsaoSampler : register(s0);
Texture2D gLightingAlbedo : register(t0);
Texture2D gLightingNormal : register(t1);
Texture2D gLightingMaterial : register(t2);
Texture2D gLightingDepth : register(t3);
Texture2DArray gShadowMap : register(t4);
Texture2D gLightingSsao : register(t5);
SamplerState gLightingSampler : register(s0);
SamplerComparisonState gShadowSampler : register(s1);

cbuffer SandboxLightingPassData : register(b0)
{
    float3 gDirectionalLightDirection;
    float gDirectionalLightIntensity;
    float3 gDirectionalLightColor;
    float gAmbientIntensity;
    float2 gPointLightUv;
    float gPointLightRadius;
    float gPointLightIntensity;
    float3 gPointLightColor;
    float gDepthInfluence;
    float gAspectRatio;
    float gNormalStrength;
    float gMaterialInfluence;
    float gShadowEnabled;
    float3 gCameraPosition;
    float gShadowStrength;
    float3 gCameraForward;
    float gShadowDepthBias;
    row_major float4x4 gShadowWorldToTexture0;
    row_major float4x4 gShadowWorldToTexture1;
    float4 gShadowCascadeData;
    float4 gSsaoParameters;
};

cbuffer SandboxSsaoPassData : register(b0)
{
    float2 gSsaoInverseResolution;
    float gSsaoSampleRadius;
    float gSsaoWorldRadius;
    float gSsaoIntensity;
    float gSsaoPower;
    float gSsaoNormalBias;
    float gSsaoRotation;
};

cbuffer SandboxShadowPassData : register(b0)
{
    row_major float4x4 gShadowWorldToClip;
};

struct SandboxShadowVertexOutput
{
    float4 position : SV_POSITION;
};

SandboxVertexOutput SandboxVertexMain(SandboxVertexInput input)
{
    SandboxVertexOutput output;
    output.position = float4(input.position, 1.0f);
    output.color = input.color;
    output.uv = input.uv;
    output.worldPosition = input.position;
    return output;
}

float4 SandboxPixelMain(SandboxVertexOutput input) : SV_TARGET
{
    return gSandboxTexture.Sample(gSandboxSampler, input.uv) * input.color;
}

float4 SandboxTransparentPixelMain(SandboxVertexOutput input) : SV_TARGET
{
    const float4 surface = gSandboxTexture.Sample(gSandboxSampler, input.uv) * input.color;
    const float alpha = saturate(surface.a * 0.85f);
    const float3 tint = lerp(surface.rgb, float3(0.22f, 0.80f, 0.95f), 0.30f);
    return float4(tint, alpha);
}

SandboxGBufferOutput SandboxGBufferPixelMain(SandboxVertexOutput input)
{
    SandboxGBufferOutput output;
    const float4 albedo = gSandboxTexture.Sample(gSandboxSampler, input.uv) * input.color;
    output.albedo = albedo;
    output.normal = float4(0.5f, 0.5f, 1.0f, 1.0f);
    output.material = float4(input.worldPosition * 0.5f + 0.5f, albedo.a);
    return output;
}

SandboxFullscreenVertexOutput SandboxLightingVertexMain(SandboxFullscreenVertexInput input)
{
    SandboxFullscreenVertexOutput output;
    output.position = float4(input.position, 0.0f, 1.0f);
    output.uv = input.uv;
    return output;
}

float2 RotateSandboxOffset(float2 offset, float rotation)
{
    const float sine = sin(rotation);
    const float cosine = cos(rotation);
    return float2(
        offset.x * cosine - offset.y * sine,
        offset.x * sine + offset.y * cosine);
}

float SandboxSsaoPixelMain(SandboxFullscreenVertexOutput input) : SV_TARGET
{
    static const float2 kKernel[8] = {
        float2(1.0f, 0.0f),
        float2(-1.0f, 0.0f),
        float2(0.0f, 1.0f),
        float2(0.0f, -1.0f),
        float2(0.7071f, 0.7071f),
        float2(-0.7071f, 0.7071f),
        float2(0.7071f, -0.7071f),
        float2(-0.7071f, -0.7071f)
    };

    const float3 centerNormal = normalize((gSsaoNormal.Sample(gSsaoSampler, input.uv).xyz * 2.0f) - 1.0f);
    const float3 centerWorld = gSsaoMaterial.Sample(gSsaoSampler, input.uv).xyz * 2.0f - 1.0f;
    const float centerDepth = gSsaoDepth.Sample(gSsaoSampler, input.uv).r;
    const float2 safeInverseResolution = max(gSsaoInverseResolution, float2(1.0f / 4096.0f, 1.0f / 4096.0f));
    const float safeSampleRadius = max(gSsaoSampleRadius, 1.0f);
    const float safeWorldRadius = max(gSsaoWorldRadius, 0.001f);
    const float safePower = max(gSsaoPower, 0.001f);

    float occlusion = 0.0f;
    [unroll]
    for (int sampleIndex = 0; sampleIndex < 8; ++sampleIndex)
    {
        const float2 rotatedOffset = RotateSandboxOffset(kKernel[sampleIndex], gSsaoRotation);
        const float2 sampleUv = saturate(input.uv + rotatedOffset * safeSampleRadius * safeInverseResolution);
        const float3 sampleWorld = gSsaoMaterial.Sample(gSsaoSampler, sampleUv).xyz * 2.0f - 1.0f;
        const float3 sampleNormal = normalize((gSsaoNormal.Sample(gSsaoSampler, sampleUv).xyz * 2.0f) - 1.0f);
        const float sampleDepth = gSsaoDepth.Sample(gSsaoSampler, sampleUv).r;

        const float3 sampleDelta = sampleWorld - centerWorld;
        const float sampleDistance = length(sampleDelta);
        const float3 sampleDirection = sampleDistance > 0.0001f ? sampleDelta / sampleDistance : float3(0.0f, 0.0f, 0.0f);
        const float hemisphereWeight = saturate(dot(centerNormal, sampleDirection) - gSsaoNormalBias);
        const float rangeWeight = saturate(1.0f - sampleDistance / safeWorldRadius);
        const float depthWeight = saturate(1.0f - abs(sampleDepth - centerDepth) * 6.0f);
        const float normalWeight = saturate(dot(centerNormal, sampleNormal));
        occlusion += hemisphereWeight * rangeWeight * depthWeight * normalWeight;
    }

    const float normalizedOcclusion = occlusion / 8.0f;
    const float ambientOcclusion = saturate(1.0f - normalizedOcclusion * gSsaoIntensity);
    return pow(ambientOcclusion, safePower);
}

float SampleSandboxShadow(float3 worldPosition)
{
    if (gShadowEnabled < 0.5f || gShadowCascadeData.z < 0.5f)
    {
        return 1.0f;
    }

    const float3 safeCameraForward = normalize(
        abs(gCameraForward.x) + abs(gCameraForward.y) + abs(gCameraForward.z) > 0.0001f
            ? gCameraForward
            : float3(0.0f, 0.0f, 1.0f));
    const float viewDepth = dot(worldPosition - gCameraPosition, safeCameraForward);

    float cascadeIndex = 0.0f;
    if (gShadowCascadeData.z > 1.5f && viewDepth > gShadowCascadeData.x)
    {
        cascadeIndex = 1.0f;
    }

    row_major float4x4 shadowMatrix = gShadowWorldToTexture0;
    if (cascadeIndex > 0.5f)
    {
        shadowMatrix = gShadowWorldToTexture1;
    }
    const float4 shadowCoordinate = mul(float4(worldPosition, 1.0f), shadowMatrix);
    if (shadowCoordinate.x < 0.0f
        || shadowCoordinate.x > 1.0f
        || shadowCoordinate.y < 0.0f
        || shadowCoordinate.y > 1.0f
        || shadowCoordinate.z <= 0.0f
        || shadowCoordinate.z >= 1.0f)
    {
        return 1.0f;
    }

    return gShadowMap.SampleCmpLevelZero(
        gShadowSampler,
        float3(shadowCoordinate.xy, cascadeIndex),
        shadowCoordinate.z - gShadowDepthBias);
}

float4 SandboxLightingPixelMain(SandboxFullscreenVertexOutput input) : SV_TARGET
{
    const float4 albedo = gLightingAlbedo.Sample(gLightingSampler, input.uv);
    const float4 encodedNormal = gLightingNormal.Sample(gLightingSampler, input.uv);
    const float4 material = gLightingMaterial.Sample(gLightingSampler, input.uv);
    const float depth = gLightingDepth.Sample(gLightingSampler, input.uv).r;
    const float3 worldPosition = material.xyz * 2.0f - 1.0f;

    const float normalStrength = max(gNormalStrength, 0.001f);
    const float3 normal = normalize(((encodedNormal.xyz * 2.0f) - 1.0f) * normalStrength);
    const float3 directionalLightDirection = normalize(-gDirectionalLightDirection);
    const float directionalTerm = saturate(dot(normal, directionalLightDirection));
    const float shadowVisibility = SampleSandboxShadow(worldPosition);
    const float shadowFactor = lerp(1.0f, shadowVisibility, saturate(gShadowStrength));
    const float ssaoVisibility = gSsaoParameters.x > 0.5f
        ? gLightingSsao.Sample(gLightingSampler, input.uv).r
        : 1.0f;
    const float ambientOcclusion = lerp(1.0f, ssaoVisibility, saturate(gSsaoParameters.y));
    const float3 directionalLighting =
        (gAmbientIntensity * ambientOcclusion + directionalTerm * gDirectionalLightIntensity * shadowFactor) * gDirectionalLightColor;

    float2 pointDelta = input.uv - gPointLightUv;
    pointDelta.x *= max(gAspectRatio, 0.001f);
    const float pointDistance = length(pointDelta);
    float pointAttenuation = saturate(1.0f - pointDistance / max(gPointLightRadius, 0.0001f));
    pointAttenuation *= pointAttenuation;
    pointAttenuation *= lerp(1.0f, saturate(1.0f - depth), saturate(gDepthInfluence));
    const float3 pointLighting = pointAttenuation * gPointLightIntensity * gPointLightColor;

    const float3 materialTint = lerp(
        float3(1.0f, 1.0f, 1.0f),
        float3(material.a, material.a, 1.0f),
        saturate(gMaterialInfluence));
    const float3 litColor = albedo.rgb * (directionalLighting + pointLighting) * materialTint;
    return float4(saturate(litColor), albedo.a);
}

SandboxShadowVertexOutput SandboxShadowVertexMain(SandboxVertexInput input)
{
    SandboxShadowVertexOutput output;
    output.position = mul(float4(input.position, 1.0f), gShadowWorldToClip);
    return output;
}

float4 SandboxShadowPixelMain() : SV_TARGET
{
    return float4(1.0f, 1.0f, 1.0f, 1.0f);
}
