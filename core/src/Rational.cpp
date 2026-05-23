#include "ida/Rational.h"

#include <limits>
#include <numeric>
#include <stdexcept>

namespace sirius
{

namespace
{
    constexpr std::int64_t kInt64Max = std::numeric_limits<std::int64_t>::max();
    constexpr std::int64_t kInt64Min = std::numeric_limits<std::int64_t>::min();

    [[noreturn]] void throwOverflow()
    {
        throw std::overflow_error ("ida::Rational: int64 overflow");
    }

    std::int64_t checkedMul (std::int64_t a, std::int64_t b)
    {
#if defined(__SIZEOF_INT128__)
        const __int128 result = static_cast<__int128> (a) * static_cast<__int128> (b);
        if (result > static_cast<__int128> (kInt64Max)
         || result < static_cast<__int128> (kInt64Min))
            throwOverflow();
        return static_cast<std::int64_t> (result);
#else
        if (a == 0 || b == 0)
            return 0;
        if ((a == -1 && b == kInt64Min) || (b == -1 && a == kInt64Min))
            throwOverflow();
        const std::int64_t result = a * b;
        if (result / a != b)
            throwOverflow();
        return result;
#endif
    }

    std::int64_t checkedAdd (std::int64_t a, std::int64_t b)
    {
        if (b > 0 && a > kInt64Max - b)
            throwOverflow();
        if (b < 0 && a < kInt64Min - b)
            throwOverflow();
        return a + b;
    }

    std::int64_t checkedNegate (std::int64_t a)
    {
        if (a == kInt64Min)
            throwOverflow();
        return -a;
    }
}

void Rational::normalize()
{
    if (den == 0)
        throw std::invalid_argument ("ida::Rational: zero denominator");

    // Canonicalize the sign onto the numerator so the denominator is always
    // positive. INT64_MIN has no positive counterpart, so negating it overflows.
    if (den < 0)
    {
        num = checkedNegate (num);
        den = checkedNegate (den);
    }

    if (num == 0)
    {
        den = 1;
        return;
    }

    // std::gcd would need std::abs of each argument, and abs(INT64_MIN)
    // overflows. Reject that boundary explicitly rather than invoke UB.
    if (num == kInt64Min || den == kInt64Min)
        throwOverflow();

    const std::int64_t g = std::gcd (num, den);
    num /= g;
    den /= g;
}

Rational::Rational (std::int64_t numerator)
    : num (numerator), den (1)
{
}

Rational::Rational (std::int64_t numerator, std::int64_t denominator)
    : num (numerator), den (denominator)
{
    normalize();
}

Rational Rational::operator+ (const Rational& other) const
{
    // a/b + c/d = (a*d + c*b) / (b*d); the constructor normalizes the result.
    return Rational (checkedAdd (checkedMul (num, other.den),
                                 checkedMul (other.num, den)),
                     checkedMul (den, other.den));
}

Rational Rational::operator- (const Rational& other) const
{
    return Rational (checkedAdd (checkedMul (num, other.den),
                                 checkedNegate (checkedMul (other.num, den))),
                     checkedMul (den, other.den));
}

Rational Rational::operator* (const Rational& other) const
{
    return Rational (checkedMul (num, other.num),
                     checkedMul (den, other.den));
}

Rational Rational::operator/ (const Rational& other) const
{
    // Division by zero surfaces as a zero denominator, which the constructor
    // rejects with std::invalid_argument.
    return Rational (checkedMul (num, other.den),
                     checkedMul (den, other.num));
}

Rational Rational::operator- () const
{
    Rational result;
    result.num = checkedNegate (num);
    result.den = den;
    return result;
}

Rational& Rational::operator+= (const Rational& other) { return *this = *this + other; }
Rational& Rational::operator-= (const Rational& other) { return *this = *this - other; }
Rational& Rational::operator*= (const Rational& other) { return *this = *this * other; }
Rational& Rational::operator/= (const Rational& other) { return *this = *this / other; }

bool Rational::operator== (const Rational& other) const noexcept
{
    // Both operands are normalized, so identical fields mean equal values.
    return num == other.num && den == other.den;
}

bool Rational::operator!= (const Rational& other) const noexcept
{
    return ! (*this == other);
}

bool Rational::operator< (const Rational& other) const
{
    // Denominators are positive after normalization, so cross-multiplication
    // preserves the inequality direction.
    return checkedMul (num, other.den) < checkedMul (other.num, den);
}

bool Rational::operator<= (const Rational& other) const { return ! (other < *this); }
bool Rational::operator>  (const Rational& other) const { return other < *this; }
bool Rational::operator>= (const Rational& other) const { return ! (*this < other); }

std::int64_t Rational::floor() const noexcept
{
    // den is always positive after normalization, so the remainder carries the
    // sign of the numerator. Integer division truncates toward zero; when the
    // value is negative and not exact, that is one too high — round it down.
    const std::int64_t quotient  = num / den;
    const std::int64_t remainder = num % den;
    return (remainder < 0) ? quotient - 1 : quotient;
}

double Rational::toDouble() const noexcept
{
    return static_cast<double> (num) / static_cast<double> (den);
}

std::string Rational::toString() const
{
    return std::to_string (num) + '/' + std::to_string (den);
}

} // namespace sirius
