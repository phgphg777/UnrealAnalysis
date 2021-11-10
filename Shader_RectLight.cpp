struct FRect
{
	float3		Origin;
	float3x3	Axis;
	float2		Extent;
	float2		FullExtent;
	float2		Offset;
};

struct FRectTexture {
#if USE_SOURCE_TEXTURE
	Texture2D SourceTexture;
#else
	uint Dummy;
#endif
};

FRectTexture InitRectTexture(Texture2D SourceTexture)
{
	FRectTexture Output;
#if USE_SOURCE_TEXTURE
	Output.SourceTexture = SourceTexture;
#else
	Output.Dummy = 0;
#endif
	return Output;
}

FRect GetRect(float3 ToLight, FDeferredLightData LightData)
{
	float HalfWidth = LightData.SourceRadius; // 0.5 * Width
	float HalfHeight = LightData.SourceLength; // 0.5 * Height
	float BarnCosAngle = LightData.RectLightBarnCosAngle;
	float BarnLength = LightData.RectLightBarnLength;

	FRect Rect;
	Rect.FullExtent = float2(HalfWidth, HalfHeight);
	Rect.Axis[1] = LightData.Tangent;	// ZAxis
	Rect.Axis[2] = LightData.Direction; // -XAxis
	Rect.Axis[0] = cross( Rect.Axis[1], Rect.Axis[2] ); // -YAxis

	Rect.Origin = ToLight;
	Rect.Offset = 0;
	Rect.Extent = Rect.FullExtent;

	if (BarnCosAngle < 0.035f) // Cos(88 degree) == 0.035f
		return Rect;

	// Compute the visible rectangle from the current shading point. 
	// The new rectangle will have reduced width/height, and a shifted origin
	//                 D_S
	//                 <---------------->
	//                 BarnWidth
	//                 <-->
	//                     D_S - BarnWidth
	//                     <------------>     O         +X
	//					   -------------.--------------->------       ^
	//					  /        .          |                \      |
	//					 /   .                |					\     |  BarnDepth
	//				   ./                     |					 \    v
	//		     .      C                     v +Z
	//		.  
	// .
	// SS
	const float3 LightX	= -Rect.Axis[0]; // YAxis
	const float3 LightY	= -Rect.Axis[1]; // -ZAxis
	const float3 LightZ	= -Rect.Axis[2]; // XAxis
	const float2 LightExtent = float2(HalfWidth, HalfHeight);
	const float3 S_World = -ToLight;
	const float3 S_Light = float3(dot(S_World, LightX), dot(S_World, LightY), dot(S_World, LightZ));
	
	const float CosTheta = LightDataRectLightBarnCosAngle;
	const float SinTheta = sqrt(1 - max(0, CosTheta * CosTheta));
	const float BarnDepth = min(S_Light.z, CosTheta * BarnLength);
	const float BarnWidth	= (SinTheta/CosTheta) * BarnDepth;
	
	const float2 SignS = sign(S_Light.xy);
	const float3 SS = float3( SignS * max(abs(S_Light.xy), LightExtent + BarnWidth), S_Light.z );
	const float3 C = float3( SignS * (LightExtent + BarnWidth), BarnDepth );
		
	const float3 SProj  = SS - C;
	const float2 TanEta	= SProj.z > 0.001f ? abs(SProj.xy)/SProj.z : 10000.f;
	const float2 D_S = BarnDepth * TanEta;

	float MinX = -HalfWidth, MaxX = HalfWidth;
	if (SignS.x < 0) MinX += (D_S.x - BarnWidth);
	if (SignS.x > 0) MaxX -= (D_S.x - BarnWidth);

	float MinY = -HalfHeight, MaxY = HalfHeight;
	if (SignS.y < 0) MinY += (D_S.y - BarnWidth);
	if (SignS.y > 0) MaxY -= (D_S.y - BarnWidth);

	MinX = clamp(MinX, -HalfWidth, HalfWidth);
	MaxX = clamp(MaxX, -HalfWidth, HalfWidth);
	MinY = clamp(MinY, -HalfWidth, HalfWidth);
	MaxY = clamp(MaxY, -HalfWidth, HalfWidth);
	
	const float2 RectOffset = 0.5f * float2(MinX + MaxX, MinY + MaxY);
	Rect.Origin = ToLight + RectOffset.x * LightX + RectOffset.y * LightY;
	Rect.Offset = -RectOffset;
	Rect.Extent = 0.5f * float2(MaxX - MinX, MaxY - MinY);

	return Rect;
}

float3 SampleRectTextureInternal(Texture2D RectSourceTexture, float2 RectUV, float Level)
{
	float2 TextureSize;
	RectSourceTexture.GetDimensions( TextureSize.x, TextureSize.y );	// TODO optimize
    Level += log2(max(TextureSize.x, TextureSize.y)) - 2;
	return RectSourceTexture.SampleLevel( View.SharedTrilinearClampedSampler, RectUV, Level ).rgb;
}

float3 SampleSourceTexture( float3 L, FRect Rect, FRectTexture RectTexture)
{
#if USE_SOURCE_TEXTURE
	float DistToPlane = dot(Rect.Axis[2], Rect.Origin) / dot(Rect.Axis[2], L);
	L *= DistToPlane;

	float2 PointInRect;
	PointInRect.x = dot( Rect.Axis[0], PointOnPlane - Rect.Origin );
	PointInRect.y = dot( Rect.Axis[1], PointOnPlane - Rect.Origin );
    float2 RectUV = (PointInRect + Rect.Offset) / Rect.FullExtent * float2(0.5, -0.5) + 0.5;
	
	float2 TextureSize = RectTexture.SourceTexture.GetDimensions();	// TODO optimize
	float RectArea = 4 * Rect.FullExtent.x * Rect.FullExtent.y;
	
	float Level = -1
		+ log2(max(TextureSize.x, TextureSize.y))
		- log2(sqrt(RectArea))
		+ log2(DistToPlane);

	return RectTexture.SourceTexture.SampleLevel( View.SharedTrilinearClampedSampler, RectUV, Level ).rgb;
#else
	return 1;
#endif
}

float3 RectIrradianceLambert0( float3 N, FRect Rect, out float BaseIrradiance, out float NoL )
{
	float3 LocalPosition = float3(
		dot(Rect.Axis[0], Rect.Origin), 
		dot(Rect.Axis[1], Rect.Origin), 
		dot(Rect.Axis[2], Rect.Origin) ); 
	
	float x0 = LocalPosition.x - Rect.Extent.x;
	float x1 = LocalPosition.x + Rect.Extent.x;
	float y0 = LocalPosition.y - Rect.Extent.y;
	float y1 = LocalPosition.y + Rect.Extent.y;
	float z0 = LocalPosition.z;
	
	float3 P[5];
	P[0] = float3( x0, y0, z0 );
	P[1] = float3( x1, y0, z0 );
	P[2] = float3( x1, y1, z0 );
	P[3] = float3( x0, y1, z0 );
	P[4] = P[0];

	float3 n = float3(
		dot(Rect.Axis[0], N), 
		dot(Rect.Axis[1], N), 
		dot(Rect.Axis[2], N) ); 
	
	float3 v[6];
	uint num = 0;
	float epsilon = 0.0001;
	float nextSign;
	float sign = dot(n, P[0]) / length(P[0]);

	for(uint i=0; i<4; ++i)
	{
		nextSign = dot(n, P[i+1]) / length(P[i+1]);
		
		if(sign > -epsilon)
		{
			v[num++] = normalize(P[i]);
		}

		if(  (sign > epsilon && nextSign < -epsilon) 
			|| (sign < -epsilon && nextSign > epsilon) )
		{
			float t = -dot(n, P[i]) / dot(n, P[i+1] - P[i]);
			v[num++] = normalize(P[i] + t * (P[i+1] - P[i]));
		}
		
		sign = nextSign;
	}	
	v[num] = v[0];

	float3 L = (float3) 0;
	for(uint i=0; i<num; ++i)
	{
		float cosTheta = dot(v[i], v[i+1]);
		float Theta = ( 1.5708 - 0.175 * cosTheta ) * rsqrt( cosTheta + 1 );
		L += Theta * cross(v[i], v[i+1]);
	}
	L = L.x * Rect.Axis[0] + L.y * Rect.Axis[1] + L.z * Rect.Axis[2];
	L *= 0.5;

	BaseIrradiance = dot(N, L);
	NoL = 1.0;
	
	return normalize(L);
}

float SphereHorizonCosWrap( float CosBeta, float SinAlphaSqr )
{
	float SinAlpha = sqrt( SinAlphaSqr );

	if( SinAlpha < CosBeta )
	{
		/* Sin(Alpha) == Cos(PI/2 - Alpha)
		=> Cos(PI/2 - Alpha) < Cos(Beta)
		=> Beta < PI/2 - Alpha
		In this case, lighting source is entirely above the horizon*/	
	}	
	else if(CosBeta < -SinAlpha) 
	{
		/* -Sin(Alpha) == Cos(Alpha + PI/2)
		=> Cos(Beta) < Cos(Alpha + PI/2)
		=> Alpha + PI/2 < Beta 
		In this case, lighting source is entirely below the horizon*/
		CosBeta = 0;
	}
	else
	{
		// Hermite spline approximation
		// Fairly accurate with SinAlpha < 0.8
		// y=0 and dy/dx=0 at -SinAlpha
		// y=SinAlpha and dy/dx=1 at SinAlpha
		CosBeta = Pow2( SinAlpha + CosBeta ) / ( 4 * SinAlpha );
	}

	return CosBeta;
}

float3 RectIrradianceLambert( float3 N, FRect Rect, out float Irradiance )
{
	float3 LocalPosition = float3( // Light coordinates origined on shading point
		dot(Rect.Axis[0], Rect.Origin), 
		dot(Rect.Axis[1], Rect.Origin), 
		dot(Rect.Axis[2], Rect.Origin) ); 
	
	float x0 = LocalPosition.x - Rect.Extent.x;
	float x1 = LocalPosition.x + Rect.Extent.x;
	float y0 = LocalPosition.y - Rect.Extent.y;
	float y1 = LocalPosition.y + Rect.Extent.y;
	float z0 = LocalPosition.z;
	
	float3 L0 = normalize( float3( x0, y0, z0 ) );
	float3 L1 = normalize( float3( x1, y0, z0 ) );
	float3 L2 = normalize( float3( x1, y1, z0 ) );
	float3 L3 = normalize( float3( x0, y1, z0 ) );

	float w01 = ( 1.5708 - 0.175 * c01 ) * rsqrt( c01 + 1 );
	float w12 = ( 1.5708 - 0.175 * c12 ) * rsqrt( c12 + 1 );
	float w23 = ( 1.5708 - 0.175 * c23 ) * rsqrt( c23 + 1 );
	float w30 = ( 1.5708 - 0.175 * c30 ) * rsqrt( c30 + 1 );

	float3 L = acos(dot(L0, L1)) * normalize(cross(L0, L1));
	L += acos(dot(L1, L2)) * normalize(cross(L1, L2));
	L += acos(dot(L2, L3)) * normalize(cross(L2, L3));
	L += acos(dot(L3, L0)) * normalize(cross(L3, L0));
	L = L.x * Rect.Axis[0] + L.y * Rect.Axis[1] + L.z * Rect.Axis[2];
	L /= (2*PI);	

	float NormL = length(L);
	float SinAlphaSqr = NormL;
	float CosBeta = dot(N, L) / NormL;
	float InvPi_Cos_Integration = SinAlphaSqr * SphereHorizonCosWrap( CosBeta, SinAlphaSqr );

	Irradiance = PI * InvPi_Cos_Integration;
	return L / NormL;
}

float3 RectGGXApproxLTC( float Roughness, float3 SpecularColor, half3 N, float3 V, FRect Rect, FRectTexture RectTexture )
{
	float NoV = saturate( abs( dot(N, V) ) + 1e-5 );

	float2 UV = float2( Roughness, sqrt( 1 - NoV ) );
	UV = UV * (63.0 / 64.0) + (0.5 / 64.0);
   
	float4 LTCMat = LTCMatTexture.SampleLevel( LTCMatSampler, UV, 0 );
	float4 LTCAmp = LTCAmpTexture.SampleLevel( LTCAmpSampler, UV, 0 );

	float3x3 LTC = {
		float3( LTCMat.x, 0, LTCMat.z ),
		float3(        0, 1,        0 ),
		float3( LTCMat.y, 0, LTCMat.w )
	};

	float LTCDet = LTCMat.x * LTCMat.w - LTCMat.y * LTCMat.z;

	float4 InvLTCMat = LTCMat / LTCDet;
	float3x3 InvLTC = {
		float3( InvLTCMat.w, 0,-InvLTCMat.z ),
		float3(	          0, 1,           0 ),
		float3(-InvLTCMat.y, 0, InvLTCMat.x )
	};

	// Rotate to tangent space
	float3 T1 = normalize( V - N * dot( N, V ) );
	float3 T2 = cross( N, T1 );
	float3x3 TangentBasis = float3x3( T1, T2, N );

	LTC = mul( LTC, TangentBasis );
	InvLTC = mul( transpose( TangentBasis ), InvLTC );

	float3 Poly[4];
	Poly[0] = mul( LTC, Rect.Origin - Rect.Axis[0] * Rect.Extent.x - Rect.Axis[1] * Rect.Extent.y );
	Poly[1] = mul( LTC, Rect.Origin + Rect.Axis[0] * Rect.Extent.x - Rect.Axis[1] * Rect.Extent.y );
	Poly[2] = mul( LTC, Rect.Origin + Rect.Axis[0] * Rect.Extent.x + Rect.Axis[1] * Rect.Extent.y );
	Poly[3] = mul( LTC, Rect.Origin - Rect.Axis[0] * Rect.Extent.x + Rect.Axis[1] * Rect.Extent.y );

	float3 L = PolygonIrradiance( Poly );
	L = mul( InvLTC, L );

	float NormL = length(L);
	float SinAlphaSqr = NormL;
	float CosBeta = dot(N, L) / NormL;
	float GGX_WithoutF_Cos_Integration = SinAlphaSqr * SphereHorizonCosWrap( CosBeta, SinAlphaSqr );
	
	float3 LightColor = SampleSourceTexture( L, Rect, RectTexture );
	SpecularColor = LTCAmp.y + ( LTCAmp.x - LTCAmp.y ) * SpecularColor;

	return LightColor * GGX_WithoutF_Cos_Integration * SpecularColor;
}

FDirectLighting IntegrateBxDF( 
	FGBufferData GBuffer, half3 N, half3 V, FRect Rect, , FRectTexture SourceTexture )
{
	if (Rect.Extent.x == 0 || Rect.Extent.y == 0) // No-visible rect light due to barn door occlusion
        return (FDirectLighting)0;
	
	float Irradiance
	float3 L = RectIrradianceLambert( N, Rect, Irradiance );
	
	FAreaLight AreaLight = (AreaLight)0;
	AreaLight.LineCosSubtended = 1;
	AreaLight.FalloffColor = SampleSourceTexture( L, Rect, SourceTexture );
	AreaLight.Rect = Rect;
	AreaLight.Texture = SourceTexture;
	AreaLight.bIsRect = true;

	switch( GBuffer.ShadingModelID )
	{
		case SHADINGMODELID_DEFAULT_LIT:
			return DefaultLitBxDF( 
				GBuffer, N, V, 
				L, Irradiance, AreaLight, );
		default:
			return (FDirectLighting)0;
	}
}

// GBuffer.DiffuseColor  = lerp(BaseColor, 0, Metallic);
// GBuffer.SpecularColor = lerp(float3(0.08f*Specular), BaseColor, Metallic); 
FDirectLighting DefaultLitBxDF( 
	FGBufferData GBuffer, half3 N, half3 V, 
	half3 L, float Irradiance, float NoL, FAreaLight AreaLight, )
{	
	BxDFContext Context;
	Init( Context, N, V, L );
	Context.NoV = saturate( abs( Context.NoV ) + 1e-5 );

	FDirectLighting Lighting;
	Lighting.Transmission = 0;
	Lighting.Diffuse  = AreaLight.FalloffColor * Irradiance * Diffuse_Lambert( GBuffer.DiffuseColor ); 
	Lighting.Specular = RectGGXApproxLTC( GBuffer.Roughness, GBuffer.SpecularColor, N, V, AreaLight.Rect, AreaLight.Texture ) ;
	return Lighting;
}








