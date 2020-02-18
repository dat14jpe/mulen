#version 450

#include "../noise.glsl"
#include "../geometry.glsl"
#include "common.glsl"
layout(local_size_x = BrickRes, local_size_y = BrickRes, local_size_z = BrickRes) in;
#include "compute.glsl"

uniform layout(binding=0, r16f) writeonly image3D lightImage;
uniform uint brickUploadOffset;

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

float TraceTransmittance(vec3 ori, vec3 dir, float dist, vec3 nodeCenter, float nodeSize, uint ni, const float stepFactor)
{
    const float atmScale = atmosphereRadius;
    
    float opticalDepthM = 0.0;
    float prevDensityM = 0.0; // - maybe to do: get these from start values

    //ori += offsetOrigin(p, dir, voxelSize); // - trying this *after* the planet shadowing. But it's not helping.
    
    const float thresholdFraction = 1e-2; // - might need tuning
    const float threshold = -log(thresholdFraction) / betaMEx;
    
    float tmin, tmax;
    float R = planetRadius + atmosphereHeight;
    if (IntersectSphere(ori, dir, planetLocation, R, tmin, tmax)) // atmosphere intersection
    {
        const uint maxSteps = 512u; // - arbitrary, for testing
        float maxDist = tmax;
        
        /*if (IntersectSphere(ori, dir, planetLocation, planetRadius - voxelSize, tmin, tmax))
        {
            maxDist = min(maxDist, tmin);
        }*/
     
        uint depth; // - to do: initialise correctly
        
        uint numBricks = 0u, numSteps = 0u;
        
        vec3 p = (ori + dist * dir) / atmScale;
        ni = OctreeDescendMap(p, nodeCenter, nodeSize, depth);
        
        while (InvalidIndex != ni)
        {
            nodeCenter *= atmScale;
            nodeSize *= atmScale;
            const float step = nodeSize / atmScale * stepFactor;
            const float atmStep = step * atmScale;
            
            AabbIntersection(tmin, tmax, vec3(-nodeSize) + nodeCenter, vec3(nodeSize) + nodeCenter, ori, dir);
            tmax = min(tmax, maxDist);
            
            const vec3 brickOffs = vec3(BrickIndexTo3D(ni));
            vec3 localStart = (ori - nodeCenter) / nodeSize;
            vec3 lc = localStart + dist / nodeSize * dir;
            
            //do // do while to not erroneously miss the first voxel if it's on the border
            while (dist < tmax && numSteps < maxSteps)
            {
                vec3 tc = lc * 0.5 + 0.5;
                tc = clamp(tc, vec3(0.0), vec3(1.0));
                tc = BrickSampleCoordinates(brickOffs, tc);
                vec4 voxelData = texture(brickTexture, tc);
                
                float mie = voxelData.x;
                float densityM = MieDensityFromSample(mie);
                
                densityM = max(0.0, (mie * scaleM + offsetM) * 200.0); // - a test
                
                // - debugging aliasing with hardcoded cutoff(s)
                {
                    float p = length(ori + dir * dist) / planetRadius;
                    const float atmHeight = 0.01;
                    const float relativeCloudTop = 0.4; // - to do: tune
                    if ((p - 1.0) / atmHeight > relativeCloudTop) densityM = 0.0;
                    //densityM = 0.0;
                    //mieDensity *= 1.0 - smoothstep(relativeCloudTop * 0.75, relativeCloudTop, (p - 1.0) / atmHeight);
                }
                
                opticalDepthM += (prevDensityM + densityM) * 0.5 * atmStep;
                prevDensityM = densityM;
                
                dist += atmStep;
                lc = localStart + dist / nodeSize * dir;
                ++numSteps;
            } //while (dist < tmax && numSteps < maxSteps);
            ++numBricks;
            
            if (opticalDepthM > threshold) break; // - testing
            
            const uint old = ni;
            vec3 p = (ori + dist * dir) / atmScale;
            //if (length(p * atmScale) < planetRadius * 0.99) break; // - testing (but this should be done accurately and once with a maxDist update)
            if (dist > maxDist) break;
            ni = OctreeDescendMap(p, nodeCenter, nodeSize, depth);
            if (old == ni) break; // - error (but can this even happen?)
            
            if (numBricks >= 64u) break; // - testing
            // - maybe also try breaking if transmittance is too high (though needlessly evaluating it might be expensive in itself)
        }
    }
    
    float opticalDepth = opticalDepthM * betaMEx;
    return exp(-opticalDepth);
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
        ori = planetLocation + normalize(ori - planetLocation) * planetRadius;
    }*/
    
    
    float tmin, tmax;
    float R = planetRadius + atmosphereHeight;
    if (IntersectSphere(ori, dir, planetLocation, R, tmin, tmax)) // atmosphere intersection
    {
        const uint maxSteps = 512u; // - arbitrary, for testing
        float maxDist = tmax;
        
        /*if (IntersectSphere(ori, dir, planetLocation, planetRadius - voxelSize, tmin, tmax))
        {
            maxDist = min(maxDist, tmin);
        }*/
        
        uint numSteps = 0u;
        
        while (dist < tmax && numSteps < maxSteps)
        {
            vec3 p = (ori + dist * dir) / atmScale;
            //p += offsetOrigin(p, dir, voxelSize / atmScale) * dist * 1e-5; // - testing
            vec3 nodeCenter;
            float nodeSize;
            uint depth;
            uint ni = OctreeDescendMap(p, nodeCenter, nodeSize, depth);
            if (InvalidIndex == ni) break;
            
            nodeCenter *= atmScale;
            nodeSize *= atmScale;
            const float step = nodeSize / atmScale * stepFactor;
            const float atmStep = step * atmScale;
            
            const vec3 brickOffs = vec3(BrickIndexTo3D(ni));
            vec3 localStart = (ori - nodeCenter) / nodeSize;
            vec3 lc = localStart + dist / nodeSize * dir;
            lc = (p * atmScale - nodeCenter) / nodeSize;
            
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
            lc = localStart + dist / nodeSize * dir;
            ++numSteps;
        }
    }
    
    float opticalDepth = opticalDepthM * betaMEx;
    return exp(-opticalDepth);
}

void main()
{
    const uint loadId = GetWorkGroupIndex() + brickUploadOffset;
    const UploadBrick upload = uploadBricks[loadId];
    uvec3 writeOffs = BrickIndexTo3D(upload.brickIndex) * BrickRes + gl_LocalInvocationID;
    
    vec3 lp = vec3(gl_LocalInvocationID) / float(BrickRes - 1u) * 2 - 1;
    vec3 gp = (upload.nodeLocation.xyz + upload.nodeLocation.w * lp);
    vec3 p = gp * atmosphereScale;
    
    
    const float voxelSize = 1.0 / float(BrickRes - 1u) * 2 * upload.nodeLocation.w * atmosphereScale * planetRadius;
    const float atmScale = atmosphereRadius;
    float dist = 0.0;
    
    if (false)
    {
        // - experiment: clamp p to spherical position
        vec3 pp = p * planetRadius;
        float len = length(pp);
        float shell = ceil(len / voxelSize);
        pp = pp / len * shell * voxelSize;
        p = pp / planetRadius;
    }
    if (false)
    {
        if (length(p) < 1.01) p = normalize(p);
    }
    
    const vec3 dir = lightDir;
    vec3 ori = p * planetRadius;
    
    const float stepFactor = 0.1 * stepSize; // - to-be-tuned
    
    vec3 light = vec3(1.0);
    {
        float dist = 0.0;
        //dist += voxelSize * 1.0 * (rand(gp.xy, gp.z)); // - to do: make this work
        
        float shadow = PlanetShadow(ori, dir, vec3(0.0), voxelSize);
        //shadow = min(1.0, shadow); // - test (probably bad, for interpolation. Or not?)
        //shadow = 1.0; // - debugging banding
        light = vec3(1.0);
        
        //ori += offsetOrigin(p, dir, voxelSize); // - trying this *after* the planet shadowing. But it's not helping.
        
        //if (false)
        if (shadow > 0.0
            && length(ori - planetLocation) < planetRadius + atmosphereHeight // only ray trace for voxels within the atmosphere
            )
        {
            //dist += 1 * stepFactor * upload.nodeLocation.w * atmScale; // avoid self-shadowing
            dist += sqrt(2.0) * voxelSize; // avoid self-shadowing
            //ori += 0.5 * voxelSize * normalize(p); // trying to avoid more self-shadowing, but... incorrect?
            
            vec3 nodeCenter = upload.nodeLocation.xyz;
            float nodeSize = upload.nodeLocation.w;
            uint ni = upload.nodeIndex;
            light *= TraceTransmittance(ori, dir, dist, nodeCenter, nodeSize, ni, stepFactor);
            //light *= ConeTraceTransmittance(ori, dir, dist, stepFactor, voxelSize);
            
            
            if (false)
            { // - experimenting with supersampling:
                vec3 a = perpendicular(dir);
                vec3 b = cross(dir, a);
                
                /*// - testing tangent plane instead: (to no discernible improvement)
                vec3 n = normalize(p);
                a = perpendicular(n);
                b = cross(n, a);*/
                
                float num = 0.0;
                light = vec3(0.0);
                int ires = 4; // - tested up to 4, with no noticeable improvements over 2 (which were already minimal)
                float res = float(ires);
                for (int iy = 0; iy < ires; ++iy)
                for (int ix = 0; ix < ires; ++ix)
                {
                    float x = float(ix) / (res - 1) * 2 - 1, y = float(iy) / (res - 1) * 2 - 1;
                    vec3 p = ori + (a * x + b * y) * voxelSize * 0.5;
                    light += TraceTransmittance(p, dir, dist, nodeCenter, nodeSize, ni, stepFactor);
                    num += 1.0;
                }
                light /= num;
                
                /*
                // - just testing:
                light += TraceTransmittance(ori + dir * voxelSize, dir, dist, nodeCenter, nodeSize, ni, stepFactor);
                light *= 0.5;*/
            }
        }
        // - outdated by newer transmittance method, no? But its absence causes more banding now
        //light *= shadow;
        //if (shadow < 0.0) light = vec3(shadow); // - test
    }
    
    //light = clamp(light, vec3(0.0), vec3(1.0)); // - probably not needed, and... even incorrect?
    //light = max(light, vec3(0.0));
    imageStore(lightImage, ivec3(writeOffs), vec4(light, 0.0));
}
