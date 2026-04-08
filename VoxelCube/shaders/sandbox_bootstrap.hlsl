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
};

Texture2D gSandboxTexture : register(t0);
SamplerState gSandboxSampler : register(s0);

SandboxVertexOutput SandboxVertexMain(SandboxVertexInput input)
{
    SandboxVertexOutput output;
    output.position = float4(input.position, 1.0f);
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

float4 SandboxPixelMain(SandboxVertexOutput input) : SV_TARGET
{
    return gSandboxTexture.Sample(gSandboxSampler, input.uv) * input.color;
}
