#include "Shaders.h"

// Shader sources are adapted from NifSkope; see data/shaders/NIFSKOPE LICENSE.md.

namespace
{
constexpr auto DefaultVert = R"NIFSHADER(#version 120

uniform mat4 modelViewMatrix;
uniform mat4 mvpMatrix;
uniform mat3 normalMatrix;
uniform vec3 lightDirection;
uniform vec4 ambientColor;
uniform vec4 diffuseColor;

attribute vec3 position;
attribute vec3 normal;
attribute vec3 tangent;
attribute vec3 bitangent;
attribute vec2 texCoord;
attribute vec4 color;

varying vec2 TexCoord;
varying vec3 LightDir;
varying vec3 ViewDir;

varying vec3 N;
varying vec3 t;
varying vec3 b;
varying vec3 v;

varying vec4 A;
varying vec4 C;
varying vec4 D;

void main( void )
{
    gl_Position = mvpMatrix * vec4(position, 1.0);
    TexCoord = texCoord;

    N = normalize(normalMatrix * normal);
    t = normalize(normalMatrix * tangent);
    b = normalize(normalMatrix * bitangent);
    v = vec3(modelViewMatrix * vec4(position, 1.0));

    mat3 tbnMatrix = mat3(b.x, t.x, N.x,
                          b.y, t.y, N.y,
                          b.z, t.z, N.z);

    ViewDir = tbnMatrix * -v;
    LightDir = tbnMatrix * lightDirection;

    A = ambientColor;
    C = color;
    D = diffuseColor;
}
)NIFSHADER";

constexpr auto SkEffectShaderVert = R"NIFSHADER(#version 120

uniform mat4 modelViewMatrix;
uniform mat4 mvpMatrix;
uniform mat3 normalMatrix;
uniform vec3 lightDirection;
uniform vec4 ambientColor;
uniform vec4 diffuseColor;

attribute vec3 position;
attribute vec3 normal;
attribute vec3 tangent;
attribute vec3 bitangent;
attribute vec2 texCoord;
attribute vec4 color;

varying vec2 TexCoord;
varying vec3 LightDir;
varying vec3 ViewDir;

varying vec4 C;

varying vec3 N;
varying vec3 t;
varying vec3 b;
varying vec3 v;

void main( void )
{
    gl_Position = mvpMatrix * vec4(position, 1);
    TexCoord = texCoord;

    N = normalize(normalMatrix * normal);
    t = normalize(normalMatrix * tangent);
    b = normalize(normalMatrix * bitangent);
    v = vec3(modelViewMatrix * vec4(position, 1));

    mat3 tbnMatrix = mat3(b.x, t.x, N.x,
                          b.y, t.y, N.y,
                          b.z, t.z, N.z);

    ViewDir = tbnMatrix * -v.xyz;
    LightDir = tbnMatrix * lightDirection.xyz;

    C = color;
}
)NIFSHADER";

constexpr auto SkMsnVert = R"NIFSHADER(#version 120

uniform mat4 modelViewMatrix;
uniform mat4 mvpMatrix;
uniform mat3 normalMatrix;
uniform vec3 lightDirection;
uniform vec4 ambientColor;
uniform vec4 diffuseColor;

attribute vec3 position;
attribute vec3 normal;
attribute vec3 tangent;
attribute vec3 bitangent;
attribute vec2 texCoord;
attribute vec4 color;

varying vec2 TexCoord;
varying vec3 LightDir;
varying vec3 ViewDir;

varying vec3 v;

varying vec4 A;
varying vec4 C;
varying vec4 D;


void main( void )
{
    gl_Position = mvpMatrix * vec4(position, 1);
    TexCoord = texCoord;

    v = vec3(modelViewMatrix * vec4(position, 1));

    ViewDir = -v.xyz;
    LightDir = lightDirection;

    A = ambientColor;
    C = color;
    D = diffuseColor;
}
)NIFSHADER";

constexpr auto SkDefaultFrag = R"NIFSHADER(#version 120

uniform sampler2D BaseMap;
uniform sampler2D NormalMap;
uniform sampler2D GlowMap;
uniform sampler2D HeightMap;
uniform sampler2D LightMask;
uniform sampler2D BacklightMap;
uniform sampler2D EnvironmentMap;
uniform samplerCube CubeMap;

uniform vec3 specColor;
uniform float specStrength;
uniform float specGlossiness;

uniform bool hasGlowMap;
uniform vec3 glowColor;
uniform float glowMult;

uniform float alpha;
uniform float alphaThreshold;

uniform vec3 tintColor;

uniform bool hasHeightMap;
uniform vec2 uvScale;
uniform vec2 uvOffset;

uniform bool hasEmit;
uniform bool hasSoftlight;
uniform bool hasBacklight;
uniform bool hasRimlight;
uniform bool hasTintColor;
uniform bool hasCubeMap;
uniform bool hasEnvMask;

uniform float softlight;
uniform float rimPower;

uniform float envReflection;

uniform mat4 modelViewMatrixInverse;
uniform mat4 worldMatrix;

varying vec2 TexCoord;
varying vec3 LightDir;
varying vec3 ViewDir;

varying vec4 A;
varying vec4 C;
varying vec4 D;

varying vec3 N;
varying vec3 t;
varying vec3 b;

vec3 tonemap(vec3 x)
{
    float _A = 0.15;
    float _B = 0.50;
    float _C = 0.10;
    float _D = 0.20;
    float _E = 0.02;
    float _F = 0.30;

    return ((x*(_A*x+_C*_B)+_D*_E)/(x*(_A*x+_B)+_D*_F))-_E/_F;
}

vec3 toGrayscale(vec3 color)
{
    return vec3(dot(vec3(0.3, 0.59, 0.11), color));
}

void main(void)
{
    vec2 offset = TexCoord * uvScale + uvOffset;

    vec3 E = normalize(ViewDir);

    if (hasHeightMap) {
        float height = texture2D(HeightMap, offset).r;
        offset += E.xy * (height * 0.08 - 0.04);
    }

    vec4 baseMap = texture2D(BaseMap, offset);
    vec4 normalMap = texture2D(NormalMap, offset);
    vec4 glowMap = texture2D(GlowMap, offset);

    vec3 normal = normalize(normalMap.rgb * 2.0 - 1.0);

    vec3 L = normalize(LightDir);
    vec3 R = reflect(-L, normal);
    vec3 H = normalize(L + E);

    float NdotL = max(dot(normal, L), 0.0);
    float NdotH = max(dot(normal, H), 0.0);
    float EdotN = max(dot(normal, E), 0.0);
    float NdotNegL = max(dot(normal, -L), 0.0);

    vec3 reflected = reflect(-E, normal);
    vec3 reflectedVS = b * reflected.x + t * reflected.y + N * reflected.z;
    vec3 reflectedWS = vec3(worldMatrix * (modelViewMatrixInverse * vec4(reflectedVS, 0.0)));

    vec4 color;
    vec3 albedo = baseMap.rgb * C.rgb;
    vec3 diffuse = A.rgb + (D.rgb * NdotL);

    // Environment
    if (hasCubeMap) {
        vec4 cube = textureCube(CubeMap, reflectedWS);
        cube.rgb *= envReflection;

        if (hasEnvMask) {
            vec4 env = texture2D(EnvironmentMap, offset);
            cube.rgb *= env.r;
        } else {
            cube.rgb *= normalMap.a;
        }

        albedo += cube.rgb;
    }

    // Emissive & Glow
    vec3 emissive = vec3(0.0);
    if (hasEmit) {
        emissive += glowColor * glowMult;

        if (hasGlowMap) {
            emissive *= glowMap.rgb;
        }
    }

    // Specular
    vec3 spec = clamp(specColor * specStrength * normalMap.a * pow(NdotH, specGlossiness), 0.0, 1.0);
    spec *= D.rgb;

    vec3 backlight = vec3(0.0);
    if (hasBacklight) {
        backlight = texture2D(BacklightMap, offset).rgb;
        backlight *= NdotNegL;

        emissive += backlight * D.rgb;
    }

    vec4 mask = vec4(0.0);
    if (hasRimlight || hasSoftlight) {
        mask = texture2D(LightMask, offset);
    }

    vec3 rim = vec3(0.0);
    if (hasRimlight) {
        rim = mask.rgb * pow(vec3((1.0 - EdotN)), vec3(rimPower));
        rim *= smoothstep(-0.2, 1.0, dot(-L, E));

        emissive += rim * D.rgb;
    }

    vec3 soft = vec3(0.0);
    if (hasSoftlight) {
        float wrap = (dot(normal, L) + softlight) / (1.0 + softlight);

        soft = max(wrap, 0.0) * mask.rgb * smoothstep(1.0, 0.0, NdotL);
        soft *= sqrt(clamp(softlight, 0.0, 1.0));

        emissive += soft * D.rgb;
    }

    if (hasTintColor) {
        albedo *= tintColor;
    }

    color.rgb = albedo * (diffuse + emissive) + spec;
    color.rgb = tonemap(color.rgb) / tonemap(vec3(1.0));
    color.a = C.a * baseMap.a;

    if (color.a < alphaThreshold) {
        discard;
    }

    color.a *= alpha;
    gl_FragColor = color;
}
)NIFSHADER";

constexpr auto SkMsnFrag = R"NIFSHADER(#version 120

uniform sampler2D BaseMap;
uniform sampler2D NormalMap;
uniform sampler2D SpecularMap;
uniform sampler2D LightMask;
uniform sampler2D TintMask;
uniform sampler2D DetailMask;
uniform sampler2D BacklightMap;

uniform vec3 specColor;
uniform float specStrength;
uniform float specGlossiness;

uniform vec3 glowColor;
uniform float glowMult;

uniform float alpha;

uniform vec3 tintColor;

uniform vec2 uvScale;
uniform vec2 uvOffset;

uniform bool hasEmit;
uniform bool hasSoftlight;
uniform bool hasBacklight;
uniform bool hasRimlight;
uniform bool hasSpecularMap;
uniform bool hasDetailMask;
uniform bool hasTintMask;
uniform bool hasTintColor;

uniform float softlight;
uniform float rimPower;

uniform mat4 viewMatrix;

varying vec3 v;

varying vec2 TexCoord;
varying vec3 LightDir;
varying vec3 ViewDir;

varying vec4 A;
varying vec4 C;
varying vec4 D;


vec3 tonemap(vec3 x)
{
    float _A = 0.15;
    float _B = 0.50;
    float _C = 0.10;
    float _D = 0.20;
    float _E = 0.02;
    float _F = 0.30;

    return ((x*(_A*x+_C*_B)+_D*_E)/(x*(_A*x+_B)+_D*_F))-_E/_F;
}

vec3 toGrayscale(vec3 color)
{
    return vec3(dot(vec3(0.3, 0.59, 0.11), color));
}

float overlay( float base, float blend )
{
    float result;
    if ( base < 0.5 ) {
        result = 2.0 * base * blend;
    } else {
        result = 1.0 - 2.0 * (1.0 - blend) * (1.0 - base);
    }
    return result;
}

vec3 overlay( vec3 ba, vec3 bl )
{
    return vec3( overlay(ba.r, bl.r), overlay(ba.g, bl.g), overlay( ba.b, bl.b ) );
}

void main( void )
{
    vec2 offset = TexCoord.st * uvScale + uvOffset;

    vec4 baseMap = texture2D( BaseMap, offset );
    vec4 normalMap = texture2D( NormalMap, offset );

    vec3 normal = normalMap.rgb * 2.0 - 1.0;

    // Convert model space to view space
    //    Swizzled G/B values!
    normal = normalize( vec3( viewMatrix * vec4( normal.rbg, 0.0 )));

    // Face Normals
    //vec3 X = dFdx(v);
    //vec3 Y = dFdy(v);
    //vec3 constructedNormal = normalize(cross(X,Y));


    vec3 L = normalize(LightDir);
    vec3 E = normalize(ViewDir);
    vec3 R = reflect(-L, normal);
    vec3 H = normalize( L + E );

    float NdotL = max( dot(normal, L), 0.0 );
    float NdotH = max( dot(normal, H), 0.0 );
    float EdotN = max( dot(normal, E), 0.0 );
    float NdotNegL = max( dot(normal, -L), 0.0 );


    vec4 color;
    vec3 albedo = baseMap.rgb * C.rgb;
    vec3 diffuse = A.rgb + (D.rgb * NdotL);


    // Emissive
    vec3 emissive = vec3(0.0);
    if ( hasEmit ) {
        emissive += glowColor * glowMult;
    }

    // Specular

    float s = texture2D( SpecularMap, offset ).r;
    if ( !hasSpecularMap || hasBacklight ) {
        s = normalMap.a;
    }

    vec3 spec = clamp( specColor * specStrength * s * pow(NdotH, specGlossiness), 0.0, 1.0 );
    spec *= D.rgb;


    vec3 backlight = vec3(0.0);
    if ( hasBacklight ) {
        backlight = texture2D( BacklightMap, offset ).rgb;
        backlight *= NdotNegL;

        emissive += backlight * D.rgb;
    }

    vec4 mask = vec4(0.0);
    if ( hasRimlight || hasSoftlight ) {
        mask = texture2D( LightMask, offset );
    }

    vec3 rim = vec3(0.0);
    if ( hasRimlight ) {
        rim = mask.rgb * pow(vec3((1.0 - EdotN)), vec3(rimPower));
        rim *= smoothstep( -0.2, 1.0, dot(-L, E) );

        emissive += rim * D.rgb;
    }

    vec3 soft = vec3(0.0);
    if ( hasSoftlight ) {
        float wrap = (dot(normal, L) + softlight) / (1.0 + softlight);

        soft = max( wrap, 0.0 ) * mask.rgb * smoothstep( 1.0, 0.0, NdotL );
        soft *= sqrt( clamp( softlight, 0.0, 1.0 ) );

        emissive += soft * D.rgb;
    }

    vec3 detail = vec3(0.0);
    if ( hasDetailMask ) {
        detail = texture2D( DetailMask, offset ).rgb;

        albedo = overlay( albedo, detail );
    }

    vec3 tint = vec3(0.0);
    if ( hasTintMask ) {
        tint = texture2D( TintMask, offset ).rgb;

        albedo = overlay( albedo, tint );
    }

    if ( hasDetailMask ) {
        albedo += albedo;
    }

    if ( hasTintColor ) {
        albedo *= tintColor;
    }

    color.rgb = albedo * (diffuse + emissive) + spec;
    color.rgb = tonemap( color.rgb ) / tonemap( vec3(1.0) );
    color.a = C.a * baseMap.a;

    gl_FragColor = color;
    gl_FragColor.a *= alpha;
}
)NIFSHADER";

constexpr auto SkMultilayerFrag = R"NIFSHADER(#version 120

uniform sampler2D BaseMap;
uniform sampler2D NormalMap;
uniform sampler2D LightMask;
uniform sampler2D BacklightMap;
uniform sampler2D InnerMap;
uniform sampler2D EnvironmentMap;
uniform samplerCube CubeMap;

uniform vec3 specColor;
uniform float specStrength;
uniform float specGlossiness;

uniform vec3 glowColor;
uniform float glowMult;

uniform float alpha;

uniform vec2 uvScale;
uniform vec2 uvOffset;

uniform bool hasEmit;
uniform bool hasSoftlight;
uniform bool hasBacklight;
uniform bool hasRimlight;
uniform bool hasCubeMap;
uniform bool hasEnvMask;

uniform float softlight;
uniform float rimPower;

uniform vec2 innerScale;
uniform float innerThickness;
uniform float outerRefraction;
uniform float outerReflection;

uniform mat4 modelViewMatrixInverse;
uniform mat4 worldMatrix;

varying vec2 TexCoord;
varying vec3 LightDir;
varying vec3 ViewDir;

varying vec4 A;
varying vec4 C;
varying vec4 D;

varying vec3 N;
varying vec3 t;
varying vec3 b;


vec3 tonemap(vec3 x)
{
    float _A = 0.15;
    float _B = 0.50;
    float _C = 0.10;
    float _D = 0.20;
    float _E = 0.02;
    float _F = 0.30;

    return ((x*(_A*x+_C*_B)+_D*_E)/(x*(_A*x+_B)+_D*_F))-_E/_F;
}

vec3 toGrayscale(vec3 color)
{
    return vec3(dot(vec3(0.3, 0.59, 0.11), color));
}

// Compute inner layer?s texture coordinate and transmission depth
// vTexCoord: Outer layer?s texture coordinate
// vInnerScale: Tiling of inner texture
// vViewTS: View vector in tangent space
// vNormalTS: Normal in tangent space (sampled normal map)
// fLayerThickness: Distance from outer layer to inner layer
vec3 ParallaxOffsetAndDepth( vec2 vTexCoord, vec2 vInnerScale, vec3 vViewTS, vec3 vNormalTS, float fLayerThickness )
{
    // Tangent space reflection vector
    vec3 vReflectionTS = reflect( -vViewTS, vNormalTS );
    // Tangent space transmission vector (reflect about surface plane)
    vec3 vTransTS = vec3( vReflectionTS.xy, -vReflectionTS.z );

    // Distance along transmission vector to intersect inner layer
    float fTransDist = fLayerThickness / abs(vTransTS.z);

    // Texel size
    //     Bethesda's version does indeed seem to assume 1024, which is why they
    //    introduced the additional parameter.
    vec2 vTexelSize = vec2( 1.0/(1024.0 * vInnerScale.x), 1.0/(1024.0 * vInnerScale.y) );

    // Inner layer?s texture coordinate due to parallax
    vec2 vOffset = vTexelSize * fTransDist * vTransTS.xy;
    vec2 vOffsetTexCoord = vTexCoord + vOffset;

    // Return offset texture coordinate in xy and transmission dist in z
    return vec3( vOffsetTexCoord, fTransDist );
}

void main( void )
{
    vec2 offset = TexCoord * uvScale + uvOffset;

    vec4 baseMap = texture2D( BaseMap, offset );
    vec4 normalMap = texture2D( NormalMap, offset );

    vec3 normal = normalize(normalMap.rgb * 2.0 - 1.0);

    // Sample the non-parallax offset alpha channel of the inner map
    //    Used to modulate the innerThickness
    float innerMapAlpha = texture2D( InnerMap, offset ).a;


    vec3 L = normalize(LightDir);
    vec3 E = normalize(ViewDir);
    vec3 R = reflect(-L, normal);
    vec3 H = normalize( L + E );

    float NdotL = max( dot(normal, L), 0.0 );
    float NdotH = max( dot(normal, H), 0.0 );
    float EdotN = max( dot(normal, E), 0.0 );
    float NdotNegL = max( dot(normal, -L), 0.0 );


    // Mix between the face normal and the normal map based on the refraction scale
    vec3 mixedNormal = mix( vec3(0.0, 0.0, 1.0), normal, clamp( outerRefraction, 0.0, 1.0 ) );
    vec3 parallax = ParallaxOffsetAndDepth( offset, innerScale, E, mixedNormal, innerThickness * innerMapAlpha );

    // Sample the inner map at the offset coords
    vec4 innerMap = texture2D( InnerMap, parallax.xy * innerScale );

    vec3 reflected = reflect( -E, normal );
    vec3 reflectedVS = b * reflected.x + t * reflected.y + N * reflected.z;
    vec3 reflectedWS = vec3( worldMatrix * (modelViewMatrixInverse * vec4( reflectedVS, 0.0 )) );


    vec4 color;
    vec3 albedo;
    vec3 diffuse = A.rgb + (D.rgb * NdotL);
    vec3 inner = innerMap.rgb * C.rgb;
    vec3 outer = baseMap.rgb * C.rgb;


    // Mix inner/outer layer based on fresnel
    float outerMix = max( 1.0 - EdotN, baseMap.a );
    albedo = mix( inner, outer, outerMix );


    // Environment
    if ( hasCubeMap ) {
        vec4 cube = textureCube( CubeMap, reflectedWS );
        cube.rgb *= outerReflection;

        if ( hasEnvMask ) {
            vec4 env = texture2D( EnvironmentMap, offset );
            cube.rgb *= env.r;
        } else {
            cube.rgb *= normalMap.a;
        }

        albedo += cube.rgb;
    }

    // Specular
    vec3 spec = clamp( specColor * specStrength * normalMap.a * pow(NdotH, specGlossiness), 0.0, 1.0 );
    spec *= D.rgb;

    // Emissive
    //    Mixed with outer map
    vec3 emissive = vec3(0.0);
    if ( hasEmit ) {
        emissive += glowColor * glowMult;
    }

    // Backlight
    //     Mixed with inner and outer map
    vec3 backlight = vec3(0.0);
    if ( hasBacklight ) {
        backlight = texture2D( BacklightMap, offset ).rgb;
        backlight *= NdotNegL;

        emissive += backlight * D.rgb;
    }

    // TODO: Test rim and soft light mixing with inner/outer layer

    vec4 mask = vec4(0.0);
    if ( hasRimlight || hasSoftlight ) {
        mask = texture2D( LightMask, offset );
    }

    vec3 rim = vec3(0.0);
    if ( hasRimlight ) {
        rim = mask.rgb * pow(vec3((1.0 - EdotN)), vec3(rimPower));
        rim *= smoothstep( -0.2, 1.0, dot(-L, E) );

        emissive += rim * D.rgb;
    }

    vec3 soft = vec3(0.0);
    if ( hasSoftlight ) {
        float wrap = (dot(normal, L) + softlight) / (1.0 + softlight);

        soft = max( wrap, 0.0 ) * mask.rgb * smoothstep( 1.0, 0.0, NdotL );
        soft *= sqrt( clamp( softlight, 0.0, 1.0 ) );

        emissive += soft * D.rgb;
    }

    color.rgb = albedo * (diffuse + emissive) + spec;
    color.rgb = tonemap( color.rgb ) / tonemap( vec3(1.0) );
    color.a = C.a * baseMap.a;

    gl_FragColor = color;
    gl_FragColor.a *= alpha;
}
)NIFSHADER";

constexpr auto SkPbrFrag = R"NIFSHADER(#version 120

uniform sampler2D BaseMap;
uniform sampler2D NormalMap;
uniform sampler2D GlowMap;
uniform sampler2D HeightMap;
uniform sampler2D LightMask;
uniform sampler2D BacklightMap;
uniform sampler2D EnvironmentMap;
uniform samplerCube CubeMap;

uniform vec3 specColor;
uniform float specStrength;
uniform float specGlossiness;

uniform bool hasGlowMap;
uniform vec3 glowColor;
uniform float glowMult;

uniform float alpha;
uniform float alphaThreshold;

uniform vec3 tintColor;

uniform bool hasHeightMap;
uniform vec2 uvScale;
uniform vec2 uvOffset;

uniform bool hasEmit;
uniform bool hasSoftlight;
uniform bool hasBacklight;
uniform bool hasRimlight;
uniform bool hasTintColor;
uniform bool hasCubeMap;
uniform bool hasEnvMask;

uniform float softlight;
uniform float rimPower;

uniform float envReflection;

uniform mat4 modelViewMatrixInverse;
uniform mat4 worldMatrix;

varying vec2 TexCoord;
varying vec3 LightDir;
varying vec3 ViewDir;

varying vec4 A;
varying vec4 C;
varying vec4 D;

varying vec3 N;
varying vec3 t;
varying vec3 b;

vec3 tonemap(vec3 x)
{
    float _A = 0.15;
    float _B = 0.50;
    float _C = 0.10;
    float _D = 0.20;
    float _E = 0.02;
    float _F = 0.30;

    return ((x*(_A*x+_C*_B)+_D*_E)/(x*(_A*x+_B)+_D*_F))-_E/_F;
}

vec3 toGrayscale(vec3 color)
{
    return vec3(dot(vec3(0.3, 0.59, 0.11), color));
}

void main(void)
{
    vec2 offset = TexCoord * uvScale + uvOffset;

    vec3 E = normalize(ViewDir);

    if (hasHeightMap) {
        float height = texture2D(HeightMap, offset).r;
        offset += E.xy * (height * 0.08 - 0.04);
    }

    vec4 baseMap = texture2D(BaseMap, offset);
    vec4 normalMap = texture2D(NormalMap, offset);
    vec4 glowMap = texture2D(GlowMap, offset);

    vec3 normal = normalize(normalMap.rgb * 2.0 - 1.0);

    vec3 L = normalize(LightDir);
    vec3 R = reflect(-L, normal);
    vec3 H = normalize(L + E);

    float NdotL = max(dot(normal, L), 0.0);
    float NdotH = max(dot(normal, H), 0.0);
    float EdotN = max(dot(normal, E), 0.0);
    float NdotNegL = max(dot(normal, -L), 0.0);

    vec3 reflected = reflect(-E, normal);
    vec3 reflectedVS = b * reflected.x + t * reflected.y + N * reflected.z;
    vec3 reflectedWS = vec3(worldMatrix * (modelViewMatrixInverse * vec4(reflectedVS, 0.0)));

    vec4 color;
    vec3 albedo = baseMap.rgb * C.rgb;
    vec3 diffuse = A.rgb + (D.rgb * NdotL);

    // Environment
    if (hasCubeMap) {
        vec4 cube = textureCube(CubeMap, reflectedWS);
        cube.rgb *= envReflection;

        if (hasEnvMask) {
            vec4 env = texture2D(EnvironmentMap, offset);
            cube.rgb *= env.r;
        } else {
            cube.rgb *= normalMap.a;
        }

        albedo += cube.rgb;
    }

    // Emissive & Glow
    vec3 emissive = vec3(0.0);
    if (hasEmit) {
        emissive += glowColor * glowMult;

        if (hasGlowMap) {
            emissive *= glowMap.rgb;
        }
    }

    vec3 backlight = vec3(0.0);
    if (hasBacklight) {
        backlight = texture2D(BacklightMap, offset).rgb;
        backlight *= NdotNegL;

        emissive += backlight * D.rgb;
    }

    vec4 mask = vec4(0.0);
    if (hasRimlight || hasSoftlight) {
        mask = texture2D(LightMask, offset);
    }

    vec3 rim = vec3(0.0);
    if (hasRimlight) {
        rim = mask.rgb * pow(vec3((1.0 - EdotN)), vec3(rimPower));
        rim *= smoothstep(-0.2, 1.0, dot(-L, E));

        emissive += rim * D.rgb;
    }

    vec3 soft = vec3(0.0);
    if (hasSoftlight) {
        float wrap = (dot(normal, L) + softlight) / (1.0 + softlight);

        soft = max(wrap, 0.0) * mask.rgb * smoothstep(1.0, 0.0, NdotL);
        soft *= sqrt(clamp(softlight, 0.0, 1.0));

        emissive += soft * D.rgb;
    }

    if (hasTintColor) {
        albedo *= tintColor;
    }

    color.rgb = albedo * (diffuse + emissive);
    color.rgb = tonemap(color.rgb) / tonemap(vec3(1.0));
    color.a = C.a * baseMap.a;

    if (color.a < alphaThreshold) {
        discard;
    }

    color.a *= alpha;
    gl_FragColor = color;
}
)NIFSHADER";

constexpr auto SkEffectShaderFrag = R"NIFSHADER(#version 120

uniform sampler2D BaseMap;
uniform sampler2D GreyscaleMap;

uniform bool doubleSided;

uniform bool hasSourceTexture;
uniform bool hasGreyscaleMap;
uniform bool greyscaleAlpha;
uniform bool greyscaleColor;

uniform bool useFalloff;
uniform bool vertexColors;
uniform bool vertexAlpha;

uniform bool hasWeaponBlood;

uniform vec4 glowColor;
uniform float glowMult;

uniform vec2 uvScale;
uniform vec2 uvOffset;

uniform vec4 falloffParams;
uniform float falloffDepth;

varying vec2 TexCoord;
varying vec3 LightDir;
varying vec3 ViewDir;

varying vec4 C;

varying vec3 N;
varying vec3 v;

vec4 colorLookup( float x, float y ) {

    return texture2D( GreyscaleMap, vec2( clamp(x, 0.0, 1.0), clamp(y, 0.0, 1.0)) );
}

void main( void )
{
    vec4 baseMap = texture2D( BaseMap, TexCoord.st * uvScale + uvOffset );

    vec4 color;

    vec3 normal = N;

    // Reconstructed normal
    //normal = normalize(cross(dFdy(v.xyz), dFdx(v.xyz)));

    //if ( !doubleSided && !gl_FrontFacing ) { return; }

    vec3 E = normalize(ViewDir);

    float tmp2 = falloffDepth; // Unused right now

    // Falloff
    float falloff = 1.0;
    if ( useFalloff ) {
        float startO = min(falloffParams.z, 1.0);
        float stopO = max(falloffParams.w, 0.0);

        // TODO: When X and Y are both 0.0 or both 1.0 the effect is reversed.
        falloff = smoothstep( falloffParams.y, falloffParams.x, abs(E.b));

        falloff = mix( max(falloffParams.w, 0.0), min(falloffParams.z, 1.0), falloff );
    }

    float alphaMult = glowColor.a * glowColor.a;

    color.rgb = baseMap.rgb;
    color.a = baseMap.a;

    if ( hasWeaponBlood ) {
        color.rgb = vec3( 1.0, 0.0, 0.0 ) * baseMap.r;
        color.a = baseMap.a * baseMap.g;
    }

    color.rgb *= C.rgb * glowColor.rgb;
    color.a *= C.a * falloff * alphaMult;

    if ( greyscaleColor ) {
        // Only Red emissive channel is used
        float emRGB = glowColor.r;

        vec4 luG = colorLookup( baseMap.g, C.g * falloff * emRGB );

        color.rgb = luG.rgb;
    }

    if ( greyscaleAlpha ) {
        vec4 luA = colorLookup( baseMap.a, C.a * falloff * alphaMult );

        color.a = luA.a;
    }

    gl_FragColor.rgb = color.rgb * glowMult;
    gl_FragColor.a = color.a;
}
)NIFSHADER";

constexpr auto Fo4DefaultFrag = R"NIFSHADER(#version 120
#extension GL_ARB_shader_texture_lod : require

uniform sampler2D BaseMap;
uniform sampler2D NormalMap;
uniform sampler2D GlowMap;
uniform sampler2D BacklightMap;
uniform sampler2D SpecularMap;
uniform sampler2D GreyscaleMap;
uniform sampler2D EnvironmentMap;
uniform samplerCube CubeMap;

uniform vec3 specColor;
uniform float specStrength;
uniform float specGlossiness; // "Smoothness" in FO4; 0-1
uniform float fresnelPower;

uniform float paletteScale;

uniform vec3 glowColor;
uniform float glowMult;

uniform float alpha;

uniform vec3 tintColor;

uniform vec2 uvScale;
uniform vec2 uvOffset;

uniform bool hasEmit;
uniform bool hasGlowMap;
uniform bool hasSoftlight;
uniform bool hasBacklight;
uniform bool hasRimlight;
uniform bool hasTintColor;
uniform bool hasCubeMap;
uniform bool hasEnvMask;
uniform bool hasSpecularMap;
uniform bool greyscaleColor;
uniform bool doubleSided;

uniform float subsurfaceRolloff;
uniform float rimPower;
uniform float backlightPower;

uniform float envReflection;

uniform mat4 modelViewMatrixInverse;
uniform mat4 worldMatrix;

varying vec2 TexCoord;
varying vec3 LightDir;
varying vec3 ViewDir;

varying vec4 A;
varying vec4 C;
varying vec4 D;

varying vec3 N;
varying vec3 t;
varying vec3 b;

#ifndef M_PI
    #define M_PI 3.1415926535897932384626433832795
#endif

#define FLT_EPSILON 1.192092896e-07F // smallest such that 1.0 + FLT_EPSILON != 1.0

float OrenNayar( vec3 L, vec3 V, vec3 N, float roughness, float NdotL )
{
    //float NdotL = dot(N, L);
    float NdotV = dot(N, V);
    float LdotV = dot(L, V);

    float rough2 = roughness * roughness;

    float A = 1.0 - 0.5 * (rough2 / (rough2 + 0.57));
    float B = 0.45 * (rough2 / (rough2 + 0.09));

    float a = min( NdotV, NdotL );
    float b = max( NdotV, NdotL );
    b = (sign(b) == 0.0) ? FLT_EPSILON : sign(b) * max( 0.01, abs(b) ); // For fudging the smoothness of C
    float C = sqrt( (1.0 - a * a) * (1.0 - b * b) ) / b;

    float gamma = LdotV - NdotL * NdotV;
    float L1 = A + B * max( gamma, FLT_EPSILON ) * C;

    return L1 * max( NdotL, FLT_EPSILON );
}

float OrenNayarFull( vec3 L, vec3 V, vec3 N, float roughness, float NdotL )
{
    //float NdotL = dot(N, L);
    float NdotV = dot(N, V);
    float LdotV = dot(L, V);

    float angleVN = acos(max(NdotV, FLT_EPSILON));
    float angleLN = acos(max(NdotL, FLT_EPSILON));

    float alpha = max(angleVN, angleLN);
    float beta = min(angleVN, angleLN);
    float gamma = LdotV - NdotL * NdotV;

    float roughnessSquared = roughness * roughness;
    float roughnessSquared9 = (roughnessSquared / (roughnessSquared + 0.09));

    // C1, C2, and C3
    float C1 = 1.0 - 0.5 * (roughnessSquared / (roughnessSquared + 0.33));
    float C2 = 0.45 * roughnessSquared9;

    if( gamma >= 0.0 ) {
        C2 *= sin(alpha);
    } else {
        C2 *= (sin(alpha) - pow((2.0 * beta) / M_PI, 3.0));
    }

    float powValue = (4.0 * alpha * beta) / (M_PI * M_PI);
    float C3 = 0.125 * roughnessSquared9 * powValue * powValue;

    // Avoid asymptote at pi/2
    float asym = M_PI / 2.0;
    float lim1 = asym + 0.01;
    float lim2 = asym - 0.01;

    float ab2 = (alpha + beta) / 2.0;

    if ( beta >= asym && beta < lim1 )
        beta = lim1;
    else if ( beta < asym && beta >= lim2 )
        beta = lim2;

    if ( ab2 >= asym && ab2 < lim1 )
        ab2 = lim1;
    else if ( ab2 < asym && ab2 >= lim2 )
        ab2 = lim2;

    // Reflection
    float A = gamma * C2 * tan(beta);
    float B = (1.0 - abs(gamma)) * C3 * tan(ab2);

    float L1 = max(FLT_EPSILON, NdotL) * (C1 + A + B);

    // Interreflection
    float twoBetaPi = 2.0 * beta / M_PI;
    float L2 = 0.17 * max(FLT_EPSILON, NdotL) * (roughnessSquared / (roughnessSquared + 0.13)) * (1.0 - gamma * twoBetaPi * twoBetaPi);

    return L1 + L2;
}

// Schlick's Fresnel approximation
float fresnelSchlick( float VdotH, float F0 )
{
    float base = 1.0 - VdotH;
    float exp = pow( base, fresnelPower );
    return clamp( exp + F0 * (1.0 - exp), 0.0, 1.0 );
}

// The Torrance-Sparrow visibility factor, G
float VisibDiv( float NdotL, float NdotV, float VdotH, float NdotH )
{
    float denom = max( VdotH, FLT_EPSILON );
    float numL = min( NdotV, NdotL );
    float numR = 2.0 * NdotH;
    if ( denom >= (numL * numR) ) {
        numL = (numL == NdotV) ? 1.0 : (NdotL / NdotV);
        return (numL * numR) / denom;
    }
    return 1.0 / NdotV;
}

// this is a normalized Phong model used in the Torrance-Sparrow model
vec3 TorranceSparrow(float NdotL, float NdotH, float NdotV, float VdotH, vec3 color, float power, float F0)
{
    // D: Normalized phong model
    float D = ((power + 2.0) / (2.0 * M_PI)) * pow( NdotH, power );

    // G: Torrance-Sparrow visibility term divided by NdotV
    float G_NdotV = VisibDiv( NdotL, NdotV, VdotH, NdotH );

    // F: Schlick's approximation
    float F = fresnelSchlick( VdotH, F0 );

    // Torrance-Sparrow:
    // (F * G * D) / (4 * NdotL * NdotV)
    // Division by NdotV is done in VisibDiv()
    // and division by NdotL is removed since
    // outgoing radiance is determined by:
    // BRDF * NdotL * L()
    float spec = (F * G_NdotV * D) / 4.0;

    return color * spec * M_PI;
}

vec3 tonemap(vec3 x)
{
    float _A = 0.15;
    float _B = 0.50;
    float _C = 0.10;
    float _D = 0.20;
    float _E = 0.02;
    float _F = 0.30;

    return ((x*(_A*x+_C*_B)+_D*_E)/(x*(_A*x+_B)+_D*_F))-_E/_F;
}

vec4 colorLookup( float x, float y ) {

    return texture2D( GreyscaleMap, vec2( clamp(x, 0.0, 1.0), clamp(y, 0.0, 1.0) ) );
}

void main( void )
{
    vec2 offset = TexCoord * uvScale + uvOffset;

    vec4 baseMap = texture2D( BaseMap, offset );
    vec4 normalMap = texture2D( NormalMap, offset );
    vec4 specMap = texture2D( SpecularMap, offset );
    vec4 glowMap = texture2D( GlowMap, offset );

    vec3 normal = normalize(normalMap.rgb * 2.0 - 1.0);
    // Calculate missing blue channel
    normal.b = sqrt(1.0 - dot(normal.rg, normal.rg));
    if ( !gl_FrontFacing && doubleSided ) {
        normal *= -1.0;
    }
    // For _msn (Test with FSF1_Face)
    //normal.z = sqrt( 1.0 - dot( normal.xy, normal.xy ) );

    vec3 L = normalize(LightDir);
    vec3 V = normalize(ViewDir);
    vec3 R = reflect(-L, normal);
    vec3 H = normalize( L + V );

    float NdotL = dot(normal, L);
    float NdotL0 = max( NdotL, FLT_EPSILON );
    float NdotH = max( dot(normal, H), FLT_EPSILON );
    float NdotV = max( dot(normal, V), FLT_EPSILON );
    float VdotH = max( dot(V, H), FLT_EPSILON );
    float NdotNegL = max( dot(normal, -L), FLT_EPSILON );

    vec3 reflected = reflect( V, normal );
    vec3 reflectedVS = b * reflected.x + t * reflected.y + N * reflected.z;
    vec3 reflectedWS = vec3( worldMatrix * (modelViewMatrixInverse * vec4( reflectedVS, 0.0 )) );

    vec4 color;
    vec3 albedo = baseMap.rgb * C.rgb;
    vec3 diffuse = A.rgb + D.rgb * NdotL0;
    if ( greyscaleColor ) {
        vec4 luG = colorLookup( baseMap.g, paletteScale - (1 - C.r) );

        albedo = luG.rgb;
    }

    // Emissive
    vec3 emissive = vec3(0.0);
    if ( hasEmit ) {
        emissive += glowColor * glowMult;

        if ( hasGlowMap ) {
            emissive *= glowMap.rgb;
        }
    }

    // Specular
    float g = 1.0;
    float s = 1.0;
    float smoothness = clamp( specGlossiness, 0.0, 1.0 );
    float specMask = 1.0;
    vec3 spec = vec3(0.0);
    if ( hasSpecularMap ) {
        g = specMap.g;
        s = specMap.r;
        smoothness = g * smoothness;
        float fSpecularPower = exp2( smoothness * 10 + 1 );
        specMask = s * specStrength;

        spec = TorranceSparrow( NdotL0, NdotH, NdotV, VdotH, vec3(specMask), fSpecularPower, 0.2 ) * NdotL0 * D.rgb * specColor;
    }

    // Environment
    vec4 cube = textureCubeLod( CubeMap, reflectedWS, 8.0 - smoothness * 8.0 );
    vec4 env = texture2D( EnvironmentMap, offset );
    if ( hasCubeMap ) {
        cube.rgb *= envReflection * specStrength;
        if ( hasEnvMask ) {
            cube.rgb *= env.r;
        } else {
            cube.rgb *= s;
        }

        spec += cube.rgb * diffuse;
    }

    vec3 backlight = vec3(0.0);
    if ( backlightPower > 0.0 ) {
        backlight = albedo * NdotNegL * clamp( backlightPower, 0.0, 1.0 );

        emissive += backlight * D.rgb;
    }

    vec3 rim = vec3(0.0);
    if ( hasRimlight ) {
        rim = vec3(pow((1.0 - NdotV), rimPower));
        rim *= smoothstep( -0.2, 1.0, dot(-L, V) );

        //emissive += rim * D.rgb * specMask;
    }

    // Diffuse
    float diff = OrenNayarFull( L, V, normal, 1.0 - smoothness, NdotL );
    diffuse = vec3(diff);

    vec3 soft = vec3(0.0);
    float wrap = NdotL;
    if ( hasSoftlight || subsurfaceRolloff > 0.0 ) {
        wrap = (wrap + subsurfaceRolloff) / (1.0 + subsurfaceRolloff);
        soft = albedo * max( 0.0, wrap ) * smoothstep( 1.0, 0.0, sqrt(diff) );

        diffuse += soft;
    }

    if ( hasTintColor ) {
        albedo *= tintColor;
    }

    // Diffuse
    color.rgb = diffuse * albedo * D.rgb;
    // Ambient
    color.rgb += A.rgb * albedo;
    // Specular
    color.rgb += spec;
    color.rgb += A.rgb * specMask * fresnelSchlick( VdotH, 0.2 ) * (1.0 - NdotV) * D.rgb;
    // Emissive
    color.rgb += emissive;

    color.rgb = tonemap( color.rgb ) / tonemap( vec3(1.0) );
    color.a = C.a * baseMap.a;

    gl_FragColor = color;
    gl_FragColor.a *= alpha;
}
)NIFSHADER";

constexpr auto Fo4EffectShaderFrag = R"NIFSHADER(#version 120

uniform sampler2D BaseMap;
uniform sampler2D GreyscaleMap;
uniform samplerCube CubeMap;
uniform sampler2D NormalMap;
uniform sampler2D SpecularMap;

uniform bool doubleSided;

uniform bool hasSourceTexture;
uniform bool hasGreyscaleMap;
uniform bool hasCubeMap;
uniform bool hasNormalMap;
uniform bool hasEnvMask;

uniform bool greyscaleAlpha;
uniform bool greyscaleColor;

uniform bool useFalloff;
uniform bool hasRGBFalloff;

uniform bool hasWeaponBlood;

uniform vec4 glowColor;
uniform float glowMult;

uniform vec2 uvScale;
uniform vec2 uvOffset;

uniform vec4 falloffParams;
uniform float falloffDepth;

uniform float lightingInfluence;
uniform float envReflection;

uniform mat4 modelViewMatrixInverse;
uniform mat4 worldMatrix;

varying vec2 TexCoord;
varying vec3 LightDir;
varying vec3 ViewDir;

varying vec4 A;
varying vec4 C;
varying vec4 D;

varying vec3 N;
varying vec3 t;
varying vec3 b;
varying vec3 v;

vec4 colorLookup( float x, float y ) {

    return texture2D( GreyscaleMap, vec2( clamp(x, 0.0, 1.0), clamp(y, 0.0, 1.0)) );
}

void main( void )
{
    vec2 offset = TexCoord * uvScale + uvOffset;

    vec4 baseMap = texture2D( BaseMap, offset );
    vec4 normalMap = texture2D( NormalMap, offset );
    vec4 specMap = texture2D( SpecularMap, offset );

    vec3 normal = normalize(normalMap.rgb * 2.0 - 1.0);
    // Calculate missing blue channel
    normal.b = sqrt(1.0 - dot(normal.rg, normal.rg));
    if ( !gl_FrontFacing && doubleSided ) {
        normal *= -1.0;
    }

    vec3 L = normalize(LightDir);
    vec3 V = normalize(ViewDir);
    vec3 R = reflect(-L, normal);
    vec3 H = normalize( L + V );

    float NdotL = max( dot(normal, L), 0.000001 );
    float NdotH = max( dot(normal, H), 0.000001 );
    float NdotV = max( dot(normal, V), 0.000001 );
    float LdotH = max( dot(L, H), 0.000001 );
    float NdotNegL = max( dot(normal, -L), 0.000001 );

    vec3 reflected = reflect( V, normal );
    vec3 reflectedVS = b * reflected.x + t * reflected.y + N * reflected.z;
    vec3 reflectedWS = vec3( worldMatrix * (modelViewMatrixInverse * vec4( reflectedVS, 0.0 )) );

    if ( greyscaleAlpha )
        baseMap.a = 1.0;

    vec4 baseColor = glowColor;
    if ( !greyscaleColor )
        baseColor.rgb *= glowMult;

    // Falloff
    float falloff = 1.0;
    if ( useFalloff || hasRGBFalloff ) {
        falloff = smoothstep( falloffParams.x, falloffParams.y, abs(dot(normal, V)) );
        falloff = mix( max(falloffParams.z, 0.0), min(falloffParams.w, 1.0), falloff );

        if ( useFalloff )
            baseMap.a *= falloff;

        if ( hasRGBFalloff )
            baseMap.rgb *= falloff;
    }

    float alphaMult = baseColor.a * baseColor.a;

    vec4 color;
    color.rgb = baseMap.rgb * C.rgb * baseColor.rgb;
    color.a = alphaMult * C.a * baseMap.a;

    if ( greyscaleColor ) {
        vec4 luG = colorLookup( texture2D( BaseMap, offset ).g, baseColor.r * C.r * falloff );

        color.rgb = luG.rgb;
    }

    if ( greyscaleAlpha ) {
        vec4 luA = colorLookup( texture2D( BaseMap, offset ).a, color.a );

        color.a = luA.a;
    }

    vec3 diffuse = A.rgb + (D.rgb * NdotL);
    color.rgb = mix( color.rgb, color.rgb * D.rgb, lightingInfluence );

    // Specular
    float g = 1.0;
    float s = 1.0;
    if ( hasEnvMask ) {
        g = specMap.r;
        s = specMap.g;
    }

    // Environment
    vec4 cube = textureCube( CubeMap, reflectedWS );
    if ( hasCubeMap ) {
        cube.rgb *= envReflection * s;
        cube.rgb = mix( cube.rgb, cube.rgb * D.rgb, lightingInfluence );

        color.rgb += cube.rgb * falloff;
    }

    gl_FragColor.rgb = color.rgb;
    gl_FragColor.a = color.a;
}
)NIFSHADER";

}

const ShaderSource* shaderSourceFor(const ShaderManager::ShaderType type)
{
  switch (type) {
  case ShaderManager::SKDefault: {
    static constexpr ShaderSource source{
      "default.vert", "sk_default.frag", DefaultVert, SkDefaultFrag};
    return &source;
  }
  case ShaderManager::SKMSN: {
    static constexpr ShaderSource source{
      "sk_msn.vert", "sk_msn.frag", SkMsnVert, SkMsnFrag};
    return &source;
  }
  case ShaderManager::SKMultilayer: {
    static constexpr ShaderSource source{
      "default.vert", "sk_multilayer.frag", DefaultVert, SkMultilayerFrag};
    return &source;
  }
  case ShaderManager::SKEffectShader: {
    static constexpr ShaderSource source{
      "sk_effectshader.vert", "sk_effectshader.frag", SkEffectShaderVert,
      SkEffectShaderFrag};
    return &source;
  }
  case ShaderManager::SKPBR: {
    static constexpr ShaderSource source{
      "default.vert", "sk_pbr.frag", DefaultVert, SkPbrFrag};
    return &source;
  }
  case ShaderManager::FO4Default: {
    static constexpr ShaderSource source{
      "default.vert", "fo4_default.frag", DefaultVert, Fo4DefaultFrag};
    return &source;
  }
  case ShaderManager::FO4EffectShader: {
    static constexpr ShaderSource source{
      "default.vert", "fo4_effectshader.frag", DefaultVert, Fo4EffectShaderFrag};
    return &source;
  }
  default:
    return nullptr;
  }
}
