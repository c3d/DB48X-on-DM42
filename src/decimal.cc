// ****************************************************************************
//  decimal.cc                                                    DB48X project
// ****************************************************************************
//
//   File Description:
//
//     Variable-precision decimal implementation
//
//     This is intended to save some code space when running on DM42,
//     while improving the avaiable precision.
//     The bid128 implementation takes 59.7% of the PGM space and 79.7%
//     of the entire ELF file size. We can probably do better.
//
//
// ****************************************************************************
//   (C) 2023 Christophe de Dinechin <christophe@dinechin.org>
//   This software is licensed under the terms outlined in LICENSE.txt
// ****************************************************************************
//   This file is part of DB48X.
//
//   DB48X is free software: you can redistribute it and/or modify
//   it under the terms outlined in the LICENSE.txt file
//
//   DB48X is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
// ****************************************************************************

#include "decimal.h"

#include "arithmetic.h"
#include "bignum.h"
#include "fraction.h"
#include "parser.h"
#include "renderer.h"
#include "runtime.h"
#include "settings.h"
#include "utf8.h"



RECORDER(decimal, 32, "Variable-precision decimal data type");


SIZE_BODY(decimal)
// ----------------------------------------------------------------------------
//   Compute the size of a decimal number
// ----------------------------------------------------------------------------
{
    byte_p p       = o->payload();
    int    exp     = leb128<int>(p); (void) exp;
    size_t nkigits = leb128<size_t>(p);
    p += (nkigits * 10 + 7) / 8;
    return ptrdiff(p, o);
}


HELP_BODY(decimal)
// ----------------------------------------------------------------------------
//   Help topic for decimal numbers
// ----------------------------------------------------------------------------
{
    return utf8("Decimal numbers");
}


PARSE_BODY(decimal)
// ----------------------------------------------------------------------------
//    Try to parse this as an decimal
// ----------------------------------------------------------------------------
//    Note that this does not try to parse named constants like "inf" or "NaN"
{
    record(decimal, "Parsing [%s]", (utf8) p.source);

    gcutf8   source = p.source;
    gcutf8   s      = source;
    gcutf8   last   = source + p.length;
    id       type   = ID_decimal;
    scribble scr;

    // Skip leading sign
    if (*s == '+' || *s == '-')
    {
        // In an equation, `1 + 3` should interpret `+` as an infix
        if (p.precedence < 0)
            return SKIP;
        if (*s == '-')
            type = ID_neg_decimal;
        ++s;
    }

    // Scan digits and decimal dot
    kint   kigit      = 0;
    uint   kigc       = 0;
    int    exponent   = 0;
    int    decimalDot = -1;
    size_t digits     = 0;
    bool   zeroes     = true;
    while (+s < +last)
    {
        if (*s >= '0' && *s <= '9')
        {
            digits++;
            if (!zeroes || *s != '0')
            {
                if (decimalDot < 0)
                    exponent++;
                kigit = kigit * 10 + (*s - '0');
                if (++kigc == 3)
                {
                    kint *kigp = (kint *) rt.allocate(sizeof(kint));
                    if (!kigp)
                        return ERROR;
                    *kigp = kigit;
                    kigc = 0;
                    kigit = 0;
                }
                zeroes = false;
            }
            else if (decimalDot >= 0)
            {
                exponent--;
            }
        }
        else if (decimalDot < 0 && (*s == '.' || *s == ','))
        {
            decimalDot = +s - +source;
        }
        else
        {
            break;
        }
        ++s;
    }
    if (!digits)
        return SKIP;

    if (kigc)
    {
        while (kigc++ < 3)
            kigit *= 10;
        kint *kigp = (kint *) rt.allocate(sizeof(kint));
        if (!kigp)
            return ERROR;
        *kigp = kigit;
        kigc = 0;
        kigit = 0;
    }

    // Check how many digits were given
    const size_t maxdigits = Settings.Precision();
    record(decimal, "Had %u digits, max %u", digits, maxdigits);
    if (Settings.TooManyDigitsErrors() && digits > maxdigits)
    {
        rt.mantissa_error().source(source, digits + (decimalDot >= 0));
        return ERROR;
    }

    // Check if we were given an exponent
    utf8 expsrc = nullptr;
    if (*s == 'e' || *s == 'E' ||
        utf8_codepoint(s) == Settings.ExponentSeparator())
    {
        s = utf8_next(s);
        expsrc = s;
        if (*s == '+' || *s == '-')
            ++s;
        utf8 expstart = s;
        while (+s < +last && (*s >= '0' && *s <= '9'))
            ++s;
        if (s == expstart)
        {
            rt.exponent_error().source(s);
            return ERROR;
        }

        int expval =  atoi(cstring(expsrc));
        exponent += expval;
        record(decimal, "Exponent value is %d for %d", expval, exponent);
    }

    // Success: build the resulting number
    gcp<kint> kigits     = (const kint *) scr.scratch();
    size_t    nkigs      = scr.growth() / sizeof(kint);
    p.end = +s - +source;
    p.out = rt.make<decimal>(type, exponent, nkigs, kigits);

    return p.out ? OK : ERROR;
}


RENDER_BODY(decimal)
// ----------------------------------------------------------------------------
//   Render the decimal number into the given renderer
// ----------------------------------------------------------------------------
{
    // Read information about the number
    info      sh       = o->shape();
    int       exponent = sh.exponent;
    size_t    nkigits  = sh.nkigits;
    gcbytes   base     = sh.base;
    decimal_g d        = o;
    bool      negative = o->type() == ID_neg_decimal;

    // Read formatting information from the renderer
    bool      editing  = !r.stack();
    bool      raw      = r.file_save();
    size_t    rsize    = r.size();

    // Read settings
    settings &ds       = Settings;
    id        mode     = editing ? object::ID_Std : ds.DisplayMode();
    int       digits   = editing ? 3 * nkigits : ds.DisplayDigits();
    int       std_exp  = ds.StandardExponent();
    bool      showdec  = ds.TrailingDecimal();
    unicode   space    = ds.NumberSeparator();
    uint      mant_spc = ds.MantissaSpacing();
    uint      frac_spc = ds.FractionSpacing();
    bool      fancy    = !editing && ds.FancyExponent();
    char      decimal  = ds.DecimalSeparator(); // Can be '.' or ','

    if (raw)
    {
        mode = object::ID_Std;
        digits = BID128_MAXDIGITS;
        std_exp = 9;
        showdec = true;
        space = 0;
        mant_spc = 0;
        frac_spc = 0;
        fancy = false;
        decimal = '.';
    }
    if (mode == object::ID_Std)
        mode = object::ID_Sig;

    static uint16_t fancy_digit[10] =
    {
        L'⁰', L'¹', L'²', L'³', L'⁴',
        L'⁵', L'⁶', L'⁷', L'⁸', L'⁹'
    };

    // Emit sign if necessary
    if (negative)
    {
        r.put('-');
        rsize++;
    }

    // Loop checking for overflow
    bool   overflow = false;
    do
    {
        // Position where we will emit the decimal dot when there is an exponent
        int decpos = 1;

        // Mantissa is between 0 and 1
        int    realexp  = exponent - 1;
        int    mexp     = nkigits * 3;

        // Check if we need to switch to scientific notation in normal mode
        // On the negative exponents, we switch when digits would be lost on
        // display compared to actual digits. This is consistent with how HP
        // calculators do it. e.g 1.234556789 when divided by 10 repeatedly
        // switches to scientific notation at 1.23456789E-5, but 1.23 at
        // 1.23E-11 and 1.2 at 1.2E-12 (on an HP50G with 12 digits).
        // This is not symmetrical. Positive exponents switch at 1E12.
        // Note that the behaviour here is purposely different than HP's
        // when in FIX mode. In FIX 5, for example, 1.2345678E-5 is shown
        // on HP50s as 0.00001, and is shown here as 1.23457E-5, which I believe
        // is more useful. This behaviour is enabled by setting min_fix_digits
        // to a non-zero value. If the value is zero, FIX works like on HP.
        // Also, since DB48X can compute on 34 digits, and counting zeroes
        // can be annoying, there is a separate setting for when to switch
        // to scientific notation.
        bool hasexp = mode == object::ID_Sci || mode == object::ID_Eng;
        if (!hasexp)
        {
            if (realexp < 0)
            {
                if (mode <= object::ID_Fix)
                {
                    // Need to round up if last digit is above 5
                    bool roundup = nkigits &&
                        (kigit(+base, nkigits-1) % 10) >= 5;
                    int shown = digits + realexp + roundup;
                    int minfix = ds.MinimumSignificantDigits();
                    if (minfix < 0)
                    {
                        if (shown < 0)
                            realexp = -digits;
                    }
                    else
                    {
                        if (minfix > mexp + 1)
                            minfix = mexp + 1;
                        hasexp = shown < minfix;
                    }
                }
                else
                {
                    int minexp = digits < std_exp ? digits : std_exp;
                    hasexp = mexp - realexp - 1 >= minexp;
                }
            }
            else
            {
                hasexp = realexp >= std_exp;
                if (!hasexp)
                    decpos = realexp + 1;
            }
        }

        // Position where we emit spacing (at sep == 0)
        //     10_000_000 with mant_spc = 3
        // sep=10-210-210
        uint sep = mant_spc ? (decpos - 1) % mant_spc : ~0U;

        // Number of decimals to show is given number of digits for most modes
        // (This counts *all* digits for standard / SIG mode)
        int decimals = digits;

        // Write leading zeroes if necessary
        if (!hasexp && realexp < 0)
        {
            // HP RPL calculators don't show leading 0, i.e. 0.5 shows as .5,
            // but this is only in STD mode, not in other modes.
            // This is pure evil and inconsistent with all older HP calculators
            // (which, granted, did not have STD mode) and later ones (Prime)
            // So let's decide that 0.3 will show as 0.3 in STD mode and not .3
            r.put('0');
            decpos--;               // Don't emit the decimal separator twice

            // Emit decimal dot and leading zeros on fractional part
            r.put(decimal);
            sep = frac_spc-1;
            for (int zeroes = realexp + 1; zeroes < 0; zeroes++)
            {
                r.put('0');
                if (sep-- == 0)
                {
                    r.put(space);
                    sep = frac_spc - 1;
                }
                decimals--;
            }
        }

        // Adjust exponent being displayed for engineering mode
        int dispexp = realexp;
        bool engmode = mode == object::ID_Eng;
        if (engmode)
        {
            int offset = dispexp >= 0 ? dispexp % 3 : (dispexp - 2) % 3 + 2;
            decpos += offset;
            dispexp -= offset;
            if (mant_spc)
                sep = (sep + offset) % mant_spc;
            decimals += 1;
        }

        // Copy significant digits, inserting decimal separator when needed
        bool   sigmode = mode == object::ID_Sig;
        size_t lastnz  = r.size();
        size_t midx    = 0;
        uint   decade  = 0;
        kint   md      = 0;
        kint   d       = 0;
        while (midx < nkigits || decade)
        {
            // Find next digit and emit it
            if (decade == 0)
            {
                if (overflow)
                {
                    md = 1;
                    decade = 1;
                    midx = nkigits;
                }
                else
                {
                    md = kigit(+base, midx++);
                    decade = 3;
                }
            }
            decade--;

            d =  decade == 2 ? md / 100 : (decade == 1 ? (md / 10) : md) % 10;
            if (decimals <= 0)
                break;

            r.put(char('0' + d));
            decpos--;

            // Check if we will need to eliminate trailing zeros
            if (decpos >= 0 || d)
                lastnz = r.size();

            // Insert spacing on the left of the decimal mark
            bool more = (midx < nkigits || decade) || !sigmode || decpos > 0;
            if (sep-- == 0 && more && decimals > 1)
            {
                if (decpos)
                {
                    r.put(space);
                    if (decpos > 0)
                        lastnz = r.size();
                }
                sep = (decpos > 0 ? mant_spc : frac_spc) - 1;
            }

            if (decpos == 0 && (more || showdec))
            {
                r.put(decimal);
                lastnz = r.size();
                sep = frac_spc - 1;
            }

            // Count decimals after decimal separator, except in SIG mode
            // where we count all significant digits being displayed
            if (decpos < 0 || sigmode || engmode)
                decimals--;
        }

        // Check if we need some rounding on what is being displayed
        if ((midx < nkigits || decade) && d >= 5)
        {
            char *rptr = (char *) rt.scratchpad();
            char *start = rptr - (r.size() - rsize);
            bool rounding = true;
            bool stripzeros = mode == object::ID_Sig;
            while (rounding && --rptr >= start)
            {
                if (*rptr >= '0' && *rptr <= '9')   // Do not convert '.' or '-'
                {
                    *rptr += 1;
                    rounding = *rptr > '9';
                    if (rounding)
                    {
                        *rptr -= 10;
                        if (stripzeros && *rptr == '0' && rptr > start)
                        {
                            r.unwrite(1);
                            decimals++;
                            decpos++;
                            uint spc = decpos > 0 ? mant_spc : frac_spc;
                            sep = (sep + 1) % spc;
                        }
                        else
                        {
                            stripzeros = false;
                        }
                    }
                }
                else if (*rptr == decimal)
                {
                    stripzeros = false;
                }
                else if (stripzeros) // Inserted separator
                {
                    r.unwrite(1);
                    sep = 0;
                }
            }

            // If we ran past the first digit, we overflowed during rounding
            // Need to re-run with the next larger exponent
            // This can only occur with a conversion of 9.9999 to 1
            if (rounding)
            {
                overflow = true;
                exponent++;
                r.reset_to(rsize);
                continue;
            }

            // Check if we need to reinsert the last separator
            if (sep-- == 0 && decimals > 1)
            {
                r.put(space);
                sep = (decpos > 0 ? mant_spc : frac_spc) - 1;
            }
        }

        // Return to position of last inserted zero
        else if (mode == object::ID_Sig && r.size() > lastnz)
        {
            r.reset_to(lastnz);
        }


        // Do not add trailing zeroes in standard mode
        if (sigmode)
        {
            decimals = decpos > 0 ? decpos : 0;
        }
        else if (mode == object::ID_Fix && decpos > 0)
        {
            decimals = digits + decpos;
        }

        // Add trailing zeroes if necessary
        while (decimals > 0)
        {
            r.put('0');
            decpos--;

            if (sep-- == 0 && decimals > 1)
            {
                if (decpos)
                    r.put(space);
                sep = (decpos > 0 ? mant_spc : frac_spc) - 1;
            }

            if (decpos == 0 && showdec)
                r.put(decimal);
            decimals--;
        }

        // Add exponent if necessary
        if (hasexp)
        {
            r.put(ds.ExponentSeparator());
            if (fancy)
            {
                char expbuf[8];
                size_t written = snprintf(expbuf, 8, "%d", dispexp);
                for (uint e = 0; e < written; e++)
                {
                    char c = expbuf[e];
                    unicode u = c == '-' ? L'⁻' : fancy_digit[c - '0'];
                    r.put(u);
                }
            }
            else
            {
                r.printf("%d", dispexp);
            }
        }
        return r.size();
    } while (overflow);
    return 0;
}



// ============================================================================
//
//   Conversions
//
// ============================================================================

ularge decimal::as_unsigned(bool magnitude) const
// ----------------------------------------------------------------------------
//   Convert a decimal value to an unsigned value
// ----------------------------------------------------------------------------
//   When magnitude is set, we return magnitude for negative values
{
    info   s       = shape();
    int    exp     = s.exponent;
    size_t nkigits = s.nkigits;
    byte_p bp      = s.base;
    if (exp < 0 || (!magnitude && type() == ID_neg_decimal))
        return 0;

    uint xp = exp;
    ularge pow = 1;
    ularge mul = 10;
    while (xp)
    {
        if (xp & 1)
            pow *= mul;
        mul = mul * mul;
        xp /= 2;
    }
    if (!pow)
        return ~0ULL;

    ularge result = 0;
    for (size_t m = 0; m < nkigits && pow; m++)
    {
        kint d = kigit(bp, m);
        ularge next = result + d * pow / 1000;
        if (next < result)
            return ~0ULL;
        result = next;
        pow /= 1000;
    }
    return result;
}


large decimal::as_integer() const
// ----------------------------------------------------------------------------
//   Convert a decimal value to an integer
// ----------------------------------------------------------------------------
{
    large result = (large) as_unsigned(true);
    if (type() == ID_neg_decimal)
        result = -result;
    return result;
}


decimal::class_type decimal::fpclass() const
// ----------------------------------------------------------------------------
//   Return the floating-point class for the decimal number
// ----------------------------------------------------------------------------
{
    info   s       = shape();
    size_t nkigits = s.nkigits;
    byte_p bp      = s.base;
    bool   neg     = type() == ID_neg_decimal;
    if (nkigits == 0)
        return neg ? negativeZero : positiveZero;
    kint d = kigit(bp, 0);
    if (d >= 1000)
    {
        if (d == infinity)
            return neg ? negativeInfinity : positiveInfinity;
    }
    if (d < 100)
        return neg ? negativeSubnormal : positiveSubnormal;
    return neg ? negativeNormal : positiveNormal;
}


bool decimal::is_zero() const
// ----------------------------------------------------------------------------
//   The normal zero has no digits
// ----------------------------------------------------------------------------
{
    return shape().nkigits == 0;
}


bool decimal::is_one() const
// ----------------------------------------------------------------------------
//   Normal representation for one
// ----------------------------------------------------------------------------
{
    if (type() == ID_neg_decimal)
        return false;
    info   s       = shape();
    int    exp     = s.exponent;
    size_t nkigits = s.nkigits;
    byte_p bp      = s.base;
    return exp == 1 && nkigits == 1 && kigit(bp, 0) == 100;
}


bool decimal::is_negative() const
// ----------------------------------------------------------------------------
//   Return true if the value is strictly negative
// ----------------------------------------------------------------------------
{
    if (type() == ID_decimal)
        return false;
    return shape().nkigits != 0;
}


bool decimal::is_negative_or_zero() const
// ----------------------------------------------------------------------------
//   Return true if the value is zero o rnegative
// ------------------------------------------------------------------------
{
    if (type() == ID_neg_decimal)
        return true;
    return shape().nkigits == 0;
}



decimal_g decimal::round_to_zero(int to_exp) const
// ----------------------------------------------------------------------------
//   Round a given decimal number to zero
// ----------------------------------------------------------------------------
{
    info     s       = shape();
    int      exp     = s.exponent;
    size_t   nkigits = s.nkigits;
    gcbytes  bp      = s.base;
    id       ty      = type();
    scribble scr;

    if (exp < to_exp)
        return rt.make<decimal>(0);
    size_t zeroed = (exp - to_exp) / 3;
    if (zeroed > nkigits)
        return rt.make<decimal>(0);
    nkigits -= zeroed;
    for (size_t i = 0; i < nkigits; i++)
    {
        kint k = kigit(+bp, i);
        if (i == nkigits - 1)
        {
            size_t rm = (exp - to_exp) % 3;
            if (rm == 0)
                k = 0;
            else if (rm == 1)
                k -= k % 100;
            else if (rm == 2)
                k -= k % 10;
        }
        kint *kp = (kint *) rt.allocate(sizeof(kint));
        if (!kp)
            return nullptr;
        *kp = k;
    }
    gcp<kint> buf = (kint *) scr.scratch();
    return rt.make<decimal>(ty, exp, nkigits, buf);
}


algebraic_p decimal::to_fraction(uint count, uint decimals) const
// ----------------------------------------------------------------------------
//   Convert a decimal value to a fraction
// ----------------------------------------------------------------------------
{
    decimal_g num = this;
    decimal_g next, whole_part, decimal_part, one;
    decimal_g v1num, v1den, v2num, v2den, s;
    bool      neg = num->is_negative();
    if (neg)
        num = -num;
    whole_part = num->round_to_zero();
    decimal_part = num - whole_part;
    one = rt.make<decimal>(1);
    v1num = whole_part;
    v1den = one;
    v2num = one;
    v2den = rt.make<decimal>(0);

    uint maxdec = Settings.Precision() - 3;
    if (decimals > maxdec)
        decimals = maxdec;

    while (count--)
    {
        // Check if the decimal part is small enough
        if (decimal_part->is_zero())
            break;
        int exp = decimal_part->exponent();
        if (-exp > int(decimals))
            break;

        next = one / decimal_part;
        whole_part = next->round_to_zero();

        s = v1num;
        v1num = whole_part * v1num + v2num;
        v2num = s;

        s = v1den;
        v1den = whole_part * v1den + v2den;
        v2den = s;

        decimal_part = next - whole_part;
    }

    ularge      numerator   = v1num->as_unsigned();
    ularge      denominator = v1den->as_unsigned();
    algebraic_g result;
    if (denominator == 1)
        result = +integer::make(numerator);
    else
        result = +fraction::make(integer::make(numerator),
                                 integer::make(denominator));
    if (neg)
        result = -result;
    return +result;
}


int decimal::compare(decimal_r x, decimal_r y)
// ----------------------------------------------------------------------------
//   Return -1, 0 or 1 for comparison
// ----------------------------------------------------------------------------
{
    // Quick exit if identical pointers
    if (+x == +y)
        return 0;

    // Check if input is nullptr - If so, nullptr is smaller than value
    if (!x || !y)
        return !!x - !!y;

    id   xty = x->type();
    id   yty = y->type();

    // Check negative vs. positive
    if (xty != yty)
        return (xty == ID_decimal) - (yty == ID_decimal);

    // Read information from both numbers
    int  sign = xty == ID_neg_decimal ? -1 : 1;
    info xi   = x->shape();
    info yi   = y->shape();

    // Number with largest exponent is larger
    int  xe   = xi.exponent;
    int  ye   = yi.exponent;
    if (xe != ye)
        return sign * (xe - ye);

    // If same exponent, compare mantissa digits starting with highest one
    size_t xs = xi.nkigits;
    size_t ys = yi.nkigits;
    byte_p xb = xi.base;
    byte_p yb = yi.base;
    size_t s  = std::min(xs, ys);
    for (size_t i = 0; i < s; i++)
        if (int diff = kigit(xb, i) - kigit(yb, i))
            return sign * diff;

    // If all kigits were the same, longest number is larger
    if (xs != ys)
        return sign * int(xs - ys);

    // Otherwise, numbers are identical
    return 0;
}



// ============================================================================
//
//    Basic arithmetic operations
//
// ============================================================================

static inline object::id negtype(object::id type)
// ----------------------------------------------------------------------------
//   Return the opposite type
// ----------------------------------------------------------------------------
{
    return type == object::ID_decimal ? object::ID_neg_decimal
                                      : object::ID_decimal;
}


decimal_p decimal::neg(decimal_r x)
// ----------------------------------------------------------------------------
//   Negation
// ----------------------------------------------------------------------------
{
    object::id type  = x->type();
    object::id ntype = negtype(type);
    gcbytes data = x->payload();
    size_t len = x->size() - leb128size(type);
    return rt.make<decimal>(ntype, len, data);
}


decimal_p decimal::add(decimal_r x, decimal_r y)
// ----------------------------------------------------------------------------
//   Addition of two numbers with the same sign
// ----------------------------------------------------------------------------
{
    // Read information from both numbers
    info xi = x->shape();
    info yi = y->shape();
    int  xe = xi.exponent;
    int  ye = yi.exponent;
    id   ty = x->type();

    // Put the smallest exponent in y
    bool lt = xe < ye;
    if (lt)
    {
        std::swap(xe, ye);
        std::swap(xi, yi);
    }

    // Check dimensions
    size_t   xs     = xi.nkigits;
    size_t   ys     = yi.nkigits;
    gcbytes  xb     = xi.base;
    gcbytes  yb     = yi.base;
    size_t   yshift = xe - ye;
    size_t   kshift = yshift / 3;
    kint     mod3   = yshift % 3;

    // Size of result - y can be wider than x
    size_t   ps     = (Settings.Precision() + 2) / 3;
    size_t   rs     = std::min(ps, std::max(xs, ys + (yshift + 2) / 3));

    // Check if y is negligible relative to x
    if (rs < kshift)
        return lt ? y : x;

    // Allocate the mantissa
    scribble scr;
    kint    *rb = (kint *) rt.allocate(rs * sizeof(kint));
    if (!rb)
        return nullptr;

    // Addition loop
    kint   hmul  = mod3 == 2 ? 100 : mod3 == 1 ? 10 : 1;
    kint   lmul  = 1000 / hmul;
    kint   carry = 0;
    size_t ko    = rs;
    while (ko-- > 0)
    {
        kint xk = ko < xs ? kigit(+xb, ko) : 0;
        if (ko >= kshift)
        {
            size_t yo = ko - kshift;
            kint   yk = yo < ys ? kigit(+yb, yo) : 0;
            xk += yk / hmul;
            if (mod3 && ko > kshift && yo - 1 < ys)
            {
                yo--;
                yk = kigit(+yb, yo);
                xk += (yk % hmul) * lmul;
            }
        }
        xk += carry;
        rb[ko] = xk % 1000;
        carry = xk / 1000;
    }

    // Check if a carry remains above top
    if (carry)
    {
        uint expincr = 1;
        hmul = 10;
        while (carry >= hmul)
        {
            hmul *= 10;
            expincr++;
        }
        xe += expincr;

        ko = rs;
        lmul = 1000 / hmul;
        while (ko --> 0)
        {
            kint above = ko ? rb[ko-1] : carry;
            rb[ko] = rb[ko] / hmul + (above % hmul) * lmul;
        }
    }

    // Strip trailing zeroes
    while (rs && rb[rs-1] == 0)
        rs--;

    // Build the result
    gcp<kint> kigits = rb;
    decimal_p result = rt.make<decimal>(ty, xe, rs, kigits);
    return result;
}


decimal_p decimal::sub(decimal_r x, decimal_r y)
// ----------------------------------------------------------------------------
//   Subtraction of two numbers with the same sign
// ----------------------------------------------------------------------------
{
    // Read information from both numbers
    info xi  = x->shape();
    info yi  = y->shape();
    int  xe  = xi.exponent;
    int  ye  = yi.exponent;
    id   ty  = x->type();
    bool lt  = xe < ye;

    // Put the smallest exponent in y
    if (lt)
    {
        std::swap(xe, ye);
        std::swap(xi, yi);
    }

    // Check dimensions
    size_t   xs     = xi.nkigits;
    size_t   ys     = yi.nkigits;
    gcbytes  xb     = xi.base;
    gcbytes  yb     = yi.base;
    size_t   yshift = xe - ye;
    size_t   kshift = yshift / 3;
    kint     mod3   = yshift % 3;

    // Size of result - y can be wider than x
    size_t   ps     = (Settings.Precision() + 2) / 3;
    size_t   rs     = std::min(ps, std::max(xs, ys + (yshift + 2) / 3));

    // Check if y is negligible relative to x
    if (rs < kshift)
        return lt ? neg(y) : decimal_p(x);

    // Allocate the mantissa
    scribble scr;
    kint    *rb = (kint *) rt.allocate(rs * sizeof(kint));
    if (!rb)
        return nullptr;

    // Subtraction loop
    kint   hmul  = mod3 == 2 ? 100 : mod3 == 1 ? 10 : 1;
    kint   lmul  = 1000 / hmul;
    kint   carry = 0;
    size_t ko    = rs;
    while (ko-- > 0)
    {
        kint xk = ko < xs ? kigit(+xb, ko) : 0;
        kint yk = carry;
        if (ko >= kshift)
        {
            size_t yo = ko - kshift;
            if (yo < ys)
            {
                yk += kigit(+yb, yo) / hmul;
                if (mod3 && ko > kshift && --yo < ys)
                    yk += kigit(+yb, yo) % hmul * lmul;
            }
        }
        carry = xk < yk;
        if (carry)
            xk += 1000;
        xk = xk - yk;
        rb[ko] = xk;
    }

    // Check if a carry remains above top, e.g. 0.5 - 0.6 = -0.1
    if (carry)
    {
        ko = rs;
        uint rev = 1000;
        while (ko --> 0)
        {
            rb[ko] = rev - rb[ko];
            rev = 999;
        }
        lt = !lt;
    }

    // Strip leading zeroes three by three
    while (rs && *rb == 0)
    {
        xe -= 3;
        rb++;
        rs--;
    }

    // Strip up to two individual leading zeroes
    if (rs && *rb < 100)
    {
        xe -= 1 + (*rb < 10);
        hmul = *rb < 10 ? 100 : 10;
        lmul = 1000 / hmul;
        for (ko = 0; ko < rs; ko++)
        {
            kint next = ko + 1 < rs ? rb[ko + 1] : 0;
            rb[ko] = (rb[ko] * hmul + next / lmul) % 1000;
        }
        xe -= 1 + *rb < 10;
    }
    if (!rs)
        xe = 0;


    // Check if we need to change the sign
    if (lt)
        ty = negtype(ty);

    // Strip trailing zeroes
    while (rs && rb[rs-1] == 0)
        rs--;

    // Build the result
    gcp<kint> kigits = rb;
    decimal_p result = rt.make<decimal>(ty, xe, rs, kigits);
    return result;
}


decimal_p decimal::mul(decimal_r x, decimal_r y)
// ----------------------------------------------------------------------------
//   Multiplication of two decimal numbers
// ----------------------------------------------------------------------------
//  (a0+a1/1000) * (b0+b1/1000) = a0*b0 + (a0*b1+a1*b0) / 1000 + epsilon
//  Exponent is the sum of the two exponents
{
    // Read information from both numbers
    info     xi  = x->shape();
    info     yi  = y->shape();
    int      xe  = xi.exponent;
    int      ye  = yi.exponent;
    id       xty = x->type();
    id       yty = y->type();
    id       ty  = xty == yty ? ID_decimal : ID_neg_decimal;

    // Check dimensions
    size_t   xs  = xi.nkigits;
    size_t   ys  = yi.nkigits;
    gcbytes  xb  = xi.base;
    gcbytes  yb  = yi.base;
    int      re  = xe + ye - 3;

    // Size of result
    size_t   ps  = (Settings.Precision() + 2) / 3;
    size_t   rs  = std::min(ps, xs + ys + 1);

    // Allocate the mantissa
    scribble scr;
    kint    *rb = (kint *) rt.allocate(rs * sizeof(kint));
    if (!rb)
        return nullptr;

    // Zero the result before doing sums on it
    for (size_t ri = 0; ri < rs; ri++)
        rb[ri] = 0;

    // Sum on all digits
    uint carry = 0;
    for (size_t xi = 0; xi < xs; xi++)
    {
        kint xk = kigit(xb, xi);
        for (size_t yi = 0; yi < ys; yi++)
        {
            size_t ri = xi + yi;
            if (ri >= rs)
                break;
            kint yk = kigit(yb, yi);
            uint rk = xk * yk;
            while (rk)
            {
                rk += rb[ri];
                rb[ri] = rk % 1000;
                rk /= 1000;
                if (ri-- == 0)
                    break;
            }
            carry += rk;
        }
    }

    // Check if a carry remains above top
    while (carry)
    {
        // Round things up
        size_t ri = rs - 1;
        bool overflow = rb[ri] >= 500;
        while (overflow && ri --> 0)
        {
            overflow = ++rb[ri] >= 1000;
            if (overflow)
                rb[ri] %= 1000;
        }
        if (overflow)
            carry++;

        memmove(rb + 1, rb, sizeof(kint) * (rs - 1));
        *rb = carry % 1000;
        re += 3;
        carry = carry / 1000;
    }

    // Strip leading zeroes three by three
    while (rs && *rb == 0)
    {
        re -= 3;
        rb++;
        rs--;
    }

    // Strip up to two individual leading zeroes
    if (rs && *rb < 100)
    {
        re -= 1 + (*rb < 10);
        uint hmul = *rb < 10 ? 100 : 10;
        uint lmul = 1000 / hmul;
        for (size_t ko = 0; ko < rs; ko++)
        {
            kint next = ko + 1 < rs ? rb[ko + 1] : 0;
            rb[ko] = (rb[ko] * hmul + next / lmul) % 1000;
        }
    }

    // Strip trailing zeroes
    while (rs && rb[rs-1] == 0)
        rs--;

    // Build the result
    gcp<kint> kigits = rb;
    decimal_p result = rt.make<decimal>(ty, re, rs, kigits);
    return result;
}
