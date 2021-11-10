////////////////////////////////////////////////////////////////////////////////////////////////
float3 Sca_CrossSection = Albedo * Ext_CrossSection;
float Phase = 1 / (4*PI);
float4x4 WorldToLocal = GetPrimitiveData(Parameters.PrimitiveId).WorldToLocal;
float3 CameraPos = mul(float4(ResolvedView.WorldCameraOrigin, 1), WorldToLocal).xyz;
float3 RayDir = mul(float4(-Parameters.CameraVector, 0), WorldToLocal).xyz;
float3 SunDir = mul(float4(SunDirWS, 0), WorldToLocal).xyz;
float3 BoundSize = VolumeExtents;
float3 MaxBounds = BoundSize * 0.5;
float3 MinBounds = -MaxBounds;

float3 A = (MinBounds - CameraPos) / RayDir;
float3 B = (MaxBounds - CameraPos) / RayDir;
float3 C = min(A,B);
float3 D = max(A,B);
float tmin = max( max( C.x, C.y ), C.z );
float tmax = min( min( D.x, D.y ), D.z );
if (tmin > tmax) return float4(0,0,0,0);
if (tmax < 0   ) return float4(0,0,0,0);
tmin = max(0, tmin);

float dT = StepSize;
float3 dP = dT * RayDir / BoundSize;
float dS = ShadowStepSize;
float3 dQ = dS * SunDir / BoundSize;
float T = tmin + 0.5 * dT;
float3 P = (CameraPos + T * RayDir - MinBounds) / BoundSize;

float3 Radiance = 0;
float Transmittance = 1;
LOOP
for (int i=0; i<MaxSteps && T<tmax; ++i, T+=dT, P+=dP) 
{
    float Density = DensityMult * Texture3DSample(VolumeData, VolumeDataSampler, P).r;
    if(Density < 1e-6)
        continue;

    float TotalDensity = 0;
    LOOP
    for(int j=0, float3 Q=P+0.5*dQ; j<ShadowNumSteps; ++j, Q+=dQ)
    {
        if(dot(1, floor(abs(Q - 0.5) + 0.5)) > 0)
            break;
        TotalDensity += Texture3DSample(VolumeData, VolumeDataSampler, Q).r;
    }
    float OpticalDepth = dS * TotalDensity * DensityMult * Ext_CrossSection;

    Radiance += dT * Transmittance 
        * (Density * Sca_CrossSection * Phase * exp(-OpticalDepth) * SunLighting);
    Transmittance *= exp(-dT * Density * Ext_CrossSection);

    if( Transmittance < 1e-4 )
        break;
}
return float4(Radiance, 1 - Transmittance);
////////////////////////////////////////////////////////////////////////////////////////////////

float3 Radiance = 0;
float3 Transmittance = 1;
LOOP
for (int i=0; i<MaxSteps && T<tmax; ++i, T+=dT, P+=dP) 
{
    float Density = DensityMult * Texture3DSample(VolumeData, VolumeDataSampler, P).r;
    if(Density < 1e-6)
        continue;

    float TotalDensity = 0;
    LOOP
    for(int j=0, float3 Q=P+0.5*dQ; j<ShadowNumSteps; ++j, Q+=dQ)
    {
        if(dot(1, floor(abs(Q - 0.5) + 0.5)) > 0)
            break;
        TotalDensity += Texture3DSample(VolumeData, VolumeDataSampler, Q).r;
    }
    float3 OpticalDepth = dS * TotalDensity * DensityMult * Ext_CrossSection;

    Radiance += dT * Transmittance 
        * (Density * Sca_CrossSection * Phase * exp(-OpticalDepth) * SunLighting);
    Transmittance *= exp(-dT * Density * Ext_CrossSection);

    if( max(max(Transmittance.r, Transmittance.g), Transmittance.b) < 1e-4 )
        break;
}
//Radiance += SceneColor * Transmittance;
Radiance += SceneColor * (Transmittance - 1);
return float4(Radiance, 1);

////////////////////////////////////////////////////////////////////////////////////////////////



float4 RayMarchMain(
    Texture3D VolumeData, 
    float3 VolumeExtents,
    float CellSize,
    float DensityMult,
    float MaxSteps, 
    float StepSizeMult, /*=1.0*/
    /*Shadow*/float ShadowIntensity, /*=1.0*/
    /*Shadow*/float ShadowNumSteps,
    /*Shadow*/float ShadowStepSizeMult,
    /*Shadow*/float ShadowStartSampleWeight, /*=0.5*/
    /*particle*/float3 Albedo, 
    /*particle*/float3 Ext_CrossSection, 
    /*particle*/float3 Emi_CrossSection,
    /*sun*/float3 SunDirWS,
    /*sun*/float3 SunLighting,
    float SceneColor,
    float SceneDepth)
{
	float4x4 WorldToLocal = GetPrimitiveData(Parameters.PrimitiveId).WorldToLocal;
	float3 PixelPosWS = Parameters.AbsoluteWorldPosition;
	float3 CameraPosWS = ResolvedView.WorldCameraOrigin;
	float3 RayDirWS = normalize(PixelPosWS - CameraPosWS);
    float3 CameraFwd = mul(float3(0,0,1), (float3x3)ResolvedView.ViewToTranslatedWorld);
	float MaxRayLength = SceneDepth / dot(RayDirWS, CameraFwd);

	float3 CameraPosLS = mul(float4(CameraPosWS, 1), WorldToLocal).xyz;
	float3 RayDirLS = mul(float4(RayDirWS, 0), WorldToLocal).xyz;
    float3 SunDirLS = mul(float4(SunDirWS, 0), WorldToLocal).xyz;
	
    float3 BoundSize = VolumeExtents;
    float3 MaxBounds = BoundSize * 0.5;
    float3 MinBounds = -MaxBounds;

	float3 dirfrac = 1.0 / RayDirLS;
    float t1 = (MinBounds.x - CameraPosLS.x) * dirfrac.x;
    float t2 = (MaxBounds.x - CameraPosLS.x) * dirfrac.x;
    float t3 = (MinBounds.y - CameraPosLS.y) * dirfrac.y;
    float t4 = (MaxBounds.y - CameraPosLS.y) * dirfrac.y;
    float t5 = (MinBounds.z - CameraPosLS.z) * dirfrac.z;
    float t6 = (MaxBounds.z - CameraPosLS.z) * dirfrac.z;
    float tmin = max( max( min(t1,t2), min(t3,t4) ), min(t5,t6) );
    float tmax = min( min( max(t1,t2), max(t3,t4) ), max(t5,t6) );
    if (tmin > tmax)// if tmin > tmax, ray doesn't intersect AABB
    {
        return float4(0,0,0,1);
    }
    else if (tmax < 0)// if tmax < 0, ray (line) is intersecting AABB, but the whole AABB is behind us
    {
        return float4(0,0,0,1);
    }
    tmin = max(0, tmin);
    tmax = min(tmax, MaxRayLength);

    float T = tmin;
    float dT = CellSize * StepSizeMult;
    float3 P = (CameraPosLS + T * RayDirLS - MinBounds) / BoundSize;
    float3 dP = dT * RayDirLS / BoundSize;
    float dS = CellSize * ShadowStepSizeMult;
    float3 dQ = dS * SunDirLS / BoundSize;

    float3 Sca_CrossSection = Albedo * Ext_CrossSection;
    float Phase = 1 / (4*PI);
    
    float3 Radiance = 0;
    float3 Transmittance = 1;

    float3 Temp1 = 0;
    float3 Temp2 = 0;
    float ODMult = -dT * DensityMult * Ext_CrossSection;
    float3 ShadowODScale = -dS * ShadowIntensity * DensityMult * Ext_CrossSection;

    for (int i = 0; i < MaxSteps && T < tmax; ++i, T+=dT, P+=dP) 
    {
        float Density = Texture3DSample(VolumeData, VolumeDataSampler, P).r;
        if(Density * DensityMult < 1e-6)
            continue;

        float ShadowOD = 0;
        float3 Q = P + 0.5*dQ;
        for(int j=0; j < ShadowNumSteps; ++j, Q+=dQ)
        {
            if(dot(1, floor(abs(Q - 0.5) + 0.5)) > 0)
                break;
            ShadowOD += Texture3DSample(VolumeData, VolumeDataSampler, Q).r;
        }

        //Radiance += (dT * DensityMult * Density) * Transmittance * (Emi_CrossSection + Sca_CrossSection * Phase * exp(ShadowOD * ShadowODScale) * SunLighting);
        Temp1 += Density * Transmittance;
        Temp2 += Density * Transmittance * exp(ShadowOD * ShadowODScale);
        Transmittance *= exp(Density * ODMult);
        if( max(max(Transmittance.r, Transmittance.g), Transmittance.b) < 1e-4 )
            break;
    }
    Radiance = dT * DensityMult * (Emi_CrossSection * Temp1 + Sca_CrossSection * Phase * SunLighting * Temp2); 
    
    return float4(Radiance, dot(Transmittance, float3(0.33333)));

    Radiance += SceneColor * Transmittance;
    return float4(Radiance, 0);
}


3. phase





float X = (IndexX+0.5) * CellSize;
float Y = (IndexY+0.5) * CellSize;
float Z = (IndexZ+0.5) * CellSize;
float minZ = 50;
float maxZ = 130;
float waveLength = 200;

float eval = sin(2*PI*X/waveLength) * sin(2*PI*Y/waveLength);
eval = 0.5 * (eval + 1);

Value = saturate( eval - (Z-minZ)/(maxZ-minZ) );
//Value = saturate(minZ + (maxZ-minZ)*eval - Z);
Value = pow(Value,  2);