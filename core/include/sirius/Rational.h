#pragma once

#include <cstdint>
#include <string>

namespace sirius
{

/// Exact rational number — the arithmetic primitive of the conceptual-time
/// engine. The white paper's central claim is that the engine is "exact by
/// construction" (Part III); that claim rests on this type. Conceptual
/// positions, tempo-map breakpoints, and tuplet subdivisions are all exact
/// rationals, never floating-point, until the membrane renders them.
///
/// Always stored normalized: denominator > 0, gcd(numerator, denominator) == 1,
/// and zero is canonically 0/1. Two Rationals are equal iff their normalized
/// forms are identical, so equality is exact — there is no tolerance.
///
/// Backed by int64. Arithmetic is overflow-checked and throws std::overflow_error
/// rather than silently wrapping — a wrong-but-quiet answer is the one failure
/// mode this type exists to prevent. Musical subdivisions and tuplets stay well
/// within int64 in practice; multi-precision promotion is a documented future
/// extension if a real time domain ever needs it.
class Rational
{
public:
    /// Constructs zero (0/1).
    constexpr Rational() = default;

    /// Constructs an integer value (numerator/1).
    Rational (std::int64_t numerator);

    /// Constructs numerator/denominator, normalized. Throws std::invalid_argument
    /// if denominator is zero.
    Rational (std::int64_t numerator, std::int64_t denominator);

    std::int64_t numerator()   const noexcept { return num; }
    std::int64_t denominator() const noexcept { return den; }

    bool isZero()     const noexcept { return num == 0; }
    bool isInteger()  const noexcept { return den == 1; }
    bool isNegative() const noexcept { return num < 0; }

    Rational operator+ (const Rational& other) const;
    Rational operator- (const Rational& other) const;
    Rational operator* (const Rational& other) const;
    Rational operator/ (const Rational& other) const;
    Rational operator- () const;

    Rational& operator+= (const Rational& other);
    Rational& operator-= (const Rational& other);
    Rational& operator*= (const Rational& other);
    Rational& operator/= (const Rational& other);

    bool operator== (const Rational& other) const noexcept;
    bool operator!= (const Rational& other) const noexcept;
    bool operator<  (const Rational& other) const;
    bool operator<= (const Rational& other) const;
    bool operator>  (const Rational& other) const;
    bool operator>= (const Rational& other) const;

    /// Lossy conversion to double. This belongs at the membrane only — the
    /// engine never uses it internally. Named explicitly so its use is visible.
    double toDouble() const noexcept;

    /// "numerator/denominator" — for diagnostics and test failure messages.
    std::string toString() const;

private:
    void normalize();

    std::int64_t num { 0 };
    std::int64_t den { 1 };
};

} // namespace sirius
