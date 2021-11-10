/*
UMaterialInterface  ->  UMaterial
					->  UMaterialInterface  ->  UMaterialInstanceConstant
											->  UMaterialInstanceDynamic

FMaterial  ->  FMaterialResource

FMaterialResource  ->
				   ->
				   ->

UMaterialInstance::Resource 		<->		FMaterialInstanceResource::Owner
UMaterial::DefaultMaterialInstance 	<-> 	FDefaultMaterialInstance::Material
UMaterial::MaterialResources[][]	<->		FMaterialResource::Material
*/

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
class UMaterialInterface : public UObject, public IBlendableInterface, public IInterface_AssetUserData {
	virtual UMaterial* GetMaterial() = 0; /** Get the material which we are instancing. Walks up parent chain and finds the base Material that this is an instance of. */
	virtual FMaterialRenderProxy* GetRenderProxy() = 0;
	virtual FMaterialResource* GetMaterialResource(, EMaterialQualityLevel::Type QualityLevel) { return NULL; }
}
class UMaterialInstance : public UMaterialInterface {
	UMaterialInterface* Parent;
	FMaterialInstanceResource* Resource; /* FMaterialRenderProxy derivative that represent this material instance to the renderer, when the renderer needs to fetch parameter values. */
	TArray<struct FScalarParameterValue> ScalarParameterValues;
	TArray<struct FVectorParameterValue> VectorParameterValues;
	TArray<struct FTextureParameterValue> TextureParameterValues;
}
class UMaterial : public UMaterialInterface {
	FDefaultMaterialInstance* DefaultMaterialInstance; /* FMaterialRenderProxy derivative that represent this material to the renderer, when the renderer needs to fetch parameter values. */
	FMaterialResource* MaterialResources[EMaterialQualityLevel::Num][ERHIFeatureLevel::Num];

	virtual UMaterial* UMaterial::GetMaterial()
	{
		return this;
	}
	virtual FMaterialRenderProxy* GetRenderProxy()
	{
		return DefaultMaterialInstance;
	}
	virtual FMaterialResource* GetMaterialResource(ERHIFeatureLevel::Type InFeatureLevel, EMaterialQualityLevel::Type QualityLevel) 
	{
		return MaterialResources[QualityLevel][InFeatureLevel];
	}
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FMaterialRenderProxy : public FRenderResource {
	FUniformExpressionCache UniformExpressionCache[ERHIFeatureLevel::Num]; /** Cached uniform expressions. */
	FImmutableSamplerState ImmutableSamplerState; /** Cached external texture immutable samplers */

	const FMaterial* GetMaterial(;) const;
	virtual const FMaterial& GetMaterialWithFallback(, const FMaterialRenderProxy*& OutFallbackMaterialRenderProxy) const = 0;
	virtual FMaterial* GetMaterialNoFallback(ERHIFeatureLevel::Type InFeatureLevel) const { return NULL; }
	virtual UMaterialInterface* GetMaterialInterface() const { return NULL; }
}

// In MaterialInstanceSupport.h
class FMaterialInstanceResource: public FMaterialRenderProxy { /** The resource used to render a UMaterialInstance. */
	UMaterialInterface* Parent; /** The parent of the material instance. */
	UMaterialInstance* Owner;   /** The UMaterialInstance which owns this resource. */
	UMaterialInterface* GameThreadParent;
	TArray<TNamedParameter<FLinearColor> > VectorParameterArray;
	TArray<TNamedParameter<float> > ScalarParameterArray;
	TArray<TNamedParameter<UTexture*> > TextureParameterArray;
};

// In Material.cpp
class FDefaultMaterialInstance : public FMaterialRenderProxy {
	UMaterial* Material;
	FDefaultMaterialInstance(UMaterial* InMaterial) : Material(InMaterial) {}
	FMaterialRenderProxy& GetFallbackRenderProxy() const
	{
		return *(UMaterial::GetDefaultMaterial(Material->MaterialDomain)->GetRenderProxy());
	}

public:
	virtual const FMaterial& GetMaterialWithFallback(ERHIFeatureLevel::Type FeatureLevel, const FMaterialRenderProxy*& OutFallbackMaterialRenderProxy) const
	{
		const FMaterialResource* MaterialResource = Material->GetMaterialResource(FeatureLevel);
		
		if (MaterialResource && MaterialResource->GetRenderingThreadShaderMap())
		{
			return *MaterialResource;
		}
		else
		{
			OutFallbackMaterialRenderProxy = &GetFallbackRenderProxy();
			return OutFallbackMaterialRenderProxy->GetMaterialWithFallback(FeatureLevel, OutFallbackMaterialRenderProxy);
		}
	}
};

//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
class FMaterialResource : public FMaterial { /** Implementation of the FMaterial interface for a UMaterial or UMaterialInstance. */
	UMaterial* Material;
	UMaterialInstance* MaterialInstance;
};


//////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * FMaterial serves 3 intertwined purposes:
 *   Represents a material to the material compilation process, and provides hooks for extensibility (CompileProperty, etc)
 *   Represents a material to the renderer, with functions to access material properties
 *   Stores a cached shader map, and other transient output from a compile, which is necessary with async shader compiling
 *      (when a material finishes async compilation, the shader map and compile errors need to be stored somewhere)
 */
class FMaterial {
	FMaterialShaderMap* RenderingThreadShaderMap; /** Shader map for this material resource which is accessible by the rendering thread. This must be updated along with GameThreadShaderMap, but on the rendering thread. */

	/**
	 * Finds the shader matching the template type and the passed in vertex factory, asserts if not found.
	 * Note - Only implemented for FMeshMaterialShaderTypes
	 */
	template<typename MeshShaderType>
	MeshShaderType* GetShader(FVertexFactoryType* VertexFactoryType)
	{
		return (MeshShaderType*)GetShader(&MeshShaderType::StaticType, VertexFactoryType);
	}
	ENGINE_API FShader* GetShader(FMeshMaterialShaderType* MeshShaderType, FVertexFactoryType* VertexFactoryType)
	{
		return RenderingThreadShaderMap->GetMeshShaderMap(VertexFactoryType)->GetShader(MeshShaderType);
	}
}



