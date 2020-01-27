
bool IntersectSphere(vec3 ori, vec3 dir, vec3 center, float radius, out float t0, out float t1)
{
    const float radius2 = radius*radius;
    
    vec3 L = center - ori;
    float tca = dot(L, dir);
    float d2 = dot(L, L) - tca * tca;
    if (d2 > radius2) return false;
    float thc = sqrt(radius2 - d2);
    t0 = tca - thc;
    t1 = tca + thc;
    t0 = max(0.0, t0);
    if (t1 < t0) return false;
    
    return true;
}
