#version 450
#include "common.glsl"
#include "../noise.glsl"

// - to do: also depth (both old and new)
uniform layout(binding=4) sampler2D oldLightTexture;
uniform layout(binding=5) sampler2D oldTransmittanceTexture;

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;
#include "compute.glsl"

uniform layout(binding=0, rgba16f) image2D lightImage;
uniform layout(binding=1, rgba16f) image2D transmittanceImage;
uniform layout(binding=2, rgba16f) image2D outLightImage;


uniform uvec2 fragOffset, fragFactor;

void main()
{
    const ivec2 ifragCoords = ivec2(gl_GlobalInvocationID);
    const vec2 fragCoords = vec2(ifragCoords) + vec2(0.5);
    const vec2 coords = fragCoords / vec2(imageSize(lightImage));
    if (coords.x >= 1.0 || coords.y >= 1.0) return; // outside the target
    const vec4 clipCoords = vec4(coords * 2.0 - 1.0, 1.0, 1.0);
    
    vec3 backLight = imageLoad(outLightImage, ifragCoords).rgb;
    
    // - to do: combine old light/transmittance with (sparse) new, storing to 
    // lightImage and transmittanceImage, then computing final light and storing
    // to outLightImage
    
    vec3 color = vec3(0.0);
    vec3 transmittance = vec3(1.0);
    
    if (ifragCoords % ivec2(fragFactor) == ivec2(fragOffset)) // new
    {
        color = imageLoad(lightImage, ifragCoords).rgb;
        transmittance = imageLoad(transmittanceImage, ifragCoords).rgb;
    }
    else // old
    {
        const vec3 ori = vec3(invViewMat * vec4(0, 0, 0, 1));
        const vec3 dir = normalize(vec3(invViewProjMat * clipCoords));
        AtmosphereIntersection ai = IntersectAtmosphere(ori, dir);
        
        if (ai.intersectsOuter) // only show atmosphere where there is atmosphere
        {
            vec3 p = ori + dir * ai.outerMax; // - to do: more intelligent choice (nearer when intersecting planet and/or inner (cloud) shell)
            vec4 pp = prevViewProjMat * vec4(p, 1.0);
            vec2 oldCoords = pp.xy / pp.w * 0.5 + 0.5;
            vec2 halfTexel = 0.5 / vec2(imageSize(lightImage));
            //halfTexel = vec2(0.0);
            
            if (ifragCoords.y >= imageSize(lightImage).x) color += vec3(1.0);
            
            if (any(lessThan(oldCoords, vec2(0.0))) || any(greaterThan(oldCoords, vec2(1.0))))
            {
                // - to do: use closest current frame point when oldCoords out of bounds?
                // - to do: maybe look into making this more sophisticated
                ivec2 icoords = ifragCoords / ivec2(fragFactor);
                icoords = icoords * ivec2(fragFactor) + ivec2(fragOffset);
                
                // - these conditionals would be unnecessary if the image was always guaranteed to be evenly divisible by the downscale factor
                if (icoords.x >= imageSize(lightImage).x) icoords.x -= int(fragFactor.x);
                if (icoords.y >= imageSize(lightImage).y) icoords.y -= int(fragFactor.y);
                
                color = imageLoad(lightImage, icoords).rgb;
                transmittance = imageLoad(transmittanceImage, icoords).rgb;
            }
            else
            {
                color = texture(oldLightTexture, oldCoords).rgb;
                transmittance = texture(oldTransmittanceTexture, oldCoords).rgb;
            }
            
            // - to do: retrieve depth and search for closest depth in small pixel neighbourhood (among both old and new values)
        }
    }
    
    imageStore(lightImage, ifragCoords, vec4(color, 0.0));
    imageStore(transmittanceImage, ifragCoords, vec4(transmittance, 0.0));
    
    color += backLight * transmittance;
    imageStore(outLightImage, ifragCoords, vec4(color, 0.0));
}
