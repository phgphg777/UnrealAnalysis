

ProjMatrix = 
|	cot_x	0 		0 		0	|
|	0		cot_y	0		0	|
|	jitterX	jitterY	0		1	|
|	0		0		near 	0 	|
// : [x y z 1] -> [x*cot_x + z*jitterX,  y*cot_y + z*jitterY, near,  z] -> [(x/z)cot_x + jitterX,  (y/z)cot_y + jitterY,  near/z,  1]
// , (BottomLeft) -> (-1,-1) + (jitterX,jitterY), (TopRight) -> (1,1) + (jitterX,jitterY),   near -> 1, inf -> 0
InvProjMatrix = 
|	tan_x			0				0		0		|
|	0				tan_y			0		0		|
|	0				0				0		1/near 	|
|	-jitterX*tan_x	-jitterY*tan_y	1		0		|

ViewMatrix = 
|	CameraY.x 				CameraZ.x 				CameraX.x 				0	|
|	CameraY.y 				CameraZ.y 				CameraX.y 				0	|
|	CameraY.z 				CameraZ.z 				CameraX.z 				0	|
|	-dot(CameraY, CameraO)	-dot(CameraZ, CameraO)	-dot(CameraX, CameraO)	1	|
 =
|	1 			0 			0 			0	|		|	CameraY.x	CameraZ.x	CameraX.x	0	|
|	0 			1 			0			0	|   *  	|	CameraY.y	CameraZ.y	CameraX.y	0	|
|	0 			0 			1			0	|		|	CameraY.z	CameraZ.z	CameraX.z	0	|
|	-CameraO.x 	-CameraO.y 	-CameraO.z 	1	|		|	0			0			0			1	|
 = 
|	1 			0 			0 			0	|		|	CameraX.x	CameraY.x	CameraZ.x	0	|		|	0	0	1	0	|
|	0 			1 			0			0	|   *  	|	CameraX.y	CameraY.y	CameraZ.y	0	|   *  	|	1	0	0	0	|
|	0 			0 			1			0	|		|	CameraX.z	CameraY.z	CameraZ.z	0	|		|	0	1	0	0	|
|	-CameraO.x 	-CameraO.y 	-CameraO.z 	1	|		|	0			0			0			1	|		|	0	0	0	1	|

InvViewMatrix =
|	CameraY		0	|
|	CameraZ		0	|
|	CameraX		0	|
|	CameraO		1	|


enum class ERHIZBuffer
{
	FarPlane = 0,
	NearPlane = 1,
	IsInverted = 1
};

struct FSceneViewProjectionData
{
	FVector ViewOrigin;
	FMatrix ViewRotationMatrix; /** Rotation matrix transforming from world space to view space. */
	FMatrix ProjectionMatrix; /** UE4 projection matrix projects such that clip space Z=1 is the near plane, and Z=0 is the infinite far plane. */
	FIntRect ViewRect; //The unconstrained (no aspect ratio bars applied) view rectangle (also unscaled)
	FIntRect ConstrainedViewRect; // The constrained view rectangle (identical to UnconstrainedUnscaledViewRect if aspect ratio is not constrained)
};

struct FSceneViewInitOptions : public FSceneViewProjectionData 
{}

struct FViewMatrices
{	
	ENGINE_API FViewMatrices(const FSceneViewInitOptions& InitOptions);

	/** ViewToClip : UE4 projection matrix projects such that clip space Z=1 is the near plane, and Z=0 is the infinite far plane. */
	FMatrix		ProjectionMatrix;
	/** ViewToClipNoAA : UE4 projection matrix projects such that clip space Z=1 is the near plane, and Z=0 is the infinite far plane. Don't apply any AA jitter */
	FMatrix		ProjectionNoAAMatrix;
	/** ClipToView : UE4 projection matrix projects such that clip space Z=1 is the near plane, and Z=0 is the infinite far plane. */
	FMatrix		InvProjectionMatrix;
	// WorldToView..
	FMatrix		ViewMatrix;
	// ViewToWorld..
	FMatrix		InvViewMatrix;
	// WorldToClip : UE4 projection matrix projects such that clip space Z=1 is the near plane, and Z=0 is the infinite far plane. */
	FMatrix		ViewProjectionMatrix;
	// ClipToWorld : UE4 projection matrix projects such that clip space Z=1 is the near plane, and Z=0 is the infinite far plane. */
	FMatrix		InvViewProjectionMatrix;
	// HMD WorldToView with roll removed
	FMatrix		HMDViewMatrixNoRoll;
	/** WorldToView with PreViewTranslation. */
	FMatrix		TranslatedViewMatrix;
	/** ViewToWorld with PreViewTranslation. */
	FMatrix		InvTranslatedViewMatrix;
	/** WorldToView with PreViewTranslation. */
	FMatrix		OverriddenTranslatedViewMatrix;
	/** ViewToWorld with PreViewTranslation. */
	FMatrix		OverriddenInvTranslatedViewMatrix;
	/** The view-projection transform, starting from world-space points translated by -ViewOrigin. */
	FMatrix		TranslatedViewProjectionMatrix;
	/** The inverse view-projection transform, ending with world-space points translated by -ViewOrigin. */
	FMatrix		InvTranslatedViewProjectionMatrix;
	/** The translation to apply to the world before TranslatedViewProjectionMatrix. Usually it is -ViewOrigin but with rereflections this can differ */
	FVector		PreViewTranslation;
	/** To support ortho and other modes this is redundant, in world space */
	FVector		ViewOrigin;
	/** Scale applied by the projection matrix in X and Y. */
	FVector2D	ProjectionScale;
	/** TemporalAA jitter offset currently stored in the projection matrix */
	FVector2D	TemporalAAProjectionJitter;
};


struct FPreviousViewInfo
{
	FViewMatrices ViewMatrices;
}
class FSceneView
{
	FSceneViewInitOptions SceneViewInitOptions;
	FViewMatrices ViewMatrices;
	FMatrix ProjectionMatrixUnadjustedForRHI;
	FVector4 InvDeviceZToWorldZTransform;
}
class FViewInfo : public FSceneView
{
	FPreviousViewInfo PrevViewInfo;
}

// FViewport::Draw() -> FEditorViewportClient::Draw() -> FLevelEditorViewportClient::CalcSceneView() ->
FEditorViewportClient::CalcSceneView()
{
	FSceneViewInitOptions ViewInitOptions;
	ViewInitOptions.ViewOrigin = GetViewTransform().GetLocation();
	ViewInitOptions.ViewRotationMatrix = FInverseRotationMatrix(GetViewTransform().GetRotation()) * FMatrix(FPlane(0, 0, 1, 0), FPlane(1, 0, 0, 0), FPlane(0, 1, 0, 0), FPlane(0, 0, 0, 1));
	ViewInitOptions.ProjectionMatrix = FReversedZPerspectiveMatrix(ViewFOV*(float)PI/360.0f, ViewportSize.X, ViewportSize.Y, GetNearClipPlane());

	FSceneView* View = new FSceneView(ViewInitOptions);
	{
		ViewMatrices = FViewMatrices(InitOptions)
		{
			FVector LocalViewOrigin = InitOptions.ViewOrigin;
			FMatrix LocalTranslatedViewMatrix = ViewRotationMatrix;
			FMatrix LocalInvTranslatedViewMatrix = LocalTranslatedViewMatrix.GetTransposed();

			ViewOrigin = LocalViewOrigin;
			ViewMatrix = FTranslationMatrix(-LocalViewOrigin) * ViewRotationMatrix;
			InvViewMatrix = ViewRotationMatrix.GetTransposed() * FTranslationMatrix(LocalViewOrigin);
			ProjectionMatrix = AdjustProjectionMatrixForRHI(InitOptions.ProjectionMatrix);
			InvProjectionMatrix = ProjectionMatrix.Inverse();
			ViewProjectionMatrix = ViewMatrix * ProjectionMatrix;
			InvViewProjectionMatrix = InvProjectionMatrix * InvViewMatrix;

			PreViewTranslation = -LocalViewOrigin;
			TranslatedViewMatrix = ViewRotationMatrix;
			InvTranslatedViewMatrix = ViewRotationMatrix.GetTransposed();
			TranslatedViewProjectionMatrix = TranslatedViewMatrix * ProjectionMatrix;
			InvTranslatedViewProjectionMatrix = InvProjectionMatrix * InvTranslatedViewMatrix;

			OverriddenTranslatedViewMatrix = FTranslationMatrix(-PreViewTranslation) * ViewMatrix; //==TranslatedViewMatrix
			OverriddenInvTranslatedViewMatrix = InvViewMatrix * FTranslationMatrix(PreViewTranslation); //==InvTranslatedViewMatrix
		}

		ProjectionMatrixUnadjustedForRHI = InitOptions.ProjectionMatrix;
		InvDeviceZToWorldZTransform = CreateInvDeviceZToWorldZTransform(ProjectionMatrixUnadjustedForRHI);
	}
}

FMatrix AdjustProjectionMatrixForRHI(const FMatrix& InProjectionMatrix)
{	
	// GProjectionSignY = 1, GMinClipZ = 0
	FScaleMatrix ClipSpaceFixScale(FVector(1.0f, GProjectionSignY, 1.0f - GMinClipZ));
	FTranslationMatrix ClipSpaceFixTranslate(FVector(0.0f, 0.0f, GMinClipZ));	
	return InProjectionMatrix * ClipSpaceFixScale * ClipSpaceFixTranslate;
}


FViewInfo::InitRHIResources()
{
	SetupUniformBufferParameters( , ViewMatrices, , , , *CachedViewUniformShaderParameters);
	{
		FViewUniformShaderParameters& ViewParameters = *CachedViewUniformShaderParameters;
		SetupCommonViewUniformBufferParameters(ViewParameters, , , , ViewMatrices, )
		{
			ViewParameters.ViewToClip = ViewMatrices.ProjectionMatrix;
			ViewParameters.ClipToView = ViewMatrices.InvProjectionMatrix;
			ViewParameters.WorldToClip = ViewMatrices.ViewProjectionMatrix;
			ViewParameters.ClipToWorld = ViewMatrices.InvViewProjectionMatrix;
			
			ViewParameters.TranslatedWorldToCameraView = ViewMatrices.TranslatedViewMatrix;
			ViewParameters.CameraViewToTranslatedWorld = ViewMatrices.InvTranslatedViewMatrix;
			ViewParameters.TranslatedWorldToView = ViewMatrices.OverriddenTranslatedViewMatrix;
			ViewParameters.ViewToTranslatedWorld = ViewMatrices.OverriddenInvTranslatedViewMatrix;
			ViewParameters.TranslatedWorldToClip = ViewMatrices.TranslatedViewProjectionMatrix;
			ViewParameters.ClipToTranslatedWorld = ViewMatrices.InvTranslatedViewProjectionMatrix;			

			ViewParameters.WorldCameraOrigin = ViewMatrices.ViewOrigin;
			ViewParameters.PreViewTranslation = ViewMatrices.PreViewTranslation;
			ViewParameters.TranslatedWorldCameraOrigin = ViewMatrices.ViewOrigin + ViewMatrices.PreViewTranslation; //==0

			ViewParameters.WorldViewOrigin = 
				ViewMatrices.OverriddenInvTranslatedViewMatrix.TransformPosition(FVector(0)) - ViewMatrices.PreViewTranslation;
			
			ViewParameters.ScreenToWorld = 
				FMatrix(
					FPlane(1, 0, 0, 0),
					FPlane(0, 1, 0, 0),
					FPlane(0, 0, ProjectionMatrixUnadjustedForRHI.M[2][2], 1),
					FPlane(0, 0, ProjectionMatrixUnadjustedForRHI.M[3][2], 0))
				* ViewMatrices.InvViewProjectionMatrix;

			// [u, v, 1]  |->  [u*tanX, v*tanY, 1] * ViewRotationMatrix.GetTransposed() ,
			// the ray direction from camera to screen at z=1 w.r.t world coordinates.
			ViewParameters.ScreenToTranslatedWorld = 
				FMatrix(
					FPlane(1, 0, 0, 0),
					FPlane(0, 1, 0, 0),
					FPlane(0, 0, ProjectionMatrixUnadjustedForRHI.M[2][2], 1),
					FPlane(0, 0, ProjectionMatrixUnadjustedForRHI.M[3][2], 0))
				* ViewMatrices.InvTranslatedViewProjectionMatrix;

			ViewParameters.InvDeviceZToWorldZTransform = InvDeviceZToWorldZTransform;
		}
	}
}


(-1,-1), (-1,1), (1,1), (1,-1), 
void Main(
	in float2 InPosition : ATTRIBUTE0,
	out float2 OutTexCoord : TEXCOORD0,
	out float3 OutScreenVector : TEXCOORD1,
	out float4 OutPosition : SV_POSITION
	)
{
	ResolvedView = ResolveView();

	// screenspace position from vb
	OutPosition = float4(InPosition,0,1);
	// texture coord from vb
	OutTexCoord = InPosition * ResolvedView.ScreenPositionScaleBias.xy + ResolvedView.ScreenPositionScaleBias.wz;

	// deproject to world space
	OutScreenVector = mul(float4(InPosition,1,0), ResolvedView.ScreenToTranslatedWorld).xyz;
}


Texture2D OcclusionTexture;
SamplerState OcclusionSampler;

void ExponentialPixelMain(
	float2 TexCoord : TEXCOORD0,
	float3 ScreenVector : TEXCOORD1,
	float4 SVPos : SV_POSITION,
	out float4 OutColor : SV_Target0
	)
{
	float DeviceZ = Texture2DSampleLevel(SceneTexturesStruct.SceneDepthTexture, SceneTexturesStruct.SceneDepthTextureSampler, TexCoord, 0).r;
	float SceneDepth = ConvertFromDeviceZ(DeviceZ);

	float3 WorldPositionRelativeToCamera = ScreenVector.xyz * SceneDepth;

	float ZSlice = log2(SceneDepth * View.VolumetricFogGridZParams.x + View.VolumetricFogGridZParams.y) * View.VolumetricFogGridZParams.z * View.VolumetricFogInvGridSize.z;
	float3 VolumeUV = float3((SVPos.xy - View.ViewRectMin.xy) * View.VolumetricFogSVPosToVolumeUV, ZSlice);

	float4 HeightFogInscatteringAndOpacity = CalculateHeightFog(WorldPositionRelativeToCamera);
	float4 FogInscatteringAndOpacity = CombineVolumetricFog(HeightFogInscatteringAndOpacity, VolumeUV);

	float LightShaftMask = Texture2DSample(OcclusionTexture, OcclusionSampler, TexCoord).x;
	FogInscatteringAndOpacity.rgb *= LightShaftMask;

	OutColor = float4(FogInscatteringAndOpacity.rgb, FogInscatteringAndOpacity.w);
}












View.InvDeviceZToWorldZTransform = CreateInvDeviceZToWorldZTransform(ProjectionMatrixUnadjustedForRHI);

float ConvertFromDeviceZ(float DeviceZ)
{
	// Supports ortho and perspective, see CreateInvDeviceZToWorldZTransform()
	return DeviceZ * View.InvDeviceZToWorldZTransform[0] 
		+ View.InvDeviceZToWorldZTransform[1] 
		+ 1.0f / (DeviceZ * View.InvDeviceZToWorldZTransform[2] - View.InvDeviceZToWorldZTransform[3]);
}

/**
 * Utility function to create the inverse depth projection transform to be used
 * by the shader system.
 * @param ProjMatrix - used to extract the scene depth ratios
 * @param InvertZ - projection calc is affected by inverted device Z
 * @return vector containing the ratios needed to convert from device Z to world Z
 */
FVector4 CreateInvDeviceZToWorldZTransform(const FMatrix& ProjMatrix)
{
	// The perspective depth projection comes from the the following projection matrix:
	//
	// | 1  0  0  0 |
	// | 0  1  0  0 |
	// | 0  0  A  1 |
	// | 0  0  B  0 |
	//
	// Z' = (Z * A + B) / Z
	// Z' = A + B / Z
	//
	// So to get Z from Z' is just:
	// Z = B / (Z' - A)
	//
	// Note a reversed Z projection matrix will have A=0.
	//
	// Done in shader as:
	// Z = 1 / (Z' * C1 - C2)   --- Where C1 = 1/B, C2 = A/B
	//

	float DepthMul = ProjMatrix.M[2][2];
	float DepthAdd = ProjMatrix.M[3][2];

	if (DepthAdd == 0.f)
	{
		// Avoid dividing by 0 in this case
		DepthAdd = 0.00000001f;
	}

	// perspective
	// SceneDepth = 1.0f / (DeviceZ / ProjMatrix.M[3][2] - ProjMatrix.M[2][2] / ProjMatrix.M[3][2])

	// ortho
	// SceneDepth = DeviceZ / ProjMatrix.M[2][2] - ProjMatrix.M[3][2] / ProjMatrix.M[2][2];

	// combined equation in shader to handle either
	// SceneDepth = DeviceZ * View.InvDeviceZToWorldZTransform[0] + View.InvDeviceZToWorldZTransform[1] + 1.0f / (DeviceZ * View.InvDeviceZToWorldZTransform[2] - View.InvDeviceZToWorldZTransform[3]);

	// therefore perspective needs
	// View.InvDeviceZToWorldZTransform[0] = 0.0f
	// View.InvDeviceZToWorldZTransform[1] = 0.0f
	// View.InvDeviceZToWorldZTransform[2] = 1.0f / ProjMatrix.M[3][2]
	// View.InvDeviceZToWorldZTransform[3] = ProjMatrix.M[2][2] / ProjMatrix.M[3][2]

	// and ortho needs
	// View.InvDeviceZToWorldZTransform[0] = 1.0f / ProjMatrix.M[2][2]
	// View.InvDeviceZToWorldZTransform[1] = -ProjMatrix.M[3][2] / ProjMatrix.M[2][2] + 1.0f
	// View.InvDeviceZToWorldZTransform[2] = 0.0f
	// View.InvDeviceZToWorldZTransform[3] = 1.0f

	bool bIsPerspectiveProjection = ProjMatrix.M[3][3] < 1.0f;

	if (bIsPerspectiveProjection)
	{
		float SubtractValue = DepthMul / DepthAdd;

		// Subtract a tiny number to avoid divide by 0 errors in the shader when a very far distance is decided from the depth buffer.
		// This fixes fog not being applied to the black background in the editor.
		SubtractValue -= 0.00000001f;

		return FVector4(
			0.0f,
			0.0f,
			1.0f / DepthAdd,
			SubtractValue
			);
	}
	else
	{
		return FVector4(
			1.0f / ProjMatrix.M[2][2],
			-ProjMatrix.M[3][2] / ProjMatrix.M[2][2] + 1.0f,
			0.0f,
			1.0f
			);
	}
}

