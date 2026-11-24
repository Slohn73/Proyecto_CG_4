#version 330 core

#define NUMBER_OF_POINT_LIGHTS 4
#define MAX_SPOT_LIGHTS 8

// Debe coincidir con struct Material / MaterialUniforms de Mesh.h
struct Material
{
    sampler2D diffuse;
    sampler2D specular;
    bool hasDiffuseMap;
    bool hasSpecularMap;
    vec3 diffuseColor;      // Se usa cuando no hay textura difusa
    vec3 specularColor;     // Se usa cuando no hay mapa especular
    vec3 emissive;
    float shininess;
    float opacity;
    bool alphaTest;
    bool glow;              // Haz de luz: no recibe iluminación, solo brilla (ver la función main)
    float reflectivity;     // 0 = solo Phong; >0 = mezcla Fresnel (Schlick) con el cubemap (metal / vidrio)
};

struct DirLight
{
    vec3 direction;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

struct PointLight
{
    vec3 position;

    float constant;
    float linear;
    float quadratic;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

struct SpotLight
{
    vec3 position;
    vec3 direction;
    float cutOff;
    float outerCutOff;

    float constant;
    float linear;
    float quadratic;

    vec3 ambient;
    vec3 diffuse;
    vec3 specular;
};

in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in vec3 VertexColor;

out vec4 color;

uniform vec3 viewPos;
uniform DirLight dirLight;
uniform PointLight pointLights[NUMBER_OF_POINT_LIGHTS];
uniform SpotLight spotLights[MAX_SPOT_LIGHTS];
uniform int numSpotLights;
uniform Material material;
uniform vec3 objectGlow;    // Color del haz de este objeto (lo sincroniza la luz real del reflector)
uniform samplerCube envMap; // Mapa de entorno (el mismo cubemap del skybox), unidad de textura 2

// Color base y especular del fragmento (textura o color del material)
vec3 albedo;
vec3 specColor;

// Function prototypes
vec3 CalcDirLight( DirLight light, vec3 normal, vec3 viewDir );
vec3 CalcPointLight( PointLight light, vec3 normal, vec3 fragPos, vec3 viewDir );
vec3 CalcSpotLight( SpotLight light, vec3 normal, vec3 fragPos, vec3 viewDir );

void main( )
{
    vec4 texel = material.hasDiffuseMap ? texture( material.diffuse, TexCoords ) : vec4( 1.0 );

    // Haz de luz translúcido: color emisivo teñido con el de su reflector. La opacidad sale del
    // degradado alfa de la textura (intenso en la lente y nulo en el extremo) y se atenúa cuando
    // la superficie se ve de canto, para que el cono no muestre contornos duros.
    if ( material.glow )
    {
        vec3 viewDirGlow = normalize( viewPos - FragPos );
        float facing = abs( dot( normalize( Normal ), viewDirGlow ) );
        float strength = clamp( max( objectGlow.r, max( objectGlow.g, objectGlow.b ) ), 0.0, 1.0 );
        float glowAlpha = material.opacity * texel.a * smoothstep( 0.05, 0.7, facing ) * strength;
        color = vec4( material.emissive * objectGlow, glowAlpha );
        return;
    }

    if ( material.alphaTest && texel.a < 0.1 )
        discard;

    albedo = ( material.hasDiffuseMap ? texel.rgb : material.diffuseColor ) * VertexColor;
    specColor = material.hasSpecularMap ? texture( material.specular, TexCoords ).rgb : material.specularColor;

    // Properties
    vec3 viewDir = normalize( viewPos - FragPos );
    // Iluminación de dos caras: el culling está desactivado porque varios modelos tienen
    // superficies de una sola cara (banderas, letreros); su cara trasera usa la normal invertida.
    vec3 norm = normalize( Normal );
    norm = faceforward( norm, -viewDir, norm );

    // Directional lighting
    vec3 result = CalcDirLight( dirLight, norm, viewDir );

    // Point lights
    for ( int i = 0; i < NUMBER_OF_POINT_LIGHTS; i++ )
    {
        result += CalcPointLight( pointLights[i], norm, FragPos, viewDir );
    }

    // Spot lights
    for ( int i = 0; i < numSpotLights && i < MAX_SPOT_LIGHTS; i++ )
    {
        result += CalcSpotLight( spotLights[i], norm, FragPos, viewDir );
    }

    result += material.emissive;

    float alpha = material.opacity * ( material.alphaTest ? texel.a : 1.0 );

    // ---- Técnica 2: Fresnel (aproximación de Schlick) + environment mapping (cube map) ----
    // Phong (arriba) describe el material opaco. Para metales y superficies translúcidas se suma el
    // reflejo del entorno, ponderado por Fresnel: F = F0 + (1 - F0) * (1 - N.V)^5
    // (poco reflejo de frente, mucho en ángulos rasantes). En translúcidos F también sube la opacidad.
    if ( material.reflectivity > 0.0 )
    {
        float k = material.reflectivity;
        float cosTheta = clamp( dot( norm, viewDir ), 0.0, 1.0 );
        float F0 = 0.04 + 0.5 * k;                         // dieléctrico ~0.04, metal ~0.5-0.9
        float fresnel = F0 + ( 1.0 - F0 ) * pow( 1.0 - cosTheta, 5.0 );
        vec3 envColor = texture( envMap, reflect( -viewDir, norm ) ).rgb;
        envColor *= mix( vec3( 1.0 ), albedo, k );         // los metales tiñen el reflejo con su color
        float amount = clamp( fresnel * k * 1.5, 0.0, 1.0 );
        result = mix( result, envColor, amount );
        alpha = mix( alpha, 1.0, fresnel * k );            // translúcido: más opaco en ángulos rasantes
    }

    color = vec4( result, alpha );
}

// Calculates the color when using a directional light.
vec3 CalcDirLight( DirLight light, vec3 normal, vec3 viewDir )
{
    vec3 lightDir = normalize( -light.direction );

    // Diffuse shading
    float diff = max( dot( normal, lightDir ), 0.0 );

    // Specular shading
    vec3 reflectDir = reflect( -lightDir, normal );
    float spec = pow( max( dot( viewDir, reflectDir ), 0.0 ), material.shininess );

    // Combine results
    vec3 ambient = light.ambient * albedo;
    vec3 diffuse = light.diffuse * diff * albedo;
    vec3 specular = light.specular * spec * specColor;

    return ( ambient + diffuse + specular );
}

// Calculates the color when using a point light.
vec3 CalcPointLight( PointLight light, vec3 normal, vec3 fragPos, vec3 viewDir )
{
    vec3 lightDir = normalize( light.position - fragPos );

    // Diffuse shading
    float diff = max( dot( normal, lightDir ), 0.0 );

    // Specular shading
    vec3 reflectDir = reflect( -lightDir, normal );
    float spec = pow( max( dot( viewDir, reflectDir ), 0.0 ), material.shininess );

    // Attenuation (el máximo evita dividir entre cero si una luz no se configuró)
    float distance = length( light.position - fragPos );
    float attenuation = 1.0f / max( light.constant + light.linear * distance + light.quadratic * ( distance * distance ), 1e-4 );

    // Combine results
    vec3 ambient = light.ambient * albedo;
    vec3 diffuse = light.diffuse * diff * albedo;
    vec3 specular = light.specular * spec * specColor;

    ambient *= attenuation;
    diffuse *= attenuation;
    specular *= attenuation;

    return ( ambient + diffuse + specular );
}

// Calculates the color when using a spot light.
vec3 CalcSpotLight( SpotLight light, vec3 normal, vec3 fragPos, vec3 viewDir )
{
    vec3 lightDir = normalize( light.position - fragPos );

    // Diffuse shading
    float diff = max( dot( normal, lightDir ), 0.0 );

    // Specular shading
    vec3 reflectDir = reflect( -lightDir, normal );
    float spec = pow( max( dot( viewDir, reflectDir ), 0.0 ), material.shininess );

    // Attenuation
    float distance = length( light.position - fragPos );
    float attenuation = 1.0f / max( light.constant + light.linear * distance + light.quadratic * ( distance * distance ), 1e-4 );

    // Spotlight intensity
    float theta = dot( lightDir, normalize( -light.direction ) );
    float epsilon = max( light.cutOff - light.outerCutOff, 1e-4 );
    float intensity = clamp( ( theta - light.outerCutOff ) / epsilon, 0.0, 1.0 );

    // Combine results
    vec3 ambient = light.ambient * albedo;
    vec3 diffuse = light.diffuse * diff * albedo;
    vec3 specular = light.specular * spec * specColor;

    ambient *= attenuation * intensity;
    diffuse *= attenuation * intensity;
    specular *= attenuation * intensity;

    return ( ambient + diffuse + specular );
}
