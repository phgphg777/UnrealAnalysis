
struct FShaderPermutationBool {
	using Type = bool;/** Setup the dimension's type in permutation domain as boolean. */
	static constexpr int32 PermutationCount = 2;/** Setup the dimension's number of permutation. */
	static constexpr bool IsMultiDimensional = false;

	/** Converts dimension boolean value to dimension's value id. */
	static int32 ToDimensionValueId(Type E)
	{
		return E ? 1 : 0;
	}
	/** Pass down a boolean to FShaderCompilerEnvironment::SetDefine(). */
	static bool ToDefineValue(Type E)
	{
		return E;
	}
	/** Converts dimension's value id to dimension boolean value (exact reciprocal of ToDimensionValueId). */
	static Type FromDimensionValueId(int32 PermutationId)
	{
		return PermutationId == 1;
	}
};

#define SHADER_PERMUTATION_BOOL(InDefineName) \
	public FShaderPermutationBool { \
	public: \
		static constexpr const TCHAR* DefineName = TEXT(InDefineName); \
	}

#define SHADER_PERMUTATION_ENUM_CLASS(InDefineName, EnumName) \
	public TShaderPermutationInt<EnumName, static_cast<int32>(EnumName::MAX)> { \
	public: \
		static constexpr const TCHAR* DefineName = TEXT(InDefineName); \
	}	

class FFirstDim : SHADER_PERMUTATION_BOOL("USE_SOME");
/*
class FSomeDim : public FShaderPermutationBool {
public: 
	static constexpr const TCHAR* DefineName = TEXT("USE_SOME"); 
};
*/

class FSecondDim : SHADER_PERMUTATION_ENUM_CLASS("USE_MODE", ESomeMode); 
/*
class FSecondDim : public TShaderPermutationInt<EnumName, static_cast<int32>(EnumName::MAX)> { 
public: 
	static constexpr const TCHAR* DefineName = TEXT("USE_MODE"); 
}
*/

class TShaderPermutationDomainSpetialization<false> {
	template<typename TPermutationVector, typename TDimensionToSet>
	static void SetDimension(TPermutationVector& PermutationVector, const TDimensionToSet::Type& Value)
	{
		return PermutationVector.Tail.Set<TDimensionToSet>(Value);
	}
};
class TShaderPermutationDomainSpetialization<true> {
	template<typename TPermutationVector, typename TDimensionToSet>
	static void SetDimension(TPermutationVector& PermutationVector, const TDimensionToSet::Type& Value)
	{
		PermutationVector.DimensionValue = Value;
	}
};

template <typename TDimension, typename... Ts>
struct TShaderPermutationDomain<TDimension, Ts...> {
	using Type = TShaderPermutationDomain<TDimension, Ts...>;
	using Super = TShaderPermutationDomain<Ts...>;
	static constexpr bool IsMultiDimensional = true;
	static constexpr int32 PermutationCount = 
		Super::PermutationCount * TDimension::PermutationCount;

	TShaderPermutationDomain<TDimension, Ts...>()
		: DimensionValue(TDimension::FromDimensionValueId(0)) {}

	template<class DimensionToSet>
	void Set(typename DimensionToSet::Type Value)
	{
		return TShaderPermutationDomainSpetialization< TIsSame<TDimension, DimensionToSet >::Value>::SetDimension<Type, DimensionToSet>(*this, Value);
	}

private:
	typename TDimension::Type DimensionValue;
	Super Tail;
}


class FSomePS : public FGlobalShader {
	class FFirstDim	: SHADER_PERMUTATION_BOOL("USE_SOME"); 
	class FSecondDim: SHADER_PERMUTATION_ENUM_CLASS("USE_MODE", ESomeMode); 
	using FPermutationDomain = TShaderPermutationDomain<FFirstDim, FSecondDim>;
}

void foo(...)
{
	FSomePS::FPermutationDomain PermutationVector;
	/*
	PermutationVector = TShaderPermutationDomain<FFirstDim, FSecondDim>()
	{
		DimensionValue = FFirstDim::FromDimensionValueId(0);
		Tail = TShaderPermutationDomain<FSecondDim>();
		{
			DimensionValue = FSecondDim::FromDimensionValueId(0);
			Tail = TShaderPermutationDomain<>();
		}
	}
	*/
	bool bSomeFeature = ...;
	ESomeMode SomeMode = ...;
	PermutationVector.Set< FSomePS::FFirstDim >(bSomeFeature);
	PermutationVector.Set< FSomePS::FSecondDim >(SomeMode);

	TShaderMapRef< FSomePS > PixelShader( View.ShaderMap, PermutationVector );

	...
}
