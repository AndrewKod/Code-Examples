#pragma once

#include "CoreMinimal.h"

#define MAX_DENSITYVALUE (MAX_int16 / 2)

struct FDensityValue
{
private:
	int16 Value;

public:
	FDensityValue() 
	{ 
		this->Value = 0;
	}

	FDensityValue(int16 Value)
	{
		this->Value = Value;
	}

	FDensityValue(float Value)
	{
		//this->Value = FMath::Clamp<int32>(FMath::RoundToInt(FMath::Clamp<float>(Value, -1, 1) * MAX_DENSITYVALUE), MIN_int16, MAX_int16);
		this->Value = FMath::Clamp<int32>(FMath::RoundToInt(FMath::Clamp<float>(Value, -1, 1) * MAX_DENSITYVALUE), -MAX_DENSITYVALUE, MAX_DENSITYVALUE);
	}

	inline bool IsZero() const { return this->Value == 0; }
	inline bool IsValid() const { return !FMath::IsNaN(this->Value) && FMath::IsFinite(this->Value); }
	inline bool IsInEmptySpace() const { return this->Value > 0; }	

	inline float ToFloat() const { return float(this->Value) / float(MAX_DENSITYVALUE); }

	inline float CalcAlpha(const FDensityValue& OtherValue, bool& bSuccess) const
	{
		float Alpha = this->ToFloat() / (this->ToFloat() - OtherValue.ToFloat());
		bSuccess = !FMath::IsNaN(Alpha) && FMath::IsFinite(Alpha);

		return Alpha;		
	}

public:
	bool operator==(const FDensityValue& Other) const { return this->Value == Other.Value; }
	bool operator!=(const FDensityValue& Other) const { return this->Value != Other.Value; }
	bool operator<(const FDensityValue& Other) const { return this->Value < Other.Value; }
	bool operator>(const FDensityValue& Other) const { return this->Value > Other.Value; }
	bool operator<=(const FDensityValue& Other) const { return this->Value <= Other.Value; }
	bool operator>=(const FDensityValue& Other) const { return this->Value >= Other.Value; }

public:
	friend inline FArchive& operator<<(FArchive &Ar, FDensityValue& Save)
	{
		Ar << Save.Value;
		return Ar;
	}

	bool Serialize(FArchive& Ar)
	{
		Ar << *this;
		return true;
	}
};