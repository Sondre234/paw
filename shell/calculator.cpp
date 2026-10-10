// SPDX-License-Identifier: GPL-3.0-or-later
#include "calculator.hpp"
#include <QList>
#include <QStringList>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace {
// What is wrong with an expression; evaluate() catches it.
struct Failure {
    QString message;
};
[[noreturn]] void fail(const QString &message) { throw Failure{message}; }

struct Token {
    enum Kind { Number, Name, Symbol, End } kind = End;
    double number = 0;
    // A name as written, or a symbol in its plain form (`*` for `×`, `^` for `**`, `>` for
    // `->` and `→`).
    QString text = {};
    // A number written in another base than ten (0x1f, 0b101, 0o17).
    bool based = false;
};

bool startsName(QChar c) { return c.isLetter() || c == u'°' || c == u'_'; }
bool continuesName(QChar c) { return c.isLetterOrNumber() || c == u'°' || c == u'_'; }

QList<Token> tokenize(const QString &text) {
    QList<Token> tokens;
    const qsizetype n = text.size();
    qsizetype i = 0;
    auto digitAt = [&](qsizetype at) { return at < n && text[at] >= u'0' && text[at] <= u'9'; };
    while (i < n) {
        const QChar c = text[i];
        if (c.isSpace()) {
            ++i;
            continue;
        }
        if (digitAt(i) || (c == u'.' && digitAt(i + 1))) {
            Token token{Token::Number};
            // 0x1f, 0b101 and 0o17, when digits of the base follow.
            if (c == u'0' && i + 2 < n) {
                const QChar prefix = text[i + 1].toLower();
                const int base = prefix == u'x' ? 16 : prefix == u'b' ? 2 : prefix == u'o' ? 8 : 0;
                auto valid = [&](QChar digit) {
                    const int value = digit.isDigit() ? digit.digitValue()
                                      : digit.toLower() >= u'a' && digit.toLower() <= u'f'
                                          ? digit.toLower().unicode() - u'a' + 10
                                          : 99;
                    return digit.unicode() < 128 && value < base;
                };
                if (base && valid(text[i + 2])) {
                    qsizetype end = i + 2;
                    double value = 0;
                    while (end < n && valid(text[end])) {
                        const QChar digit = text[end].toLower();
                        value = value * base + (digit.isDigit() ? digit.digitValue()
                                                                : digit.unicode() - u'a' + 10);
                        ++end;
                    }
                    if (end < n && continuesName(text[end]))
                        fail("Unexpected “" + text.mid(end, 1) + "”");
                    token.number = value;
                    token.based = true;
                    token.text = text.mid(i, end - i);
                    tokens.push_back(token);
                    i = end;
                    continue;
                }
            }
            qsizetype end = i;
            while (digitAt(end))
                ++end;
            if (end < n && text[end] == u'.') {
                ++end;
                while (digitAt(end))
                    ++end;
            }
            // An exponent only when digits follow: `2e` is two times e.
            if (end < n && (text[end] == u'e' || text[end] == u'E')) {
                qsizetype at = end + 1;
                if (at < n && (text[at] == u'+' || text[at] == u'-'))
                    ++at;
                if (digitAt(at)) {
                    end = at;
                    while (digitAt(end))
                        ++end;
                }
            }
            token.text = text.mid(i, end - i);
            bool ok = false;
            token.number = token.text.toDouble(&ok);
            if (!ok)
                fail("Unexpected “" + token.text + "”");
            tokens.push_back(token);
            i = end;
            continue;
        }
        if (startsName(c)) {
            qsizetype end = i + 1;
            while (end < n && continuesName(text[end]))
                ++end;
            tokens.push_back({Token::Name, 0, text.mid(i, end - i)});
            i = end;
            continue;
        }
        // Symbols, each in its plain form.
        QString symbol;
        qsizetype length = 1;
        if (text.mid(i, 2) == "**") {
            symbol = "^";
            length = 2;
        } else if (text.mid(i, 2) == "->") {
            symbol = ">";
            length = 2;
        } else if (c == u'×' || c == u'·' || c == u'⋅' || c == u'*') {
            symbol = "*";
        } else if (c == u'÷' || c == u'/') {
            symbol = "/";
        } else if (c == u'−' || c == u'-') {
            symbol = "-";
        } else if (c == u'→') {
            symbol = ">";
        } else if (QStringLiteral("+%^!(),√").contains(c)) {
            symbol = c;
        } else {
            fail("Unexpected “" + QString(c) + "”");
        }
        tokens.push_back({Token::Symbol, 0, symbol});
        i += length;
    }
    tokens.push_back({Token::End});
    return tokens;
}

// What a unit measures, and its name for saying that two cannot be converted.
enum class Kind { Length, Mass, Temperature, Data, Time, Volume, Speed, Angle, Area, Energy };
const char *kindName(Kind kind) {
    static constexpr const char *names[] = {"length", "mass",  "temperature", "data", "time",
                                            "volume", "speed", "angle",       "area", "energy"};
    return names[static_cast<int>(kind)];
}

// A unit: its names, the first the one shown, separated by spaces; what it measures; and how
// much of the kind's base unit (metre, kilogram, kelvin, byte, second, litre, metre per second,
// radian, square metre, joule) one is, after adding `offset` (temperatures).
struct Unit {
    const char *names;
    Kind kind;
    double factor;
    double offset = 0;
};
constexpr double pi = std::numbers::pi;
// Bytes before bits, so that `mb` without its capitals is megabytes.
constexpr Unit units[] = {
    {"m metre metres meter meters", Kind::Length, 1},
    {"km kilometre kilometres kilometer kilometers", Kind::Length, 1000},
    {"cm centimetre centimetres centimeter centimeters", Kind::Length, 0.01},
    {"mm millimetre millimetres millimeter millimeters", Kind::Length, 0.001},
    {"µm μm um micrometre micrometres micrometer micrometers", Kind::Length, 1e-6},
    {"nm nanometre nanometres nanometer nanometers", Kind::Length, 1e-9},
    {"in inch inches", Kind::Length, 0.0254},
    {"ft foot feet", Kind::Length, 0.3048},
    {"yd yard yards", Kind::Length, 0.9144},
    {"mi mile miles", Kind::Length, 1609.344},
    {"nmi", Kind::Length, 1852},
    {"kg kilogram kilograms kilo kilos", Kind::Mass, 1},
    {"g gram grams", Kind::Mass, 0.001},
    {"mg milligram milligrams", Kind::Mass, 1e-6},
    {"t tonne tonnes", Kind::Mass, 1000},
    {"lb lbs pound pounds", Kind::Mass, 0.45359237},
    {"oz ounce ounces", Kind::Mass, 0.028349523125},
    {"st stone stones", Kind::Mass, 6.35029318},
    {"°C C celsius degC", Kind::Temperature, 1, 273.15},
    {"°F F fahrenheit degF", Kind::Temperature, 5.0 / 9, 459.67},
    {"K kelvin kelvins", Kind::Temperature, 1},
    {"B byte bytes", Kind::Data, 1},
    {"kB KB kilobyte kilobytes", Kind::Data, 1e3},
    {"MB megabyte megabytes", Kind::Data, 1e6},
    {"GB gigabyte gigabytes", Kind::Data, 1e9},
    {"TB terabyte terabytes", Kind::Data, 1e12},
    {"PB petabyte petabytes", Kind::Data, 1e15},
    {"KiB kibibyte kibibytes", Kind::Data, 1024},
    {"MiB mebibyte mebibytes", Kind::Data, 1048576},
    {"GiB gibibyte gibibytes", Kind::Data, 1073741824},
    {"TiB tebibyte tebibytes", Kind::Data, 1099511627776},
    {"PiB pebibyte pebibytes", Kind::Data, 1125899906842624},
    {"bit bits", Kind::Data, 0.125},
    {"kbit Kb kilobit kilobits", Kind::Data, 125},
    {"Mbit Mb megabit megabits", Kind::Data, 125e3},
    {"Gbit Gb gigabit gigabits", Kind::Data, 125e6},
    {"Tbit Tb terabit terabits", Kind::Data, 125e9},
    {"s sec secs second seconds", Kind::Time, 1},
    {"ms millisecond milliseconds", Kind::Time, 0.001},
    {"min mins minute minutes", Kind::Time, 60},
    {"h hr hrs hour hours", Kind::Time, 3600},
    {"d day days", Kind::Time, 86400},
    {"wk week weeks", Kind::Time, 604800},
    {"yr year years", Kind::Time, 31557600},
    {"l L litre litres liter liters", Kind::Volume, 1},
    {"ml mL millilitre millilitres milliliter milliliters", Kind::Volume, 0.001},
    {"cl cL centilitre centilitres", Kind::Volume, 0.01},
    {"dl dL decilitre decilitres", Kind::Volume, 0.1},
    {"m³ m3", Kind::Volume, 1000},
    {"gal gallon gallons", Kind::Volume, 3.785411784},
    {"qt quart quarts", Kind::Volume, 0.946352946},
    {"pt pint pints", Kind::Volume, 0.473176473},
    {"cup cups", Kind::Volume, 0.2365882365},
    {"floz", Kind::Volume, 0.0295735295625},
    {"tbsp tablespoon tablespoons", Kind::Volume, 0.01478676478125},
    {"tsp teaspoon teaspoons", Kind::Volume, 0.00492892159375},
    {"km/h kmh kph", Kind::Speed, 1 / 3.6},
    {"m/s mps", Kind::Speed, 1},
    {"mph mi/h", Kind::Speed, 0.44704},
    {"kn knot knots kt", Kind::Speed, 1852.0 / 3600},
    {"ft/s fps", Kind::Speed, 0.3048},
    {"rad radian radians", Kind::Angle, 1},
    {"deg ° degree degrees", Kind::Angle, pi / 180},
    {"grad gon", Kind::Angle, pi / 200},
    {"turn turns", Kind::Angle, 2 * pi},
    {"m² m2", Kind::Area, 1},
    {"km² km2", Kind::Area, 1e6},
    {"cm² cm2", Kind::Area, 1e-4},
    {"ha hectare hectares", Kind::Area, 1e4},
    {"acre acres", Kind::Area, 4046.8564224},
    {"ft² ft2 sqft", Kind::Area, 0.09290304},
    {"mi² mi2", Kind::Area, 2589988.110336},
    {"J joule joules", Kind::Energy, 1},
    {"kJ kilojoule kilojoules", Kind::Energy, 1000},
    {"cal calorie calories", Kind::Energy, 4.184},
    {"kcal kilocalorie kilocalories", Kind::Energy, 4184},
    {"Wh", Kind::Energy, 3600},
    {"kWh", Kind::Energy, 3.6e6},
};

QStringList unitNames(const Unit &unit) {
    return QString::fromUtf8(unit.names).split(' ', Qt::SkipEmptyParts);
}

// The unit called `name`: as written, else the first whose name differs only in case.
const Unit *findUnit(const QString &name) {
    for (const auto &unit : units)
        if (unitNames(unit).contains(name))
            return &unit;
    for (const auto &unit : units)
        if (unitNames(unit).contains(name, Qt::CaseInsensitive))
            return &unit;
    return nullptr;
}

// The bases a whole number can be written in: `255 in hex`.
int baseNamed(const QString &name) {
    const auto folded = name.toLower();
    if (folded == "hex" || folded == "hexadecimal")
        return 16;
    if (folded == "bin" || folded == "binary")
        return 2;
    if (folded == "oct" || folded == "octal")
        return 8;
    if (folded == "dec" || folded == "decimal")
        return 10;
    return 0;
}

bool isConstant(const QString &name) {
    const auto folded = name.toLower();
    return folded == "pi" || folded == "π" || folded == "e" || folded == "tau" || folded == "τ";
}

struct Function {
    const char *name;
    int least, most; // how many arguments it takes; -1 for any number
};
constexpr Function functions[] = {
    {"sqrt", 1, 1}, {"cbrt", 1, 1},  {"abs", 1, 1},  {"round", 1, 2}, {"floor", 1, 1},
    {"ceil", 1, 1}, {"trunc", 1, 1}, {"sin", 1, 1},  {"cos", 1, 1},   {"tan", 1, 1},
    {"asin", 1, 1}, {"acos", 1, 1},  {"atan", 1, 1}, {"sinh", 1, 1},  {"cosh", 1, 1},
    {"tanh", 1, 1}, {"log", 1, 2},   {"log2", 1, 1}, {"ln", 1, 1},    {"exp", 1, 1},
    {"min", 1, -1}, {"max", 1, -1},
};
const Function *findFunction(const QString &name) {
    for (const auto &function : functions)
        if (name.compare(QLatin1String(function.name), Qt::CaseInsensitive) == 0)
            return &function;
    return nullptr;
}

// A finite value, or a failure saying why not.
double checked(double value) {
    if (std::isnan(value))
        fail("Undefined");
    if (std::isinf(value))
        fail("Too large");
    return value;
}

// The sine, cosine or tangent of `x`, where floating point leaves a crumb for an exact zero
// (sin(pi) is 1.2e-16): what is that close to nothing for the size of `x` is 0.
double nearZero(double value, double x) {
    return std::fabs(x) > 1e-6 && std::fabs(value) < 1e-15 * std::max(1.0, std::fabs(x)) ? 0
                                                                                         : value;
}

// The sum or difference `value` of `a` and `b`, 0 where they cancel but for rounding
// (0.1 + 0.2 - 0.3 is 5.6e-17).
double cancelled(double value, double a, double b) {
    return std::fabs(value) < 1e-15 * std::max(std::fabs(a), std::fabs(b)) ? 0 : value;
}

class Parser {
  public:
    explicit Parser(const QString &text) : tokens_(tokenize(text)) {}
    // The value of the whole text, and whether it did any arithmetic.
    calculator::Answer run() {
        if (peek().kind == Token::End)
            fail("Empty");
        calculator::Answer answer;
        answer.value = sum();
        if (peek().kind != Token::End)
            convert(answer);
        else
            answer.number = calculator::format(answer.value);
        return answer;
    }
    bool operated = false;

  private:
    QList<Token> tokens_;
    qsizetype at_ = 0;
    int depth_ = 0;
    const Token &peek(qsizetype ahead = 0) const {
        return tokens_[std::min(at_ + ahead, tokens_.size() - 1)];
    }
    const Token &next() {
        const Token &token = peek();
        if (at_ < tokens_.size() - 1)
            ++at_;
        return token;
    }
    bool isSymbol(const char *symbol, qsizetype ahead = 0) const {
        return peek(ahead).kind == Token::Symbol && peek(ahead).text == QString::fromUtf8(symbol);
    }
    [[noreturn]] void unexpected() const {
        const Token &token = peek();
        if (token.kind == Token::End)
            fail("Incomplete");
        if (token.kind == Token::Name && !isConstant(token.text) && !findFunction(token.text))
            fail("Unknown name “" + token.text + "”");
        fail("Unexpected “" + token.text + "”");
    }
    // Deep enough for anyone, shallow enough for the stack.
    struct Deeper {
        explicit Deeper(Parser &parser) : parser(parser) {
            if (++parser.depth_ > 200)
                fail("Too deeply nested");
        }
        ~Deeper() { --parser.depth_; }
        Parser &parser;
    };

    double sum() {
        double value = product();
        while (isSymbol("+") || isSymbol("-")) {
            const bool plus = next().text == "+";
            const double operand = product();
            operated = true;
            value = cancelled(checked(plus ? value + operand : value - operand), value, operand);
        }
        return value;
    }
    // Whether what comes next is multiplied by what came before without a sign: a constant,
    // a function called, a parenthesis or a root (`2pi`, `3(1 + 2)`, `2√2`).
    bool implicitProduct() const {
        const Token &token = peek();
        if (token.kind == Token::Symbol)
            return token.text == "(" || token.text == "√";
        if (token.kind != Token::Name)
            return false;
        return isConstant(token.text) || (findFunction(token.text) && isSymbol("(", 1));
    }
    double product() {
        double value = unary();
        for (;;) {
            QString op;
            if (isSymbol("*") || isSymbol("/") || isSymbol("%")) {
                op = next().text;
            } else if (peek().kind == Token::Name &&
                       peek().text.compare("mod", Qt::CaseInsensitive) == 0) {
                next();
                op = "%";
            } else if (implicitProduct()) {
                op = "*";
            } else {
                return value;
            }
            const double operand = unary();
            operated = true;
            if (op == "*") {
                value = checked(value * operand);
            } else {
                if (operand == 0)
                    fail("Division by zero");
                value = checked(op == "/" ? value / operand : std::fmod(value, operand));
            }
        }
    }
    double unary() {
        Deeper deeper(*this);
        if (isSymbol("-")) {
            next();
            return -unary();
        }
        if (isSymbol("+")) {
            next();
            return unary();
        }
        return power();
    }
    // A power binds tighter than a sign before it (-2^2 is -4) and goes from the right
    // (2^3^2 is 2^9), and its exponent may have a sign (2^-1).
    double power() {
        const double base = postfix();
        if (!isSymbol("^"))
            return base;
        next();
        const double exponent = unary();
        operated = true;
        if (base == 0 && exponent < 0)
            fail("Division by zero");
        return checked(std::pow(base, exponent));
    }
    double postfix() {
        double value = primary();
        while (isSymbol("!")) {
            next();
            operated = true;
            if (value < 0 || value != std::floor(value))
                fail("Factorial of a fraction or a negative number");
            if (value > 170)
                fail("Too large");
            double product = 1;
            for (int i = 2; i <= static_cast<int>(value); ++i)
                product *= i;
            value = product;
        }
        return value;
    }
    double primary() {
        Deeper deeper(*this);
        const Token &token = peek();
        if (token.kind == Token::Number) {
            next();
            operated = operated || token.based;
            return token.number;
        }
        if (isSymbol("(")) {
            next();
            const double value = sum();
            if (!isSymbol(")"))
                peek().kind == Token::End ? fail("Missing )") : unexpected();
            next();
            return value;
        }
        if (isSymbol("√")) {
            next();
            operated = true;
            return checked(std::sqrt(postfix()));
        }
        if (token.kind != Token::Name)
            unexpected();
        const QString name = token.text.toLower();
        if (isConstant(name)) {
            next();
            return name == "e" ? std::numbers::e : name == "tau" || name == "τ" ? 2 * pi : pi;
        }
        const Function *function = findFunction(name);
        if (!function)
            unexpected();
        next();
        if (!isSymbol("("))
            fail("“" + token.text + "” needs its argument in parentheses");
        next();
        QList<double> arguments{sum()};
        while (isSymbol(",")) {
            next();
            arguments.push_back(sum());
        }
        if (!isSymbol(")"))
            peek().kind == Token::End ? fail("Missing )") : unexpected();
        next();
        if (arguments.size() < function->least ||
            (function->most >= 0 && arguments.size() > function->most))
            fail(QString("“%1” takes %2")
                     .arg(QLatin1String(function->name), function->least == function->most
                                                             ? QString("one argument")
                                                             : QString("one or two arguments")));
        operated = true;
        return checked(call(QLatin1String(function->name), arguments));
    }
    static double call(QLatin1String name, const QList<double> &arguments) {
        const double x = arguments[0];
        if (name == "sqrt")
            return std::sqrt(x);
        if (name == "cbrt")
            return std::cbrt(x);
        if (name == "abs")
            return std::fabs(x);
        if (name == "round") {
            if (arguments.size() == 1)
                return std::round(x);
            const double digits = arguments[1];
            if (digits != std::floor(digits) || std::fabs(digits) > 15)
                fail("round takes a whole number of digits, up to 15");
            const double scale = std::pow(10.0, digits);
            return std::round(x * scale) / scale;
        }
        if (name == "floor")
            return std::floor(x);
        if (name == "ceil")
            return std::ceil(x);
        if (name == "trunc")
            return std::trunc(x);
        if (name == "sin")
            return nearZero(std::sin(x), x);
        if (name == "cos")
            return nearZero(std::cos(x), x);
        if (name == "tan") {
            if (nearZero(std::cos(x), x) == 0)
                fail("Undefined");
            return nearZero(std::tan(x), x);
        }
        if (name == "asin")
            return std::asin(x);
        if (name == "acos")
            return std::acos(x);
        if (name == "atan")
            return std::atan(x);
        if (name == "sinh")
            return std::sinh(x);
        if (name == "cosh")
            return std::cosh(x);
        if (name == "tanh")
            return std::tanh(x);
        if (name == "exp")
            return std::exp(x);
        if (name == "log" || name == "log2" || name == "ln") {
            if (x <= 0)
                fail("Undefined");
            if (name == "ln")
                return std::log(x);
            if (name == "log2")
                return std::log2(x);
            if (arguments.size() == 1)
                return std::log10(x);
            const double base = arguments[1];
            if (base <= 0 || base == 1)
                fail("Undefined");
            return std::log(x) / std::log(base);
        }
        if (name == "min")
            return *std::min_element(arguments.begin(), arguments.end());
        if (name == "max")
            return *std::max_element(arguments.begin(), arguments.end());
        fail("Unknown name “" + QString(name) + "”");
    }

    // A unit at the parser, or null; km/h and m/s are a name, a slash and a name.
    const Unit *unit() {
        if (peek().kind != Token::Name)
            return nullptr;
        if (isSymbol("/", 1) && peek(2).kind == Token::Name)
            if (const Unit *found = findUnit(peek().text + '/' + peek(2).text)) {
                at_ += 3;
                return found;
            }
        const Unit *found = findUnit(peek().text);
        if (found)
            next();
        return found;
    }
    bool conversionWord(qsizetype ahead = 0) const {
        const Token &token = peek(ahead);
        if (token.kind == Token::Symbol)
            return token.text == ">";
        const auto word = token.text.toLower();
        return token.kind == Token::Name && (word == "in" || word == "to" || word == "as");
    }
    // The rest after an expression: `in hex` (a whole number in another base), or a unit, a word
    // and another unit of the same kind.
    void convert(calculator::Answer &answer) {
        operated = true;
        if (conversionWord() && peek(1).kind == Token::Name && baseNamed(peek(1).text) &&
            peek(2).kind == Token::End) {
            next();
            answer.number = inBase(answer.value, baseNamed(next().text));
            return;
        }
        if (peek().kind != Token::Name)
            unexpected();
        const QString fromName = peek().text;
        const Unit *from = unit();
        if (!from)
            fail("Unknown unit “" + fromName + "”");
        if (!conversionWord())
            peek().kind == Token::End ? fail("Convert to what?") : unexpected();
        next();
        const QString toName = peek().text;
        const Unit *to = unit();
        if (!to)
            peek().kind == Token::End ? fail("Convert to what?")
                                      : fail("Unknown unit “" + toName + "”");
        if (peek().kind != Token::End)
            unexpected();
        if (from->kind != to->kind)
            fail(QString("Cannot convert %1 to %2").arg(kindName(from->kind), kindName(to->kind)));
        const double base = (answer.value + from->offset) * from->factor;
        answer.value = checked(base / to->factor - to->offset);
        // A temperature's offset can leave a crumb where the answer is a round number.
        answer.value = cancelled(answer.value, base / to->factor, to->offset);
        answer.number = calculator::format(answer.value);
        answer.unit = unitNames(*to).first();
    }
    static QString inBase(double value, int base) {
        if (value != std::floor(value) || std::fabs(value) >= 9007199254740992.0)
            fail("Only a whole number can be written in another base");
        if (base == 10)
            return calculator::format(value);
        const auto magnitude = static_cast<qint64>(std::fabs(value));
        const QString digits = QString::number(magnitude, base);
        return (value < 0 ? "-" : "") +
               QString(base == 16  ? "0x"
                       : base == 2 ? "0b"
                                   : "0o") +
               digits;
    }
};

// The value of `text` and whether it took arithmetic to get it.
std::optional<calculator::Answer> compute(const QString &text, bool *operated, QString *error) {
    try {
        Parser parser(text);
        auto answer = parser.run();
        if (operated)
            *operated = parser.operated;
        return answer;
    } catch (const Failure &failure) {
        if (error)
            *error = failure.message;
        return std::nullopt;
    }
}
} // namespace

std::optional<calculator::Answer> calculator::evaluate(const QString &text, QString *error) {
    return compute(text, nullptr, error);
}

std::optional<calculator::Answer> calculator::answer(const QString &query) {
    QString text = query.trimmed();
    const bool asked = text.startsWith('=');
    if (asked)
        text = text.sliced(1);
    bool operated = false;
    auto found = compute(text, &operated, nullptr);
    if (!found || (!asked && !operated))
        return std::nullopt;
    return found;
}

QString calculator::format(double value) {
    if (value == 0)
        return "0"; // -0 too
    if (value == std::floor(value) && std::fabs(value) < 9007199254740992.0)
        return QString::number(static_cast<qint64>(value));
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.12g", value);
    QString text = QString::fromLatin1(buffer);
    // 1e+20 and 1e-05 as 1e20 and 1e-5.
    const auto exponent = text.indexOf('e');
    if (exponent >= 0) {
        QString power = text.sliced(exponent + 1);
        const bool negative = power.startsWith('-');
        if (power.startsWith('+') || negative)
            power.remove(0, 1);
        while (power.size() > 1 && power.startsWith('0'))
            power.remove(0, 1);
        text = text.left(exponent) + 'e' + (negative ? "-" : "") + power;
    }
    return text;
}

QVariantMap calculator::entry(const QString &query) {
    const auto found = answer(query);
    if (!found)
        return {};
    return {{"kind", "calc"},
            {"title", found->text()},
            {"subtitle", "Calculator · Enter copies the result"},
            {"icon", "accessories-calculator"},
            {"target", found->number}};
}
