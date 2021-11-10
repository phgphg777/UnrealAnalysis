

/** 
 * Parameters calculated from the pixel material inputs.
 */
struct FPixelMaterialInputs
{
	MaterialFloat3 EmissiveColor;
	MaterialFloat Opacity;
	MaterialFloat OpacityMask;
	MaterialFloat3 BaseColor;
	MaterialFloat Metallic;
	MaterialFloat Specular;
	MaterialFloat Roughness;
	MaterialFloat3 Normal;
	MaterialFloat4 Subsurface;
	MaterialFloat AmbientOcclusion;
	MaterialFloat2 Refraction;
	MaterialFloat PixelDepthOffset;
};

/** 
 * Parameters needed by pixel shader material inputs, related to Geometry.
 * These are independent of vertex factory.
 */
struct FMaterialPixelParameters
{
#if NUM_TEX_COORD_INTERPOLATORS
	float2 TexCoords[NUM_TEX_COORD_INTERPOLATORS];
#endif

	/** Interpolated vertex color, in linear color space. */
	half4 VertexColor;

	/** Normalized world space normal. */
	half3 WorldNormal;

	/** Normalized world space reflected camera vector. */
	half3 ReflectionVector;

	/** Normalized world space camera vector, which is the vector from the point being shaded to the camera position. */
	half3 CameraVector;

	/** World space light vector, only valid when rendering a light function. */
	half3 LightVector;

	/**
	 * Like SV_Position (.xy is pixel position at pixel center, z:DeviceZ, .w:SceneDepth)
	 * using shader generated value SV_POSITION
	 * Note: this is not relative to the current viewport.  RelativePixelPosition = MaterialParameters.SvPosition.xy - View.ViewRectMin.xy;
	 */
	float4 SvPosition;
		
	/** Post projection position reconstructed from SvPosition, before the divide by W. left..top -1..1, bottom..top -1..1  within the viewport, W is the SceneDepth */
	float4 ScreenPosition;

	half UnMirrored;

	half TwoSidedSign;

	/**
	 * Orthonormal rotation-only transform from tangent space to world space
	 * The transpose(TangentToWorld) is WorldToTangent, and TangentToWorld[2] is WorldVertexNormal
	 */
	half3x3 TangentToWorld;

#if USE_WORLDVERTEXNORMAL_CENTER_INTERPOLATION
	/** World vertex normal interpolated at the pixel center that is safe to use for derivatives. */
	half3 WorldVertexNormal_Center;
#endif

	/** 
	 * Interpolated worldspace position of this pixel
	 * todo: Make this TranslatedWorldPosition and also rename the VS/DS/HS WorldPosition to be TranslatedWorldPosition
	 */
	float3 AbsoluteWorldPosition;

	/** 
	 * Interpolated worldspace position of this pixel, centered around the camera
	 */
	float3 WorldPosition_CamRelative;

	/** 
	 * Interpolated worldspace position of this pixel, not including any world position offset or displacement.
	 * Only valid if shader is compiled with NEEDS_WORLD_POSITION_EXCLUDING_SHADER_OFFSETS, otherwise just contains 0
	 */
	float3 WorldPosition_NoOffsets;

	/** 
	 * Interpolated worldspace position of this pixel, not including any world position offset or displacement.
	 * Only valid if shader is compiled with NEEDS_WORLD_POSITION_EXCLUDING_SHADER_OFFSETS, otherwise just contains 0
	 */
	float3 WorldPosition_NoOffsets_CamRelative;

	/** Offset applied to the lighting position for translucency, used to break up aliasing artifacts. */
	half3 LightingPositionOffset;

	float AOMaterialMask;

#if LIGHTMAP_UV_ACCESS
	float2	LightmapUVs;
#endif

#if USE_INSTANCING
	half4 PerInstanceParams;
#endif

	// Index into View.PrimitiveSceneData
	uint PrimitiveId;

	/** Per-particle properties. Only valid for particle vertex factories. */
	FMaterialParticleParameters Particle;

#if TEX_COORD_SCALE_ANALYSIS
	/** Parameters used by the MaterialTexCoordScales shader. */
	FTexCoordScalesParams TexCoordScalesParams;
#endif
};


void CalcPixelMaterialInputs(in out FMaterialPixelParameters Parameters, in out FPixelMaterialInputs PixelMaterialInputs)
{
	// Initial calculations (required for Normal)
%s
	// The Normal is a special case as it might have its own expressions and also be used to calculate other inputs, so perform the assignment here
%s

	// Note that here MaterialNormal can be in world space or tangent space
	float3 MaterialNormal = normalize(GetMaterialNormal(Parameters, PixelMaterialInputs));
	
	// normalizing after the tangent space to world space conversion improves quality with sheared bases (UV layout to WS causes shrearing)
	// use full precision normalize to avoid overflows
	Parameters.WorldNormal = TransformTangentNormalToWorld(Parameters.TangentToWorld, MaterialNormal);
	Parameters.WorldNormal *= Parameters.TwoSidedSign;

	Parameters.ReflectionVector = ReflectionAboutCustomWorldNormal(Parameters, Parameters.WorldNormal, false);

#if !PARTICLE_SPRITE_FACTORY
	Parameters.Particle.MotionBlurFade = 1.0f;
#endif // !PARTICLE_SPRITE_FACTORY

	// Now the rest of the inputs
%s
}


/** Initializes the subset of Parameters that was not set in GetMaterialPixelParameters. */
void CalcMaterialParametersEx(
	in out FMaterialPixelParameters Parameters,
	in out FPixelMaterialInputs PixelMaterialInputs,
	float4 SvPosition,
	float4 ScreenPosition,
	FIsFrontFace bIsFrontFace,
	float3 TranslatedWorldPosition,
	float3 TranslatedWorldPositionExcludingShaderOffsets)
{
	// Remove the pre view translation
	Parameters.WorldPosition_CamRelative = TranslatedWorldPosition.xyz;
	Parameters.AbsoluteWorldPosition = TranslatedWorldPosition.xyz - ResolvedView.PreViewTranslation.xyz;

	// If the material uses any non-offset world position expressions, calculate those parameters. If not, 
	// the variables will have been initialised to 0 earlier.
#if USE_WORLD_POSITION_EXCLUDING_SHADER_OFFSETS
	Parameters.WorldPosition_NoOffsets_CamRelative = TranslatedWorldPositionExcludingShaderOffsets;
	Parameters.WorldPosition_NoOffsets = TranslatedWorldPositionExcludingShaderOffsets - ResolvedView.PreViewTranslation.xyz;
#endif

	Parameters.SvPosition = SvPosition;
	Parameters.ScreenPosition = ScreenPosition;

	// TranslatedWorldPosition is the world position translated to the camera position, which is just -CameraVector
	Parameters.CameraVector = normalize(-Parameters.WorldPosition_CamRelative.xyz);

	Parameters.LightVector = 0;

	Parameters.TwoSidedSign = 1.0f;

#if MATERIAL_TWOSIDED && HAS_PRIMITIVE_UNIFORM_BUFFER
    // #dxr: DirectX Raytracing's HitKind() intrinsic already accounts for negative scaling
    #if PIXELSHADER
	    Parameters.TwoSidedSign *= ResolvedView.CullingSign * GetPrimitiveData(Parameters.PrimitiveId).InvNonUniformScaleAndDeterminantSign.w;
    #endif

	#if !MATERIAL_TWOSIDED_SEPARATE_PASS
		Parameters.TwoSidedSign *= GetFloatFacingSign(bIsFrontFace);
	#endif
#endif

	// Now that we have all the pixel-related parameters setup, calculate the Material Input/Attributes and Normal
	CalcPixelMaterialInputs(Parameters, PixelMaterialInputs);
}

// convenience function to setup CalcMaterialParameters assuming we don't support TranslatedWorldPositionExcludingShaderOffsets
// @param SvPosition from SV_Position when rendering the view, for other projections e.g. shadowmaps this function cannot be used and you need to call CalcMaterialParametersEx()
void CalcMaterialParameters(
	in out FMaterialPixelParameters Parameters,
	in out FPixelMaterialInputs PixelMaterialInputs,
	float4 SvPosition,
	FIsFrontFace bIsFrontFace)
{
	float4 ScreenPosition = SvPositionToResolvedScreenPosition(SvPosition);
	float3 TranslatedWorldPosition = SvPositionToResolvedTranslatedWorld(SvPosition);

	CalcMaterialParametersEx(Parameters, PixelMaterialInputs, SvPosition, ScreenPosition, bIsFrontFace, TranslatedWorldPosition, TranslatedWorldPosition);
}









enum EMaterialProperty
{
	MP_EmissiveColor = 0 UMETA(DisplayName = "Emissive"),
	MP_Opacity UMETA(DisplayName = "Opacity"),
	MP_OpacityMask UMETA(DisplayName = "Opacity Mask"),
	MP_DiffuseColor UMETA(Hidden),			// used in Lightmass, not exposed to user, computed from: BaseColor, Metallic
	MP_SpecularColor UMETA(Hidden),			// used in Lightmass, not exposed to user, derived from: SpecularColor, Metallic, Specular
	MP_BaseColor UMETA(DisplayName = "Diffuse"),
	MP_Metallic UMETA(DisplayName = "Metallic"),
	MP_Specular UMETA(DisplayName = "Specular"),
	MP_Roughness UMETA(DisplayName = "Roughness "),
	MP_Normal UMETA(DisplayName = "Normal"),
	MP_WorldPositionOffset UMETA(Hidden),
	MP_WorldDisplacement UMETA(Hidden),
	MP_TessellationMultiplier UMETA(Hidden),
	MP_SubsurfaceColor UMETA(DisplayName = "Subsurface"),
	MP_CustomData0 UMETA(Hidden),
	MP_CustomData1 UMETA(Hidden),
	MP_AmbientOcclusion UMETA(DisplayName = "Ambient Occlusion"),
	MP_Refraction UMETA(DisplayName = "Refraction"),
	MP_CustomizedUVs0 UMETA(Hidden),
	MP_CustomizedUVs1 UMETA(Hidden),
	MP_CustomizedUVs2 UMETA(Hidden),
	MP_CustomizedUVs3 UMETA(Hidden),
	MP_CustomizedUVs4 UMETA(Hidden),
	MP_CustomizedUVs5 UMETA(Hidden),
	MP_CustomizedUVs6 UMETA(Hidden),
	MP_CustomizedUVs7 UMETA(Hidden),
	MP_PixelDepthOffset UMETA(Hidden),

	//^^^ New material properties go above here ^^^^
	MP_MaterialAttributes UMETA(Hidden),
	MP_CustomOutput UMETA(Hidden),
	MP_MAX UMETA(DisplayName = "None"),
};

enum EMaterialShadingModel
{
	MSM_Unlit				UMETA(DisplayName="Unlit"),
	MSM_DefaultLit			UMETA(DisplayName="Default Lit"),
	MSM_Subsurface			UMETA(DisplayName="Subsurface"),
	MSM_PreintegratedSkin	UMETA(DisplayName="Preintegrated Skin"),
	MSM_ClearCoat			UMETA(DisplayName="Clear Coat"),
	MSM_SubsurfaceProfile	UMETA(DisplayName="Subsurface Profile"),
	MSM_TwoSidedFoliage		UMETA(DisplayName="Two Sided Foliage"),
	MSM_Hair				UMETA(DisplayName="Hair"),
	MSM_Cloth				UMETA(DisplayName="Cloth"),
	MSM_Eye					UMETA(DisplayName="Eye"),
	MSM_MAX,
};

enum EMaterialDomain
{
	MD_Surface 				UMETA(DisplayName = "Surface"),
	MD_DeferredDecal 		UMETA(DisplayName = "Deferred Decal"),
	MD_LightFunction 		UMETA(DisplayName = "Light Function"),
	MD_Volume 				UMETA(DisplayName = "Volume"),
	MD_PostProcess 			UMETA(DisplayName = "Post Process"),
	MD_UI 					UMETA(DisplayName = "User Interface"),
	MD_MAX
};

enum EBlendMode
{
	BLEND_Opaque 			UMETA(DisplayName="Opaque"),
	BLEND_Masked 			UMETA(DisplayName="Masked"),
	BLEND_Translucent 		UMETA(DisplayName="Translucent"),
	BLEND_Additive 			UMETA(DisplayName="Additive"),
	BLEND_Modulate 			UMETA(DisplayName="Modulate"),
	BLEND_AlphaComposite 	UMETA(DisplayName ="AlphaComposite (Premultiplied Alpha)"),
	BLEND_MAX,
};


class FMaterialAttributeDefintion
{
	FGuid				AttributeID;
	FString				DisplayName;
	EMaterialProperty	Property;	
	EMaterialValueType	ValueType;
	FVector4			DefaultValue;
	EShaderFrequency	ShaderFrequency;
	int32				TexCoordIndex;
	MaterialAttributeBlendFunction BlendFunction;
	bool				bIsHidden;
};

class FMaterialAttributeDefinitionMap
{
	TMap<EMaterialProperty, FMaterialAttributeDefintion>	AttributeMap; 
	TArray<FMaterialCustomOutputAttributeDefintion>			CustomAttributes;
	TArray<FGuid>											OrderedVisibleAttributeList; 
	FString													AttributeDDCString;
	bool 													bIsInitialized;
};
FMaterialAttributeDefinitionMap GMaterialPropertyAttributesMap;
{
	bIsInitialized = true;
	AttributeMap.Empty(MP_MAX + 1);

	// Basic attributes
	Add(FGuid(0x69B8D336, 0x16ED4D49, 0x9AA49729, 0x2F050F7A), TEXT("BaseColor"),		MP_BaseColor,		MCT_Float3,	FVector4(0,0,0,0),	SF_Pixel);
	Add(FGuid(0x57C3A161, 0x7F064296, 0xB00B24A5, 0xA496F34C), TEXT("Metallic"),		MP_Metallic,		MCT_Float,	FVector4(0,0,0,0),	SF_Pixel);
	Add(FGuid(0x9FDAB399, 0x25564CC9, 0x8CD2D572, 0xC12C8FED), TEXT("Specular"),		MP_Specular,		MCT_Float,	FVector4(.5,0,0,0), SF_Pixel);
	Add(FGuid(0xD1DD967C, 0x4CAD47D3, 0x9E6346FB, 0x08ECF210), TEXT("Roughness"),		MP_Roughness,		MCT_Float,	FVector4(.5,0,0,0), SF_Pixel);
	Add(FGuid(0xB769B54D, 0xD08D4440, 0xABC21BA6, 0xCD27D0E2), TEXT("EmissiveColor"),	MP_EmissiveColor,	MCT_Float3,	FVector4(0,0,0,0),	SF_Pixel);
	Add(FGuid(0xB8F50FBA, 0x2A754EC1, 0x9EF672CF, 0xEB27BF51), TEXT("Opacity"),			MP_Opacity,			MCT_Float,	FVector4(1,0,0,0),	SF_Pixel);
	Add(FGuid(0x679FFB17, 0x2BB5422C, 0xAD520483, 0x166E0C75), TEXT("OpacityMask"),		MP_OpacityMask,		MCT_Float,	FVector4(1,0,0,0),	SF_Pixel);
	Add(FGuid(0x0FA2821A, 0x200F4A4A, 0xB719B789, 0xC1259C64), TEXT("Normal"),			MP_Normal,			MCT_Float3,	FVector4(0,0,1,0),	SF_Pixel);

	// Advanced attributes
	Add(FGuid(0xF905F895, 0xD5814314, 0x916D2434, 0x8C40CE9E), TEXT("WorldPositionOffset"),		MP_WorldPositionOffset,		MCT_Float3,	FVector4(0,0,0,0),	SF_Vertex);
	Add(FGuid(0x2091ECA2, 0xB59248EE, 0x8E2CD578, 0xD371926D), TEXT("WorldDisplacement"),		MP_WorldDisplacement,		MCT_Float3,	FVector4(0,0,0,0),	SF_Domain);
	Add(FGuid(0xA0119D44, 0xC456450D, 0x9C39C933, 0x1F72D8D1), TEXT("TessellationMultiplier"),	MP_TessellationMultiplier,	MCT_Float,	FVector4(1,0,0,0),	SF_Hull);
	Add(FGuid(0x5B8FC679, 0x51CE4082, 0x9D777BEE, 0xF4F72C44), TEXT("SubsurfaceColor"),			MP_SubsurfaceColor,			MCT_Float3,	FVector4(1,1,1,0),	SF_Pixel);
	Add(FGuid(0x9E502E69, 0x3C8F48FA, 0x94645CFD, 0x28E5428D), TEXT("ClearCoat"),				MP_CustomData0,				MCT_Float,	FVector4(1,0,0,0),	SF_Pixel);
	Add(FGuid(0xBE4F2FFD, 0x12FC4296, 0xB0124EEA, 0x12C28D92), TEXT("ClearCoatRoughness"),		MP_CustomData1,				MCT_Float,	FVector4(.1,0,0,0),	SF_Pixel);
	Add(FGuid(0xE8EBD0AD, 0xB1654CBE, 0xB079C3A8, 0xB39B9F15), TEXT("AmbientOcclusion"),		MP_AmbientOcclusion,		MCT_Float,	FVector4(1,0,0,0),	SF_Pixel);
	Add(FGuid(0xD0B0FA03, 0x14D74455, 0xA851BAC5, 0x81A0788B), TEXT("Refraction"),				MP_Refraction,				MCT_Float2,	FVector4(1,0,0,0),	SF_Pixel);
	Add(FGuid(0x0AC97EC3, 0xE3D047BA, 0xB610167D, 0xC4D919FF), TEXT("PixelDepthOffset"),		MP_PixelDepthOffset,		MCT_Float,	FVector4(0,0,0,0),	SF_Pixel);

	// Texture coordinates
	Add(FGuid(0xD30EC284, 0xE13A4160, 0x87BB5230, 0x2ED115DC), TEXT("CustomizedUV0"), MP_CustomizedUVs0, MCT_Float2, FVector4(0,0,0,0), SF_Vertex, 0);
	Add(FGuid(0xC67B093C, 0x2A5249AA, 0xABC97ADE, 0x4A1F49C5), TEXT("CustomizedUV1"), MP_CustomizedUVs1, MCT_Float2, FVector4(0,0,0,0), SF_Vertex, 1);
	Add(FGuid(0x85C15B24, 0xF3E047CA, 0x85856872, 0x01AE0F4F), TEXT("CustomizedUV2"), MP_CustomizedUVs2, MCT_Float2, FVector4(0,0,0,0), SF_Vertex, 2);
	Add(FGuid(0x777819DC, 0x31AE4676, 0xB864EF77, 0xB807E873), TEXT("CustomizedUV3"), MP_CustomizedUVs3, MCT_Float2, FVector4(0,0,0,0), SF_Vertex, 3);
	Add(FGuid(0xDA63B233, 0xDDF44CAD, 0xB93D867B, 0x8DAFDBCC), TEXT("CustomizedUV4"), MP_CustomizedUVs4, MCT_Float2, FVector4(0,0,0,0), SF_Vertex, 4);
	Add(FGuid(0xC2F52B76, 0x4A034388, 0x89119528, 0x2071B190), TEXT("CustomizedUV5"), MP_CustomizedUVs5, MCT_Float2, FVector4(0,0,0,0), SF_Vertex, 5);
	Add(FGuid(0x8214A8CA, 0x0CB944CF, 0x9DFD78DB, 0xE48BB55F), TEXT("CustomizedUV6"), MP_CustomizedUVs6, MCT_Float2, FVector4(0,0,0,0), SF_Vertex, 6);
	Add(FGuid(0xD8F8D01F, 0xC6F74715, 0xA3CFB4FF, 0x9EF51FAC), TEXT("CustomizedUV7"), MP_CustomizedUVs7, MCT_Float2, FVector4(0,0,0,0), SF_Vertex, 7);

	// Lightmass attributes	
	Add(FGuid(0x68934E1B, 0x70EB411B, 0x86DF5AA5, 0xDF2F626C), TEXT("DiffuseColor"),	MP_DiffuseColor,	MCT_Float3, FVector4(0,0,0,0), SF_Pixel, INDEX_NONE, true);
	Add(FGuid(0xE89CBD84, 0x62EA48BE, 0x80F88521, 0x2B0C403C), TEXT("SpecularColor"),	MP_SpecularColor,	MCT_Float3, FVector4(0,0,0,0), SF_Pixel, INDEX_NONE, true);

	// Debug attributes
	Add(FGuid(0x5BF6BA94, 0xA3264629, 0xA253A05B, 0x0EABBB86), TEXT("Missing"), MP_MAX, MCT_Float, FVector4(0,0,0,0), SF_Pixel, INDEX_NONE, true);

	// UMaterialExpression custom outputs
	AddCustomAttribute(FGuid(0xfbd7b46e, 0xb1234824, 0xbde76b23, 0x609f984c), "BentNormal", "GetBentNormal", MCT_Float3, FVector4(0, 0, 1, 0));
	AddCustomAttribute(FGuid(0xAA3D5C04, 0x16294716, 0xBBDEC869, 0x6A27DD72), "ClearCoatBottomNormal", "ClearCoatBottomNormal", MCT_Float3, FVector4(0, 0, 1, 0));
	AddCustomAttribute(FGuid(0x8EAB2CB2, 0x73634A24, 0x8CD14F47, 0x3F9C8E55), "CustomEyeTangent", "GetTangentOutput", MCT_Float3, FVector4(0, 0, 0, 0));
}


class UMaterial : public UMaterialInterface
{
	FColorMaterialInput BaseColor;
	FScalarMaterialInput Metallic;
	FScalarMaterialInput Specular;
	FScalarMaterialInput Roughness;
	FVectorMaterialInput Normal;
	FColorMaterialInput EmissiveColor;
	FScalarMaterialInput Opacity;
	FScalarMaterialInput OpacityMask;

	TEnumAsByte<enum EMaterialDomain> MaterialDomain;
	TEnumAsByte<enum EBlendMode> BlendMode;
	TEnumAsByte<enum EMaterialShadingModel> ShadingModel;
	TArray<class UMaterialExpression*> Expressions;
};


class FMaterial
{
};

class FMaterialResource : public FMaterial 
{
	UMaterial* Material;
	UMaterialInstance* MaterialInstance;
};

struct ENGINE_API FMaterialParameterInfo
{
	FName Name;
	TEnumAsByte<EMaterialParameterAssociation> Association;
	int32 Index;
};

struct FShaderCodeChunk
{
	FString Definition;
	FString SymbolName;
	TRefCountPtr<FMaterialUniformExpression> UniformExpression;
	EMaterialValueType Type;
	bool bInline;
};

class FUniformExpressionSet : public FRefCountedObject
{
	TArray<TRefCountPtr<FMaterialUniformExpression> > UniformVectorExpressions;
	TArray<TRefCountPtr<FMaterialUniformExpression> > UniformScalarExpressions;
	TArray<TRefCountPtr<FMaterialUniformExpressionTexture> > Uniform2DTextureExpressions;
	TArray<TRefCountPtr<FMaterialUniformExpressionTexture> > UniformCubeTextureExpressions;
	TArray<TRefCountPtr<FMaterialUniformExpressionTexture> > UniformVolumeTextureExpressions;
	TArray<TRefCountPtr<FMaterialUniformExpressionExternalTexture> > UniformExternalTextureExpressions;

	/** Ids of parameter collections referenced by the material that was translated. */
	// TArray<FGuid> ParameterCollections;

	/** The structure of a uniform buffer containing values for these uniform expressions. */
	// TOptional<FShaderParametersMetadata> UniformBufferStruct;
};

class FMaterialCompilationOutput
{
	FUniformExpressionSet UniformExpressionSet;
};

class FMaterialFunctionCompileState
{
	UMaterialExpressionMaterialFunctionCall* FunctionCall;
	TArray<FMaterialExpressionKey> ExpressionStack;
	TMap  <FMaterialExpressionKey, int32> ExpressionCodeMap;
	TMap  <FMaterialExpressionKey, FMaterialFunctionCompileState*> SharedFunctionStates;
};


class FHLSLMaterialTranslator : public FMaterialCompiler
{
	FMaterial* Material;
	FMaterialCompilationOutput& MaterialCompilationOutput;

	EMaterialProperty MaterialProperty;						// = mp;
	EShaderFrequency ShaderFrequency;						// = FMaterialAttributeDefinitionMap::GetShaderFrequency( mp );
	TArray<FGuid> MaterialAttributesStack;					// = { FGuid( GMaterialPropertyAttributesMap.Find( mp )->AttributeID ) };
	TArray<FShaderCodeChunk> * CurrentScopeChunks;			// = &SharedPropertyCodeChunks[ ShaderFrequency ];
	TArray<FMaterialParameterInfo> ParameterOwnerStack;  	// = { FMaterialParameterInfo( "None", GlobalParameter, INDEX_NONE )  };
	
	TArray<FShaderCodeChunk> SharedPropertyCodeChunks[SF_NumFrequencies];
	TArray<FShaderCodeChunk> UniformExpressions;

	// TArray<FMaterialFunctionCompileState*> FunctionStacks[SF_NumFrequencies];

	FString TranslatedCodeChunkDefinitions[CompiledMP_MAX];
	FString TranslatedCodeChunks[CompiledMP_MAX];

	FString MaterialTemplate;
};


class FMaterialUniformExpression : public FRefCountedObject {};
class FMaterialUniformExpressionVectorParameter: public FMaterialUniformExpression 
{
	FMaterialParameterInfo ParameterInfo;
	FLinearColor DefaultValue;
	bool bUseOverriddenDefault;
	FLinearColor OverriddenDefaultValue;
};
class FMaterialUniformExpressionComponentSwizzle : public FMaterialUniformExpression
{
	TRefCountPtr<FMaterialUniformExpression> X;
	int8 IndexR;
	int8 IndexG;
	int8 IndexB;
	int8 IndexA;
	int8 NumElements;
};
class FMaterialUniformExpressionConstant: public FMaterialUniformExpression
{
	FLinearColor Value;
	uint8 ValueType;
};
class FMaterialUniformExpressionFoldedMath: public FMaterialUniformExpression
{
	TRefCountPtr<FMaterialUniformExpression> A;
	TRefCountPtr<FMaterialUniformExpression> B;
	uint32 ValueType;
	uint8 Op;
};




struct FExpressionOutput
{
	FName	OutputName;
	int32	Mask,
	int32	MaskR,
	int32	MaskG,
	int32	MaskB,
	int32	MaskA;
};

struct FExpressionInput
{
	UMaterialExpression*	Expression;
	int32					OutputIndex;
	FName					ExpressionName;				

	FName	InputName;
	int32	Mask;
	int32	MaskR;
	int32	MaskG;
	int32	MaskB;
	int32	MaskA;				
};

struct FMaterialAttributesInput : FExpressionInput
{
	uint32 PropertyConnectedBitmask;
};

template<class InputType> struct FMaterialInput : FExpressionInput
{
	uint32		UseConstant : 1;
	InputType	Constant;
};

struct FColorMaterialInput : FMaterialInput<FColor> {};
struct FScalarMaterialInput : FMaterialInput<float> {};
struct FVectorMaterialInput : FMaterialInput<FVector> {};
struct FVector2MaterialInput : FMaterialInput<FVector2D> {};


class FMaterialExpressionKey
{
	UMaterialExpression* Expression;
	int32 OutputIndex;
	FGuid MaterialAttributeID;
	bool bCompilingPreviousFrameKey;
};



SharedPixelProperties[MP_Normal] = true;
SharedPixelProperties[MP_EmissiveColor] = true;
SharedPixelProperties[MP_Opacity] = true;
SharedPixelProperties[MP_OpacityMask] = true;
SharedPixelProperties[MP_BaseColor] = true;
SharedPixelProperties[MP_Metallic] = true;
SharedPixelProperties[MP_Specular] = true;
SharedPixelProperties[MP_Roughness] = true;
SharedPixelProperties[MP_AmbientOcclusion] = true;
SharedPixelProperties[MP_Refraction] = true;
SharedPixelProperties[MP_PixelDepthOffset] = true;
SharedPixelProperties[MP_SubsurfaceColor] = true;


+++++++++++++++++++++++++++++++++SF_Vertex+++++++++++++++++++++++++++++++++
// MP_WorldPositionOffset(10) -> 0
 0 o MaterialFloat3(0.0, 0.0, 0.0)				// FMaterialUniformExpressionConstant

+++++++++++++++++++++++++++++++++SF_Domain+++++++++++++++++++++++++++++++++
// MP_WorldDisplacement(12) -> 0
 0 o MaterialFloat3(0.0, 0.0, 0.0)				// FMaterialUniformExpressionConstant

+++++++++++++++++++++++++++++++++SF_Hull+++++++++++++++++++++++++++++++++
// MP_TessellationMultiplier(12) -> 0
 0 o 1.0										// FMaterialUniformExpressionConstant 

+++++++++++++++++++++++++++++++++SF_Pixel+++++++++++++++++++++++++++++++++
// MP_Normal(9) -> 3
 0 un None 										// FMaterialUniformExpressionVectorParameter
 1 ni Material.VectorExpressions[0]	
 2 un Material.VectorExpressions[0].rgb			// FMaterialUniformExpressionComponentSwizzle
 3 uc MaterialFloat3(0.0, 0.0, 1.0)				// FMaterialUniformExpressionConstant

// MP_EmissiveColor(0) -> 8
 4 ni Material.VectorExpressions[0]
 5 uc MaterialFloat3(0.0, 0.0, 0.0)				// FMaterialUniformExpressionConstant
 6 ni Material.VectorExpressions[1].rgb
 7 un (MaterialFloat3(0.0, 0.0, 0.0) + Material.VectorExpressions[1].rgb)		// FMaterialUniformExpressionFoldedMath
 8 ni Material.VectorExpressions[2].rgb

// MP_DiffuseColor(3) -> 5
 9 ni Material.VectorExpressions[0]

// MP_SpecularColor(4) -> 5
10 ni Material.VectorExpressions[0]

// MP_BaseColor(5) -> 12
11 ni Material.VectorExpressions[0]
12 uc MaterialFloat3(?, ?, ?)					// FMaterialUniformExpressionConstant

// MP_Metallic(6) -> 14
13 ni Material.VectorExpressions[0]
14 uc 0.0										// FMaterialUniformExpressionConstant

// MP_Specular(7) -> 16
15 ni Material.VectorExpressions[0]
16 uc 0.5										// FMaterialUniformExpressionConstant

// MP_Roughness(8) -> 16
17 ni Material.VectorExpressions[0]

// MP_Opacity(1) -> 22
18 ni Material.VectorExpressions[0]
19 ni GetScreenPosition(Parameters).w
20 ni GetScreenPosition(Parameters).w.r
21 uc 3000.0									// FMaterialUniformExpressionConstant
22 nn   MaterialFloat Local0 = (GetScreenPosition(Parameters).w.r / 3000.0);\n

// MP_OpacityMask(2) -> 24
23 ni Material.VectorExpressions[0]
24 uc 1.0										// FMaterialUniformExpressionConstant

// MP_CustomData0(14) -> 24
25 ni Material.VectorExpressions[0]

// MP_CustomData1(15) -> 27
26 ni Material.VectorExpressions[0]
27 uc 1.0										// FMaterialUniformExpressionConstant

// MP_AmbientOcclusion(16) -> 24
28 ni Material.VectorExpressions[0]

// MP_Refraction(17) -> 36
29 ni Material.VectorExpressions[0]
30 uc MaterialFloat2(1.0, 0.0)					// FMaterialUniformExpressionConstant
31 ni MaterialFloat2(1.0, 0.0).r 						// added in ForceCast(MCT_Float2, MCT_Float1)
32 ni MaterialFloat2( MaterialFloat2(1.0, 0.0).r, 0 )	// added in ForceCast(MCT_Float1, MCT_Float2)
33 ni MaterialFloat2( MaterialFloat2(1.0, 0.0).r, 0 ).r	// added in ForceCast(MCT_Float1, MCT_Float2)
34 un None 										// FMaterialUniformExpressionScalaParameter
35
36


// PixelMembersDeclaration
	MaterialFloat3 EmissiveColor;
	MaterialFloat Opacity;
	MaterialFloat OpacityMask;
	MaterialFloat3 BaseColor;
	MaterialFloat Metallic;
	MaterialFloat Specular;
	MaterialFloat Roughness;
	MaterialFloat3 Normal;
	MaterialFloat4 Subsurface;
	MaterialFloat AmbientOcclusion;
	MaterialFloat2 Refraction;
	MaterialFloat PixelDepthOffset;

// NormalAssignment
	PixelMaterialInputs.Normal = MaterialFloat3(0, 0, 1);

// PixelMembersSetupAndAssignments
	MaterialFloat Local0 = (GetScreenPosition(Parameters).w.r / 3000.0);
	\n
	PixelMaterialInputs.EmissiveColor = Material.VectorExpressions[2].rgb;
	PixelMaterialInputs.Opacity = Local0;
	PixelMaterialInputs.OpacityMask = 1.0;
	PixelMaterialInputs.BaseColor = MaterialFloat3(?, ?, ?);
	PixelMaterialInputs.Metallic = 0.0;
	PixelMaterialInputs.Specular = 0.5;
	PixelMaterialInputs.Roughness = 0.5;
	PixelMaterialInputs.Subsurface = 0.0;
	PixelMaterialInputs.AmbientOcclusion = 1.0;
	PixelMaterialInputs.Refraction = MaterialFloat2( MaterialFloat2( MaterialFloat2(1.00000000,0.00000000).r, 0 ).r, Material.ScalarExpressions[0].x );
	PixelMaterialInputs.PixelDepthOffset = 0.0;




struct FPixelMaterialInputs
{
	MaterialFloat3 EmissiveColor;
	MaterialFloat Opacity;
	MaterialFloat OpacityMask;
	MaterialFloat3 BaseColor;
	MaterialFloat Metallic;
	MaterialFloat Specular;
	MaterialFloat Roughness;
	MaterialFloat3 Normal;
	MaterialFloat4 Subsurface;
	MaterialFloat AmbientOcclusion;
	MaterialFloat2 Refraction;
	MaterialFloat PixelDepthOffset;
};

void CalcPixelMaterialInputs(in out FMaterialPixelParameters Parameters, in out FPixelMaterialInputs PixelMaterialInputs)
{
	PixelMaterialInputs.Normal = MaterialFloat3(0, 0, 1);

	float3 MaterialNormal = GetMaterialNormal(Parameters, PixelMaterialInputs);

#if MATERIAL_TANGENTSPACENORMAL
  #if SIMPLE_FORWARD_SHADING
	Parameters.WorldNormal = float3(0, 0, 1);
  #endif
	MaterialNormal = normalize(MaterialNormal);
	Parameters.WorldNormal = TransformTangentNormalToWorld(Parameters.TangentToWorld, MaterialNormal);
#else
	Parameters.WorldNormal = normalize(MaterialNormal);
#endif

#if MATERIAL_TANGENTSPACENORMAL
	Parameters.WorldNormal *= Parameters.TwoSidedSign;
#endif

	Parameters.ReflectionVector = ReflectionAboutCustomWorldNormal(Parameters, Parameters.WorldNormal, false);

#if !PARTICLE_SPRITE_FACTORY
	Parameters.Particle.MotionBlurFade = 1.0f;
#endif

	MaterialFloat Local0 = (GetScreenPosition(Parameters).w.r / 3000.0);
	
	PixelMaterialInputs.EmissiveColor = Material.VectorExpressions[2].rgb;
	PixelMaterialInputs.Opacity = Local0;
	PixelMaterialInputs.OpacityMask = 1.0;
	PixelMaterialInputs.BaseColor = MaterialFloat3(?, ?, ?);
	PixelMaterialInputs.Metallic = 0.0;
	PixelMaterialInputs.Specular = 0.5;
	PixelMaterialInputs.Roughness = 0.5;
	PixelMaterialInputs.Subsurface = 0.0;
	PixelMaterialInputs.AmbientOcclusion = 1.0;
	PixelMaterialInputs.Refraction = MaterialFloat2( MaterialFloat2( MaterialFloat2(1.00000000,0.00000000).r, 0 ).r, Material.ScalarExpressions[0].x );
	PixelMaterialInputs.PixelDepthOffset = 0.0;
}




class ENGINE_API UMaterialExpression : public UObject
{
	int32 MaterialExpressionEditorX;
	int32 MaterialExpressionEditorY;
	UEdGraphNode*	GraphNode;
	FString LastErrorText;
	FGuid MaterialExpressionGuid;
	class UMaterial* Material;
	class UMaterialFunction* Function;
	FString Desc;
	uint32 bRealtimePreview:1;
	uint32 bNeedToUpdatePreview:1;
	uint32 bIsParameterExpression:1;
	uint32 bCommentBubbleVisible:1;
	uint32 bShowOutputNameOnPin:1;
	uint32 bShowMaskColorsOnPin:1;
	uint32 bHidePreviewWindow:1;
	uint32 bCollapsed:1;
	uint32 bShaderInputData:1;
	uint32 bShowInputs:1;
	uint32 bShowOutputs:1;
	TArray<FText> MenuCategories;
	TArray<FExpressionOutput> Outputs;
};

class UMaterialExpressionDivide : public UMaterialExpression {
	FExpressionInput A;
	FExpressionInput B;
	float ConstA;
	float ConstB;
};
class UMaterialExpressionPixelDepth : public UMaterialExpression {	
};
class UMaterialExpressionSceneDepth : public UMaterialExpression {
	TEnumAsByte<enum EMaterialSceneAttributeInputMode::Type> InputMode;
	FExpressionInput Input;
	FVector2D ConstInput;
};
class UMaterialExpressionDepthFade : public UMaterialExpression {
	FExpressionInput InOpacity;
	FExpressionInput FadeDistance;
	float OpacityDefault;
	float FadeDistanceDefault;
};



FString CreateHLSLUniformBufferDeclaration(const TCHAR* Name,const FShaderParametersMetadata& UniformBufferStruct)
{
	return "
		#ifndef __UniformBuffer_Material_Definition__
		#define __UniformBuffer_Material_Definition__

		cbuffer Material
		{
			half4 Material_VectorExpressions[3];
			half4 Material_ScalarExpressions[1];
		}

		SamplerState Material_Wrap_WorldGroupSettings;
		SamplerState Material_Clamp_WorldGroupSettings;

		static const struct
		{
			half4 VectorExpressions[3];
			half4 ScalarExpressions[1];
			SamplerState Wrap_WorldGroupSettings;
			SamplerState Clamp_WorldGroupSettings;
		} Material = { Material_VectorExpressions, Material_ScalarExpressions, Material_Wrap_WorldGroupSettings, Material_Clamp_WorldGroupSettings };

		#endif
	";
}
