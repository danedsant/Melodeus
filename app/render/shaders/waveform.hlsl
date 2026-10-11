// Pase de waveform F2: quads instanciados desde un vertex buffer
// per-instance (upload heap preasignado, slot por frame). Sin texturas ni
// root tables: las esquinas salen de SV_VertexID y los datos de la
// instancia del input layout con STEP_RATE 1.

struct Instance {
    float2 x0x1; // NDC x: x0 borde izquierdo, x1 borde derecho
    float2 y0y1; // NDC y (arriba +)
    float3 rgb;
};

struct VSOut {
    float4 pos : SV_Position;
    float3 col : COLOR0;
};

static const float2 kCorners[6] = {
    float2(-1.0, -1.0), float2(1.0, -1.0), float2(-1.0, 1.0),
    float2(-1.0, 1.0),  float2(1.0, -1.0), float2(1.0, 1.0),
};

VSOut vs_main(uint vid : SV_VertexID, Instance inst : TEXCOORD0) {
    const float2 c = kCorners[vid % 6];
    const float2 t = c * 0.5 + 0.5;
    VSOut o;
    o.pos =
        float4(lerp(inst.x0x1.x, inst.x0x1.y, t.x), lerp(inst.y0y1.x, inst.y0y1.y, t.y), 0.0, 1.0);
    o.col = inst.rgb;
    return o;
}

float4 ps_main(VSOut i) : SV_Target {
    return float4(i.col, 1.0);
}
