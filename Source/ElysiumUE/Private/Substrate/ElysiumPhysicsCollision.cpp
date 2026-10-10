#include "ElysiumPhysicsCollision.h"

#include <cmath>

#if PLATFORM_WINDOWS
#pragma float_control(precise, on, push)
#endif

namespace ElysiumBoxConstruction
{
	// 2600a1a0 -> 26009b10 -> 2606bec0: generated seed's point order, recovered by
	// read-only Collision007#19 execution. Full packed features retain opposite-edge indices.
	constexpr int32 CornerPermutation[8] = {7, 2, 6, 3, 5, 4, 0, 1};
	constexpr uint32 Triangles[12][4] = {
		{0xb000, 0x00060000, 0x000f0001, 0x00080002},
		{0xa001, 0x00160000, 0x001b0003, 0x7ffa0001},
		{0x9002, 0x00060002, 0x00130004, 0x7ff80000},
		{0x8003, 0x00060002, 0x00200005, 0x7ffa0004},
		{0x7004, 0x7ff10002, 0x00030001, 0x7ffa0005},
		{0x6005, 0x7ffd0005, 0x000d0001, 0x00180006},
		{0x5006, 0x00060000, 0x000c0007, 0x7fea0003},
		{0x4007, 0x7fed0000, 0x000b0004, 0x7ffa0007},
		{0x3008, 0x7fe50001, 0x00030003, 0x7ff30006},
		{0x2009, 0x7ffd0006, 0x7ff40003, 0x00040007},
		{0x100a, 0x7ff50007, 0x00030004, 0x7ffc0006},
		{0x000b, 0x7ffd0006, 0x7fe00004, 0x7fe80005}
	};

	template<typename T> T Read(const TArray<uint8>& Data, int32 Offset)
	{
		T Value;
		FMemory::Memcpy(&Value, Data.GetData() + Offset, sizeof(T));
		return Value;
	}
	template<typename T> void Write(TArray<uint8>& Data, int32 Offset, T Value)
	{
		FMemory::Memcpy(Data.GetData() + Offset, &Value, sizeof(T));
	}
	FVector3d Point(const TArray<uint8>& Data, int32 Index)
	{
		return FVector3d(Read<float>(Data, 256 + Index * 16),
			Read<float>(Data, 260 + Index * 16), Read<float>(Data, 264 + Index * 16));
	}
	FVector3d TrianglePoint(const TArray<uint8>& Data, int32 Triangle, int32 Edge)
	{
		return Point(Data, Triangles[Triangle][Edge + 1] & 0xffff);
	}
	void Bounds(const TArray<uint8>& Data, FVector3d& OutMins, FVector3d& OutMaxs)
	{
		// 26031e60: initialize from first triangle's first point; preserve unordered arms.
		OutMins = OutMaxs = TrianglePoint(Data, 0, 0);
		for (int32 Triangle = 0; Triangle < 12; ++Triangle)
		{
			for (int32 Edge = 0; Edge < 3; ++Edge)
			{
				const FVector3d Position = TrianglePoint(Data, Triangle, Edge);
				for (int32 Axis = 0; Axis < 3; ++Axis)
				{
					if (Position[Axis] < OutMins[Axis]) OutMins[Axis] = Position[Axis];
					else if (OutMaxs[Axis] < Position[Axis]) OutMaxs[Axis] = Position[Axis];
				}
			}
		}
	}
	FVector3d TriangleNormal(const TArray<uint8>& Data, int32 Triangle)
	{
		// 26032f10: next cross previous, not a renderer's winding convention.
		const FVector3d Origin = TrianglePoint(Data, Triangle, 0);
		return FVector3d::CrossProduct(TrianglePoint(Data, Triangle, 1) - Origin,
			TrianglePoint(Data, Triangle, 2) - Origin);
	}

	struct FAxisIntegrals { double Volume = 0.; double First = 0.; double Second = 0.; };
	FAxisIntegrals IntegrateAxis(const TArray<uint8>& Data, const FVector3d& Center,
		int32 Axis, int32 NextAxis, int32 LastAxis)
	{
		// 2607ff80 -> 2607f9f0: ordered directed-edge polynomial integration. Constants
		// 260ca328=1e-12, 260cab80 is FLOAT 0.2 promoted to double, not exact 1/5.
		FAxisIntegrals Result;
		for (int32 Triangle = 0; Triangle < 12; ++Triangle)
		{
			FVector3d Normal = TriangleNormal(Data, Triangle);
			const double NormalLength = std::sqrt(Normal.SizeSquared());
			if (NormalLength != 0.) Normal /= NormalLength; // 260279a0
			const bool bUseLast = FMath::Abs(Normal[NextAxis]) <= FMath::Abs(Normal[LastAxis]);
			const double LastFactor = bUseLast && FMath::Abs(Normal[LastAxis]) > 1e-12
				? -0.5 / Normal[LastAxis] * Normal[NextAxis] : 0.;
			const double NextFactor = !bUseLast ? 0.5 / Normal[NextAxis] * Normal[LastAxis] : 0.;
			double Volume = 0., First = 0., Second = 0.;
			for (int32 Edge = 0; Edge < 3; ++Edge)
			{
				// 2602a0c0's identity inverse transform stores translated points as float.
				const FVector3f Start(TrianglePoint(Data, Triangle, Edge) - Center);
				const FVector3f End(TrianglePoint(Data, Triangle, (Edge + 1) % 3) - Center);
				const FVector3d Delta = FVector3d(End) - FVector3d(Start);
				if (!(std::sqrt(Delta.SizeSquared()) * 1e-12 <= FMath::Abs(Delta[Axis]))) continue;
				const double Slope = Delta[NextAxis] / Delta[Axis];
				const double Intercept = double(Start[NextAxis]) - double(Start[Axis]) * Slope;
				double Constant, Linear, Quadratic;
				if (bUseLast)
				{
					Constant = Intercept * Intercept * LastFactor;
					Linear = Intercept * Slope * LastFactor;
					Linear += Linear;
					Quadratic = Slope * Slope * LastFactor;
				}
				else
				{
					const double LastSlope = Delta[LastAxis] / Delta[Axis];
					const double LastIntercept = double(Start[LastAxis]) - double(Start[Axis]) * LastSlope;
					Constant = (NextFactor * LastIntercept + Intercept) * LastIntercept;
					const double Repeated = LastIntercept * LastSlope * NextFactor;
					Linear = LastIntercept * Slope + LastSlope * Intercept + Repeated + Repeated;
					Quadratic = (LastSlope * NextFactor + Slope) * LastSlope;
				}
				const double Start1 = Start[Axis], End1 = End[Axis];
				const double Start2 = Start1 * Start1, End2 = End1 * End1;
				const double Start3 = Start2 * Start1, End3 = End2 * End1;
				const double Start4 = Start3 * Start1, End4 = End3 * End1;
				const double Integral2 = (End2 - Start2) * 0.5;
				const double Integral3 = (End3 - Start3) * (1. / 3.);
				const double Integral4 = (End4 - Start4) * 0.25;
				Volume = Integral2 * Linear + Integral3 * Quadratic + (End1 - Start1) * Constant + Volume;
				First = Integral2 * Constant + Integral3 * Linear + Integral4 * Quadratic + First;
				Second = Integral3 * Constant + Integral4 * Linear
					+ (End4 * End1 - Start4 * Start1) * double(0.2f) * Quadratic + Second;
			}
			Result.Volume += Volume;
			Result.First += First;
			Result.Second += Second;
		}
		return Result;
	}

	void MassProperties(const TArray<uint8>& Data, FVector3d& OutCenter, FVector3d& OutInertia)
	{
		// 2607f9d0 / 2607ff30 / 2607fd80: float first-moment writes, double volume/area.
		FVector3f Moment(0.f);
		double Area = 0., Volume = 0.;
		for (int32 Triangle = 0; Triangle < 12; ++Triangle)
		{
			const FVector3d A = TrianglePoint(Data, Triangle, 0);
			const FVector3d B = TrianglePoint(Data, Triangle, 1) - A;
			const FVector3d C = TrianglePoint(Data, Triangle, 2) - A;
			const double NormalX = float(B.Y * C.Z - B.Z * C.Y);
			const double NormalY = float(B.Z * C.X - B.X * C.Z);
			const double NormalZ = B.X * C.Y - B.Y * C.X;
			const double Signed = NormalX * A.X + NormalZ * A.Z + NormalY * A.Y;
			// pop edi at 2607fe76 shifts the subsequent +2c read to saved NormalX.
			Area += NormalZ * NormalZ + NormalY * NormalY + NormalX * NormalX;
			Volume += Signed;
			double FirstZ = 0.;
			for (int32 Edge = 0; Edge < 3; ++Edge)
			{
				const FVector3d Position = TrianglePoint(Data, Triangle, Edge);
				for (int32 Axis = 0; Axis < 2; ++Axis)
					Moment[Axis] = float(Position[Axis] * (Signed * 0.25) + double(Moment[Axis]));
				// 2607feab/2607fec4 retain first Z in double through the second addition;
				// 2607fef6 rounds that sum before the third point, unlike X and Y.
				if (Edge == 0) FirstZ = Position.Z * (Signed * 0.25) + double(Moment.Z);
				else Moment.Z = float(Position.Z * (Signed * 0.25) + (Edge == 1 ? FirstZ : double(Moment.Z)));
			}
		}
		const double AreaRoot = std::sqrt(Area);
		if (!(Volume > AreaRoot * AreaRoot * AreaRoot * double(1e-9f)))
		{
			// 260800f0 negative, small, degenerate and unordered-volume fallback.
			FVector3d Mins, Maxs; Bounds(Data, Mins, Maxs);
			OutCenter = (Mins + Maxs) * 0.5;
			const double HalfDiagonal = (Mins - Maxs).Size() * 0.5;
			OutInertia = FVector3d(HalfDiagonal * HalfDiagonal * 0.5);
			return;
		}
		// 260803d0: actual float first moments are multiplied by a double inverse volume.
		OutCenter = FVector3d(Moment) / Volume;
		FVector3d SecondMoments;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const FAxisIntegrals Integrals = IntegrateAxis(Data, OutCenter, Axis, (Axis + 1) % 3, (Axis + 2) % 3);
			SecondMoments[Axis] = Integrals.Volume >= 1e-19 ? Integrals.Second / Integrals.Volume : 1.;
		}
		// 260800f0 stores the norm of pairs of second moments (not their sum).
		OutInertia.X = std::sqrt(SecondMoments.Z * SecondMoments.Z + SecondMoments.Y * SecondMoments.Y);
		OutInertia.Y = std::sqrt(SecondMoments.Z * SecondMoments.Z + SecondMoments.X * SecondMoments.X);
		OutInertia.Z = std::sqrt(SecondMoments.Y * SecondMoments.Y + SecondMoments.X * SecondMoments.X);
	}

	uint8 QuantizeByte(double Value)
	{
		// 260b4bc4 truncates with fistp QWORD; the INT64_MIN indefinite low byte is zero.
		return FMath::IsFinite(Value) && Value >= -9223372036854775808. && Value < 9223372036854775808.
			? uint8(int64(Value)) : 0;
	}
	uint8 QuantizeRadiusRatio(double Numerator, double Radius, bool bAddOne)
	{
		// 2606d9b2 / 2606f92a keep radius*.004 and division on the x87 stack.
		// A double rounds 1/.004 up to 250; retail truncates the value just below 250.
		// Recover the discarded product/division residual before integer truncation.
		const double Denominator = Radius * 0.004;
		const double Ratio = Numerator / Denominator;
		const double ProductError = std::fma(Radius, 0.004, -Denominator);
		const double Residual = (std::fma(-Ratio, Denominator, Numerator) - Ratio * ProductError) / Denominator;
		double Value = Ratio + (bAddOne ? 1. : 0.);
		if (FMath::IsFinite(Value) && Value == std::trunc(Value) && Residual != 0.)
			Value = std::nextafter(Value, Residual < 0. ? -INFINITY : INFINITY);
		return QuantizeByte(Value);
	}
	void BuildSurface(TArray<uint8>& Data, const FVector3f& SourceMins, const FVector3f& SourceMaxs)
	{
		Data.SetNumZeroed(412);
		// 2600a050: clone seed ledge, retain packed topology, rewrite only XYZ coordinates.
		Write<int32>(Data, 48, 208); Write<uint32>(Data, 56, 0x1504); Write<int16>(Data, 60, 12);
		for (int32 Triangle = 0; Triangle < 12; ++Triangle)
			for (int32 Word = 0; Word < 4; ++Word) Write(Data, 64 + Triangle * 16 + Word * 4, Triangles[Triangle][Word]);
		for (int32 VertexIndex = 0; VertexIndex < 8; ++VertexIndex)
		{
			// 2600a140 bit-selector order; 2600a050 conversion rounds each product to float.
			const int32 Corner = CornerPermutation[VertexIndex];
			const float X = (Corner & 1) ? SourceMaxs.X : SourceMins.X;
			const float Y = (Corner & 2) ? SourceMaxs.Y : SourceMins.Y;
			const float Z = (Corner & 4) ? SourceMaxs.Z : SourceMins.Z;
			Write(Data, 256 + VertexIndex * 16, X * 0.0254f);
			Write(Data, 260 + VertexIndex * 16, -(Z * 0.0254f));
			Write(Data, 264 + VertexIndex * 16, Y * 0.0254f);
		}
		FVector3d Center, Inertia; MassProperties(Data, Center, Inertia);
		double RadiusSquared = 0., DeviationSquared = 0.;
		for (int32 Triangle = 0; Triangle < 12; ++Triangle)
		{
			// 26032360 -> 260320f0; triangle order and NaN comparisons are retained.
			const FVector3d Normal = TriangleNormal(Data, Triangle);
			for (int32 Edge = 0; Edge < 3; ++Edge)
			{
				const FVector3d Relative = TrianglePoint(Data, Triangle, Edge) - Center;
				const double Squared = Relative.Z * Relative.Z + Relative.X * Relative.X + Relative.Y * Relative.Y;
				if (RadiusSquared < Squared) RadiusSquared = Squared;
				const FVector3d Tangent = FVector3d::CrossProduct(Relative, Normal);
				double TangentSquared = (Tangent.Y * Tangent.Y + Tangent.Z * Tangent.Z + Tangent.X * Tangent.X)
					* (1. / (Normal.Y * Normal.Y + Normal.Z * Normal.Z + Normal.X * Normal.X));
				// This service constructs axis-aligned box ledges only. Cancel the single
				// nonzero normal component before double rounding, preserving the x87 result
				// at planar deviation/radius=1 boundaries (26032204 -> 2606f92a).
				if (FMath::IsFinite(Normal.X) && Normal.X != 0. && Normal.Y == 0. && Normal.Z == 0.)
					TangentSquared = Relative.Y * Relative.Y + Relative.Z * Relative.Z;
				else if (FMath::IsFinite(Normal.Y) && Normal.Y != 0. && Normal.X == 0. && Normal.Z == 0.)
					TangentSquared = Relative.Z * Relative.Z + Relative.X * Relative.X;
				else if (FMath::IsFinite(Normal.Z) && Normal.Z != 0. && Normal.X == 0. && Normal.Y == 0.)
					TangentSquared = Relative.Y * Relative.Y + Relative.X * Relative.X;
				if (DeviationSquared < TangentSquared) DeviationSquared = TangentSquared;
			}
		}
		const double Radius = std::sqrt(RadiusSquared);
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Write(Data, Axis * 4, float(Center[Axis]));
			Write(Data, 12 + Axis * 4, float(Inertia[Axis]));
		}
		Write(Data, 24, float(Radius));
		Write<uint32>(Data, 28, (412u << 8) | QuantizeRadiusRatio(std::sqrt(DeviationSquared), Radius, true));
		Write<int32>(Data, 32, 384); Write<uint32>(Data, 44, 0x53505649); // 26009fd0 IVPS tag
		FVector3d Mins, Maxs; Bounds(Data, Mins, Maxs);
		const FVector3d LeafCenter = (Mins + Maxs) * 0.5;
		const double LeafRadius = (Maxs - LeafCenter).Size();
		Write<int32>(Data, 388, -336); // 2606f750 leaf -> cloned ledge
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Write(Data, 392 + Axis * 4, float(LeafCenter[Axis]));
			Data[408 + Axis] = QuantizeRadiusRatio(Maxs[Axis] - LeafCenter[Axis], LeafRadius, false) + uint8(1);
		}
		Write(Data, 404, float(LeafRadius));
	}
	bool ExactlyEqual(const FVector3f& A, const FVector3f& B)
	{
		return A.X == B.X && A.Y == B.Y && A.Z == B.Z;
	}
}

FElysiumPhysicsCollide::FElysiumPhysicsCollide(const FVector3f& SourceMins, const FVector3f& SourceMaxs)
{
	ElysiumBoxConstruction::BuildSurface(Bytes, SourceMins, SourceMaxs);
}
FVector3f FElysiumPhysicsCollide::Vertex(int32 Index) const
{
	check(Index >= 0 && Index < 8);
	return FVector3f(ElysiumBoxConstruction::Point(Bytes, Index));
}
uint32 FElysiumPhysicsCollide::TriangleWord(int32 Triangle, int32 Word) const
{
	check(Triangle >= 0 && Triangle < 12 && Word >= 0 && Word < 4);
	return ElysiumBoxConstruction::Read<uint32>(Bytes, 64 + Triangle * 16 + Word * 4);
}
FVector3f FElysiumPhysicsCollide::MassCenter() const
{
	return FVector3f(ElysiumBoxConstruction::Read<float>(Bytes, 0), ElysiumBoxConstruction::Read<float>(Bytes, 4), ElysiumBoxConstruction::Read<float>(Bytes, 8));
}
FVector3f FElysiumPhysicsCollide::RotationInertia() const
{
	return FVector3f(ElysiumBoxConstruction::Read<float>(Bytes, 12), ElysiumBoxConstruction::Read<float>(Bytes, 16), ElysiumBoxConstruction::Read<float>(Bytes, 20));
}
float FElysiumPhysicsCollide::Radius() const { return ElysiumBoxConstruction::Read<float>(Bytes, 24); }

FElysiumPhysicsCollide* FElysiumPhysicsCollision::BBoxToCollide(const FVector3f& SourceMins, const FVector3f& SourceMaxs)
{
	// 2600a310: only full point degeneracy returns null; no sorting or thickness repair.
	if (ElysiumBoxConstruction::ExactlyEqual(SourceMins, SourceMaxs)) return nullptr;
	// 2600a480: newest-first exact floating comparisons (NaN never matches, signed zero does).
	for (int32 Index = BoxCache.Num() - 1; Index >= 0; --Index)
	{
		const FBoxCacheEntry& Entry = BoxCache[Index];
		if (ElysiumBoxConstruction::ExactlyEqual(SourceMins, Entry.Mins)
			&& ElysiumBoxConstruction::ExactlyEqual(SourceMaxs, Entry.Maxs)) return Entry.Geometry;
	}
	if (BoxCache.IsEmpty())
	{
		// 2600a1a0 seeds the cache, even when the request itself has these exact endpoints.
		const FVector3f SeedMins(-16.f, -16.f, 0.f), SeedMaxs(16.f, 16.f, 72.f);
		BoxCache.Add({SeedMins, SeedMaxs, new FElysiumPhysicsCollide(SeedMins, SeedMaxs)});
	}
	FElysiumPhysicsCollide* Geometry = new FElysiumPhysicsCollide(SourceMins, SourceMaxs);
	BoxCache.Add({SourceMins, SourceMaxs, Geometry}); // 2600a3e0 append, non-owning
	return Geometry;
}
void FElysiumPhysicsCollision::DestroyCollide(FElysiumPhysicsCollide* Geometry)
{
	delete Geometry; // Collision007#7 2600a520 -> aligned free2602bf10; cache untouched
}

#if PLATFORM_WINDOWS
#pragma float_control(pop)
#endif
