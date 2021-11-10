#define GBUFFER_HAS_TANGENT 0


float3 Diffuse_Lambert( float3 DiffuseColor )
{
	return DiffuseColor * (1 / PI);
}

float3 F_Schlick( float3 SpecularColor, float VoH )
{
	float Fc = Pow5( 1 - VoH );					// 1 sub, 3 mul
	return Fc + (1 - Fc) * SpecularColor;		// 1 add, 3 mad
	
}

float3 F_Fresnel( float3 SpecularColor, float VoH )
{
	float3 SpecularColorSqrt = sqrt( clamp( float3(0, 0, 0), float3(0.99, 0.99, 0.99), SpecularColor ) );
	float3 n = ( 1 + SpecularColorSqrt ) / ( 1 - SpecularColorSqrt );
	float3 g = sqrt( n*n + VoH*VoH - 1 );
	return 0.5 * Square( (g - VoH) / (g + VoH) ) * ( 1 + Square( ((g+VoH)*VoH - 1) / ((g-VoH)*VoH + 1) ) );
}

float D_GGX( float a2, float NoH )
{
	float d = ( NoH * a2 - NoH ) * NoH + 1;	// 2 mad
	return a2 / ( PI*d*d );					// 4 mul, 1 rcp
}

float Vis_Smith( float a2, float NoV, float NoL ) // [1/(1+Lambda(NoV)) * 1/(1+Lambda(NoL))] / (4*NoV*NoL)
{
	float Vis_SmithV = NoV + sqrt(NoV * (NoV - NoV * a2) + a2);
	float Vis_SmithL = NoL + sqrt(NoL * (NoL - NoL * a2) + a2);
	return rcp(Vis_SmithV * Vis_SmithL);
}

float Vis_SmithJoint(float a2, float NoV, float NoL)  // [1 / (1+Lambda(NoV)+Lambda(NoL))] / (4*NoV*NoL)
{
	float Vis_SmithV = NoL * sqrt(NoV * (NoV - NoV * a2) + a2);
	float Vis_SmithL = NoV * sqrt(NoL * (NoL - NoL * a2) + a2);
	return 0.5 * rcp(Vis_SmithV + Vis_SmithL);
}

float Vis_SmithJointApprox( float a2, float NoV, float NoL )
{
	float a = sqrt(a2);
	float Vis_SmithV = NoL * ( NoV * ( 1 - a ) + a );
	float Vis_SmithL = NoV * ( NoL * ( 1 - a ) + a );
	return 0.5 * rcp( Vis_SmithV + Vis_SmithL );
}

float3 SpecularGGX( float Roughness, float3 SpecularColor, BxDFContext Context, float NoL, FAreaLight AreaLight )
{
	float a2 = Pow4( Roughness );
	float Energy = EnergyNormalization( a2, Context.VoH, AreaLight );
	float D = D_GGX( a2, Context.NoH ) * Energy;
	float Vis = Vis_SmithJointApprox( a2, Context.NoV, NoL ); // G / (4*NoL*NoV)
	float3 F = F_Schlick( SpecularColor, Context.VoH );
	return (D * Vis) * F;
}


struct BxDFContext {
	float NoV;
	float NoL;
	float VoL;
	float NoH;
	float VoH;
}

void Init( inout BxDFContext Context, half3 N, half3 V, half3 L )
{
	Context = 0;
	Context.NoL = dot(N, L);
	Context.NoV = dot(N, V);
	Context.VoL = dot(V, L);
	float InvLenH = rsqrt( 2 + 2 * Context.VoL ); // half3 H = (L + V) * InvLenH;
	Context.NoH = saturate((Context.NoL + Context.NoV) * InvLenH);
	Context.VoH = saturate((1 + Context.VoL) * InvLenH);
}

FDirectLighting DefaultLitBxDF( 
	FGBufferData GBuffer, 
	half3 N, half3 V, half3 L, 
	float Falloff, float NoL, 
	FAreaLight AreaLight, )
{	
	BxDFContext Context;
	Init( Context, N, V, L );
	SphereMaxNoH( Context, AreaLight.SphereSinAlpha, true );
	Context.NoV = saturate( abs( Context.NoV ) + 1e-5 );
	float3 FalloffColor = Falloff * (AreaLight.bIsRect ? AreaLight.FalloffColor : 1);

	FDirectLighting Lighting;
	Lighting.Transmission = 0;
	Lighting.Diffuse  = FalloffColor * NoL * Diffuse_Lambert( GBuffer.DiffuseColor ); 
	Lighting.Specular = AreaLight.bIsRect ? 
		RectGGXApproxLTC( GBuffer.Roughness, GBuffer.SpecularColor, N, V, AreaLight.Rect, AreaLight.Texture ) : 
		FalloffColor * NoL * SpecularGGX( GBuffer.Roughness, GBuffer.SpecularColor, Context, NoL, AreaLight ) ;
	return Lighting;
}

