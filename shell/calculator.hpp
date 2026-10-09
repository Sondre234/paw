// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
#include <QVariantMap>
#include <optional>

// The calculator of the command palette and the start menu's search: arithmetic on what is typed,
// and conversions between units of one kind.
//
// An expression has numbers (`12`, `1.5`, `.5`, `1e3`, `0x1f`, `0b101`, `0o17`), the constants
// `pi` (or `π`), `tau` and `e`, the operators `+`, `-`, `*`, `/`, `%` or `mod` (the remainder),
// `^` or `**` (power, from the right), postfix `!` (factorial) and prefix `√`, parentheses,
// functions called with parentheses (sqrt, cbrt, abs, round, floor, ceil, trunc, sin, cos, tan,
// asin, acos, atan, sinh, cosh, tanh, log, log2, ln, exp, min, max; `round(x, digits)` and
// `log(x, base)` take a second argument), and multiplication without its sign before a constant,
// a function or a parenthesis (`2pi`, `3(1 + 2)`). `×`, `÷` and `−` stand for `*`, `/` and `-`.
// Angles are in radians.
//
// A conversion is an expression, a unit, `in`, `to`, `as` or `->`, and another unit of the same
// kind: `5 km in mi`, `100 f in c`, `3 GiB in MB`, `90 deg to rad`.
namespace calculator {
struct Answer {
    double value = 0;
    // The value as it is shown and copied: `14`, `0.3`, `1.41421356237`, `1.07150860719e301`.
    QString number;
    // The unit a conversion gives, as written in its table (`mi`, `°C`, `MB`); empty for none.
    QString unit;
    // The number and its unit, for a title.
    QString text() const { return unit.isEmpty() ? number : number + ' ' + unit; }
};

// The value of `text`, or nothing, with what is wrong in `error` when it is given.
std::optional<Answer> evaluate(const QString &text, QString *error = nullptr);

// What a search shows for `query`: its value when it is a calculation rather than words to search
// for, that is when it uses an operator, a function, a conversion or a number in another base,
// not when it is only a number or a constant (`42`, `e`, which start other searches). A leading
// `=` asks for the value of anything, `=pi` too.
std::optional<Answer> answer(const QString &query);

// `value` as the calculator shows it: an integer whole (below 2^53), else 12 significant digits
// without trailing zeros, in scientific notation (`1.5e-10`, `6.02214076e23`) when very small or
// large.
QString format(double value);

// The search result for `query`'s answer, an empty map when there is none: {kind: "calc", title
// (the number and its unit), subtitle, icon, target (the number, which choosing it copies)}.
QVariantMap entry(const QString &query);
} // namespace calculator
