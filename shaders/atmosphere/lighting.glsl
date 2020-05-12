
//
// Lighting utilities inclusion file.
//

#include "../noise.glsl"
#include "common.glsl"



const uint GroupRes = BrickRes*2u;
uniform uvec3 uGroupsRes;

uvec3 GroupIndexTo3D(uint groupIndex)
{
    //return IndexTo3D(groupIndex, uGroupsRes);
    return uvec3(groupIndex % uGroupsRes.x, groupIndex / uGroupsRes.x, 0u);
}

uniform mat4 groupLightMat, invGroupLightMat;


float PlanetShadow(vec3 ori, vec3 dir, vec3 planetCenter, float voxelSize)
{
    // - to do: maybe decrease planet radius by approximately one voxel length, to avoid overshadowing?
    float R = planetRadius;// - voxelSize; // - possibly remove the subtraction if gradual shadowing is implemented
    
    vec3 offs = planetCenter - ori;
    float d = dot(offs, dir);
    if (d < 0.0) 
    {
        //return 1.0; // wrong? Possibly
        return (length(ori - planetCenter) - R) / voxelSize + 1.0;
    }
    offs -= dir * dot(offs, dir);
    // - this should also depend on distance and angular size of the sun, no? To do
    float s = (length(offs) - R) / voxelSize + 1.0;
    return s;
}

float TraceTransmittance(vec3 ori, vec3 dir, float dist, const float stepFactor, const uint maxDepth, const float inMaxDist)
{
    const float atmScale = atmosphereRadius;
    float opticalDepthM = 0.0;
    float prevDensityM = 0.0; // - maybe to do: get these from start values
    //ori += offsetOrigin(p, dir, voxelSize); // - trying this *after* the planet shadowing. But it's not helping.
    
    const float thresholdFraction = 1e-2; // - might need tuning
    const float threshold = -log(thresholdFraction) / betaMEx;
    
    float tmin, tmax;
    float R = planetRadius + atmosphereHeight;
    if (IntersectSphere(ori, dir, vec3(0.0), R, tmin, tmax)) // atmosphere intersection
    {
        const uint maxSteps = 512u; // - arbitrary, for testing
        float maxDist = min(tmax, inMaxDist);
        
        uint numBricks = 0u, numSteps = 0u;
        
        
        uint old = InvalidIndex;
        uint it = 0u;
        //while (InvalidIndex != o.ni)
        while (true)
        {
            ++it;
            ++numBricks;
            
            OctreeTraversalData o;
            o.p = (ori + dist * dir) / atmScale;
            OctreeDescendMap(o);
            /*if (old == o.ni && numBricks < 6u) // - this happens fairly often (with low numbers of bricks, that is). Why?
            {
                opticalDepthM = 1e9;
                break;
            }*/
            if (InvalidIndex == o.ni) break;
            old = o.ni;
            
            if (opticalDepthM > threshold) break; // - testing
            if (dist > maxDist) break;
            // - (more than 64 are "needed", though... even 8 give "local" cloud shadows)
            if (numBricks >= 128u) break; // - testing
            
            o.center *= atmScale;
            o.size *= atmScale;
            const float step = o.size / atmScale * stepFactor;
            float atmStep = step * atmScale;
            
            AabbIntersection(tmin, tmax, vec3(-o.size) + o.center, vec3(o.size) + o.center, ori, dir);
            tmax = min(tmax, maxDist);
            
            //if (false)
            if ((o.flags & EmptyBrickBit) != 0u)
            {
                // - simple test:
                //atmStep *= 4.0;
                // (this does improve overall speed a bit - probably relatively
                // little because it also allows for physically longer and thus
                // more accurate shadows? Might be)
                // - scratch the above results: the nodes are just *all* marked as empty, aren't they...
                // (... which might indicate longer step sizes are overall usable?)
                // (well, we should rather be using lower levels of detail. To do: do that)
                
                // - to do: skip the brick correctly
                dist = tmax + atmStep * 1.0; // - testing // - does seem like a gain (maybe 1/3?). Investigate more
                //if (it == 1u) return 0.0; // - testing (to see where bricks are marked as empty)
                continue;
            }
            //if (it == 1u) return 1.0; // - testing
            
            const vec3 brickOffs = vec3(BrickIndexTo3D(o.ni));
            vec3 localStart = (ori - o.center) / o.size;
            
            do // do while to not erroneously miss the first voxel if it's on the border
            //while (dist < tmax)
            {
                vec3 lc = localStart + dist / o.size * dir;
                vec3 tc = lc * 0.5 + 0.5;
                tc = clamp(tc, vec3(0.0), vec3(1.0));
                tc = BrickSampleCoordinates(brickOffs, tc);
                vec4 voxelData = texture(brickTexture, tc);
                
                float mie = voxelData.x;
                float densityM = MieDensityFromSample(mie);
                
                densityM = mie * mieMul;
                opticalDepthM += (prevDensityM + densityM) * 0.5 * atmStep;
                prevDensityM = densityM;
                
                dist += atmStep;
                ++numSteps;
            } while (dist < tmax);// && numSteps < maxSteps);
        }
        
        // - testing performance impact of not sampling any voxel data:
        // (interestingly, it's about the same. Traversal seems heavy, then)
        //return 1.0 - float(numBricks) / 256.0;
        //return 1.0; // - testing
        //return numBricks > 63u ? 0.0 : 1.0;
    }
    
    if (opticalDepthM > threshold) return 0.0; // - trying to avoid sampling pattern from early exit
    float opticalDepth = opticalDepthM * betaMEx;
    return exp(-opticalDepth);
}

// Infinite max distance variant.
float TraceTransmittance(vec3 ori, vec3 dir, float dist, const float stepFactor, const uint maxDepth)
{
    return TraceTransmittance(ori, dir, dist, stepFactor, maxDepth, 1e30);
}

vec3 perpendicular(vec3 v)
{
    vec3 a = cross(vec3(1, 0, 0), v), b = cross(vec3(0, 1, 0), v);
    return dot(a, a) > 1e-5 ? a : b;
}

vec3 offsetOrigin(vec3 p, vec3 dir, float voxelSize)
{
    const float PI = 3.141592653589793;
    
    // - the ray *origin* has to be offset in a plane with normal dir, right? Hmm...
    vec3 a = perpendicular(dir);
    vec3 b = cross(dir, a);
    const float offs = 0.5; // - to do
    
    //return 1.0 * normalize(p) * (rand3D(p) * 0.5 + 0.5) * voxelSize;
    //return vec3(0.0);
    
    float r = rand3D(p) * 0.5 + 0.5, theta = 2 * PI * (rand3D(p.zyx) * 0.5 + 05);
    float x = sqrt(r) * cos(theta);
    float y = sqrt(r) * sin(theta);
    return 
        //dir * voxelSize 
        //p * voxelSize
        + offs * voxelSize * (x * a + y * b)
        ;
}

float ConeTraceTransmittance(vec3 ori, vec3 dir, float dist, const float stepFactor, const float voxelSize)
{
    const float atmScale = atmosphereRadius;
    
    float opticalDepthR = 0.0, opticalDepthM = 0.0;
    float prevDensityR = 0.0, prevDensityM = 0.0; // - maybe to do: get these from start values

    //ori += offsetOrigin(p, dir, voxelSize); // - trying this *after* the planet shadowing. But it's not helping.
    //ori += normalize(ori) * voxelSize * 0.5 * rand3D(ori / 6371000.0);
    /*if (length(ori) / planetRadius < 1.0)
    {
        ori = normalize(ori) * planetRadius;
    }*/
    
    
    float tmin, tmax;
    float R = planetRadius + atmosphereHeight;
    if (IntersectSphere(ori, dir, vec3(0.0), R, tmin, tmax)) // atmosphere intersection
    {
        const uint maxSteps = 1024u; // - arbitrary, for testing
        float maxDist = tmax;
        
        /*if (IntersectSphere(ori, dir, vec3(0.0), planetRadius - voxelSize, tmin, tmax))
        {
            maxDist = min(maxDist, tmin);
        }*/
        
        uint numSteps = 0u;
        
        while (dist < tmax && numSteps < maxSteps)
        {
            vec3 p = (ori + dist * dir) / atmScale;
            //p += offsetOrigin(p, dir, voxelSize / atmScale) * dist * 1e-5; // - testing
            OctreeTraversalData o;
            o.p = p;
            OctreeDescendMap(o);
            if (InvalidIndex == o.ni) break;
            
            o.center *= atmScale;
            o.size *= atmScale;
            const float step = o.size / atmScale * stepFactor;
            const float atmStep = step * atmScale;
            
            const vec3 brickOffs = vec3(BrickIndexTo3D(o.ni));
            vec3 localStart = (ori - o.center) / o.size;
            vec3 lc = localStart + dist / o.size * dir;
            lc = (p * atmScale - o.center) / o.size;
            
            vec3 tc = lc * 0.5 + 0.5;
            tc = clamp(tc, vec3(0.0), vec3(1.0));
            tc = BrickSampleCoordinates(brickOffs, tc);
            vec4 voxelData = texture(brickTexture, tc);
            
            float rayleigh = voxelData.y, mie = voxelData.x;
            float densityR = RayleighDensityFromSample(rayleigh);
            float densityM = MieDensityFromSample(mie);
            
            // - debugging aliasing with hardcoded cutoff(s)
            {
                float p = length(ori + dir * dist) / planetRadius;
                const float atmHeight = 0.01;
                const float relativeCloudTop = 0.4; // - to do: tune
                if ((p - 1.0) / atmHeight > relativeCloudTop) densityM = 0.0;
                //densityM = 0.0;
                densityR = 0.0;
                //mieDensity *= 1.0 - smoothstep(relativeCloudTop * 0.75, relativeCloudTop, (p - 1.0) / atmHeight);
            }
            
            const bool midPoint = false;//true;
            if (midPoint)
            {
                opticalDepthR += densityR * atmStep;
                opticalDepthM += densityM * atmStep;
            }
            else
            {
                opticalDepthR += (prevDensityR + densityR) * 0.5 * atmStep;
                opticalDepthM += (prevDensityM + densityM) * 0.5 * atmStep;
            }
            
            prevDensityR = densityR;
            prevDensityM = densityM;
            
            dist += atmStep;
            lc = localStart + dist / o.size * dir;
            ++numSteps;
        }
    }
    
    float opticalDepth = opticalDepthM * betaMEx;
    return exp(-opticalDepth);
}
