

struct FLightAccumulator {
	float3 TotalLight;
	float3 TotalLightDiffuse;
	float3 TotalLightSpecular;
	float3 ScatterableLight;// only actually used SUBSURFACE_CHANNEL_MODE == 2
	float ScatterableLightLuma;// only actually used SUBSURFACE_CHANNEL_MODE == 1
};

struct FDeferredLightingSplit {
	float4 DiffuseLighting;
	float4 SpecularLighting;
};

float4 LightAccumulator_GetResult(FLightAccumulator In)
{
	float Alpha = 0.0f;
	if (SUBSURFACE_CHANNEL_MODE == 1 && View.bCheckerboardSubsurfaceProfileRendering == 0)
	{
		Alpha = In.ScatterableLightLuma;
	}
	else if (SUBSURFACE_CHANNEL_MODE == 2)
	{
		Alpha = Luminance(In.ScatterableLight);
	}

	float4 Ret = float4(In.TotalLight, Alpha);
	return Ret;
}

FDeferredLightingSplit LightAccumulator_GetResultSplit(FLightAccumulator In)
{
	float Alpha = 0.0f;
	if (SUBSURFACE_CHANNEL_MODE == 1 && View.bCheckerboardSubsurfaceProfileRendering == 0)
	{
		Alpha = In.ScatterableLightLuma;
	}
	else if (SUBSURFACE_CHANNEL_MODE == 2)
	{
		Alpha = Luminance(In.ScatterableLight);
	}

	FDeferredLightingSplit Ret;
	Ret.DiffuseLighting = float4(In.TotalLightDiffuse, Alpha);
	Ret.SpecularLighting = float4(In.TotalLightSpecular, 0);
	return Ret;
}

void LightAccumulator_AddSplit(inout FLightAccumulator In, 
	float3 DiffuseTotalLight, 
	float3 SpecularTotalLight, 
	float3 ScatterableLight, 
	float3 CommonMultiplier, 
	const bool bNeedsSeparateSubsurfaceLightAccumulation)
{
	if (bNeedsSeparateSubsurfaceLightAccumulation)
	{
		if (SUBSURFACE_CHANNEL_MODE == 1)
		{
			if (View.bCheckerboardSubsurfaceProfileRendering == 0)
			{
				In.ScatterableLightLuma += Luminance(ScatterableLight * CommonMultiplier);
			}
		}
		else if (SUBSURFACE_CHANNEL_MODE == 2)
		{
			In.ScatterableLight += ScatterableLight * CommonMultiplier;
		}
	}

	In.TotalLightDiffuse += DiffuseTotalLight * CommonMultiplier;
	In.TotalLightSpecular += SpecularTotalLight * CommonMultiplier;
	In.TotalLight += (DiffuseTotalLight + SpecularTotalLight) * CommonMultiplier;
}

void LightAccumulator_Add(inout FLightAccumulator In, 
	float3 TotalLight, 
	float3 ScatterableLight, 
	float3 CommonMultiplier, 
	const bool bNeedsSeparateSubsurfaceLightAccumulation)
{
	LightAccumulator_AddSplit(In, 
		TotalLight, 
		0.0f, 
		ScatterableLight, 
		CommonMultiplier, 
		bNeedsSeparateSubsurfaceLightAccumulation);
}


void ReflectionEnvironmentSkyLighting()
{
	float3 SkyLighting = SkyLightDiffuse(GBuffer, AmbientOcclusion, BufferUV, ScreenPosition, BentNormal, DiffuseColor);
		
	FLightAccumulator LightAccumulator = (FLightAccumulator)0;
	LightAccumulator_Add(LightAccumulator, SkyLighting, SkyLighting, 1.0f, UseSubsurfaceProfile(ShadingModelID));
	OutColor = LightAccumulator_GetResult(LightAccumulator);
}

void FPixelShaderInOut_MainPS()
{
	half3 DiffuseColor = 0;
	half3 Color = 0;
	float IndirectIrradiance = 0;
	
	float3 DiffuseColorForIndirect = GBuffer.DiffuseColor;
	float3 DiffuseIndirectLighting;
	float3 SubsurfaceIndirectLighting;
	GetPrecomputedIndirectLightingAndSkyLight(, , , , , , , DiffuseIndirectLighting, SubsurfaceIndirectLighting, IndirectIrradiance);

	float3 DiffuseColor += (DiffuseIndirectLighting * DiffuseColorForIndirect + SubsurfaceIndirectLighting * SubsurfaceColor) * 
		AOMultiBounce( GBuffer.BaseColor, MaterialAO );

#if MATERIALBLENDING_TRANSLUCENT
	Out.MRT[0] = half4(Color * Fogging.a + Fogging.rgb, Opacity);

#else
	{
		FLightAccumulator LightAccumulator = (FLightAccumulator)0;

		Color = Color * Fogging.a + Fogging.rgb;

	#if POST_PROCESS_SUBSURFACE
		DiffuseColor = DiffuseColor * Fogging.a + Fogging.rgb;

		if (UseSubsurfaceProfile(GBuffer.ShadingModelID) && 
	        View.bSubsurfacePostprocessEnabled > 0 && 
	        View.bCheckerboardSubsurfaceProfileRendering > 0 )
		{
			Color *= !bChecker;
		}
		LightAccumulator_Add(LightAccumulator, Color + DiffuseColor, DiffuseColor, 1.0f, UseSubsurfaceProfile(GBuffer.ShadingModelID));

	#else
		LightAccumulator_Add(LightAccumulator, Color, 0, 1.0f, false);
	#endif

		Out.MRT[0] = LightAccumulator_GetResult(LightAccumulator);	
	}
#endif
}